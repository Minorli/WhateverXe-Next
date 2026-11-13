# 源码审查入口

本文件给首次审查者和外部代码审查工具提供完整上下文。请区分确定事实、解释假说、
实验结果和未满足的部署条件，不因项目名称含 Next 而假定修复已经完成。

2026-11-13 新增了 [异常重启分析](Incident-20261113.md)、
[脱敏审查包](../review/2026-11-13/README.md) 和
[完整外部审查提示词](ExternalReview-20261113.md)。请先阅读新事故，再复核以下研究。

## 2026-11-13 18:07–18:09：`ig=0x20` 重启后 UU 观测

- **成立〔整机〕：**维护者已手动重启，当前启动参数确实包含 `ig=0x20`；探针 1.0.1
  保持 `DonorCounterState=1`、`DonorCountersValid=1`，121 个每秒样本读失败为 0，`Writes=0`。
- **成立〔整机〕：**120.57 秒前台采集完整结束。RAM 计数 entry、pipe A/B/C 和 dispatch
  均为 0→0；`MMIOAccessState=0`，所有采样仍因没有合格 display power-domain lease 而跳过 MMIO。
- **成立〔整机〕：**显示 ACK 环仍为开机时的 6 条清除写入，`TGLStreamErrorCount` 为 6→6；
  没有新的 bit31 ACK。恢复 calls/returns 为 0，也没有新的 gpuRestart 或 WindowServer watchdog。
- **证据不足〔整机〕：**这次采集没有把闪烁时间落在同一个 120 秒窗口内，且 MMIO pipe status
  不可读。因此不能说 `ig=0x20` 修复或排除了欠载，只能说本轮没有捕获到新的原生计数。
- **成立〔整机快照〕：**采集结束时 WindowServer 约 45.4% CPU、UU 约 13.2%、VTDecoder
  约 13.8%；这是单次快照，不代表窗口中位数。

人工反馈的 18:01:04 和 18:05:13 已作为“message receipt”写入本机 ignored `local/`，
不是驱动硬件事件。脱敏汇总见
[validation/tgldisplayprobe-ig20-20261113.json](../validation/tgldisplayprobe-ig20-20261113.json)。



## 2026-11-13 17:35–17:46：探针启动验证与 UU 基线（历史窗口）

- **成立〔整机〕：**探针1.0.1实际加载，UUID与构建匹配；684.48秒、682个快照全部资格有效，
  读失败0、暂停0、Writes0。睡眠/唤醒尚未测试，完整MMIO仍不可用。
- **成立〔用户观察＋整机〕：**用户至少报告一次闪烁（17:36:37接收，非精确onset）。
  同窗原生欠载计数增量0、ACK6→6、恢复calls/returns0、无新gpuRestart。
- **不成立：**由计数0排除欠载。MMIO覆盖为0，所有tick均SkippedNoPowerLease；
  当前未开启原生中断报告，DC/PW/PCI skip比例不可测。不能套用“低skip、无计数→欠载排除”。
- **成立〔整机、限本窗口〕：**WindowServer physical footprint最后392.40MiB、峰值577.09MiB；
  剔除前120秒后斜率−3.03MiB/min，未见持续增长。CPU单核中位数WS26.84%、UU20.14%；
  SMC返回的CPU温度中位数51°C、范围44–70°C，thermal pressure均Nominal。部分SMC key不可用，温度只作估计。
- **成立〔批准＋磁盘部署〕：**在该历史窗口结束时，维护者单独同意仅追加`ig=0x20`，已配置并通过ocvalidate；
  探针/WEX/framebuffer/Metal文件SHA均未改变。随后已手动重启并完成18:07–18:09复核，见本文件顶部。

[采样窗口与统计](../validation/tgldisplayprobe-postboot-20261113.json)；
[下一轮步骤与回退](Deployment.md#下一轮原生欠载报告-ig0x20已批准配置待重启)。

## 3e8d0d6 跟进：TGLDisplayProbe 准入审查与 RAM-only 部署

**成立〔静态＋CPU＋本机构建＋磁盘部署〕：**外部探针经修订为1.0.1，只交付已经建立生命周期边界的
原生内存计数部分。维护者授权配置，并确认以前成功进入过UEFI Shell；当前EFI已配置，**未重启**。
[完整步骤](Deployment.md#tgldisplayprobe-101-本机部署)与[验证摘要](../validation/tgldisplayprobe-20261113.json)。

| S1门槛 | 审查及本次处理 |
| --- | --- |
| 只读计数、固定donor | **成立〔静态〕**：精确文件SHA、5个vtable槽和5条计数指令均匹配；运行时保留原差值守卫并新增opcode/RIP目标检查 |
| 文件偏移不是运行时地址 | **成立〔静态＋CPU〕**：分别从活对象vptr和text槽推导DATA/text地址，按实际重定位displacement验证目标；不同segment slide和目标错误有测试 |
| MMIO电源域资格 | **不成立〔静态〕**：独立timer的DC/PW检查不是lease；没有共同锁阻止模式设置、runtime power或power-well切换。取消全部生产BAR映射/MMIO适配器，发布MMIO unavailable |
| 睡眠/停止 | **成立〔代码＋CPU状态机〕**：同一work-loop的runAction同步timer，在确认sleep前完成已有采样；wake延迟3秒，stop后不能复活，notifier失败拒绝start。**证据不足〔整机〕**：未实测睡眠 |
| unavailable不填0 | **成立〔代码＋CPU〕**：未取得/失去原生计数资格时省略测量值；完整MMIO测量全部省略；采集器写JSON null。Paused与Stopped时旧值不能当新样本 |
| 默认无W1C | **成立〔静态〕**：适配器没有寄存器读写；本版也拒绝count/非零wa请求，防止误认S2已执行 |
| sticky首次与采样空窗 | **成立〔CPU模型〕**：保留StickyAtFirstSample；DC/断电/读失败/外部gap后重新建立baseline，首次sticky只记StickyAfterGap。该MMIO模型没有部署，不能称实机计数已经准确 |

本机framebuffer文件：
`115a4616e862228a8c82df6e52ba41ac02b9750f116352ff3575aadcc950888e`。
`__DATA`为`0xC8000..0x140000`；vptr`0xCBA80`、gMetaClass`0x138B80`和全部五个计数均在同段；
计数另落在`__llvm_prf_cnts`的`0xD0910..0xECB10`范围。
虚函数槽0/7/183/184/185的文件目标分别匹配`0x3E7A2/0x3E852/0x446BC/0x3F1FC/0x44982`。
计数指令`0x744CA/0x744EC/0x74514/0x74539/0x74084`的目标分别为
`0xE6CD8/0xE6CE0/0xE6CE8/0xE6CF0/0xE6BC8`。原始反汇编、字节表和运行时指针不上传。

不能把同一个work-loop存在当作已证明所有电源访问串行化：controller `getWorkLoop`在`0x462AC`，
framebuffer `setFramebufferPowerState`在`0x24DE8`，controller `setPowerState`在`0x27342`，
native power-well enable/disable在`0x5BEDC/0x5B366`。
还存在AGDC和port路径；本轮没有建立覆盖这些入口的只读lease。
`SafeReadRegister32`会请求/释放power-domain，不能悄悄以它替代并声称零硬件副作用。

代码修订语义：去掉不合格MMIO部署路径；保留待审查CPU模型并修复sticky空窗重复计数、
未供电IMR有效性、all-ones快照/WA写入和二次读取造成的陈旧RMW。
原生读取只对映射中、生命周期被活对象持有的固定globals进行；不是通用no-fault读取器。
运行时字节资格也不是整个内存映像的SHA校验，不扩大支持到其它donor文件。

本机16项CPU case（ASan/UBSan）及现有采集器解析测试通过；前者不执行IOKit timer/PM，
不替代重启或睡眠验收。SDK26.5构建，部署主文件SHA：
`192e72cf5b53da0b09d7b606177470a1a1cb94b9fb002fa5e596d9892b2ecf63`。
逐一将`nm -u`的311个导入与本机`kmutil libraries`解析表比对，无缺失；
`registerPrioritySleepWakeInterest`、`IOService::copyMatchingService`、`IOPCIDevice::extendedConfigRead16`均可解析。
不能只依据kmutil退出码判断所有符号均已解析。

`tgl-capture --probe`每秒记录探针dictionary和墙钟；每10秒复用现有ACK按需发布。
缺失/非字典/缺测量键的解析反例通过；新增寄存器写入计数非零时立即结束采集。
旧`driver/TGLDisplayProbe`未跟踪草稿已移入ignored local归档，仓库只维护`probe/TGLDisplayProbe`。

### 原生计数观测的一个明确盲区

**成立〔静态〕：**controller start的`0x3F236..0x3F254`解析`ig`；
`hwEnableInterrupts`在`0x757EE`测试其bit4/5，后续仅在此条件满足时解除pipe欠载bit31屏蔽。
`0x75A90..0x75ABB`、`0x75AFC..0x75B27`、`0x75B68..0x75B93`分别覆盖A/B/C。
handler的panic分支由bit4控制。当前EFI没有`ig`参数；本轮没有修改中断掩码或新增该参数。
因此“原生计数不增长”等同“没有欠载”的结论**不成立**；这是native计数与PIPESTATUS独立性的实际限制。
`ig=0x20`是后续可审查的原生报告候选，不是本轮已批准或执行的变量；`ig=0x10`会进入危险分支，不使用。

**证据不足〔整机〕：**未重启、未采集新的UU窗口、没有欠载关联或闪烁改善结果。
当前也不能按“无计数增长且低Skipped比例”排除欠载，因为MMIO覆盖完全不可用。

本轮引用的生命周期API依据：[Apple IOTimerEventSource文档](https://developer.apple.com/documentation/kernel/iotimereventsource)、
[Apple XNU IOWorkLoop源码](https://github.com/apple-oss-distributions/xnu/blob/main/iokit/Kernel/IOWorkLoop.cpp)。
外部审查与i915依据继续保留在[原报告](ClaudeReview-20261113-Display.md)，不改写其作者结论为实机事实。

## d852aa9 跟进：显示 S0 已命中用户反馈窗口

保留 [Claude 原审查](ClaudeReview-20261113-Display.md) 原文，下面是本机复核，
不是把外部静态推论升级为整机结论。全部数值见 [验证摘要](../validation/uu-flicker-followup-20261113.json)。

**成立〔整机只读〕：**2026-11-13 16:15:11–16:20:12（UTC+8），约301.27秒、31个快照。
每10秒只打开一个 AppleIntelFramebuffer 的 type-1 shared client 并立即关闭，不调用 method；
31次均成功。它触发已安装 WEX 的按需发布，包括既有 cursor observer，不是完全无扰动操作。
使用 `tgl-capture --display` 按键读取，未高频序列化整个 registry。
维护者在16:16:13附近反馈：UU开着，操作其它页面也会局部闪；这只是收到反馈的时间，不是精确 onset。

| 观测 / 主张 | 结果及结论 |
| --- | --- |
| TGLStreamErrorCount | 6 → 6；环内六项都在 uptime 17.698秒，ACK value 全为 `0xFFFFFFFF`，A/B/C各两项 |
| 六次等于六次欠载 | **不成立〔源码＋整机〕**：`tglStreamAck` 记录的是写入 IIR 的 ACK 参数含 bit31，包括 clear-all；这六次不能作硬件欠载证据 |
| 快照是否陈旧 | 正常/fast ACK 计数 A 增加93,084/14,457，B增加100,844/14,460；序号也持续增加，说明发布在更新 |
| 本轮闪烁对应新的欠载 ACK | **不成立〔该观测通道〕**：没有新条目；但 IMR/PIPESTATUS 未采集，不能据此排除欠载或其它扫描输出问题 |
| TGLScan2 | 1,063,666 → 1,120,758；每秒平均189.51条，10秒区间106.34–313.51条；每条2KiB |
| 这是190fps刷新 | **不成立**：updatePlane前后各一条，约94.75次调用/秒且是全局多个plane合计，不等于硬件刷新率 |
| 默认追踪开销 | **成立〔整机＋源码〕**：没有 `-wex-display-trace`，仍在上述频率替换OSData/registry属性；不是只有打开诊断才发生 |
| 开销导致闪烁 | **证据不足**：约0.37MiB/s的记录载荷本身不证明内存带宽耗尽；需测分配/锁/时序及对照效果 |
| 内存配置 | SMBIOS报告DDR4 2667MHz，16+8GB；**证据不足**以确认真实通道数或flex模式，不能据此直接选0x1F |

本机 framebuffer SHA 与原报告一致：
`115a4616e862228a8c82df6e52ba41ac02b9750f116352ff3575aadcc950888e`。
独立复核 `invokeDEErrorIntrHandler` 的 `0x744CA/0x744EC/0x74514/0x74539`，
确实更新 `0xE6CD8/E6CE0/E6CE8/E6CF0`；分派点`0x74084`更新`0xE6BC8`。
`0x7455D`调用 kprintf，`0x74565`调用 dumpMMIO。**成立〔静态〕**：这些是原生处理路径计数；
尚未读取加载后的值，非原子计数可能低估、也受中断是否送达影响。

对照 [Linux v6.12 intel_display_power.c](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/i915/display/intel_display_power.c)
的 `tgl_bw_buddy_init`、`gen12_dbuf_slices_config`、`icl_display_core_init`，
三项TGL初始化与条件确实存在；未知内存配置有单独处理，不能套用推测值。
[i915_reg.h](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/i915/i915_reg.h)
定义 ICL_PIPESTATUS=pipe base+0x58、bit31；
[intel_fifo_underrun.c](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/i915/display/intel_fifo_underrun.c)
的 `bdw_set_fifo_underrun_reporting` 在开启报告前写它清状态。
**证据不足〔本机〕**：寄存器缺省/固件遗留值错误、PSR/FBC实际开启状态，以及欠载与闪烁因果。
只搜常量未出现也不排除计算地址、DMC或固件写入；不把该方法提升为绝对缺失证明。

S1的直接计数读出和MMIO访问资格见 [部署门槛](Deployment.md#显示-s0-之后的-s1s2门槛)。
本轮没有部署探针或写显示寄存器；既有POSH问题另见下节，不与显示诊断混成同一项修复。

### 默认快照开销：最小修订候选

依据Claude报告第2.6节与上述实机频率，只修改 `tglObserveScanout`、`tglSnapshotFB`
的入口：复用已有 `-wex-display-trace`，关闭时立即返回；保留原生update/属性调用和所有字节守卫。
相关 registry 键在驱动内没有读消费者，属于观察输出，不是显示控制状态。
**成立〔源码＋CPU〕：**生产 `wrapTGLPlaneUpdate` 连续100次仍恰好调用native100次、参数不变，
默认快照写入为0；开启trace后原First/Last、FB状态快照及64组内存观察边界仍可执行。
同一最终fixture对修改前代码在“默认发布为0”断言失败，修改后通过；不是只检查源代码文本。

这是两处入口的默认开销修正，不宣称已经完成环形缓冲重构：显式开启trace时仍有原有registry开销；
`tglProgramRecord`的中断锁、其它诊断点及原生coverage插桩仍在。
**证据不足〔整机〕：**降低WindowServer CPU、温度或消除闪烁；当前闭源下游未修改。
DEBUG/RELEASE构建记录见验证JSON；它们基于已有0.8.2研究候选，不能为了这两处改动就替换现用下游。
未来合入下游时应只移植这两处入口，A/B保持其余功能和Metal一致。

## db7b36e 跟进：B1 与 A1–A3

本轮先做只读观测和精确二进制审查。维护者后来确认 slow2 目标事件、单内屏仍闪，
且无法同步录屏；后续转入 d852aa9 外部报告的 S0，不再等待同步视频。
没有改 EFI、bundle、boot-arg、显示布局，没有退出 UU、安装驱动或重启。
记录见 [验证摘要](../validation/uu-flicker-followup-20261113.json)。
输入 accelerator 为 `567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e`，
MTL 为 `25b6c72ee23fb71f91384982fb591d6d1e514c73be3fea99831b4559d0ec4faa`；原反汇编留在本机。

### B1：压缩修正和单进程开关

**成立〔整机只读〕：**当前下游存在 `TGLCCSAlignmentReady=Yes`、
`TGLIOSurfaceCCSPolicyReady=Yes`、`TGLIOSurfaceCCSAtStart=0`。
两次采样相隔 220.83 秒，`TGLCCSAlignmentLive` 的 revision=127，
calls 为 20,407,729 → 21,201,119；CCS_E states 和 corrected 均 0 → 0。
布局根据生产 serializer / `wrapTGLSurfaceAux` 核对，CCS_E 判据为 `(surface[6]&7)==5`。
Ready 是安装状态，AtStart 是启动时 accelerator+0x1474 的字节，不是当前所有表面的状态。
native IOSurface policy 修正点为 `0x246D9`、`0x2B872`。
`TGLNoPOSHReady/Live`、`TGLGpuRecReady/Live` 均缺失，含义是 unavailable。
UU 客户端当时在运行，但没有验证这整个采样区间始终显示远端动态画面，也没有按进程分流计数。
**不成立〔本次观测〕：**据此宣称“当前缺少压缩修正，或 corrected 持续增长”。
不能反推所有纹理均无压缩，也不能将 CCS_E 压缩与 CCS 计算引擎混为一谈。

**不成立〔静态〕：**`PoshDisable=1` 是一个已找到的 UU 环境变量。
实际路径和另一个被遗漏的既有策略如下：

| 路径 | 精确偏移 / 来源 | 结论 |
| --- | --- | --- |
| bundle 配置 | `0x21F55..0x21F97`：CFBundle → InfoDictionary → Development → PoshDisable；已解析 CFString，不靠猜键名；当前 plist 值为 0 | 全 bundle 配置，不是 getenv |
| 进程策略初始化 | 模块初始化入口 `0x44EE` 跳至 `0x10D0`；`getprogname` / getpid 后缓存 PID 和模式；WindowServer=1，SecurityAgent=2，其余（含 UU）=3；`0x11B2..0x11D7` 仅本 PID 返回缓存模式 | 进程分类是硬编码，不是用户可选开关 |
| PoshDisable 覆盖 | `0x21FA7` 调 `0x1167`；CFNumber 成功、类型=3、模式非0且原值≤1时，`0x119D` 写 1；`0x21FB8` 设 bPoshDisable，随后也设 bPtbrDisable | 正常模块初始化、当前 plist 下，静态路径抑制 POSH VS；尚未读取 UU 进程对象的实际布尔值 |
| 消费者 | `0x6E38..0x6E4D` 跳过 POSH VS 状态设置；`0x536D0..0x536D5` 跳过 extractPoshVSCompilerOutput | POSH shader 编译/设置与内核 context 的 POSH flag 是不同层 |
| 直接 getenv | `0x22992` 为 MTL_REPLACE_D32_S8_WITH_D24_S8，`0x22F73` 为 METAL_MANAGED_TEXTURE | 均不是已验证的 POSH / render-compression 开关 |
| defaults 路径 | `0x450D` 读 com.apple.driver.mtl.intel 的 plist；`0x46B3` 包含 _kIntelSharedExFtrPoshEnable；`0x4871` 将共享设置传入 IOConnectCallStructMethod | 没有已证明的 UU-only 作用域，不应写全局 defaults 来冒充单进程实验 |
| 另一 CFPreferences 路径 | `0x16684` 的域为 com.apple.telemetryGraphics，调用点 `0x262E` 处理字符串输出路径 | 不作为 POSH 开关 |

旧 `1e5e4f5b…53c6c` 与当前 MTL 在 `0x10D0..0x11D8`、`0x21F55..0x21FCB` 和
上述两个 VS 消费者窗口逐字节相同。**不成立〔静态〕：**把这个策略归因于 0.4.3 新修复，
或用内核“POSH Enabled”日志证明 UU 当时必然执行 POSH shader。
这是本机精确输入的新证据，与 #47 评论中撤回的个别设置效果说法分开。
**证据不足：**存在可直接用于 UU 的环境变量 A/B；本轮没有改 bundle 或 defaults。

### A1：no-POSH 后 POCS image 的释放

**成立〔静态〕：**`0x7BB13` 测 context+0x6E bit0x10，`0x7BB25` 请求 0x4000 字节，
`0x7BB31` 调 `IGSharedMappedBuffer::withOptions`，`0x7BB36` 把对象存入 +0xA0。
restore 调用返回点是 `0x7C131`，所以现候选清 flag 的时机在分配之后。

**不成立〔静态 + CPU〕：**析构根据该 flag 决定是否释放 +0xA0，因而清 flag 才新增泄漏。
本机 native `IGHardwareContext::free` 整个 `0x7C5D8..0x7C721` 不读 +0xA0，也不测该 flag：

| 清理动作 | 偏移 |
| --- | --- |
| 释放 +0xB0 / +0xB8 | `0x7C622..0x7C651` |
| scheduler cleanupSharedPrivateData 虚调用 | `0x7C65C..0x7C66D`，vtable+0x138 |
| host Scheduler5 对应函数是空函数 | vtable `0xD3940+0x10+0x138` → `0x39608..0x3960D` |
| 释放 +0xA8 / +0x98 / task+0x58 | `0x7C673..0x7C6E9`，+0x98 可先 transferOwnership |
| 基类清理 | `0x7C700..0x7C710` → IGGCObject::free `0x3B36`，移出 GC 队列、释放 GC 后进入 OSObject free |
| C++ 析构 | `0x7B808/0x7B812/0x7B81C` 进入 IGGCObject/OSObject 析构，不补 +0xA0 释放 |

GuC cleanup `0x19442` 做 TLB invalidate；Scheduler4 cleanup `0x1E126` 做 invalidate / detach，
不是按 POSH flag 释放 +0xA0。当前 host 路径才是本轮 CPU 回放范围。

`tgl_noposh_adapter_native` 现在执行**原始完整 context free 与原始 Scheduler5 空清理函数**，
仅 OSObject release 和 GC 基类尾调用为显式 mock；分配对象为 fixture。
在 POSH 原值 0x10 / 0xB3，以及生产 restore wrapper 清 flag 后的 0 / 0xA3 四种情况下，
都观察到其余五个成员被释放、+0xA0 保留。默认 adapter 与 required native mode 均通过。
**成立〔静态 + CPU〕：**存在 context teardown 层的原生释放遗漏；
**证据不足〔整机〕：**其实际 kernel footprint 增长率、task 退出后的最终回收范围。
测试通过表示复现遗漏，不是宣称消除了泄漏。

在析构前“恢复 flag”不能修这个遗漏。提前禁止分配也不能孤立实施：
`copyContextImage` 在 `0x7C841` 无条件读 +0xA0，`0x7C892..0x7C8A2` 取地址并复制 16KiB。
先置空会引入另一条消费者问题。没有擅加 release、提前清 flag 或扩大 hook。

### A2：清内核 flag 后，MTL token 和 batch 的去向

**部分成立〔静态〕：**清 flag 影响硬件提交头；它没有使 kernel dispatcher 自动忽略 POSH token。

- `IGAccelCommandQueue` 分派 `0x2F8EF..0x2F90E` 按 token 高位选择；
  `0x2F972..0x2F9AD` 仍调 executePosh，没有 context+0x6E 准入分支。
- `executePosh 0x53EFB` 的 0x20000..0x2000B 跳表同样不查该 flag。
  例如 0x20005 到 `0x541FA`，照常检查 token 大小、资源、修正 wait 地址；
  `0x5478B..0x547C1` 继续按 token bit2 与 queue+0x7A8 清空四个 dword 并更新历史。
  no-POSH **不会修掉这个 only-active 同步优化**。资源/格式错误仍返回 native 错误。
- `submitBatchBuffer 0x42F7A` 仍登记同一 batch 地址和参数；`0x42FFC` 读 context+0x6E，
  `0x43021..0x43035` 将 bit0x10 移入 BB_START bit20，最后 `0x43051` 写入 ring。
  清 flag 后该位为0；没有把整条 token 流拒绝、静默丢弃或返回“已完成”。

**证据不足〔GPU〕：**以上不能证明每个用户态 POCS batch 的双流依赖都能在 RCS 正确完成，
也不能证明所有异步消费者已停止引用 MTL 私有块。
加上 B1 的既有用户态 VS 抑制路径，原假说必须分为“用户态还生成哪些 token”和
“内核是否仍使用 POCS context/safe image”，不能把它们统称为一个 POSH 开关。
**不成立〔部署推论〕：**仅 no-POSH Ready 或清 flag 就解除 0.4.3 析构风险。
#109 的外部整机报告不能替代本机这一条端到端证据。

### A3：beginReset 返回 false 后的完整去向

**成立〔静态〕：**`0x7C91E` 检查 restore 结果，false 经 `0x7C995` 返回；
它还跳过 `0x7C931..0x7C978` 的页表地址重建。
**不成立〔静态〕：**上级因此停止重提交、重试、标记 wedged 或立即 panic。

host `IGHardwareCommandStreamer5::processGPUReset` 的路径是：
`beginReset(0x3A422)` → **不检查 AL** → `resetRing(0x3A42A)` →
`resubmitStampToRing(0x3A43C)` → `updateRingTail(0x3A446)` →
遍历待提交项 `0x3A44B..0x3A4BB`（部分标记，部分重新提交）→
更新 context+0xC9 / 原子计数 `0x3A4BD..0x3A4CE` → `endReset(0x3A4D6)` →
按需启用 stamp interrupt → `submitExecList(0x3A526)`。

GuC `0x1CCBE` 调 beginReset 后也不检查 AL，`0x1CCD5` resetRing，
`0x1CD40..0x1CDB0` 遍历工作队列 / 重提交 stamp，`0x1CDC7` endReset，
`0x1CE11` 发送 GPU-reset-complete 消息。之后检查的是 firmware 消息是否发送成功，
不是 beginReset 的结果。此 GuC 路径只做静态核对，不作当前模式的硬件验证。

本候选 `wrapTGLCSBReset` 初始化 CSB 后直接调用 native processGPUReset，没有捕获此 false。
所以此前修正只保证 wrapper 不虚报局部成功；**证据不足〔整机安全〕：**它比吞错更安全。
吞错会带着未经修正的 image 继续；现在 false 也会继续，还可能跳过页表恢复。
需在实际 scheduler 边界完成失败隔离、取消/错误通知和重提交协议，才能称完整安全恢复。
本轮保留如实的局部失败返回，不擅自丢任务、造完成或强加 panic；候选仍禁止作为已验证修复部署。

### slow2 与下一步分流

**成立〔手机视频〕：**输入 slow2.mp4，20.133 秒、H.264、显示方向 320×568、
约30fps、604帧。在帧111–112（3.700–3.733秒），上方窗口/壁纸的下沿短暂覆盖
原本露出的终端文字；帧113（3.767秒）恢复。这是局部水平边界变化，未据此认定 tile 损坏。
帧差排名只是定位工具，手持移动、曝光、压缩和正常内容更新都会进入排名，不作为闪烁计数。
这份导出文件不是保留120/240fps时间采样的证据；也不能用其播放时间换算真实故障持续时间。

**证据不足〔同帧对照〕：**没有这次手机视频对应的系统录屏，且远端内容是否也变化未独立核对。
“过去录屏没命中”并不等于“这次物理屏异常时合成结果正确”；即使同帧手机有而录屏无，
也应优先指向录制取样点之后的呈现/扫描/复用路径，而不是宣称完全排除渲染同步问题。
维护者已确认上述事件就是目标异常，且拔掉 HDMI 后内屏仍闪；无法同步录屏。
因此推进 framebuffer 的 S0 观测，不再把同步录制设为前置条件。单内屏可复现并不证明
上游候选具备所需背光/训练支持；暂不更换内核候选来代替诊断。

## 2026-11-13 后续：0.8.2 + #109 可审查内核候选

**部分成立〔静态 / CPU / 构建〕：候选已形成，尚未安装或取得整机验证。**
维护者授权继续修复并推送 main；较早的“遇到 MTL 不匹配即停止”记录仍保留在下方。
现在是拆分内核路径，不是放宽旧 MTL 的守卫。外部审查请使用
[本轮 txt 提示词](../review/2026-11-13/issue109-review-prompt.txt)，不要继续审查旧研究分支 HEAD。

### 来源与实际变化

- `278814e`：将 vendored driver 从 `db438fdf4d5fe903f5dfbc65537ed3884e2ea980`
  三方同步至上游 **0.8.2 / `4512191b9643e2391431fbd9639305b61568ee32`**，保留本地 DVMT、
  多引擎故障采集与 MMIO 改动。并非逐字相同的上游发行包。
- `3e52ff0`：下述内核候选、失败路径修正、回归测试及许可声明；验证文件绑定完整提交和源码 SHA。
- 参考 [SaqibAlMalik 的 #109](https://github.com/b00t0x/WhateverXe/issues/109) /
  [`e0aff60038de42520c078d833e0138a50cde2495`](https://github.com/SaqibAlMalik/WhateverXe/commit/e0aff60038de42520c078d833e0138a50cde2495)，
  拆入 `-wex-tgl-no-posh`、`-wex-tgl-nomc`、`-wex-tgl-gpurec`，以及默认关闭的附属
  `-wex-tgl-gpurec-halt`。保留 NootedGreen NP550XDA / JulioNectar XPS13Plus 来源。
- 不纳入 stampfix、MTL page-in 改写、VA fallback、nogva。vikenzo196 的 Metal 来源仍记入 NOTICE。
  没有 returned=submitted 或 signal 的替代完成路径；相关符号只留在原资格检查中，没有调用。
- **成立〔CPU〕：nomc 回退修正。**按实际两处字节回退，覆盖第二处已经写入却报错的情况；
  回退无法恢复原字节则返回 Unsafe，生产调用者拒绝 accelerator start。未知字节不强行覆盖。
- **成立〔静态 + CPU〕：no-POSH 错误传播修正。**原方案布局不符只计数，仍可返回恢复成功。
  现在缺 buffer / CPU 地址或布局不匹配均传回 false；native copy 仍恰好调用一次。
  `initWithOptions` 在 `0x7C131` 检查 false，进入 `0x7C186` 的 fresh image 路径；
  `beginReset` 在 `0x7C91E` 检查 false，经 `0x7C995` 返回失败。没有吞掉或伪造完成。
- **成立〔源码〕：gpurec installer** 现在返回真实的 route/halt 安装状态，不能只以字节资格成功返回成功。

### 本机 accelerator 的独立机制复核

输入完整 SHA 为 `567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e`。
本轮重新生成反汇编，只留本机 `local/`。下列为语义摘要；下方原始锚点表仍可复核。

| 判断 | 偏移证据 | 结论 / 证据层级 |
| --- | --- | --- |
| RCS 与 CCS 共用 GDRST bit 1 | RCS `0x84D39` 与 CCS `0x4E386` 写 ring+0x70=2；各自 `+0x78` 绑定 reset vector；`0x43773..0x43778` 写 GDRST `0x941C` | 成立 / 静态 |
| 原生仅回放本引擎 | `0x437E7` 取自身 ring+0x78；`0x437FA..0x43840` 回写；`0x4384E..0x4385B` 按自身 type 调 scheduler | 成立 / 静态 |
| RCU_MODE、CCS 附加寄存器未恢复 | RCS 常量表 `0xC2200` 32 项、CCS `0xBEE00` 4 项，均无 `0x14800 / 0xCE90 / 0x1A4D0 / 0x1A0A8 / 0x1A098`；构造分别 `0x85651..0x85692`、`0x4EB67..0x4EBA8` | 成立 / 静态 |
| RCS NONPRIV 不在任何 reset 表 | RCS 保存表有 `0x24D0..0x24FC` 多项；`populateResetRegisterList 0x28FC8..0x29C50` 的 19 项默认值中没有它们 | 不成立 / 静态；应说“缺默认恢复值”，不是“没有保存项” |
| eventTimeout reason 1–5 不做 real restart | reason 1 `0x15FC7..0x16019`，2 `0x15FA8..0x15FC5`，3 `0x15F92..0x15FA3`，4 `0x16076..0x160A1`，5 `0x160B6..0x16101`，最终返回 NULL `0x1610B..0x1611F` | 成立 / 静态；reason 1 仍执行 display stamp update，不能说“毫无副作用” |
| reason 6 发起真正恢复 | `0x16120..0x1614C` 调 scheduler vtable+0x188，返回 context+0xB8 的 channel | 成立 / 静态 |

**部分成立〔静态 + 历史整机〕：**上述机制能解释 resetOK=1 后兄弟状态仍损坏并重复报错；
Incident 中线程 111 的 `commandQueueStop → finishEvent → eventTimeout → checkForProgress`
与 #109 作者描述相符。但本机事故没有同时保存首次 reset 前后这些 MMIO 值及同一 stamp 的完整历史，
仍不能把“共享复位 → orphan stamp → 线程 111 → watchdog”全部标为已证明。
排除 stampfix 后更不能沿用作者“五次强制 hang 均恢复”的整机结论。

[#75，labrouss](https://github.com/b00t0x/WhateverXe/issues/75) 及 Aqpcet 的 TGL-H35 对照支持优先关注
视频 / CCS 恢复。禁用 CCS 的实验依据应是共享复位域，不是“CCS 不存在”。不据此断言 UU 必然提交 CCS。

### EnableMultiContext 与下游兼容边界

**部分成立〔静态〕：**`0x27488..0x27499` 以 accelerator 自身为 receiver、默认值 0 调
`utilGetProperty<unsigned>`。`0x280BE..0x280C1` 先读自身 property；缺失时
`0x28143..0x28182` 查自身 Development 字典；`0x281A4..0x28222` 再查
`IODeviceTree:/options`，将字符串形式的数字转换后覆盖。不是在此直接向 IGPU provider 递归读取。
**证据不足〔静态〕：**仅 OpenCore DeviceProperties 注入 IGPU 就一定会传播到 accelerator。
本轮没有改 EFI、NVRAM 或 personality。nomc 还修改高地址预留分支 `0x4BF0A`，
所以 native property=0 与 nomc 并非语义完全相同；第二处保留高地址范围是否必要仍需审查。

| 当前下游依赖 | 上游 0.8.2 + 本候选的结论 |
| --- | --- |
| `-wex-cdclk-sanitize` | 同名开关不存在；普通 CDCLK 处理不等于下游 sanitize 行为，覆盖证据不足 |
| `-wex-blfix` | 同名开关不存在；上游背光路径不能直接当成本机面板验证，覆盖证据不足 |
| `-wex-dcoff` | 同名开关不存在；实际电源状态策略等价性未证明 |
| `-wex-hdmi-clock600` | 同名开关不存在；600 MHz 上限修正未被此候选覆盖证明 |
| `-wex-hdmi-scdc-write` | 同名开关不存在；SCDC 写序列及下游补丁源码未取得 |
| `-wex-edp-fix-training` | 同名开关不存在；现用面板训练修正未被证明等价 |
| VA fallback | 本轮明确不移植、不恢复隔离 VA，不能算已覆盖 |
| `-wex-tgl-fix-memory-leak` | 当前下游在 Tahoe 未生效；本候选 no-POSH 不是旧 MTL 释放补丁，不能等价替换 |

因此 **不成立〔构建 / 部署〕：直接拿新构建覆盖当前闭源下游即可得到单变量 no-POSH 实验。**
应先取得下游合入，或另行完成独立适配插件的 hook 共存与准入审查；本轮没有假造一个可直接安装的插件。

### 验证结果与审查重点

记录：[issue109-candidate-20261113.json](../validation/issue109-candidate-20261113.json)。
默认 CPU 套件 224 项通过，69 个需原始输入的可选模式未执行；另外显式 require 的
`tgl_noposh_adapter_native` 在本机精确 SHA 上通过（仅执行 C++ qualifier，不执行 Apple 指令或 GPU）。
无效锚点逐一拒绝，未扩充旧 SHA 允许集合。DEBUG / RELEASE 完成链接、ABI/Compat、启动栈检查；
构建 SHA 和源码 SHA 在记录中。此前测试清单的 accelerator_fingerprint 共享模块遗漏已修复；失败记录保留。

nomc 旧 written-count 回退及 no-POSH 忽略错误各有一次语义反例失败；修复版在同一断言通过。
这证明具体 CPU 缺陷已修正，不代表闪屏和 hang 已修复。没有运行 mtl_posh_fix 测试，因为该实现未纳入。

优先审查 `pendingOwner/pendingBar` 和 good/extras 缓存对 native recovery 序列化的假设；
RCS/CCS 并行 reset / 嵌套 replay 是否会串状态；错误后兄弟 engine 是否仍在执行；halt 的长等待和恢复顺序。
`TGLNoPOSHReady` 表示安装/准入，不是所有 context 都已成功 sanitize；必须检查 Live 失败计数。
上述未知点已有本轮补充：scheduler 忽略 false；native free 与 POSH flag 无关地遗漏 +0xA0。
详见本文开头 db7b36e 跟进，不能继续将它们当作已关闭的部署风险。
本次录屏没有可靠命中用户确认的闪烁帧，不能宣布 POSH 已被证实为闪烁来源。

### 给下游维护者的请求草稿（未发送）

> 希望在现用 0.8.1 下游保留 HDMI、面板、时钟和背光增强的基础上，拆入
> SaqibAlMalik / WhateverXe#109（e0aff600）的 no-POSH、gpurec 和 nomc opt-in。
> 请默认关闭、逐点保持完整字节与设备守卫，发布对应源码和构建依赖指纹。
> 本机 accelerator SHA 为 567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e。
> 请不要合入 returned=submitted 的 stampfix，也不要把旧 MTL patch 套到 Graphics 0.4.3。
> 我们的候选额外修正了 nomc 部分写入回退和 no-POSH sanitization 错误传播，附 CPU 反例与双配置构建。
> 首轮希望只增加 no-POSH 一个开关，以保留同一 HDMI/镜像基线，核对 UU 闪烁及 POSH 生命周期。
> gpurec 的并发恢复和 halt 路径仍请共同审查，不作为已验收修复发布。

## 2026-11-13：上游 #109 准入核对（任务 1）

以下保留较早一轮的结论；后续授权拆分内核候选的结果见上方。

**结论：部分成立〔静态〕。**本机原始 accelerator / 旧 MTL / 隔离 VA 的新增固定锚点
均匹配；**不成立〔静态〕：可把 e0aff60 整包直接套用到当前 0.4.3 Metal。**
新版在唯一 MTL 43 字节窗口失败，原模式全文件出现 0 次。按维护者的停止条件，
本轮停在任务 1，未进行任务 2–7 的后续工作。没有改守卫、移植源码、CPU 测试、构建或安装。

参考源码是 [SaqibAlMalik/WhateverXe e0aff600](https://github.com/SaqibAlMalik/WhateverXe/commit/e0aff60038de42520c078d833e0138a50cde2495)，
经 GitHub commit/blob API 核对 16 个变更文件的完整 Git blob SHA-1。
核对器直接只读本机文件，解析 Mach-O segment 映射与符号表，不载入或执行候选。
本表覆盖该提交**新增**的全部固定字节数组、常量校验和符号偏移，以及三个 rel32 目标复核；
不代表继承的 CPS/host/CSB 路由、运行时对象、内核滑移后 hook 链均已准入。

40 个字节/常量检查中 39 个匹配，16 个固定符号全部匹配。
其中 accelerator 33 项（含 3 项派生 rel32 目标），旧 MTL 1 项，新 MTL 1 项，VA 5 项。
原始反汇编和完整 Apple 文件仅留在 ignored `local/`；这里只发布必要的锚点窗口与语义。
机器可读结果：[upstream-109-anchor-audit-20261113.json](../validation/upstream-109-anchor-audit-20261113.json)。

| 组件 | 完整 SHA-256 | 本轮状态 |
| --- | --- | --- |
| 本机 accelerator | `567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e` | 新增静态锚点匹配 |
| 旧 MTL（完整 bundle 备份） | `1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c` | 新增静态锚点匹配 |
| 0.4.3 MTL（当前磁盘） | `25b6c72ee23fb71f91384982fb591d6d1e514c73be3fea99831b4559d0ec4faa` | 不匹配 |
| VA（隔离备份，未恢复） | `a972cdb7ddd56bcf38c3ab71e874b73efc34bc683d30e49f0a1a1919625a726d` | 新增静态锚点匹配 |

#109 没有发布 accelerator 的完整 SHA；因此可以证明本机 `567511…d496e` 匹配其
16.0.32 的全部新增固定检查，不能单凭该检查声称已比较过作者整份 accelerator 文件。
旧 MTL 和 VA 的哈希分别吻合其头文件声明的前后缀，完整本机值如上。

### 偏移 / 期望字节 / 本机字节

偏移是无 slide 的 Mach-O VM 地址；本表各窗口的 file offset 与该地址相同。
`haltHead/Body` 是可选 `gpurec-halt` 的守卫，也包含在核对中。
VA 的 V2 是完整 363 字节窗口，未截断成前缀核对。

| 组件 / 检查 | 偏移 | 期望字节 | 本机字节 | 匹配 |
| --- | --- | --- | --- | --- |
| accelerator / no-posh / allocate | `0x7bb13` | `41 f6 45 6e 10 74 2c` | `41 f6 45 6e 10 74 2c` | 是 |
| accelerator / no-posh / clone | `0x7c15e` | `41 f6 45 6e 10 0f 85 e7 00 00 00 b8 ef ff ef ff 41 23 87 0c 10 00 00 0d 00 00 10 00` | `41 f6 45 6e 10 0f 85 e7 00 00 00 b8 ef ff ef ff 41 23 87 0c 10 00 00 0d 00 00 10 00` | 是 |
| accelerator / no-posh / restore | `0x7c421` | `41 f6 44 24 6e 10 74 31` | `41 f6 44 24 6e 10 74 31` | 是 |
| accelerator / no-posh / reset | `0x7c919` | `e8 74 fa ff ff` | `e8 74 fa ff ff` | 是 |
| accelerator / no-posh / batch | `0x4301f` | `31 c9 83 e0 10 48 c1 e0 10` | `31 c9 83 e0 10 48 c1 e0 10` | 是 |
| accelerator / no-posh / params | `0xb09a4` | `00 00 1c 00 00 00 00 00 00 00 00 00` | `00 00 1c 00 00 00 00 00 00 00 00 00` | 是 |
| accelerator / no-posh / params | `0xb0bc0` | `00 00 1c 00 00 00 00 00 00 00 00 00` | `00 00 1c 00 00 00 00 00 00 00 00 00` | 是 |
| accelerator / gpurec / gdrst | `0x43769` | `c7 80 08 4a 00 00 04 00 04 00 41 8b 4c 24 70 89 88 1c 94 00 00` | `c7 80 08 4a 00 00 04 00 04 00 41 8b 4c 24 70 89 88 1c 94 00 00` | 是 |
| accelerator / gpurec / replay | `0x4384e` | `41 0f b6 74 24 40 48 8b 07 48 8b 55 d0 ff 90 90 01 00 00` | `41 0f b6 74 24 40 48 8b 07 48 8b 55 d0 ff 90 90 01 00 00` | 是 |
| accelerator / gpurec / flags | `0x43625` | `41 0f b6 44 24 40 49 8b 84 c4 b8 08 00 00 8b 88 a4 00 00 00` | `41 0f b6 44 24 40 49 8b 84 c4 b8 08 00 00 8b 88 a4 00 00 00` | 是 |
| accelerator / gpurec / s5 | `0x380dc` | `89 f0 48 8b bc c7 a0 0c 00 00` | `89 f0 48 8b bc c7 a0 0c 00 00` | 是 |
| accelerator / gpurec / rcu | `0x28e7e` | `b9 fe ff fe ff 23 88 00 48 01 00 48 8b 97 90 11 00 00 48 c1 ea 21 83 e2 01 8d 8c 11 00 00 01 00 89 88 00 48 01 00` | `b9 fe ff fe ff 23 88 00 48 01 00 48 8b 97 90 11 00 00 48 c1 ea 21 83 e2 01 8d 8c 11 00 00 01 00 89 88 00 48 01 00` | 是 |
| accelerator / gpurec / nonpriv | `0x28f27` | `c7 80 d0 a4 01 00 34 b1 00 00` | `c7 80 d0 a4 01 00 34 b1 00 00` | 是 |
| accelerator / gpurec / whitelist | `0x28eba` | `f6 87 91 11 00 00 01 74 6e c7 80 d0 24 00 00 00 e1 00 00 c7 80 d4 24 00 00 10 70 00 00 c7 80 d8 24 00 00 34 b1 00 00 c7 80 dc 24 00 00 08 70 00 00 c7 80 e0 24 00 00 80 25 00 00 c7 80 f0 24 00 00 1c 73 00 00 c7 80 f4 24 00 00 44 22 00 00 0f ba a7 e6 0f 00 00 19 73 14 c7 80 f8 24 00 00 51 23 00 10 c7 80 fc 24 00 00 41 23 00 10` | `f6 87 91 11 00 00 01 74 6e c7 80 d0 24 00 00 00 e1 00 00 c7 80 d4 24 00 00 10 70 00 00 c7 80 d8 24 00 00 34 b1 00 00 c7 80 dc 24 00 00 08 70 00 00 c7 80 e0 24 00 00 80 25 00 00 c7 80 f0 24 00 00 1c 73 00 00 c7 80 f4 24 00 00 44 22 00 00 0f ba a7 e6 0f 00 00 19 73 14 c7 80 f8 24 00 00 51 23 00 10 c7 80 fc 24 00 00 41 23 00 10` | 是 |
| accelerator / gpurec / element | `0x3a3b9` | `8b 47 50 48 c1 e0 04 48 8b 5c 07 30 48 85 db 0f 84 5d 01 00 00` | `8b 47 50 48 c1 e0 04 48 8b 5c 07 30 48 85 db 0f 84 5d 01 00 00` | 是 |
| accelerator / gpurec / lutSCI | `0x15f5d` | `48 8d 35` | `48 8d 35` | 是 |
| accelerator / gpurec / lut | `0x15f67` | `48 8d 1d` | `48 8d 1d` | 是 |
| accelerator / gpurec / scheduler | `0x15f7b` | `48 8b b8 50 12 00 00` | `48 8b b8 50 12 00 00` | 是 |
| accelerator / gpurec / haltHead | `0x56978` | `55 48 89 e5 41 57 41 56 41 54 53 41 89 f6 49 89 ff 48 8b 7f 10 be 01 00 00 00 ba 07 00 00 00` | `55 48 89 e5 41 57 41 56 41 54 53 41 89 f6 49 89 ff 48 8b 7f 10 be 01 00 00 00 ba 07 00 00 00` | 是 |
| accelerator / gpurec / haltBody | `0x5699c` | `49 8b 7f 10 44 89 f0 48 6b c0 79 48 8d 0d 82 0d 08 00 44 8b 64 08 3d 48 8b 87 40 12 00 00 42 8b 0c 20 42 c7 04 20 01 00 01 00` | `49 8b 7f 10 44 89 f0 48 6b c0 79 48 8d 0d 82 0d 08 00 44 8b 64 08 3d 48 8b 87 40 12 00 00 42 8b 0c 20 42 c7 04 20 01 00 01 00` | 是 |
| accelerator / gpurec / haltHead | `0x56a2e` | `55 48 89 e5 41 57 41 56 41 54 53 41 89 f6 49 89 ff 48 8b 7f 10 be 01 00 00 00 ba 07 00 00 00` | `55 48 89 e5 41 57 41 56 41 54 53 41 89 f6 49 89 ff 48 8b 7f 10 be 01 00 00 00 ba 07 00 00 00` | 是 |
| accelerator / gpurec / resumeBody | `0x56a4d` | `e8 d8 21 fd ff 49 8b 7f 10 44 89 f0 48 6b c0 79 48 8d 0d cc 0c 08 00 44 8b 64 08 3d 48 8b 87 40 12 00 00 42 8b 0c 20 42 c7 04 20 00 00 01 00` | `e8 d8 21 fd ff 49 8b 7f 10 44 89 f0 48 6b c0 79 48 8d 0d cc 0c 08 00 44 8b 64 08 3d 48 8b 87 40 12 00 00 42 8b 0c 20 42 c7 04 20 00 00 01 00` | 是 |
| accelerator / no-posh / LRC CTX_CTRL identity | `0xc04a8` | `44 22 00 00` | `44 22 00 00` | 是 |
| accelerator / no-posh / LRC POSH_LRCA identity | `0xc0574` | `b0 21 00 00` | `b0 21 00 00` | 是 |
| accelerator / nomc / find | `0x27499` | `21 d8 48 c1 e0 21 48 b9 ff ff ff ff fd ff ff ff` | `21 d8 48 c1 e0 21 48 b9 ff ff ff ff fd ff ff ff` | 是 |
| accelerator / nomc / find | `0x4bf0a` | `f6 80 94 11 00 00 02 74 26 41 f6 46 24 02` | `f6 80 94 11 00 00 02 74 26 41 f6 46 24 02` | 是 |
| accelerator / gpurec / disk CSB opcode + native head (live 11 requires tglCSB12Enabled) | `0x3a3a4` | `b8 07` | `b8 07` | 是 |
| accelerator / gpurec-halt / forcewake call opcode | `0x56997` | `e8` | `e8` | 是 |
| accelerator / gpurec-halt / RCS reset-control descriptor | `0xd776d` | `d0 20 00 00` | `d0 20 00 00` | 是 |
| accelerator / gpurec-halt / CCS reset-control descriptor | `0xd77e6` | `d0 a0 01 00` | `d0 a0 01 00` | 是 |
| accelerator / gpurec-derived / halt native SafeForceWake | `0x56998` | `8e 22 fd ff` | `8e 22 fd ff` | 是 |
| accelerator / gpurec-derived / eventTimeout SCI LUT | `0x15f60` | `1c a3 08 00` | `1c a3 08 00` | 是 |
| accelerator / gpurec-derived / eventTimeout normal LUT | `0x15f6a` | `22 a3 08 00` | `22 a3 08 00` | 是 |
| mtl_old / mtl-poshfix / dealloc Find[43] | `0x7ed6b` | `48 8b 1d 3e b1 41 00 49 8b 3c 1e 48 85 ff 74 1b 48 8b 05 26 b1 41 00 41 80 3c 06 00 75 0d e8 7a fa 01 00 49 c7 04 1e 00 00 00 00` | `48 8b 1d 3e b1 41 00 49 8b 3c 1e 48 85 ff 74 1b 48 8b 05 26 b1 41 00 41 80 3c 06 00 75 0d e8 7a fa 01 00 49 c7 04 1e 00 00 00 00` | 是 |
| mtl_043 / mtl-poshfix / dealloc Find[43] | `0x7ed6b` | `48 8b 1d 3e b1 41 00 49 8b 3c 1e 48 85 ff 74 1b 48 8b 05 26 b1 41 00 41 80 3c 06 00 75 0d e8 7a fa 01 00 49 c7 04 1e 00 00 00 00` | `48 8b 1d 3e b1 41 00 49 8b 3c 1e 48 85 ff 74 1b e9 80 32 f8 ff 90 90 90 90 90 90 90 90 90 e8 7a fa 01 00 49 c7 04 1e 00 00 00 00` | **否：STOP** |
| va_quarantined / va-fix / N1 | `0x1336bf` | `e8 4c 27 ff ff 89 45 e4 48 8b 4d d8 48 81 c1 b0 2c 00 00 48 8b 55 d8 48 8b ba 20 09 00 00 48 89 4f 08 48 81 c2 b0 2c 00 00 48 8b 4d d8 48 8b b9 10 09 00 00 48 89 57 08 8b 45 e4 48 83 c4 30 5d c3` | `e8 4c 27 ff ff 89 45 e4 48 8b 4d d8 48 81 c1 b0 2c 00 00 48 8b 55 d8 48 8b ba 20 09 00 00 48 89 4f 08 48 81 c2 b0 2c 00 00 48 8b 4d d8 48 8b b9 10 09 00 00 48 89 57 08 8b 45 e4 48 83 c4 30 5d c3` | 是 |
| va_quarantined / va-fix / N2 | `0x130e7b` | `e8 a0 56 ff ff 48 8b 45 a0 8b 88 bc 09 00 00 83 c1 00 89 c9 89 ca 48 8b bc d0 10 09 00 00 48 8b 55 f0 48 89 c6 e8 7b 56 ff ff 48 8b 45 a0` | `e8 a0 56 ff ff 48 8b 45 a0 8b 88 bc 09 00 00 83 c1 00 89 c9 89 ca 48 8b bc d0 10 09 00 00 48 8b 55 f0 48 89 c6 e8 7b 56 ff ff 48 8b 45 a0` | 是 |
| va_quarantined / va-fix / N3 | `0x12d301` | `e8 9a 01 00 00 48 89 45 c0 48 8b 45 d0 48 89 45 a8 48 8b 45 a8 48 8b 08 48 89 c7 ff 51 48 48 89 45 a0 48 8b 45 e8 c6 40 20 00 48 83 7d d0 00 0f 85 0c 00 00 00 c7 45 e4 05 00 00 00 e9 32 01 00 00` | `e8 9a 01 00 00 48 89 45 c0 48 8b 45 d0 48 89 45 a8 48 8b 45 a8 48 8b 08 48 89 c7 ff 51 48 48 89 45 a0 48 8b 45 e8 c6 40 20 00 48 83 7d d0 00 0f 85 0c 00 00 00 c7 45 e4 05 00 00 00 e9 32 01 00 00` | 是 |
| va_quarantined / va-fix / V1 | `0x1c16` | `e8 93 c6 23 00 55 48 89 e5 48 8d 05 1e 9a 24 00 8b 00 89 07 48 b8 00 00 00 00 40 00 00 80 48 89 47 04 5d c3` | `e8 93 c6 23 00 55 48 89 e5 48 8d 05 1e 9a 24 00 8b 00 89 07 48 b8 00 00 00 00 40 00 00 80 48 89 47 04 5d c3` | 是 |
| va_quarantined / va-fix / V2 | `0x2c8fc` | `e8 1a 53 fd ff 45 8b 86 28 01 00 00 45 8a be 24 01 00 00 4d 85 c0 0f 84 4f 01 00 00 41 8b 86 60 01 00 00 c1 e8 05 0f b7 c0 b9 e0 07 00 00 29 c1 bf ef 07 00 00 29 c7 85 c9 0f 49 f9 4d 8b 66 08 83 e7 f0 c1 e7 0c 09 c7 41 bd 00 00 ff ff 41 01 fd 49 8d be 9c 42 00 00 31 db 48 8d 05 c3 0d 2f 00 41 b9 ff 00 ff ff 49 ba 01 00 00 80 f0 00 00 00 49 bb 01 00 00 80 0f 00 00 00 45 84 ff 74 7c 41 8b 8c 9e cc 41 00 00 48 8b 10 48 8b 70 08 48 89 77 e8 8b 70 20 89 37 48 8b 70 18 48 89 77 f8 48 8b 70 10 48 89 77 f0 48 89 57 e0 8b 57 ec 44 21 ca 81 ca 00 10 00 00 89 57 ec 41 8b b4 24 68 a8 00 00 c1 e6 10 0f b7 d2 8d 94 16 00 00 ff ff 89 57 ec 44 89 6f f4 48 c7 47 f8 00 00 00 00 c7 07 00 00 00 00 83 f9 02 74 67 83 f9 01 74 68 85 c9 75 68 c7 47 f8 00 00 00 00 eb 5f 48 8b 08 48 8b 50 08 48 89 57 e8 8b 50 20 89 17 48 8b 50 18 48 89 57 f8 48 8b 50 10 48 89 57 f0 48 89 4f e0 8b 4f ec 44 21 c9 81 c9 00 10 00 00 89 4f ec 41 8b 94 24 68 a8 00 00 c1 e2 10 0f b7 c9 8d 8c 0a 00 00 ff ff 89 4f ec 44 89 6f f4 80 4f fb 80 eb 0a 4c 89 57 f8 eb 04 4c 89 5f f8 48 ff c3 48 83 c7 24 4c 39 c3 0f 82 10 ff ff ff` | `e8 1a 53 fd ff 45 8b 86 28 01 00 00 45 8a be 24 01 00 00 4d 85 c0 0f 84 4f 01 00 00 41 8b 86 60 01 00 00 c1 e8 05 0f b7 c0 b9 e0 07 00 00 29 c1 bf ef 07 00 00 29 c7 85 c9 0f 49 f9 4d 8b 66 08 83 e7 f0 c1 e7 0c 09 c7 41 bd 00 00 ff ff 41 01 fd 49 8d be 9c 42 00 00 31 db 48 8d 05 c3 0d 2f 00 41 b9 ff 00 ff ff 49 ba 01 00 00 80 f0 00 00 00 49 bb 01 00 00 80 0f 00 00 00 45 84 ff 74 7c 41 8b 8c 9e cc 41 00 00 48 8b 10 48 8b 70 08 48 89 77 e8 8b 70 20 89 37 48 8b 70 18 48 89 77 f8 48 8b 70 10 48 89 77 f0 48 89 57 e0 8b 57 ec 44 21 ca 81 ca 00 10 00 00 89 57 ec 41 8b b4 24 68 a8 00 00 c1 e6 10 0f b7 d2 8d 94 16 00 00 ff ff 89 57 ec 44 89 6f f4 48 c7 47 f8 00 00 00 00 c7 07 00 00 00 00 83 f9 02 74 67 83 f9 01 74 68 85 c9 75 68 c7 47 f8 00 00 00 00 eb 5f 48 8b 08 48 8b 50 08 48 89 57 e8 8b 50 20 89 17 48 8b 50 18 48 89 57 f8 48 8b 50 10 48 89 57 f0 48 89 4f e0 8b 4f ec 44 21 c9 81 c9 00 10 00 00 89 4f ec 41 8b 94 24 68 a8 00 00 c1 e2 10 0f b7 c9 8d 8c 0a 00 00 ff ff 89 4f ec 44 89 6f f4 80 4f fb 80 eb 0a 4c 89 57 f8 eb 04 4c 89 5f f8 48 ff c3 48 83 c7 24 4c 39 c3 0f 82 10 ff ff ff` | 是 |

磁盘 CSB head 的原生值为 7。上游也接受运行时已由它自己的 CSB12 路由改为 11，
但要求 `tglCSB12Enabled` 同时成立；本轮没有把 11 写成磁盘预期。
`0x56998` 的 rel32 解析至 `SafeForceWake=0x28C2A`；两个 LUT 分别解析至
`0xA0280` / `0xA0290`。运行时是否已被已知 bridge 改写，需要未来独立核对。

### 固定符号检查

| 符号 | 期望偏移 | 本机偏移 | 匹配 |
| --- | --- | --- | --- |
| `__ZN20IGHardwareRingBuffer19resetGraphicsEngineEP17IGHardwareContext` | `0x43576` | `0x43576` | 是 |
| `__ZN16IntelAccelerator13SafeForceWakeEbj` | `0x28c2a` | `0x28c2a` | 是 |
| `__ZN16IntelAccelerator20getDefaultResetValueEj` | `0x2e60a` | `0x2e60a` | 是 |
| `__ZN12IGScheduler515processGPUResetE10IGHwCsTypeP17IGHardwareContext` | `0x380d8` | `0x380d8` | 是 |
| `__ZN12IGScheduler517getActiveContextsE10IGHwCsTypePP17IGHardwareContextPyS3_` | `0x380ec` | `0x380ec` | 是 |
| `__ZNK11IGAccelTask9getStampsEv` | `0x8248` | `0x8248` | 是 |
| `__ZN16IntelAccelerator17signalStampUpdateERK8IGBitSetILm64EE` | `0x2ab4e` | `0x2ab4e` | 是 |
| `__ZN19IGAccelEventMachine12eventTimeoutEi` | `0x15f22` | `0x15f22` | 是 |
| `__ZN16IntelAccelerator17initModeRegistersEv` | `0x28e1a` | `0x28e1a` | 是 |
| `__ZN26IGHardwareCommandStreamer515processGPUResetEv` | `0x3a396` | `0x3a396` | 是 |
| `_rcsResetRegisters` | `0x14b2b0` | `0x14b2b0` | 是 |
| `_ccsResetRegisters` | `0x14aeb8` | `0x14aeb8` | 是 |
| `__ZN16IntelAccelerator19populateAccelConfigEP13IOAccelConfig` | `0x270e8` | `0x270e8` | 是 |
| `__ZN21IntelTGLMemoryManager31newUnifiedAddressRangeAllocatorEP11IGAccelTask` | `0x4bed8` | `0x4bed8` | 是 |
| `__ZN11IGScheduler19haltCommandStreamerE10IGHwCsType` | `0x56978` | `0x56978` | 是 |
| `__ZN11IGScheduler21resumeCommandStreamerE10IGHwCsType` | `0x56a2e` | `0x56a2e` | 是 |

`initModeRegisters` 函数入口是 `0x28E1A`；`0x28E7E` 是内部 RCU 写入锚点，二者不能混称。
上述匹配只建立参考代码位置，不替代任务 2 对寄存器表内容和完整分支的独立语义证明。

### 0.4.3 的精确释放分支

| 地址 | 独立静态核对结果 |
| --- | --- |
| `0x7ED6B–0x7ED79` | 读取 CB `_pPoshUsedAsTarget`；NULL 跳过释放 |
| `0x7ED7B` | 由旧 only-active 测试改为跳至 `0x2000`，余下填 NOP |
| `0x2000` / `0x2007` | 读取 ivar 元数据 `0x499E20`，再从 CB 取 `_pIGCommandQueue` |
| `0x200B` / `0x2012` | 读取共享字段元数据 `0x499C20`，比较队列 `m_pPoshUsedAsTarget` 与 CB 指针 |
| `0x2016` | 不相等则跳至 `0x7ED89`，调用原生 operator delete |
| `0x201C` | 相等则跳至 `0x7ED96`，不释放队列共享指针 |
| `0x3CA8D` / `0x3CB00–0x3CB07` | queue init 清零 ECX，然后将零写入共享字段 |
| `0x7EA1F–0x7EA30` | CB init 读共享字段，为零则跳至 `0x7E9CB` 私有分配 `0x12010` bytes |
| `0x3B897–0x3B8AC` | queue dealloc 读取共享字段，非空时 delete，然后置空 |

旧新版 `__TEXT` 共 43 个**发生变化的字节**（不等于 MTL Find[43] 窗口的 43 字节）：
新 cave 占 `0x2000–0x2020` 共 33 字节，其中 29 字节从零变为非零；
旧分支 `0x7ED7B–0x7ED88` 改动 14 字节。队列 init/dealloc 和 CB init 完全未改。
完整原生 text 的共享字段元数据交叉引用只有上述 init、CB init、queue dealloc 三处；
另有新 cave 一处只读比较。未发现非空赋值。

**成立〔静态〕：**在未被其他 hook 改写的 stock 队列下，共享指针为 NULL 时，
每个到达析构的非空私有 POSH 块必走 delete。这里证明的是条件与 #47 两字节方案等价，
不是证明两段代码所有情况都相同，也不是证明任一释放必然造成 GPU use-after-free。
ivar 在文件中的值可被 Objective-C runtime 按类布局调整；上述判断使用元数据符号身份，
未把文件 ivar 数字冒充进程内最终对象偏移。

**不成立〔静态 + 既有整机日志〕：**仅以所有权比较、短测通过或加载新版，认定
“POSH 仍开启 + 0.4.3”低风险。本机已启动内核明确 POSH enabled；风险决策、观察标准
和完整 bundle 回退步骤见 [Deployment](Deployment.md)。

### 后续任务的门槛与结论边界

| 项目 | 结论 / 层级 | 本轮处理 |
| --- | --- | --- |
| 任务 1 | 部分成立 / 静态 | 原始 donor 匹配；当前新版 MTL 不匹配，触发停止 |
| 任务 2：共享 GDRST、reset 列表、reason 1–6 | 证据不足 / 参考源码与已有事故 | 未独立完成；固定字节匹配不是完整机制证明 |
| 任务 3：EnableMultiContext 属性来源 | 证据不足 / 未开展 | 未认定 DeviceProperties 可用；未改 EFI |
| 任务 4：下游覆盖与请求草稿 | 证据不足 / 未开展 | 未声称上游构建可替代下游，未发送请求 |
| 任务 5：移植、CPU 测试、DEBUG/RELEASE | 不成立 / 静态准入失败 | 当前整包不能直接适用；未构建，无新产物 SHA |
| 任务 6：实机 A/B/C | 不成立 / 源码约束 | 原 A 含 stampfix，不能原样采用；其余方案暂未设计/执行 |
| 任务 7：UU / Google RD 解码及引擎提交 | 证据不足 / 未开展 | 未把 VideoToolbox 当硬解，也未把 Google RD 当 VP8/VP9 软解；无提交计数则记缺失 |

`tglGpuRecEventTimeout` 的 `stamp[0]=submitted` 后接 `signalStampUpdate`，即便限定两次
NULL timeout 且引擎 idle，仍不能证明真实完成，违反本仓库禁止伪造 GPU 完成的约束。
该分支不得移植。获准恢复旧 MTL 基线并继续之后，可先独立研究 gpurec 与 nomc；
仅 gpurec 不承诺解决 orphan stamp 的持锁清理，需要另行核对原生失败/取消路径。

参考代码还有两点需保留：RCS 白名单在 `_rcsResetRegisters` 内，但作者说它不在
`populateResetRegisterList` 的**默认值表**，不能写成所有复位列表均缺少白名单；
nomc 有第二处高地址 reservation 修改，作者没有证明它必要，不能直接等同于单个属性置零。
这两点目前为源码合同阅读结果，不冒充任务 2/3 的本机独立验证。

### 来源与许可

- [#109，SaqibAlMalik](https://github.com/b00t0x/WhateverXe/issues/109)：同机型 CPU / PCI ID 的相关事故与 opt-in 方案。
- [#75，labrouss](https://github.com/b00t0x/WhateverXe/issues/75) 和 Aqpcet 的 TGL-H35 对照：视频相关触发线索。
- [#47，Aqpcet 的回归记录](https://github.com/b00t0x/WhateverXe/issues/47#issuecomment-6717895458)：两字节释放风险。
  其后续 PoshDisable 有效说法已撤回，不作为替代方案。
- [Graphics aa278800](https://github.com/b00t0x/WhateverXe-Graphics/commit/aa27880055cec32967bba5c54ac6ff8bfcf05fb9)：0.4.3 的所有权比较来源。
- e0aff60 的头文件注明 gpurec / stampfix / nomc / no-posh 来自 NootedGreen，
  对应 [JulioNectar XPS13Plus](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe)
  / NP550XDA 的 `kern_gen11.cpp`；MTL page-in 实现注明来自
  [vikenzo196/WhateverXe d34ded83](https://github.com/vikenzo196/WhateverXe/commit/d34ded83ad9c7bc616bd569bb989691f9a6e2ebf)、
  `c202fcb`、`7726115`。保留这些归属，不改称本项目原创。
- [e0aff60 的 LICENSE.txt](https://github.com/SaqibAlMalik/WhateverXe/blob/e0aff60038de42520c078d833e0138a50cde2495/LICENSE.txt)
  为 BSD-3-Clause，保留 vit9696 版权与免责声明。本轮仅作分析和锚点记录，没有将参考实现
  加入 driver；将来移植仍须逐项保留原文件归属并核对所引用 NootedGreen 源文件许可。

## 阅读顺序

1. `Findings.md`：三个主要问题的证据和反证。
2. `Provenance.md` 与 `Validation.md`：源版本、当前加载版本与测试层级。
3. `src/POSHAtomicClaim.mm`、`src/POSHAtomicCode.hpp` 与 `Concurrency.md`：原子计数候选和同版失败 / 通过对照；
   `src/POSHQueueCache.mm` 与 `POSH.md`：队列所有权候选。
   较早的 deferred 原型已删除；历史审查可从提交 `fd1a78a` 读取。
4. `research/0001-bcs-pending-capture.patch` 及对应三个源码文件：BCS 采集增强。
5. `Deployment.md`：不能越过的整机验证门槛。
6. `BCS.md`：等待指令现场采集的限定、拒绝条件和待回答的问题。
7. `Abandonment.md`：同队列弃用的真实失败、阶段不同的候选与晚发恢复循环；
   `validation/abandonment-tests.json` 明确哪些结果不属于最终释放逻辑。

## 希望审查者重点回答

### POSH

- 首先审查已复现的并发失败：`posh-queue-check --concurrent`。原版 active 计数丢失，
  队列实验还会让两个未提交 CB 同时借用缓存。only-active 也进入 GPU 等待 token，
  不能只修 free 分支。新原子候选已通过进程内对照，请核对领取点的寄存器存活、ZF、
  新线性化位置、移除的晚增加，以及 commit 减计数范围；CPU 测试执行实际生成指令。
- 原子领取在 POSH allocation 前发生，分配异常尚无回滚。新增可选 lifecycle 对成功初始化
  的未提交 CB 在完整 native dealloc 后归还计数；异常终止诊断，不能称已经支持回滚。
  请检查 token 保留、commit 先标记、析构重入、缓存身份和原生私有分配的延后删除。
  最后新增的未提交私有块删除路径尚无原生验收，勿把较早的纯计数结果转记给它。
- 跨 commit 的缓存重用是否早于旧 CB / descriptor 最后一次 CPU 访问，或 GPU 消费结束？
  交错提交短测通过不构成完整证明。容量探针没有观测到实际阻塞，覆盖不足需保留。

- 队列缓存是否在返回给调用方前完成发布？匹配的原生 init / dealloc、所有消费者和
  descriptor 清理是否真的支持该共享生命周期？active 数量在 commit 时下降，不能当作 GPU 在飞数量。
- 原生 render encoder 对计数表的重置能否覆盖 compute、blit、多 encoder 和跨提交路径？
  并发创建 CB、尚未完成的旧 CB 析构、错误恢复可能怎样干扰共享缓存？
- 正反两次 IOSurface 对照只验证内存斜率和正确性；呈现时间戳全零，不能计为显示时序通过。

### BCS / 恢复

- BCS 使用的 native 提交缓存结构与 owner/backlink 校验，是否需要额外的原生重放证据？
- 前后读一致只证明瞬时采样一致，不能固定内存所有权；现有 nofault 与 guard 是否始终
  在读失败时拒绝把数据解释为有效候选？
- 全局四槽预算是否足以取得第一个 BCS 失败？不能为诊断无限扩容或改变成功提交路径。
- 失败是 stamp、上下文、同步、命令生成还是恢复策略问题？现有证据不足以选择一个。
- 22:59:46 起的计算通道故障随后阻塞新 Metal 初始化，原生 commandQueueStop → finishEvent
  → eventTimeout → checkForProgress 在服务终止线程中反复等待。现有首次快照只完整采 RCS，
  该 RCS 全零且 detail ownership 拒绝，不可冒充当前 CCS / 旧 BCS 的等待目标证据。
  恢复调用返回与真正恢复进度应如何分别证明？短测后的失败不能被过程内 exit 0 覆盖。

### 呈现与整机

- UU 远端视频呈现、CVMetalTextureCache / IOSurface 与 WindowServer 合成共同出现，
  但未捕获异常帧。如何分离资源生命周期、平面 / tiling、复制与合成同步问题？
- 不能把 VideoToolbox 出现当成硬解证据，也不能把 CA::OGL 名称当成只使用 OpenGL。
- 上游新构建缺少未公开的机器特定 HDMI / 亮度修改，如何避免替换时引入回归？

## 建议审查输出

按严重性列出文件、代码位置、触发条件、为何错误、最小修改和验证方法。
单独列出待取得证据，不将推测写成已复现事实。不要建议通过关闭系统安全、隐藏 panic、
伪造完成或宽松化 SHA 检查来绕过失败。可直接审查本仓库，无需私人日志或 Apple 二进制。
