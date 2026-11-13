# 源码深审：2026-11-13

审查从 `5851889cb10a6a801be6256dde9090ef287dd56d` 开始；继承驱动基准为
`400502276ee36358bc817409b06dc3ec50ffa1bd`。原事故审查包继续固定在原提交，
本文记录后续工作，不将此前结果归到新代码名下。

**完整源码审查尚未完成，卡死、闪屏、高内存与高温尚未修复验收。**
本阶段先阅读代码、核对精确二进制与已有事故，再做 CPU 回归和只读观测。
当前安装的下游驱动与本仓库研究构建不同；本阶段没有替换 EFI、系统 bundle 或加载新 kext。

## 已确认并修正

### D01：CSB 故障记录截断 owner flags

原 `tglCaptureCSBFailure` 从 owner `+0x1190` 向一个 64 位字段复制 **1 字节**。
原生字段实际为 64 位，`0x1800` 的 forcewake 模式与 `0x18000` 的依赖处理模式
因此都不会进入记录。这个缺陷会误导故障解释，没有证据证明它导致原始 GPU hang。

将只读复制提取为 `kern_tgl_csb_failure.hpp`，保持失败时才执行、nofault reader、
六引擎固定容量和原有布局。完整复制 8 字节，并继续记录实际复制长度；部分复制不能
当成完整值使用。增加规范内核地址和偏移边界检查，schema 从 307 升为 308。
Engine/Capture 的大小仍为 1456/8752 字节。

回归在原 1 字节行为上先失败：`bytes=1 flags=80`，exit 1；修正后 exit 0。
合成 owner flags 为 `0x1234000000018880`，同时覆盖高位、短读、非法范围和 HWSP tail。
这不是对实机故障状态的注入或伪造。

### D02：测试前置检查接受未知恢复记录

原 `GPUHealth.hpp` 接受长度至少 16 字节、头两个计数为零的任何数据，未检查
`TGLRecoverySinglePassReady` 或完整布局。缺失、截断、扩展或不一致的监测数据
不能用于批准后续 GPU 对照。

新增共用 `RecoveryState.hpp`，只识别已知 r123 布局的 2064 字节和显式 ready。
calls 或 returns 任意非零均为 recovered，即使两者相等；计数零但记录区非零为
inconsistent。使用 memcpy 读取，支持非对齐输入。其他情况为 unknown。
四个此前缺少入口保护的 Metal/GL 探针也接入检查，并在工作期间的检查点复查。
异步和 GL 混合测试发现同伴错误后停止继续提交。

`tgl-capture` 使用相同解析器，明确输出监测状态。`recovery_monitor=clean` 只表示
本次读到的已知恢复记录为空。`comparison_baseline_clean` 还要求报告目录在本轮
始终可读、没有新增 gpuRestart、四个 hang-batch 槽位都没有故障数据。读取中断后
本轮不再恢复这个资格；读取恢复时只重新建立报告文件基线，不把旧报告误判为刚发生。
尚未创建的 Retired 目录可接受，活动目录丢失或已有归档不可读则不可接受。
新增 filesystem 回归直接调用生产观察函数，覆盖归档移动、新报告及部分目录失败，
不访问真实日志、不创建 GPU 对象。监测状态变化立即落盘。

以上任何 clean 标签都**不表示 GPU 已被证明健康**。报告的两次读取之间仍有窗口，
历史故障也可能未留下这些属性。检查与提交之间仍存在时间窗口；已阻塞的内核调用
不能通过用户态检查强行解锁。

### D03：预停止快照忽略已经观测到的唤醒确认丢失

`kern_tgl_hang_prestop.hpp` 检查起始 render ACK，已有的两个寄存器采样和末尾
GT ACK 却不参与最终有效性判断。合成测试让后续 ACK 归零或变为全一，旧代码仍
返回 guard=0；回归先以 exit 10 证明误判。

修正复用已有采样：任一 render ACK 无效为 guard 5；在 GT 数据已被采集的情况下，
末尾 GT ACK 无效为 guard 6。保留原有 owner/slot 错误优先级，不增加 MMIO、
forcewake、等待或硬件写入。修正后 sanitizer 回归 exit 0。
它只能拒绝已观测到的失效，不能证明整个采样期间持续持有硬件租约。

### D04：DVMT 不支持的输入仍允许改写显存公式

`processKernel` 原来在平台不可用或 GMS 编码不被解码器支持时，可能留下
`enabled=true`，同时 `dvmt` 为零或沿用旧值。现在每次重新初始化清空 `dvmt`，
拒绝这些输入，并在 framebuffer 改写入口再次检查 enabled / available / 非零大小。
实际提取的函数在旧实现上以 GMS `0x10` 触发回归失败；修改后，全部不支持编码、
关闭选项、不可用平台与设备发布失败检查通过。

本机读取到的 GGC 为 `0x02c1`，GMS=2，对应原生公式支持的 64 MiB，
所以这个修正**不解释本机 UU 故障**。

### D05：Compat 最后一个合法 MMIO 元素被拒绝

read32、同一 base 的 read64、备用 base 的 write64 使用 `offset >= size-width`，
错误拒绝了刚好覆盖映射末尾的合法元素。改成 `>`，保留 size 小于访问宽度的拒绝、
原生 DSB 写入分发及原有备用 base 语义。针对实际提取的三个函数，旧实现首先报
`read32 rejects last valid element`；修正后边界、越界、短映射与硬件拒绝回归通过。

在本地修正后比对 2026-11-13 的上游，发现上游 `d0afdb6f3f0cda5a9c6a6cdcc86da9e6c0c6e21f`
已经作出相同的三个边界修正。这属于与上游对齐，**不是待回送的新发现**。
上游还单独修正了 DSB GGTT 范围；尚未把整个后续版本合入本研究基线。
备用 base 的真实范围仍不能仅由 manager size 证明，当前没有扩大该 ABI 的权限。

### D06：同名测试覆盖失败与日志

保留的测试运行器把 C++ 和 Python 用例都以文件 stem 为键。`tgl_start_order` 与
`xelp_forcewake_irq` 各有两个不同任务，却共用结果键和日志文件；后执行结果的覆盖
可以隐藏先前的失败。旧运行器本轮显示 211 个通过，实际上调度了 213 个任务，不能
把这个汇总当作 213 个任务独立通过的证据。此前 210 项及 `validation/driver-tests.json`
的 216 键历史汇总也必须按旧运行器的同名覆盖限制解读；保留原始记录，不改写成新结果。

新增回归执行实际运行器的规划、调度和汇总代码，只将同名任务命令替换为受控的
exit 7 / exit 0。旧实现返回 exit 0，故回归先失败；修正后保留两个结果和两份日志，
整体正确返回失败。真正重复的任务 ID 在运行前拒绝。
生产测试现在分别用 `cpp_` / `py_` 前缀，增加 `--jobs` 控制并发；本机复测用 2。
这是上游 `2976a89f41f28a22576cd4f1ae146efc1952d508` 已处理问题的窄范围对齐，
保留本研究分支的 CI JSON 格式，未声称回送上游的新发现，也未解决并行启动多个
运行器实例时的输出目录共享问题。

## 原始故障的因果边界

事故记录证明：IOServiceTerminateThread 持有图形清理锁，卡在
`commandQueueStop → finishEvent → eventTimeout → checkForProgress` 等待链，
WindowServer 无法 checkin，最终触发 watchdog。见 `Incident-20261113.md`。

精确 accelerator 的 `checkForProgress` 自身具有有限重试，不能称为无限循环。
单次 reset 返回、calls=returns 或 resetOK=1 都不能证明后续任务完成，也不能证明
清理等待链已解除。必须分别捕获初始引擎故障与后续清理阻塞。

当前预停止捕获以 RCS 为对象；最新事故涉及 CCS，更早报告涉及 BCS。RCS 的零值
或拒绝记录不能回答另一个引擎的最初等待位置。扩展观察需先核对各引擎、寄存器与
forcewake 的所有权，不能只替换寄存器基址。

精确 accelerator 的 streamer 初始化使用六条描述符：0=RCS、1=CCS、2=BCS、
3=VCS0、4=VCS2、5=VECS0。当前只读 CSB 记录中 engine 1 存在实际消费记录；
Linux v6.12 TGL 平台的引擎 mask 不含 CCS，并不足以证明 Apple 的 engine 1 不可用，
更不是仅据该 mask 直接禁用它的依据。新增的
[#109（SaqibAlMalik）](https://github.com/b00t0x/WhateverXe/issues/109) 与
[#75（labrouss）](https://github.com/b00t0x/WhateverXe/issues/75) 提供了不同的调查理由：
CCS 复位可能经共享 GDRST 破坏 RCS 状态。**部分成立〔参考源码/外部整机报告〕：**
应重新评估单独关闭 multi-context 的可回退实验，而不是因“CCS 不存在”禁用它。
本轮字节核对在 0.4.3 MTL 不匹配处停止，尚未完成共享复位机制及属性来源的独立证明，
未更改 EFI 或关闭 CCS。晚期 gpuRestart 虽然明确写着 CCS，但各环已处于停止/恢复
状态，这不能替代第一故障时刻的指令、依赖和上下文证据。

下一步捕获应先在原生 STOP 之前保存对应引擎的调度器、streamer、slot/context 与
复制长度和起止时间；仅有 CPU 复制仍是顺序快照。任何新增寄存器读取都需要单独
证明该引擎的地址与当前持有的 forcewake 域，禁止以 RCS 成功快照代替 CCS/BCS。
这一设计的 CPU 观察部分已在后续代码中实现，见
`HaltObservation-20261113.md`；尚未部署或取得真实多引擎 halt 记录。
仍没有新增 CCS/BCS MMIO 观察。

## POSH 审查结论

已读本仓库 atomic claim、queue cache、deferred 与生命周期实现，并核对精确 MTL
的 init、commit、dealloc、descriptor 初始化及 wait token 路径。

- 活动计数在进入父类 commit 前已减少，因此不能代表 GPU 在飞数量或完整共享租约。
- POSH 指针会进入 render/compute descriptor；only-active 同时影响 wait token，
  不是可以任意翻转的纯释放标记。
- 原子计数修正解决已有证据中的 CPU 领取竞态，尚未证明跨 commit 的共享缓存安全。
- 未提交私有块的最终释放候选仍未完成原生验证；不得继承更早纯计数版本的短测结果。
- deferred 的 error completion 不自动等同于所有硬件消费者已停止，需独立核对。

下一步需要跟踪 descriptor 的读取/清理与原生提交边界，证明每一类分配的最后消费者。
尚未把 POSH 缺陷与旧 BCS 等待目标或最新 CCS 故障现场闭合。

继续核对到 `deleteContextStateDescriptor` 的 CHAL 析构链：command transport
析构释放自身两个其他成员，heap manager 析构为空，state-heaps terminate 清理
自己的状态区。这些已读函数没有直接读取 POSH 表；这仍不能替代整个 alias / escape
审查。

补充核对当前 Metal image UUID `6F93BBF4-F2EA-3F4B-904D-1E907FAF705A`：
`MTLIOAccelCommandBuffer` 的二参数 initializer 将 debug 参数置零，并用同一对象
重新派发三参数 selector。`_MTLCommandBuffer` 的二参数入口也如此。因此，精确
MTL donor 的这两个常用队列创建入口会进入已拦截的三参数 initializer；不再把这条
具体入口覆盖列为未知。但这个证据不外推到其他系统版本或所有可能的创建方式。

父类 `commit` 会设置私有数据、执行同步通知，并将 command buffer 交给队列的
enqueue/commit 方法；原生子类的活动计数已在这些操作之前减少。父类析构还执行
多组 dispatch-list 回调、同步通知、队列可用信号与引用释放。这支持把未提交 claim
保留到整个原析构返回后，仍不能证明所有 POSH alias 的最终生命周期。
本次通过进程自身的 Objective-C runtime 与只读代码字节核对这些实现，没有创建
Metal device/queue/buffer、调用这些方法或修改系统代码。原始反汇编留在本机。

## 其他待核实项

尚未发现当前显示工作负载触及 D05 的末尾元素；不能将该修正称为闪屏根因。

DVMT 指令扫描器的指令种类、操作数宽度、目标寄存器一致性和完整映像边界仍需
单独审查。D04 只修正输入准入；不能说整个 DVMT 改写器已被证明正确。

核对 Lilu 1.7.2 的 trampoline 实现与 accelerator 引用函数：短序言含相对寻址的
工厂入口已通过 vtable 路由避开普通 trampoline；semaphore space 已避免搬移 CALL。
简单扫描得到的地址相关指令不是缺陷证明，必须同时核对实际路由方式、覆盖长度及
Lilu 的 coverage 指令处理。相关源文件和调查中间件只保留本机。

## 下游 HDMI 增强：单独核对执行证据

用户补充：下游增强解决了外接 HDMI；回忆闪屏应当在两块镜像屏上同时发生。
这是用户观察，尚无同步视频/原始像素捕获。两屏同时受影响使共用渲染、buffer 和同步
成为优先假设，但镜像本身共享内容，不能因此完全排除显示驱动。

当前启动参数含 `-wex-hdmi-clock600`、`-wex-hdmi-scdc-write`、
`-wex-cdclk-sanitize`、`-wex-dcoff`。实际下游文件 SHA 与原事故一致。
下游发布库公开预编译文件，所指 `xiananliu/WhateverXe-Dynabook` 源码地址在
2026-11-13 仍返回 404；这不区分仓库私有、改名或不存在。
因此已开始对本机精确二进制的相关函数进行静态核对，未更换驱动。

- 系统当前报告双屏 2560×1440、60 Hz、镜像。保留的原生 timing 记录显示
  外屏 framebuffer 1 的像素时钟为 241,500,000 Hz，内屏记录为 138,500,000 Hz。
  逻辑桌面分辨率不能代替原生输出 timing。
- `WEXHDMIClock600Ready/SiteA/SiteB=true` 证实两处模式上限修改已装入。
  修改位置属于 `getOnlineInfo`，不等于把实际输出时钟强制设为 600 MHz。
- `WEXSCDCWriteReady=true` 只说明 hook 就绪。精确下游 `wrapTGLLightUpTMDS`
  的新增写入路径还要求 FB1 / DDI1、特定原生布局以及像素时钟 `(340,600] MHz`。
  当前没有 `WEXSCDCWriteTrial` 或 `WEXSCDCSourceReadback` 记录。
  当前启动证据因而不支持“这段高时钟 SCDC 写入正在导致闪屏”，也不能排除其他增强。
- CDClock sanitize 与 DC-off 已就绪；前者存在实际执行记录，不能用 SCDC 未执行
  去替它排除。还需核对调用时机、原生 power/pipe 保护和失败处理。
- `WEXTGLMTLLeakReady=false`、`WEXTGLMTLLeakRequested=false`，尽管启动参数中有
  memory-leak 选项。这与下游文档对用户态修正仅限 Sequoia 的说明一致；
  **当前 Tahoe 上不能假设下游已经修好 MTL 泄漏**。

实机只读 30 秒记录仍为 recovery calls=returns=0、无新增 gpuRestart。
同期 `mediaanalysisd` 有较高 CPU 使用，采样为图像/文档识别调用链；这既是对照实验
必须记录的背景负载，也不能据此归因为异常、内存泄漏或 GPU 故障。

### 显示时钟与节能：已执行不等于致因

`TGLCDClockSanitize` 是四个 32 位字段。本机记录为
`before=344, reference=2, recognized=0, after=1304`。
Linux v6.12 `drivers/gpu/drm/i915/display/intel_cdclk.c` 的
`skl_cdclk_decimal`、`icl_cdclk_table` 和 `intel_init_cdclk_hooks`
共同支持将这两个编码解释为 TGL 的 172.8 / 652.8 MHz 档位；decimal 字段存在
量化，不能把 `(344+2)/2=173` 当成精确硬件频率。参考源码保留了版本与 SHA256。

精确 Apple framebuffer 的 `probeCDClockFrequency` 不识别 344，返回零；
`initCDClock` 对零结果选择其最高表项作为目标，`calculateCDClockFrequency`
在有外屏时也选最高表项。下游 sanitizer 调用原生 disable/set 后再调用 probe，
所以不是仅凭“频率升高”就能认定有误。该记录是某次 probe 的前后值，**不是持续
采样的当前时钟**；尚未证明空闲功耗变化或 UU 故障与该补丁的因果关系。
关闭它之前需证明原生初始化、pipe 状态和时钟切换仍可工作。

DC-off 的精确 wrapper 在 enabled / ready 时拦截 HW DC6 开启和 SW DC6 的 true
请求，保留 SW false 调用。本次记录 `HW calls=684, skipped=684`，证明禁用路径
实际执行，不只是开关存在。它是后续空闲功耗对照的重要变量；这些计数既不表示
GPU 负载，也不能量化省电损失。保留现有配置，未为验证节能而改变显示电源状态。

背光修正的写入范围是 PWM 频率/占空比寄存器；当前记录证实执行过。两个屏幕
均闪的用户观察，使“仅内屏背光异常”不足以解释全部现象；仍缺乏同步影像证据。

本轮 CPU 编译时一次 `powermetrics` 样本报告 CPU die 77°C、package 21.36 W、
GPU active 1.56%。工具同时报告若干 SMC key 不可用，未取得可靠风扇转速。
这些是编译期间的单次系统读数，不是空闲基线，不是 UU 复现，也不是硬件传感器校准。

### 与当前公开上游的差异

2026-11-13 核对的上游 main 为 `4512191b9643e2391431fbd9639305b61568ee32`，
发布 0.8.2；比归档基准 `db438fdf4d5fe903f5dfbc65537ed3884e2ea980` 多 11 个提交。
其中涉及 Compat 范围、测试汇总、图形激活时序、早期固件快照和 Display13 唤醒。
尚未整体合入或安装。官方 0.8.1 的回归与“基于官方 0.8.0 的下游 0.8.1”不能混为一谈。

## 验证记录与下一步

根构建与五项 CPU/文件系统测试通过；修正后的驱动运行器记录 214 个独立任务，
0 失败。DEBUG/RELEASE 均编译成功，继承警告仍在，不宣称无警告。
D01、D03、D04、D05、D06 均保留旧行为失败 / 修正后
通过的回归记录；D02 覆盖不完整或未知恢复记录的拒绝。旧 CPU 套件汇总须受 D06
限制解读；最终独立任务结果、构建指纹与覆盖范围以 `validation` 记录为准。
源码阅读清单区分 read / partial / unread，阅读完成不等于逻辑已证明正确。
`kern_xe.cpp` 的全部 20,379 行已读；剩余其他平台/DSI 文件继续列为未读，不能据此
称整个驱动已完成审查。构建和 CPU 回归也不覆盖这些原生 ABI、硬件时序与故障恢复。

当前启动的只读观测未发现新增恢复/报告。该结果没有主动 UU/Google Remote Desktop
对照，也不能代表延迟故障、睡眠唤醒或长期稳定验收。

继续按模块阅读，保存具体阅读范围；先确认缺陷和最小回归，再进行受控原版/候选对照。
每次对照需记录准确源码/二进制、独立故障监听、温度与内存斜率，并观察结束后的延迟故障。
