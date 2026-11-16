# TGLSyncFlip

TGLSyncFlip 是一个独立的 Lilu 插件，用来在同一次启动里对比 async 和 sync 主平面 flip 是否影响 UU 闪烁。它不替换、也不修改正在运行的 WhateverXe（下游 0.8.0/0.8.1 闭源二进制），这样 HDMI/背光/CDCLK 等下游增强都保持原样，A/B 只有一个变量。

它是诊断候选，不是修复。没做整机 A/B 之前，不能说它解决了闪烁。

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
| 0 off | 透传，只计时 | 原样 | 原样 | 无 |
| 1 transaction | 调用前置 `params+0x34` bit0，返回后只清回这一位 | sync | 完整 sync 路径，不再在 sync/async 之间切换 | 调用方（IOAccel）怎么理解返回，没有静态证据 |
| 2 plane-ctl | `configurePlane` 返回后清 plane `+0x100` 的 bit9 | sync | 仍按 async 处理：忙等直到真实锁存 | 每次 flip 最多约一帧的 `IODelay` 忙等；完整 setup 仍会发生 |

区分方式：
- 模式 2 有效，说明问题在硬件 async 扫描；
- 只有模式 1 有效，说明问题在切换引起的完整 setup，或 sync 路径的节奏；
- 两个都无效，说明 async 假说不成立。

## 安全边界

- 没有 `-tglsync` 启动参数时什么也不 route。`-tglsyncoff` 由 Lilu 直接禁用插件。
- route 前要同时满足：
  - 符号存在；
  - 符号地址等于映像基址加固定偏移；
  - 11 处字节锚点逐字节相符。
  - 只要有一项不符，状态就是 `shape-mismatch`，不 route。
  - 如果别的插件已经 route 了这两个函数，它们的序言会被改写，锚点同样不符，插件也会拒绝。
- 启动后的默认模式是 0，可用 `tglsyncmode=N` 修改。运行时改模式需要管理员权限（`setProperties`）。计数只在查询时发布，不会每次 flip 都写 registry。
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

`ab-run.sh` 在维护者自己的终端运行：
- 每个块都会切换模式并运行一次 `tgl-plane-trace`；
- 屏幕上只显示块编号，模式写在 `schedule.txt`；
- 退出或 Ctrl-C 时自动切回模式 0。
