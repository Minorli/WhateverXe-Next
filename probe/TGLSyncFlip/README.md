# TGLSyncFlip

TGLSyncFlip 是一个独立的 Lilu 插件，用来在同一次启动里对比 async 和 sync 主平面 flip 是否影响 UU 闪烁。它不替换、也不修改正在运行的 WhateverXe（下游 0.8.0/0.8.1 闭源二进制），这样 HDMI/背光/CDCLK 等下游增强都保持原样，A/B 只有一个变量。

它是诊断候选，不是修复。没做整机 A/B 之前，不能说它解决了闪烁。

**2026-11-16 候选 1.0.2：**已修正模式2持久污染缓存以及事务中途切换模式的问题，
完成 CPU 回归、真实 framebuffer 锚点、本机构建和链接检查；维护者确认后已写入活动EFI，
默认模式0；维护者手动重启后已确认 routed/armed，完成桌面短测及 UU 的 0→1→0 短时采集。
UU 的 SURF 前 CTL 分类依次为 async/sync 2497/506、0/2850、2439/425，证实转换及回退生效；
三段均无采集缺口。该轮症状观察被维护者撤回；随后逐段确认重采，原生段至少3次、
同步段未观察到、回切段观察不确定，回切后段外又报告闪烁。存在初步信号，
但回切段提交量明显下降，不能认定因果修复；未进行长时盲测。
维护者报告的两次卡死有 RCS / CCS 恢复循环，因此暂停长时间盲测；当前保持模式0。
此插件检验呈现路径，不能当作 GPU hang 恢复修复。
详见 [本轮审查](../../docs/Integration-20261116.md#syncflip-102修正与uu并发故障)。

## 原理（静态依据，framebuffer `115a4616…888e`）

- `PerformFlipTransaction`（`0x2B602`）读取 `params+0x34` 的 bit0，它就是 `configurePlane` 读的 `args+0x2C`。原生代码在 `0x2B7F4` 只接受单 plane，所以 `args[0] = params+8`。
  - **bit0=1（sync 请求）**：
    - CTL 使用 `0x80000400`；
    - 接近帧边界时，在提交前先等待 vblank；
    - 完成状态由中断驱动。
    - 空闲桌面和 Google Remote Desktop 一直走这条路径。
  - **bit0=0（async 请求）**：
    - CTL 使用 `0x80000600`，即 bit9 async address update；
    - `armUpdate` 之后用 `IODelay(50)` 循环最多 `1e6/(refresh*50)` 次，等 pipe IIR bit3 flip-done 由 `processDEInterrupt` `0x73542` 置标志。
- `configurePlane` 在 `0x21CF` 用 `memcmp(args+8, plane+0x18, 0x50)` 比较参数。sync↔async 每切换一次，`+0x2C` 就变一次，触发一次完整 plane setup（scaler、DBUF、水位、全部 plane 寄存器）。UU 段的 829 次完整 setup 与此一致。

## 模式

| 模式 | 作用 | 硬件 CTL | 原生软件状态 | 代价 |
| --- | --- | --- | --- | --- |
| 0 off | 透传，只计时 | 原样 | 原样 | 计时和原子计数开销，整机尚未量化 |
| 1 transaction | 调用前置 `params+0x34` bit0，返回后只清回这一位 | sync | 完整 sync 路径，不再在 sync/async 之间切换 | 调用方（IOAccel）怎么理解返回，没有静态证据 |
| 2 plane-ctl | 只在 `syncPlaneUpdates(false)` 调用期间清主平面缓存 bit9，返回前恢复这一位 | 最终 CTL/SURF 提交包为 sync | 仍按 async 处理：忙等但存在超时出口 | 每次 flip 最多约一帧的 `IODelay` 忙等；完整 setup 仍会发生 |

1.0.2 不再 route `configurePlane`，改为 route `syncPlaneUpdates`。后者在 `0x351D/0x3525`
把 CTL **值**放入原生提交包，然后在 `0x356E/0x3576` 放入 SURF；
`PrepareTriggerPacket 0x92555` 复制值，不持有缓存指针。返回前只恢复 bit9，
保留原生 bit3 等其他修改。下次参数相同、命中 configurePlane 快路径时也能恢复原生 async。
只处理 A/B/C 主平面，初始 `syncPlaneUpdates(true)` 不变，不碰 WEX 已 route 的 updatePlane。

模式从一个完整原生事务持续到其返回；期间切换返回 Busy，控制工具在用户态最多重试约2秒。
失败必须如实报告，不能打印“已恢复”。没有在内核中等待 GPU 或新增覆盖原生调用的锁。

区分方式：
- 模式2有效，支持最终提交锁存路径与闪烁相关；仍需量化原生忙等和超时，不能证明缓冲生命周期安全。
- 只有模式1有效，优先区分完整 setup、sync 节奏及完成路径的影响。
- 两个都无效且 trace 证实生效，降低 async 假说优先级，转查内容、缓冲及 fence。

## 安全边界

- 没有 `-tglsync` 启动参数时什么也不 route。`-tglsyncoff` 由 Lilu 直接禁用插件。
- route 前要同时满足：
  - 符号存在；
  - 符号地址等于映像基址加固定偏移；
  - 21 处字节锚点逐字节相符，保留旧版全部11处检查。
  - 只要有一项不符，状态就是 `shape-mismatch`，不 route。
  - 如果别的插件已经 route 了这两个函数，它们的序言会被改写，锚点同样不符，插件也会拒绝。
  - 两个route都安装成功才启用修改；部分失败时，已安装wrapper保持透传，即使请求了非零启动模式。
- 启动后的默认模式是 0，可用 `tglsyncmode=N` 修改；首轮不要设置该参数。运行时改模式需要管理员权限（`setProperties`），且只在原生事务之间允许切换。计数只在查询时发布，不会每次 flip 都写 registry。
- 不写任何显示寄存器，不改其他 CTL 位：bit3 是 DOUBLE_BUFFER_CTL `0x44500` 配套的 "allow double buffer update disable" 位，保持不动。

## 构建与测试

```sh
make -C probe/TGLSyncFlip test FB=/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer
make -C probe/TGLSyncFlip            # 需要 driver/WhateverXe/MacKernelSDK 和 Lilu 1.7.2 DEBUG（或 SDK=/LILU=）
make -C probe/TGLSyncFlip ctl
```

构建目标为 10.9：使用 IOService 的 configureReport/updateReport 槽位，同时避开 DriverKit ABI。
旧目标 10.6 会导入 Darwin25 不再导出的 `_RESERVEDIOService0/1`。`make test` 额外用真实
MacKernelSDK 编译新旧 vtable，复现旧导入并检查新产物；本机 kmutil 还须独立核对。

`build/build-manifest.json` 记录二进制 SHA 与 deployment target，安装前后都要核对。

## 运行时

```sh
probe/TGLSyncFlip/build/tglsync-ctl status       # state=routed 才能做 A/B
sudo probe/TGLSyncFlip/build/tglsync-ctl mode 2
probe/TGLSyncFlip/tools/ab-run.sh local/syncflip/ab1 240 "0 2 0 2 1 0 1 0"
```

`status` 每种模式输出：
- flips；
- `PerformFlipTransaction` 耗时直方图（<1/<4/<8/<12/<16/≥16 ms）及最大值。

模式 2 的耗时会明显变长，这是原生忙等的直接计量。≥16 ms 的条目可能是原生轮询超时（超时后提前标记完成），要单独报告。

**1.0.2 的生效判据有变化：**原生准备阶段仍可能写 async CTL，不能再要求整个块的
`async_ctl == 0`。新版 plane-trace 另外记录每次 SURF 之前最后观察到的 CTL：
`surf_async_ctl`、`surf_sync_ctl`、`surf_ctl_unavailable`。无丢失、无计数器重载且覆盖完整时，
模式0要求 `surf_async_ctl > 0`；模式1/2要求 `surf_async_ctl == 0`、`surf_sync_ctl > 0`，
并分别检查 forced / plane_cleared 增长。初始或经过日志缺口无法确定 CTL 的 SURF 记 unavailable，
不能算同步。它们是写入顺序观察，不能替代硬件实际锁存证明。

`ab-run.sh` 在维护者自己的终端运行：
- 每个块都会切换模式并运行一次 `tgl-plane-trace`；
- 屏幕上只显示块编号，模式写在 `schedule.txt`；
- 退出或 Ctrl-C 时自动切回模式 0。
