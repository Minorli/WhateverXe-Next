# 现场证据与问题拆分

观察日期：2026-11-12，时区 UTC+8。以下仅发布脱敏的汇总结论。

## 2026-11-13 后续：#109 内核候选已进入源码审查

维护者随后授权继续 #109、构建并推送自己的 main。0.4.3 的旧 MTL 匹配失败仍然有效；
本轮仅拆入内核候选，未移植旧 MTL / VA，也未修改实际 EFI 或已加载驱动。

- **成立〔静态〕：**本机 RCS init `0x84D39`、CCS init `0x4E386` 均把 reset domain
  设为 2，`0x43773..0x43778` 将其写入 GDRST。`0x437E7..0x4385B` 只恢复自身列表并回放自身引擎。
- **成立〔静态〕：**RCU_MODE 和四个 CCS 附加寄存器不在 RCS/CCS reset lists。
  需纠正宽泛说法：RCS FORCE_TO_NONPRIV **在 RCS 保存表里**，但不在 accelerator 的
  默认值表中；此前被兄弟复位破坏后，下一次自身恢复可能再次写回坏快照。
- **成立〔CPU〕：**修复 nomc 的“写入后报错导致部分补丁残留”；无法回退时显式 Unsafe，
  生产调用处阻止 accelerator admission。恢复原 written-count 逻辑后，相同测试在语义断言失败。
- **成立〔静态 + CPU〕：**修复 no-POSH 在布局不符时仍回报恢复成功；错误传回两个原生调用点，
  保留 native false 行为。去掉传播后，相同测试在语义断言失败。未证明完整调度器错误恢复正确。
- **部分成立〔静态 + CPU〕：**gpurec 的寄存器恢复机制有对应证据，helper 测试通过；
  完整 reset hook 的序列化、兄弟引擎活动中的安全性及真实硬件效果仍待审查。
- **证据不足〔整机〕：**POSH 是本机镜像双屏同步闪烁的根因、gpurec 能消除卡死、
  UU 与 Google Remote Desktop 的实际 codec/引擎对照。没有执行新的实机负载。

详细偏移、验证边界、构建指纹与下游合入请求草稿集中在 [REVIEW](REVIEW.md)。
以下“停在任务 1”的内容是较早一轮的历史结论，不代表本轮未构建。

## 2026-11-13 补充：#109 / #75 与当前部署的否定结论

**任务 1，部分成立〔静态〕。**核对 SaqibAlMalik 的
[`e0aff600`](https://github.com/SaqibAlMalik/WhateverXe/commit/e0aff60038de42520c078d833e0138a50cde2495)：
新增的 accelerator 字节/常量检查 33 项、固定符号 16 项在本机 `567511be…d496e` 上匹配；
旧 MTL `1e5e4f5b…53c6c` 的 43 字节窗口和隔离 VA `a972cdb7…a726d` 的 5 个窗口也匹配。
**当前 0.4.3 MTL `25b6c72e…c4faa` 不匹配**，原始 43 字节模式在整个新版文件中出现 0 次。
结论是“原始 donor 可对上，但不能直接套用到当前组合”，不是补丁已正确运行。
依照本轮“不匹配就停”的要求，任务 2–7 未执行，没有移植、构建、安装或实机诱发负载。
完整逐项表和校验范围见 [REVIEW](REVIEW.md#2026-11-13上游-109-准入核对任务-1)。

**优先风险，成立〔静态〕。**0.4.3 在 `0x2012` 比较 CB 私有 POSH 指针与所属队列
共享指针，非相等时跳至 `0x7ED89` delete。原生队列共享指针只被置空，无非空赋值路径；
所以 stock 队列非空私有块的释放条件与 #47 两字节释放实验相同。完整解释、
重启后观察与旧 bundle 回退步骤见 [Deployment](Deployment.md)。
这否定了“所有权比较足以证明 POSH 开启时安全”，没有证明本机现有闪屏就是这次修改造成。

此前遗漏的两条上游线索已纳入：

- [#109，SaqibAlMalik](https://github.com/b00t0x/WhateverXe/issues/109)：同为 i7-1165G7 / 9A49，
  报告恢复循环、WindowServer 内存增长与 watchdog。其 `kern_tgl_gpurec.hpp` 记录的
  `commandQueueStop → finishEvent → eventTimeout → checkForProgress` 与本机线程 111 相符。
  **部分成立〔外部整机报告 + 本机已有事故〕：**属于高度相关的恢复链线索；共享 GDRST
  破坏兄弟引擎状态、stamp 不 retire 及本机首次 hang 的完整因果链尚未独立核实。
  作者的五次恢复成功是其机器结果，不能转记成本机验证。
- [#75，labrouss](https://github.com/b00t0x/WhateverXe/issues/75)：9A49 上 VideoToolbox 相关负载
  出现 CCS hang / 重启循环；[Aqpcet 的 TGL-H35 对照](https://github.com/b00t0x/WhateverXe/issues/75#issuecomment-6742194654)
  区分了本地 H.264 正常与 HLS 触发。**部分成立〔外部整机报告〕：**支持优先调查视频呈现
  与跨引擎恢复，不能推出 UU 必用 CCS，或所有硬解都会失败。
  同帖 Alder Lake-P 的后续报告为 RCS hang，也不能挪作 TGL/CCS 证据。

**不成立〔源码〕：**直接接受 #109 的 stampfix 作为本仓库候选。
它在两次 NULL timeout、无 active context 时，将 returned 写成 submitted 再 signal；
未证明被丢弃工作已经完成。即使作者观察到客户端最终返回错误，该写法仍违反
本仓库禁止伪造 GPU 完成的约束。后续可继续审查独立 gpurec / nomc / no-posh，
但不得以“恢复到 idle”为由把该 stamp 写入伪装成正常完成。

建议下一轮先获准恢复旧 Metal 基线，再继续任务 2 的独立语义复核和任务 3 的属性来源调查。
这是明确的后续路径，尚无任何候选可以在本机安装或宣称解决卡死。

## 1. 内存持续增长

独立进程中，stock 驱动在 2000 个空命令缓冲后 malloc 使用量增加约 148.806 MiB，
与每次约 76 KiB 的私有 POSH 分配未释放相符。纹理和混合复制也有近似增长。
延迟释放原型抑制了这些短测的增长，详见 Validation；它只作用于测试进程。

实际 WindowServer 在同一启动中从退出 UU 后约 531 MB RSS，增长到约 1.7 GB、
2.4 GB；第二次 UU 远端画面复现后约 3.8 GB。RSS、footprint 和 malloc 使用量是不同指标。
此后 root `vmmap -summary` 记录 physical footprint 约 4.3 G、
MALLOC_LARGE resident / dirty 约 3.6 G，region count 52,217。
退出 UU 后 CPU 回落，但这些分配没有随客户端退出完全回收。

该汇总支持“真实合成进程内存持续增长”，**不能单独证明每个大分配都是 POSH**。
独立分配测试与真实进程之间仍需按分配来源建立更精确的对应。

后续受控 UU 复现中，同一 `top MEM` 口径的 WindowServer 从约 7,495MiB 增至 8,178MiB。
连接前 GUI 加载动画期间 CPU 已约 100% 单核，远程画面期间约 107–119%；
退出后回落。后续堆摘要发现 109,189 个 77,824 字节 non-object 分配，而仅有一个存活
MTLIGAccelCommandBuffer，另有一个仍被 CB 字段引用的相同尺寸 POSH 块。
尺寸分布与独立泄漏相符，但未启用分配栈记录，不能逐一给非对象块追溯分配来源。
RSS、top MEM、physical footprint 和 malloc 不同；不能把约 2GiB RSS 与约 8GiB footprint
直接相减，作为本次新增泄漏。

后续无 GPU 提交的原生四线程检查复现 active 计数错误；补齐队列缓存的实验还出现了
两个存活 CB 共享同一可变块。only-active 标志同时参与 POSH semaphore wait 的生成，
不能通过随意翻转标志修 free。该并发缺陷已保留为失败复现，候选禁止全局部署；
尚未证明它就是本机 BCS 卡死根因。详细边界见 `Concurrency.md`。

## 2. BCS 卡死与恢复失败

较早一次 panic 为 `submitToRing: Work queue failure detected`。
按对应 UUID 符号化后，路径包含 GL2D blit → submitSyncEvents → stamp → submitToRing。
因此不能只调查 Metal。

后续一次强制关机前，18:25:29–18:28:33 产生 16 份 GPU restart 报告：
9 份明确 BCS busy/hung，7 份是恢复期间 idle/disabled。首份 RCS idle、BCS hung；
较后记录的 BCS head / tail / start 没有继续前进。

| 时间 | 观察 |
| --- | --- |
| 18:25:23.832 | WindowServer synchronize timeout |
| 18:25:26.170 | checkGPUProgress hardware error |
| 18:25:29.330 | 恢复开始 |
| 18:25:29.682 | WindowServer 记录恢复 ready，经历约 8.57 秒；此后仍继续重复恢复 |
| 18:29:58 | 下一次系统启动 |

没有新增 panic 不代表没有卡死：强制断电未必留下 panic 文件。
CPU 侧看到全零的 ring bytes 也不能单独证明 use-after-free。
本项目的 BCS 改动是诊断补全，未修改提交、MMIO、完成语义或恢复算法。

18:25–18:28 的 9 份 BCS hung 报告均记录 IPEHR `0x0e009002`，对应 PPGTT polling/GTE
semaphore wait。报告没有保留足以可靠解码其等待目标的数据，不能选择“死锁”或“陈旧映射”
中的某一项作为确定根因。新增 `kern_tgl_hang_bcs_wait.hpp` 在原生失败上报时，
采集有界的 head 附近指令候选、上下文身份、软件 PTE 和等待值，全部 nofault 读取并重查。
未部署，不能称已取得新故障现场；细节见 `BCS.md`。

### 后续新增：计算任务后的恢复循环

同一天 22:58:08 前，新增生命周期候选的短时 GPU 探针返回成功。
22:59:46 起又出现真实 GPU restart，报告 Application 为系统 `mediaanalysisd`，
硬件 context ID 为 1019，报告的受阻引擎标为 CCS。报告中寄存器已是 idle / disabled，
不能由此确定最早失败的指令，也不能将系统进程名当作与之前候选完全无关的证明。

随后两个只准备做无提交对象检查的进程，在 `MTLCreateSystemDefaultDevice` 的
`IOAccelSharedCreate` / `IOAccelSharedSetupDirtyRing` 中阻塞，尚未安装最新候选。
系统 spindump 还记录 `IOServiceTerminateThread` 内的原生
`commandQueueStop → finishEvent → eventTimeout → checkForProgress` 等待链。
只终止本次诊断进程未解除已经进入的内核等待，未杀 WindowServer，未自动重启。

保留的首次 RCS STOP 前快照寄存器全零；detail 的 ownership guard 拒绝。
CSB intake 的错误字段没有报拒绝，但恢复计数继续增长，调用返回并不等于 GPU 工作恢复。
这些信息缩小了后续调查范围，不能把它们当成原始 BCS semaphore 目标已取得。

因此本轮长期验收明确未通过。新增私有 POSH 释放逻辑在此故障状态下无法完成 native
验证，真实 GPU 测试停止；进度和各源码阶段的边界见 `Abandonment.md`。

2026-11-13 的新 panic 与两份 watchdog 快照进一步证实：WindowServer 主线程
等待图形内核锁，持锁的 IOServiceTerminateThread 仍在上述清理 / 检查进度链中。
WindowServer 120 秒无 checkin、两次 induced crashes 后未退出，最终触发系统重启。
这确认了本次重启的直接机制，最初 GPU 不前进的原因仍未确定；详细时间口径与
证据见 [本次事故分析](Incident-20261113.md)，不要用新启动短时健康覆盖该失败。

## 3. UU 远端呈现触发闪屏

用户两次观察到：显示另一台电脑的远程画面后，屏幕下半部快速闪烁；
退出 UU 客户端后闪屏消失。后台远程服务保留。
第二次连接约数分钟后风扇加速，采样后退出客户端，没有为了复现继续运行。

UU sample 包含 Video Render、VideoToolbox、CVMetalTextureCache、Metal render encoder、
CAMetalLayer present；root WindowServer sample 包含 IOSurface / 纹理创建、
QuartzCore / SkyLight 与 AppleIntelTGLGraphicsMTLDriver 合成路径。

这些是触发范围证据，尚不能判定闪屏一定由 POSH、BCS、解码器或某一个同步点造成。

2026-11-13 用户新增对照：启动 UU 远程“必现”，Google Remote Desktop 未出现同类问题。
本轮未主动复跑；重复次数、客户端阶段、远端与视频设置尚未匹配，不能推断后者不使用GPU。
这组触发差异和后续对照设计已加入 [独立审查提示词](ExternalReview-20261113.md)。

VA bundle 已隔离，VideoToolbox 仍可能软解后用 GPU 呈现。
单帧截图没有捕获可识别的异常帧。UU 当前选择 60 帧，未找到公开的软解切换选项。

退出后的 19:05 短时样本 CPU idle 约 88–92%，GPU restart 总数仍为 40，
最新文件仍是 18:28:33；内存继续保留。此结果说明退出触发负载后压力下降，
**不是问题已经修复或不会再卡死的证明**。

## 4. VA 与运行组件

过去另有 VA 路径崩溃，相关 bundle 保持隔离，与 POSH 和 BCS 分开追踪。
官方图形包 v0.4.1 的 Tahoe accelerator SHA 与本机完全相同；
7 项 native replay 拒绝旧预期指纹，不能据此认定本机二进制被意外破坏。

现用下游 WEX README 将用户空间修复限定到 Sequoia。
Tahoe 上 boot-args 里出现泄漏参数，不构成该修复已应用的证据。

## 5. 其他日志的分级

同次巡检的近 10 分钟定向硬件 / 超时查询没有命中记录；新增 600 秒只读监听中，
recovery calls 与新增 GPU 报告均为 0。这只是当前窗口，不否定先前真实故障。

`MTLCompilerService` 的 XeCompilerAdapter sandbox deny 行需要结合实际返回与编译结果判断。
现用内核发布 `WhateverXeDataAccess=True`；继承源码的 wrapper 先调用原生策略，再对
固定路径作限定放行，原生 deny 日志本身不能证明最终访问失败。同一探针的运行时 shader
编译与像素检查通过，因此未采取关闭 sandbox 或重签系统服务的操作。

`EXC_GUARD` 中的 `tgl_hang_*` / `xelp_memory_layout` 是 CPU 测试进程名，不能按名字
解释为 GPU 硬件模块再次崩溃。Wi-Fi RSSI=0 的统计错误、Chrome 的 host_statistics64 参数
数量警告、taskgated 的 DetachedSignatures 查询失败分别保留，尚无与 BCS 卡死的因果证据。
这些行不作为删除系统数据库、关闭同步或停用工作软件的依据。
