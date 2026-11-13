# TGLDisplayProbe：Tiger Lake 显示欠载探针

用来验证 [显示侧审查](../../docs/ClaudeReview-20261113-Display.md) 里的头号假设：
UU 打开时左下角或下半屏闪烁，是显示 FIFO underrun。

## 设计

- 独立的 IOService kext。**不依赖 Lilu、不 hook、不打补丁、不 `open()` provider**，使用独立的
  `IOMatchCategory`，所以可以和任何 WhateverXe 构建（包括闭源下游）共存。
- 第二次映射 IGPU BAR0（uncached），在自己的 work loop 上每 20 ms 采样一次：
  - `ICL_PIPESTATUS`（`0x70058 / 0x71058 / 0x72058`）的 bit31：sticky underrun 位，不受 IMR 影响；
  - 发生 underrun 时同时记录 frame counter 和 flip counter；
  - 快照 donor 从来不写的 TGL 显示初始化寄存器：BW_BUDDY、DCPR_2、DBUF tracker、PSR 等。
- **原生覆盖率计数器**：framebuffer `115a4616…888e` 的 `invokeDEErrorIntrHandler` 计数（入口、pipe A/B/C 欠载位、分派）。
  - 这是纯内存读取，不需要 MMIO 资格。
  - 运行时地址由一个活的 `AppleIntelFramebufferController` 推出：先校验 vptr 与 `gMetaClass` 的 `__DATA` 段内差值，以及 5 个 vtable 槽之间的 text 差值，都和文件完全一致之后才读。
  - 读取期间一直持有该对象的引用，所以 donor kext 不可能在读取中被卸载。
  - 校验失败时 `DonorCounterState = 2`，计数被禁用，不会猜测地址。
- **访问资格**：
  - 每个 tick 先确认 PCI vendor 为 0x8086，且 Command 寄存器的 Memory Space 位已开启；
  - 系统睡眠、关机、重启前通过 priority sleep/wake interest 暂停采样，完全唤醒 3 秒后再恢复；
  - 读到全 1 记为 unavailable，DC5/6/9 记为 SkippedDC，PW_1 关闭记为 SkippedPW1Off，这些情况都不会当成 0。
- 结果放在服务属性 `TGLDisplayProbe` 里，由 OSSerializer 在读取时现场生成，**不会周期性写 registry**。
- 安全约束都写在 `TGLDisplayProbeLogic.hpp` 开头，并由 CPU 测试强制检查：
  - 只访问固定白名单里的寄存器；
  - DC5 / DC6 / DC9 允许时，除 `DC_STATE_EN` 之外什么都不读；
  - 一个 pipe 的寄存器只在它的 power well 打开时才读（A = PW_1，B = PW_3，C = PW_4）；
  - **默认不做任何写入**；
  - 加 `-tgldp-count` 后，唯一的写操作是：对刚看到 bit31 的 PIPESTATUS 写 1 清零。i915 做法相同，donor 原生代码不读这个寄存器；
  - 第一次采样时就已经置位的 sticky 位，只记为 `StickyAtFirstSample`（旧的、发生时刻未知），不算作事件；
  - workaround 写入默认关闭，每一项单独 opt-in，只改自己那个字段，可重复执行结果不变，而且从不猜 page mask。

## 启动参数

| 参数 | 作用 |
| --- | --- |
| `-tgldp-off` | 不启动 |
| `-tgldp-count` | 对 PIPESTATUS bit31 写 1 清零，从而统计每一次新的欠载。默认关闭；默认情况下，每次置位只能看到一次，直到别人清掉它 |
| `-tgldp-readonly` | 强制完全不写，覆盖 `-tgldp-count` 和 `tgldp-wa` |
| `tgldp-ms=N` | 采样周期，限制在 5–1000 ms，默认 20 |
| `tgldp-wa=MASK` | 1 = BW_BUDDY TLB timer 8（Wa_22010178259），2 = BW_BUDDY page mask，4 = DCPR_2 bit27:24（Wa_14011508470），8 = DBUF tracker 8；默认 0 |
| `tgldp-pagemask=V` | `tgldp-wa` 含 bit 2 时必须提供。按 i915 表：DDR4 双通道 `0x1F`，LPDDR4 双通道 `0x1C`，DDR4 单通道 `0xF` |

所有 A/B 轮次必须使用同一个 kext SHA，**只改 boot-arg**，这样才是单变量对照。

## 构建与测试

```sh
make test          # CPU 逻辑测试（ASan/UBSan），不访问硬件
make               # build/TGLDisplayProbe.kext，打印 SHA-256
make fb-shared     # build/fb-shared-open：触发 WEX 的按需发布并打印 TGLStream* 等键
```

在 G83 上请用本机 SDK 重新构建，并记录 SHA。审查者是在 macOS 27.2 SDK、arm64 主机上交叉编译验证的，
链接时有 libkmod 版本提示，这个产物不要拿去部署。

变异检查（审查者手工执行）：分别删掉以下守卫后，测试都会失败：
- DC 检查
- pipe power 检查
- IMR 读取前的 power 检查
- DBUF powered 检查
- count 写入开关
- page mask 必须提供的检查

donor 资格校验另有 4 个反例测试：metaClass 差值不符、vptr 未对齐、空指针、vtable 槽差值不符。

## 读取

```sh
ioreg -rw0 -c TGLDisplayProbe -k TGLDisplayProbe
```

主要字段：
- `PipeAUnderruns` / `PipeAFirstNs` / `PipeALastNs` / `PipeALastFrame`
- `Events`：最近 64 次 underrun，记录 ns、pipe、frame、flips、status
- `First*` / `Last*`：寄存器快照（BuddyCtl / BuddyMask / DCPR2 / DBufS1 / DBufS2 / PSRCtl / IMR*）
- `PipeAStickyAtFirstSample` / `PipeAFirstSampled`
- `SkippedDC` / `SkippedPW1Off` / `UnavailableAllOnes` / `UnavailablePCI` / `Paused`
- `DonorCounterState`，以及 `DonorUnderrun{Entry,PipeA,PipeB,PipeC,Dispatch}` 和对应的 `…AtQualify` 基线值。它们是原生中断路径的累计值，非原子递增，数值可能偏小，而且只统计送达处理函数（未被 IMR 屏蔽）的中断
- `Writes`：累计写入次数

`NowNs` 和事件的 `Ns` 都是 `mach_absolute_time` 换算出的 ns，用来和用户记录的闪烁时刻、
`log show` 的时间对齐。换算基准是开机以来的时间，不是墙钟时间。

## 部署与回退（需要维护者确认并手动重启）

1. 用 OpenCore `Kernel → Add` 注入：`BundlePath = TGLDisplayProbe.kext`、
   `ExecutablePath = Contents/MacOS/TGLDisplayProbe`、`PlistPath = Contents/Info.plist`、`Arch = x86_64`。
   和 Lilu / WEX 的顺序无关。
2. 回退任选一种：在 OpenCore 引导菜单临时加 `-tgldp-off`，或者把这一项设为 `Enabled = false`。
3. 不需要也不允许改动现有 WEX、Graphics bundle 或镜像布局。
