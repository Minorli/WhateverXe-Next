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
