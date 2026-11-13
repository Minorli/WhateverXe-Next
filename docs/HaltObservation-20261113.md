# 多引擎停止前观察与 POSH 消费者：2026-11-13

本轮从 `e8bfd7d4111c11b8c77c591b240a549acabdf6fc` 继续。
**这是诊断能力补全，不是 UU 闪屏、初始 GPU hang 或 watchdog 的修复验收。**
本机仍运行原下游驱动；没有安装本次构建、重启、终止 WindowServer 或主动复现 UU。

## 为什么补这一段

原 `tglHangPreStopWake` 在 `engine != 0` 时直接返回，原有寄存器视图只覆盖 RCS。
已有事故报告还涉及 CCS / BCS，恢复后的闲置寄存器不能说明这些引擎最初在等待什么。
用户回忆两块镜像屏幕可能都闪；这个观察提高了共用渲染、资源和同步路径的优先级，
但没有同步视频证据，也不能排除显示侧或下游 HDMI 增强的影响。

新增 `kern_tgl_hang_cpu.hpp`，复用已有的精确 `haltCommandStreamer` 入口，
每个原生引擎最多领取一次 CPU 快照。记录在原生 wake 调用返回后、原生 STOP 之前。
原生 wake 仍恰好调用一次，STOP / resume / completion / timeout 语义不变。
新增部分不读取 MMIO、不请求额外 forcewake、不轮询、不分配内存、不等待其他记录者。
已有 RCS 寄存器观察继续保持原来的边界。

## 原生准入证据

精确 accelerator SHA256：
`567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e`。
保留已有 Tahoe、物理设备、donor 与 host 准入；新增观察还检查：

| 原生位置 | 已核对的契约 |
| --- | --- |
| `haltCommandStreamer + 0x1f` / image `0x56997` | CALL 前 engine 在 r14d、scheduler 在 r15，参数 enable=1、domains=7 |
| `0x569be` | 原生 STOP 写仍在该 CALL 之后 |
| `IGScheduler5::getActiveContexts` / `0x380ec` | 使用 scheduler `+0xca0+engine*8` 查 streamer |
| streamer init `0x39aaa` | owner / workloop / scheduler 分别存入 `+0x10/+0x18/+0x20` |
| streamer init `0x39aee` | descriptor 指向 `kIGHwCsDesc+engine*0x79` |
| descriptor table `0xd4200` | 六条原生记录的首字节依次为 0–5 |

运行中每次读取还核对 owner→scheduler、scheduler→owner、streamer→owner/scheduler、
精确 descriptor 地址/engine，以及每个非空 slot 的 context/ring 所有者、engine 和双向引用。
任何一步读取失败或身份不符即停止该依赖链，不用另一个引擎的可读数据补位。

## 记录格式与解释边界

`TGLHangCPUReady` 表示 hook 和新布局准入；`TGLHangCPULive` 是只读序列化数据。
前 8 个 uint64 为 schema=1、单记录大小=2176、六个发布状态。
状态 0=尚未领取、1=正在写、2=已发布。随后按 engine 顺序拼接状态 2 的完整记录。
状态 1 不复制记录、不等待。发布用 release，读取状态用 acquire；状态 2 后记录不再修改。
静态记录区总大小 13,056 字节；没有记录时序列化只申请 64 字节载荷容量。

每个记录包含开始/结束时间、领取序号、engine、native domains、两遍 CPU 观察。
每个字段分别记录 value 与实际复制字节数；保留完整 64 位 owner flags。
四个 slot 保留 context、原生 ID/descriptor、ring、stamp 地址/index/retired 和 write index。
guard 区分非法输入、身份失败、不完整 slot 和两遍不同；没有把零填充当成有效读取。

“首次”只指本次启动中该 engine 首次成功领取的观察槽，**不指首次硬件错误**。
并发领取序号也不证明硬件事件的先后；不同引擎不一定属于同一次事故。
原生 eventTimeout 可能在 halt 后判断可恢复并 resume，因此一条记录本身不证明 hang。
两遍相等不排除 ABA、未观察到的变化或 GPU/CPU 可见性差异，更不证明 slot 就是执行中的上下文。
这里尚无 CCS/BCS 的故障寄存器、GPU 当前指令或等待目标；没有擅自移用 RCS 寄存器基址。

`tgl-capture` 保存这个视图，并在数据变化时立即落盘，包括 writing→published。
任何已观察 halt 都取消本轮 clean 对照资格；ready 但数据缺失、布局未知或被截断也取消资格。
出现读取缺口后本轮不重新获得资格。旧驱动没有这项可选视图时明确显示 `not_available`，
不能解释为六引擎均无故障；原有恢复记录和报告目录检查继续独立执行。
现有 GPU 工作探针仍只使用原恢复检查；新的可选 halt 检查目前属于独立捕获器，
没有宣称所有提交入口已被它原子地约束。

## 进一步确认的 POSH 消费者

精确 MTL SHA256 仍为
`1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c`。
本轮继续静态追踪，不创建 Metal device/queue/buffer、不执行这些原生方法。

1. CB 的 `initContextStateDescriptorForEngine:` 将 `_pPoshUsedAsTarget` 写入
   render/compute descriptor `+0x28`。
2. `IGAccelBlitCommandEncoder` 构造从 descriptor `+0x28` 复制到自身 `+0x138`。
   因此仅追踪 render descriptor 会漏掉 blit 的直接别名。
3. `copyBufferToBuffer` 等写入目标 resource ID 表。已读代表路径按 ID 低 10 位选桶，
   每桶最多四个 64 位 ID；达到容量置 overflow 标记。
4. `IGRenderPoshEncoder::processUsedAsTarget` 的 buffer / texture 两个入口读取这些表，
   命中或 overflow 会影响同步相关状态。表不是无语义的临时内存。
5. `IGAccelRenderCommandEncoder::signalCSSemaphore` 在生成同步 token/命令后，
   清零两组计数与 overflow 标记。这是 CPU 编码时的动作，不等于 GPU 已完成信号。
6. 已读 blit 的 `endEncoding` 会结束原生 encoder、析构/释放它并清空指针；
   C++ 析构释放自身其他分配及 fence lists，未直接释放 `+0x138` 指向的表。

这解释了为什么共享可变表的生命周期、同时编码、跨 commit 重用都必须审查，
但仍没有证明所有 alias/escape 已封闭，也没有证明某个等待就是本机首次卡死的来源。
不能据此开启已提交 POSH 的提前释放，或把 only-active 简化成纯内存释放标志。

全文件线性反汇编在 jump table 附近出现过错位解码；本轮对 blit 构造/cleanup 重新用
符号确认的起始地址做定界反汇编。公开文档保留结论与二进制指纹，Apple 原始字节、
完整反汇编和真实机器报告只存本机忽略目录。

## 验证与仍未完成的工作

- 新 collector 的 ASan / UBSan 回归覆盖六个 engine、每个读取点的短读、跨 engine
  身份拒绝、越界 stamp、非法地址、第二遍身份变化及 retired 值变化。
- 五项根 CPU/文件系统测试通过，捕获器生产解析器覆盖旧驱动、未知/截断布局、
  writing 状态、六种 engine 身份和多余数据拒绝。
- 驱动运行器 215 个独立任务、0 失败；精确 donor 的静态字节/调用位置检查另行通过，
  不混算为 GPU 测试。
- DEBUG / RELEASE 构建通过；继承警告仍存在。最终构建及输入指纹见 validation。
- 30 秒普通桌面只读观测没有新增恢复或报告，未主动复现 UU。
  编译/测试期间的一次系统样本为 CPU 66°C、package 18.03W、GPU active 0.54%；
  不能外推为空闲功耗或长期温度改善，没有可靠风扇转速读数。

新增代码尚未系统安装、启动验证或取得真实 halt 记录。完整源码审查仍未完成。
后续实机部署仍需保留或重建已验证的下游 HDMI 行为，再做原版/候选对照及延迟故障观察。
本次没有据此改动等待完成、恢复流程或释放已提交对象。
