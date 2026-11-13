# 外部审查（Claude）：UU 闪屏的显示侧证据，以及驱动热路径问题

日期：2026-11-13。审查者：Claude（外部审查，没有接触 G83 实机）。
证据层级沿用本仓库规则：〔静态〕〔CPU〕〔进程内 GPU〕〔整机〕。下文所有结论都只到〔静态〕，
没有安装、重启或修改实机的任何东西。

## 0. 输入与边界

- 用户新观察（2026-11-13 下午）：**拔掉 HDMI、只用内屏，打开 UU 时左下角或下半屏仍偶发闪烁**；不开 UU 不闪。
  用户认为录屏对照不现实，下面的方案因此不依赖录屏。
- 二进制来源：官方 WhateverXe-Graphics v0.4.1 pkg（`7f7ce3b9…02c`）和 v0.4.3 pkg（`bd17c737…044`）的 Tahoe payload。
  - Tahoe framebuffer `AppleIntelTGLGraphicsFramebuffer` 在两个版本中**完全相同**，SHA
    `115a4616e862228a8c82df6e52ba41ac02b9750f116352ff3575aadcc950888e`。下文所有偏移都基于这个文件。
  - 0.4.3 的 accelerator（`6c8ab6cc…1449`）与本机 `567511…d496e` 共有 9 段不同，全部位于文件尾部 ≥`0x1B8DC3`
    的代码签名区，代码本身相同。所以"0.4.3 Metal + 0.4.1 accelerator"并不是代码版本混搭。〔静态〕
- 反汇编只保留在审查者本地，没有提交 Apple 二进制或反汇编，只引用偏移和语义。
- 对照用的是 Linux v6.12 i915：`intel_display_power.c`、`skl_watermark*.c/h`、`i915_reg.h`、`intel_display_device.c`。

## 1. 结论摘要（按优先级）

| # | 判断 | 结论 / 层级 |
| --- | --- | --- |
| 1 | 闪烁最符合**显示 FIFO underrun（扫描输出欠载）** | 证据不足〔静态推断〕；但它是最便宜、最该先确认的方向 |
| 2 | FBC 在本机配置下没有开启 | 成立〔静态〕 |
| 3 | PSR 只在 Apple 自家 TCON 类里实现，普通 eDP 面板大概率不进入 PSR | 部分成立〔静态〕；需要一次读 SRD_CTL 确认 |
| 4 | 原生 underrun 处理只走 `kprintf`，`log show` 看不到；"日志里没有 underrun"不能作为否定证据 | 成立〔静态〕 |
| 5 | 原生 framebuffer 缺少 i915 在 TGL 上防欠载的三项显示初始化（BW_BUDDY、DCPR_2、DBUF tracker） | 成立〔静态〕（指"原生代码不写"）；实机寄存器值还不知道 |
| 6 | WEX 在**每次 flip** 的路径上写 2 KiB IORegistry 属性，而且无法关闭 | 成立〔静态〕（指开销存在）；它和闪烁的因果关系证据不足 |
| 7 | WEX 已经有一个现成的 underrun 记录通道（`TGLStreamErrorCount`），仓库从来没读过 | 成立〔静态〕 |
| 8 | 这个 framebuffer 是带覆盖率插桩的 Intel CRB 启动阶段构建；原生 underrun 计数可以按固定偏移只读取出 | 成立〔静态〕 |

为什么把 underrun 排在 POSH 前面：
- underrun 的典型画面是"从某一行往下花或闪"，和"左下 / 下半屏"吻合；
- 欠载发生在扫描输出阶段，合成出来的帧本身是对的，所以录屏抓不到，和此前录屏没命中闪烁帧一致；
- 拔掉外屏也会闪，排除了 HDMI；
- UU 期间 WindowServer 单核 CPU 70–97%，软解加 60fps 合成会制造突发性的内存带宽和功耗状态切换，这正是 i915 那几项防欠载处理针对的场景。

POSH 假设没有被否定，但优先级应放在 underrun 被排除之后。

## 2. 静态证据（framebuffer `115a4616…888e`）

### 2.1 FBC：默认关闭

- WEX 设置 `AAPL,ig-platform-id = 0x9A490000`（`kern_xe.cpp:4605`）。
- `getOSInformation`（`0x40B22`）在 `_gPlatformInformationList` 中按 0x1C0 步长查找这一项，指针保存到 controller `+0xD40`。
- 这一项由静态初始化代码 `0x7ADC5..0x7AE18` 填写。`0x7AE18` 把 entry `+0xF8`（flags）写成 `0x8005`。
- `getFBCControl`（`0x42BA4`）：如果没有 `FBCControl` 字典，FBC 是否开启 = `(flags>>1)&1` = **0**。
  Info.plist 的 `FeatureControl` 里没有 `FBCControl`。DynamicFBC 用的 bit3 也是 0。
- WEX 只支持 `framebuffer-stolenmem` 这一个 framebuffer 属性（`kern_xe.cpp:4442`），EFI 里也没有注入别的。

### 2.2 PSR

- PSR 的进入、退出和事件处理只出现在 `BanksiaTcon` / `CamelliaBase` / `CamelliaTcon2` 这几个类里，它们对应 Apple 自家的 TCON。
- 二进制里有字符串 "PSR is ON which should never happen SRD_CTL = …"。
- 原生 `-nopsr` 在 `0x7BCCB` 清除 TCON 标志 `+0x40` 的 bit 0x20，另有 `-forcepsr` / `-nopsrexit`。
- 确认方法：只读 TGL eDP 的 `SRD_CTL`（i915 `_SRD_CTL_EDP 0x6F800`；TGL 的 transcoder 寄存器可能映射到 `0x60800`，两个都读）bit31。
  - 如果是 1，`-nopsr` 就是一个原生、单变量、改 boot-arg 即可回退的实验。

### 2.3 原生 underrun 处理只输出到 kprintf

- `SWInterruptHandler`（`0x73FE4`）在累积位 `bits & 0x1C0` 时调用 `invokeDEErrorIntrHandler`（`0x744BC`）。
- 在这个函数里，0x40、0x80、0x100 分别对应 pipe A、B、C。处理方式是 `kprintf("Display Pipe Underrun occurred on pipe(s) …")`，然后 `dumpMMIO`。
- 没有任何计数被导出到 IORegistry 或统一日志。原生还保留了 bring-up 阶段"underrun 时 panic"的分支（`AppleIntelInterrupt.cpp:3126` 字符串），本机没有触发它。
- 所以：`log show` 里查不到 underrun，什么也证明不了。

### 2.4 覆盖率计数器：可以直接读出原生 underrun 次数

- 这个 framebuffer 有 `__llvm_prf_cnts`（vm `0xD0910`，大小 `0x1C200`），平台名是 "Intel HD Graphics TGL CRB"，属于插桩的 bring-up 构建。几乎每个基本块都会对全局计数器做一次非原子 `incq`。
- `invokeDEErrorIntrHandler` 用到的计数器（偏移相对 kext 加载基址，每个都是 u64）：

| 计数器 | 偏移 | 来源指令 |
| --- | --- | --- |
| 函数进入次数 | `0xE6CD8` | `0x744CA incq` |
| pipe A underrun 位累计 | `0xE6CE0` | `0x744EC addq %rax` |
| pipe B underrun 位累计 | `0xE6CE8` | `0x74514` |
| pipe C underrun 位累计 | `0xE6CF0` | `0x74539` |
| SWInterruptHandler 分派次数 | `0xE6BC8` | `0x74084` |

- 在 WEX 的 framebuffer `processKext` 里，先校验 framebuffer 文件 SHA 和上面这些指令字节，再挂一个 `OSSerializer` 在读取时取这 5 个值。这样不需要 hook，也不改变任何行为。
  - 注意：`incq` 不是原子操作，多 CPU 同时写时数值偏小。它只能统计送达处理函数的中断，受 IMR 影响。
- 这个插桩本身也有代价：framebuffer 每个热路径都在多核共享的计数器 cache line 上做读-改-写。这是 donor 自带的固定开销，没有非插桩版本就无法消除。

### 2.5 和 i915 相比缺少的 TGL 显示初始化

参照 i915 v6.12 的 `icl_display_core_init`。TGL 属于 `XE_D_DISPLAY`，`abox_mask = GENMASK(2,1)`：

| i915 动作 | 寄存器 | framebuffer 是否写 |
| --- | --- | --- |
| `tgl_bw_buddy_init`：按 DRAM 类型和通道数写 page mask | `BW_BUDDY_PAGE_MASK(1/2)` = `0x45144 / 0x45154` | **不写**：整个文件里没有出现这些值 |
| Wa_22010178259：`BW_BUDDY_TLB_REQ_TIMER = 0x8`（bit21:16） | `BW_BUDDY_CTL(1/2)` = `0x45140 / 0x45150` | **不写** |
| Wa_14011508470：设置 DCPR bit24–27 | `GEN11_CHICKEN_DCPR_2 0x46434` | 原生代码不写；这个值只出现在内嵌 DMC 程序 `_CSR_PATCH_AX`（`0x12DB20`）/ `_CSR_PATCH_B0plus`（`0x130D70`）的 MMIO 表中 |
| `gen12_dbuf_slices_config`：`DBUF_TRACKER_STATE_SERVICE = 8`（bit23:19） | `DBUF_CTL_S1/S2 0x45008 / 0x44FE8` | `enableDisplayEngine` 在 `0x5A8B7` 写 `0x45008 = 0x80000000`，只发了 power request，tracker 字段为 0；如果固件已经点亮，`0x5A723` 的快速路径会直接跳过 |
| Wa_14011294188：`PCH_DPMGUNIT_CLOCK_GATE_DISABLE`（bit15） | `SOUTH_DSPCLK_GATE_D 0xC2020` | 有访问，但是否设置了 bit15 还没核对 |
| SAGV：display ≥11 通过 QGV point 限制（`ICL_PCODE_SAGV_DE_MEM_SS_CONFIG`）并计算带宽 | pcode mailbox | 原生用的是 SKL 时代的 `GEN9_PCODE_SAGV_CONTROL 0x21`（`hwSetPowerWellStatePG 0x5BAB3 / 0x5BBA0`）。整个文件只出现 mailbox 命令 0x6 / 0x7 / 0x21 / 0x22，没有 QGV 限制 |

- 本仓库和上游 WEX 都没有写过这些寄存器（grep `0x45140 / 0x46434 / BW_BUDDY / DCPR` 都为 0）。
- **前提与限制：** GOP 在开机时可能已经写过一部分，所以必须读实机的值才能下结论。BW_BUDDY 的 page mask 期望值取决于 DRAM：
  - DDR4 双通道 = `0x1F`
  - LPDDR4 双通道 = `0x1C`
  - DDR4 单通道 = `0xF`
  - i7-1165G7 + 24 GB 很可能是"板载 + SO-DIMM 的 DDR4 双通道"，需要用 `system_profiler SPMemoryDataType` 确认。
  - 睡眠 / 唤醒之后显示核心会重新初始化，原生代码不会恢复这些值，所以**醒来后的值也必须读一次**。

### 2.6 WEX 的显示热路径开销（上游 0.7.0 和 0.8.0 都有，下游 0.8.1 大概率也有）

1. **`wrapTGLPlaneUpdate` → `tglObserveScanout`**（`kern_xe.cpp:19919`、`:19715`）
   - donor 的 `AppleIntelPlane::updatePlane` 的直接调用者只有 `PerformFlipTransaction` 和 `setAttribute`，也就是**每次 flip 都会经过**。
   - wrapper 在调用前后各执行一次 `tglObserveScanout`。每次都在栈上构造 `uint64_t r[256]`，并 `snprintf`，然后对 IORegistry root `setProperty` 一个 2 KiB 的 OSData。前 32 次还要再写一份 First。
   - 这个 hook 无条件安装，没有 boot-arg 可以关闭。
   - root 上会积累 `TGLScan{0..7}Last{0..63}` / `First{1..32}`，最多约 1.5 MiB 的属性。每次 `ioreg -l` 序列化 root 都更重，持锁时间也更长。
   - 风险：flip 编程期间等锁或分配内存，可能错过 vblank，或让双缓冲寄存器分两帧锁存。这一点证据不足，但不管有没有因果关系，都应该移出 flip 路径。
2. **`wrapTGLFBGetAttr` / `wrapTGLFBSetAttr`**（`:18197`）
   - 每次调用都会执行两次 `tglSnapshotFB`，也就是两次 registry 写，而且无条件执行。`tglTrace` 已经用 `-wex-display-trace` 控制，这两次写没有。
3. **`tglProgramRecord`**（`:3230`）
   - 每次写 `0x70180..0x70400`（pipe A 的 plane 寄存器和水印）都会关中断、拿自旋锁、取时间戳、复制记录。这正好落在 flip 写 plane 寄存器的窗口里。
   - 默认开启，可以用 `-wex-tgl-prog-off` 关闭，但要先确认下游是否保留了这个开关。
4. 背光 PWM 写入（`0xC8258`）会额外读 3 次 MMIO 并写 registry，频率低。
5. `wrapTGLSurfaceAux` 每次填写 surface state 都对共享计数做 3 次 relaxed atomic，属于轻微的跨核争用。

**修复建议（WEX 源码，可以作为上游 PR）：**
- flip、属性、plane 路径上的观察数据，全部改为写进启动时预分配的固定环形缓冲区：不分配内存、不碰 registry，只用一把短自旋锁或每 CPU 槽位。
- 只在已有的按需边界（`observeFramebufferUserClient` type 1，即 `tglStreamPublish` 现在的触发点）发布到 registry。
- `tglObserveScanout` 和 `tglSnapshotFB` 默认关闭，只有 `-wex-display-trace` 时才开。
- 用 CPU 测试统计"每次 updatePlane 触发的 setProperty 次数 = 0"。

### 2.7 已经存在却从没读过的 underrun 通道

- `tglStreamAck`（`kern_xe.cpp:3349`）拦截 donor 对 `GEN8_DE_PIPE_IIR(0x44408 + 0x10·pipe)` 的确认写入。
  - 只要写入值的 bit31（`GEN8_PIPE_FIFO_UNDERRUN`）为 1，就记入 `tglStreamErrors` 环，`TGLStreamErrorCount` 加 1。
  - 每个 pipe 的确认总数记在 `TGLStreamAckCounts`。
- 这些只在 framebuffer 的 type-1（shared）user client 打开成功时发布（`:18217` → `tglStreamPublish`）。
- 仓库的 docs、validation、review 里从来没有记录过这些值。
- 前提：IIR 只锁存未被 IMR 屏蔽的位。`enableSWDC6`（`0x47788`）会把 `DE_PIPE_IMR(A) 0x44404` 写成全 1。所以读到 0 不能证明没有 underrun。

## 3. 给 Codex 的执行顺序（风险从低到高）

### S0：零改动，现在就能做

1. 写一个很小的 C 工具：对 `AppleIntelFramebuffer` 执行 `IOServiceOpen(type=1)` 后立即关闭，只用来触发 WEX 发布；不调用任何 method。
   - 分别在 UU 闪烁前后各触发一次，读取 `TGLStreamErrorCount / TGLStreamAckCounts / TGLStreamErrorRing`。
   - 再隔 10 秒读两次 `TGLScan2Last*` 里最大的 `r[2]` 序号，算出每秒 updatePlane 次数，确认 2.6-1 的实际频率。
   - 先确认下游驱动确实发布了这些键，读不到就记 unavailable，不要当成 0。
2. 请用户**只记录闪烁发生的大致时刻或次数**，不需要录屏，用来和计数做对齐。
3. 用 `system_profiler SPMemoryDataType` 确认 DRAM 类型和通道数，用来确定 BW_BUDDY 的期望值。

### S1：独立的只读探针 kext（不 hook，可以和闭源下游共存）

- 新建一个最小 IOService 插件，match IGPU，只做 BAR0 映射和读取：
  - 每 50–100 ms 采样一次 `ICL_PIPESTATUS` A/B/C（`0x70058 / 0x71058 / 0x72058`）的 bit31。它是 sticky 的 underrun 位，不受 IMR 影响。
    - 第一版完全只读，只记录第一次置位的时间。第二版可以按 opt-in 对 bit31 写 1 清零以便重复计数；donor 原生代码不读这个寄存器，DMC 表里有它，所以要说明这一点。
  - 同时快照：`BW_BUDDY_CTL / PAGE_MASK`（`0x45140 / 0x45144 / 0x45150 / 0x45154`）、`0x46434`、`0x45008 / 0x44FE8`、`0xC2020`、`DE_PIPE_IMR / ISR`、`SRD_CTL`（`0x6F800 / 0x60800`）。
  - 结果发布到自己的 registry 节点，并在开机时和唤醒后各采样一次。
- 安装需要用户批准和手动重启，回退方法是从 OpenCore 删除这个 kext。

### S2：按 S1 的结果分支，每次只改一个变量，每轮 15 分钟，同一个 UU 画面

- **underrun 和闪烁同步增长，且 2.5 中的寄存器与 i915 期望不符：**
  - 在探针 kext 里加 opt-in boot-arg，在显示引擎启用后和每次唤醒后，按 i915 的值写一次 BW_BUDDY（TLB timer 0x8 + page mask）、DCPR_2 bit24–27、DBUF tracker 8。
  - A/B 对照只差这一个开关。
- **underrun 存在但寄存器都正确：**
  - 优先看 BIOS 是否提供 SAGV 开关（零代码）。
  - 然后评估 pcode QGV 限制的实验（风险较高，单独评审）。
- **SRD_CTL 显示 PSR 已开启：** 加原生 `-nopsr` 做单变量对照。
- **没有 underrun：** 扫描输出侧基本排除，回到 flip 时序（2.6）和 POSH（no-POSH A/B）。

### S3：WEX 优化 PR

- 按 2.6 的建议改造，补 CPU 测试，提交上游，再请下游维护者合入。
- 这一项不依赖闪烁结论，属于"整体优化"。

## 4. 对 3e52ff0 / db7b36e 候选代码的补充审查

1. no-POSH 只在 `restoreFromSafeImage` 时清 `+0x6E` 的 0x10 标志，而 `initWithOptions` 在 `0x7BB13` 已经按这个标志分配了 16 KiB 的 POCS image（`+0xA0`）。
   - 需要找到 IGHardwareContext 的 free / 析构路径。如果释放也看这个标志，每个 RCS context 都会泄漏 16 KiB。
2. 需要确认内核 no-POSH 之后，MTL 仍然提交的 POSH / POCS 工作会被怎样处理：丢弃、在 RCS 执行，还是报错。这决定 no-POSH 能否解除 0.4.3 的阻塞。
3. beginReset 新增的 false 返回之后，scheduler 的完整走向还要写清楚。

## 5. 未能核实的事项

- 实机寄存器值、DRAM 配置、下游 0.8.1 是否保留了 2.6 / 2.7 的代码和 `-wex-tgl-prog-off`。
- `0xC2020` bit15 的实际写入值；`SRD_CTL` 在 TGL 上的实际映射。
- UU 与 Google Remote Desktop 的解码和合成路径差异。
- underrun 与闪烁是否同步。这是本文头号假设需要的关键证据。

## 6. 跟进：S1 探针已提交（构建 + CPU 测试，未安装）

- `probe/TGLDisplayProbe/`：独立 kext，不依赖 Lilu、不 hook，按 Codex 在 Deployment.md 中写的 S1 门槛实现：
  - 默认零写入；
  - 原生覆盖率计数器先通过运行时段内差值校验和 vtable 校验，才会读取；
  - power well、DC 状态、PCI 访问资格三重检查；
  - 睡眠 / 唤醒期间暂停采样；
  - 读不到时报 unavailable，不填 0；
  - sticky 位开机已置位的情况单独记录。
- CPU 测试 12 个用例通过，另做了变异检查；kext 在审查者主机上交叉编译、链接通过。G83 上必须用本机 SDK 重新构建并记录 SHA。
- 执行步骤见 [给 Codex 的采集提示词](../review/2026-11-13/claude-s1-probe-prompt.md)。
- 2.6 的热路径问题已由 `a9aaf05` 在源码中修复。但实机运行的下游二进制仍带着这个问题（S0 实测 TGLScan2 平均约 190 条/秒），需要下游合入后才会生效。
