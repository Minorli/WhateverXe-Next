# 现场证据与问题拆分

观察日期：2026-11-12，时区 UTC+8。以下仅发布脱敏的汇总结论。

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

## 3. UU 远端呈现触发闪屏

用户两次观察到：显示另一台电脑的远程画面后，屏幕下半部快速闪烁；
退出 UU 客户端后闪屏消失。后台远程服务保留。
第二次连接约数分钟后风扇加速，采样后退出客户端，没有为了复现继续运行。

UU sample 包含 Video Render、VideoToolbox、CVMetalTextureCache、Metal render encoder、
CAMetalLayer present；root WindowServer sample 包含 IOSurface / 纹理创建、
QuartzCore / SkyLight 与 AppleIntelTGLGraphicsMTLDriver 合成路径。

这些是触发范围证据，尚不能判定闪屏一定由 POSH、BCS、解码器或某一个同步点造成。
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
