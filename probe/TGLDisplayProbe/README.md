# TGLDisplayProbe：Tiger Lake 显示欠载观察

用于验证 [显示审查](../../docs/ClaudeReview-20261113-Display.md) 的欠载假说。
来源为外部审查提交 `3e8d0d6`，保留 BSD-3-Clause 许可。

## 当前版本 1.0.1 的范围

**只读原生 RAM 计数，完整 MMIO 探针尚未准入。** 当前适配器不映射 BAR0、不访问显示寄存器，
不 hook、不改驱动代码、不 `open()` provider。独立 IOService / IOMatchCategory，可与闭源下游共存。
`-tgldp-count` 或非零 `tgldp-wa` 会明确拒绝启动，不会静默忽略后报告实验已执行。

原版每次检查 PCI Memory Space / DC / power-well 后独立读取寄存器。这不是电源域持有引用，
也没有和原生显示电源切换共同加锁，存在检查与读取之间的竞态。系统睡眠通知也不能覆盖所有显示电源切换。
因此不能把“CPU 测试通过”当作 MMIO 准入。完整算法暂保留在 `TGLDisplayProbeLogic.hpp` 中作待审查 CPU 模型，
**没有内核 MMIO 适配器、没有 boot-arg 绕过资格检查**。不能用本版执行 S2 workaround 对照。

## 原生计数资格与生命周期

目标 framebuffer 完整 SHA：
`115a4616e862228a8c82df6e52ba41ac02b9750f116352ff3575aadcc950888e`。

部署前核对文件 SHA、段布局和计数指令。运行时持有同一物理 IGPU 的活 controller 引用，检查：

- 精确类名、vptr 到 gMetaClass 的 DATA 差值、原有五个 vtable 槽的相对 text 差值；
- 五条增量指令的 opcode 和实际重定位 RIP 目标，必须指向对应 DATA 计数；
- 每次只读五个获准的 64 位全局值。不是任意内核内存读取接口；没有修改指令或守卫。

运行时布局和指令检查不等于重新计算整个加载映像 SHA；依赖部署前精确文件核验与原生映像的生命周期。
对象引用在 timer 完全停止后才释放；活对象持有期间其 kext code/data 不会卸载。
不会假称这是通用 no-fault 内存读取器。原生计数是非原子累计，可能漏计并发事件，且只统计实际送达处理函数的中断。

采样周期默认 100 ms。priority sleep/wake 回调用同一 work-loop 的 `runAction` 排队，
等待正在进行的 timer 回调完成后才确认暂停；唤醒后等待 3 秒。停止后即使收到 wake 也不能重新启动采样。
注册 sleep notifier 失败则拒绝启动。serializer 只复制已有快照，之后构建字典，不读硬件，不在每次采样写 registry。

## 构建与测试

```sh
make test
make
```

本机使用 26.5 SDK 构建，目标为本机 26.7.3 / 25G342；不使用外部交叉构建产物。
16 项 CPU case 包括原有计数、白名单和 RMW 逻辑，以及：

- 失去采样资格后重新遇到 sticky=1 记为 `StickyAfterGap`，不认定为刚发生的新事件；
- 配置寄存器 all-ones 使快照失效，拒绝由该值生成 WA 写入；未上电 IMR 有独立有效位；
- pause/wake 延迟/stop 状态，以及部署版拒绝未准入写模式；
- 改坏运行时指令 opcode、RIP 目标和读失败均拒绝资格/发布，部分读取失败不发布半组结果。

CPU 测试不证明 IOKit 的真实时序或睡眠恢复。MMIO 模型测试也不代表部署版执行过这些访问。

## 启动参数与读取

| 参数 | 1.0.1 行为 |
| --- | --- |
| 不添加任何参数 | RAM-only，只读；默认 100 ms |
| `-tgldp-off` | 不启动 |
| `tgldp-ms=N` | RAM 计数周期，限制为 100–1000 ms |
| `-tgldp-count` / 非零 `tgldp-wa` | 拒绝启动：写模式未准入 |

```sh
ioreg -rw0 -c TGLDisplayProbe -k TGLDisplayProbe
```

- `DonorCounterState`：0 搜索中、1 资格匹配、2 拒绝、3 搜索超时。
- `DonorCountersValid=1` 才能使用 `DonorUnderrun{Entry,PipeA,PipeB,PipeC,Dispatch}` 与 `…AtQualify`。
- `NowNs` 是最后一次实际采样的 uptime 纳秒；`ReadNs` 是读取字典时间，不能混用判断陈旧。
- `Paused`、`Stopped`、`DonorSampled`、`DonorReadFailures` 说明采样覆盖。
- `MMIOAccessState=0`、`SkippedNoPowerLease` 明确完整寄存器采样不可用。
- `PipeAFirstSampled` / `PipeAUnderruns` / `First*` / `SkippedDC` 等硬件测量键被省略，**不是零**。
- `Writes=0` 是本版确实没有寄存器写入。不是闪烁已修好。

已有采集工具新增 `tgl-capture --probe PRIVATE_DIRECTORY SECONDS`：每秒按键读取字典，
写入权限 600 的 `probe.ndjson`；每 10 秒保留原来的 ACK 交叉采集。ACK 按需发布会打开并立即关闭
现有 type-1 shared client，仍可能触发下游 cursor observer，不能称为完全无扰动。
目录只能放本机 `local/`。工具不会启动 UU、自动重启或修改显示布局。

## 部署与回退

见 [Deployment](../../docs/Deployment.md#tgldisplayprobe-101-本机部署)。维护者已授权配置，
并确认以前成功进入过 UEFI Shell。2026-11-13 已写入 EFI，但尚未重启验证。
不要假定 OpenCore 普通菜单能直接编辑 boot-args；用已验证入口恢复精确原配置。
