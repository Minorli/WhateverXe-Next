# 验证记录

日期：2026-11-12。以下保留精确的通过边界；原始日志不分发。
机器与实际加载指纹见 Provenance。

## 1. CPU / mock 与 native replay

继承驱动完整测试已重新运行 216 项，全部 exit 0，包含真实指令的 CPU native replay。
首次运行有 7 项因仅允许另一完整 SHA 而拒绝，涉及：

```text
dsi_backing_adapter_native
dsi_backing_native
xelp_forcewake_irq_native
tgl_rpcs_native
small_gt_urb_native
tgl_tlb_preparser_native
tgl_semaphore_tlb_native
```

首次拒绝不能计为通过。多个测试最初预期 accelerator SHA
`bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0`，
当前官方 v0.4.1 Tahoe payload 与本机完整 SHA 相同。
经官方六个 OS payload 的独立对比，完整文件不同，但其 819,200 字节 `__TEXT` segment
完全相同，SHA 为 `04f1b594657002b95bcc6334a369d30459029327b9d83b75c78e2ab54179a3d7`。
本轮将经核实的 Tahoe 完整文件 SHA 加入独立、精确的 CPU replay 准入函数；
保留逐项代码 / ABI 断言和 forcewake 原有专用旧版本范围。不是仅按 segment 或前缀准入。
4 处单字节文件变异均被完整 SHA 拒绝，没有执行变异指令。

BCS native replay 共 128 个情形，24 个 late-map omission 在原生路径复现，已有 wrapper
纠正这 24 个情形，覆盖 WA、模式、early flush 和 ring wrap。实际运行驱动已经发布该修复
的计数，仍曾发生 BCS hung，因此不能据 CPU replay 宣称实机卡死已经解决。

随后新增 `tgl_hang_bcs_wait` CPU 测试，以 ASan / UBSan 验证等待指令的有界采集、
跨 ring 边界、变化中的等待值和身份 / 映射 / 采样竞态拒绝。单独通过；它未向 GPU 提交工作，
不能冒称新增后的 217 项已在本机完整重跑。

变更关联的七项 CPU 测试（pending、select、detail、prestop、recovery、graph、ggtt）
单独复核 exit 0。pending 新增八种 BCS 接纳、身份拒绝和采样竞态情形。
真实 BCS hung 时的新采集能力尚未在本机部署验证。

根 POSHLifetime 模型在 ASan / UBSan 下覆盖两种事件顺序、重复完成、缺失完成和
1000 次 CPU / GPU 事件线程竞态。模型通过不能证明 Apple 私有对象的真实生命周期。

## 2. 真实 GPU 短测

这些是同日较早的有界测试记录。随后按维护者授权继续进行带温度监测的受控对照。
单位 MiB 为 `malloc size_in_use` 的终点减起点，不是 RSS 或整个系统显存。

| 负载 | 工作数量 | stock 增长 MiB | deferred 增长 MiB | 数据 / GPU 错误 |
| --- | ---: | ---: | ---: | --- |
| empty | 2000 | 148.806 | 0.408 | 无错误完成 |
| async texture | 2000 | 149.064 | 0.486 | 0 mismatch / 0 GPU error |
| mixed copy | 2000 | 149.066 | 0.645 | 0 mismatch / 0 GPU error |
| compute | 1000 | 未记录本轮对照 | 0.475 | 结果读取通过 |
| draw | 1000 | 未记录本轮对照 | 0.330 | 像素读取通过 |

纹理与复制通过不等于远程呈现通过。异步测试的 `dealloc_inflight` 为 **0**，
因此没有验证“原生析构发生于 GPU 完成之前”的核心路径。
2000 次 GL blit 与 441 次并行 Metal 工作也通过，但没有重现真实 BCS 挂死。

旧两字节 / swizzle 对照仅保留为历史，不再纳入可部署候选。
本次公开整理移除了它们的运行入口。

### 队列缓存候选的反向对照

这里的 stock 是未修改的官方用户态 MTL 二进制，候选仅在测试进程内补齐队列缓存。
两者使用相同的当前下游内核驱动；没有进行“整套纯净上游 EFI 与新 EFI”的启动 A/B。
每帧包含 CVPixelBufferPool / IOSurface、CVMetalTextureCache、Metal 采样绘制和逐像素读取验证。

| 顺序 | 模式 | 帧数 | malloc 增长 MiB | 像素错误 | 新 GPU 报告 |
| --- | --- | ---: | ---: | ---: | ---: |
| A → B | stock | 300 | 22.8227 | 0 | 0 |
| A → B | queue-cache | 300 | 0.2180 | 0 | 0 |
| B → A | queue-cache | 300 | 0.2127 | 0 | 0 |
| B → A | stock | 300 | 22.4754 | 0 | 0 |
| 最终源码 B → A | queue-cache | 300 | 0.1936 | 0 | 0 |
| 最终源码 B → A | stock | 300 | 23.4075 | 0 | 0 |

反向组候选 / 原版 CPU 时间为 0.219383 / 0.219772 秒，elapsed 为 5.977 / 5.993 秒。
`frame_work_p95_ms` 为 2.340 / 1.813 ms，包含 CPU 填充、提交等待和像素验证，
不是屏幕呈现时间。不能据此声称候选降低 CPU、温度或实际显示卡顿。
最终组的 40 个 CPU 温度样本为 39–48°C；该范围不构成候选降温的 A/B 结论。
首组候选指纹读取曾保留约 5MiB 常量内存；后续独立 autoreleasepool 已排除该常量干扰，
比较使用同一进程的 malloc 增量而非不同模式的绝对起点。

100 个队列生命周期、500 空 CB 与 50 个异步纹理提交通过，后者 0 mismatch / 0 GPU error。
加入真实 CAMetalLayer 呈现后的短测，GPU 完成和离屏像素检查正常，
但回调的 `presentedTime` 全部为零；30 帧诊断确认窗口有面积、可见、30 次回调均返回。
探针以 exit 16 明确拒绝将其作为完整呈现时序验收。未证明硬件扫描输出没有闪屏。
脱敏结果见 `validation/queue-cache-tests.json`。

### 原生并发验收失败

随后新增四线程并发构造检查。stock 与 queue-cache 各 300 轮都出现 1 次 active 计数错误，
queue-cache 还出现 1 轮两个存活、未提交 CB 同时借用共享缓存。两种模式均 exit 8，
没有 GPU 提交。普通 100 队列串行生命周期仍通过，不能抵消并发失败。
候选因此明确阻止全局部署；详见 `Concurrency.md`。这不是原始 BCS 卡死的完整复现。

### 原子计数修复后的最终对照

新增原子领取与原子提交减计数；整个方法不加锁，保留原生 GPU / 析构语义。
同版原生对象测试中 stock 再次出现 2 个 only-active，exit 8；atomic 与组合候选
各通过 300 轮 × 4 个 CB，组合候选 400 个缓存所有者全部析构。

同版四线程真实渲染工具中，stock 在第 0 批发现两个 only-active 并停止该批提交；
atomic 与 queue-cache-atomic 各通过 400 次分批、400 次交错提交，0 数据差异 / 0 GPU 错误。
各有额外 2 次容量探针空提交；本机没有在请求上限 1 处阻塞，不计为容量等待路径通过。
根 `make all cpu-test` 通过，包括新指令发射器的 ASan / UBSan 合成对象执行测试。
本轮未修改内核源码，也没有把这三项根测试混算为继承驱动整套测试重跑。

300 帧 IOSurface A→B / B→A：stock malloc 增长 22.4791 / 22.4743 MiB，
组合候选增长 0.1955 / 0.2036 MiB，均 0 像素差异 / 0 新 GPU 报告。
精确数据和指纹见 `validation/atomic-claim-tests.json`。仍未系统安装、未验收呈现时序，
未复现并解决原始 BCS 卡死；不能由这些结果推导日常使用已变得稳定或更凉。

## 3. 构建

较早的 BCS pending 诊断 RELEASE 构建已成功生成两组件：

| 组件 | SHA-256 |
| --- | --- |
| WhateverXe | `2a3a609ba3b2f5328413ad9b3b167fb8dbef0891acb3ce23900d21c7c94e4a51` |
| AppleIntelTGLCompat | `a9cc21f1dc4775f9625d036abe9c1a868cf8cdbffe0b117da09eab0e74f673e1` |

构建含继承的 warning，不宣称 warning-free。构建清单见 `validation/driver-build-manifest.json`。
这些二进制没有安装到当前 EFI，也没有在该机器实际启动。

新增 BCS wait 现场读取后的独立 RELEASE 构建也已通过，含 nofault helper 和 boot-stack 检查：

| 组件 | SHA-256 |
| --- | --- |
| WhateverXe | `23676eaca7a08479f49ad4cea1231c8c2463c99b056e9654e155be2ce92d8ef4` |
| AppleIntelTGLCompat | `4a6d80332aadcc630bd73fb67d00c2d37b2f061ed4c7b19d4ec2782cff2a1d39` |

该构建也未安装。独立清单为 `validation/driver-bcs-wait-build-manifest.json`；
不得用较早清单代指它，也不得把构建成功当作新增诊断已取得实机故障证据。

公开整理后的根诊断工具已重新构建，CPU 模型与提交数量边界测试通过。
8 个非法参数或旧 `fix` / `binaryfix` 入口在创建 Metal / GL 设备之前以 exit 2 拒绝。
只读采集器完成 1 秒请求的轻量 smoke，报告目录可读，基线后新增 GPU 报告为 0；
检查该路径不向 GPU 提交工作。此处是较早的工具 smoke；后续 GPU 对照已在上节单独记录。

首次 GitHub CI 的 macOS 15 默认环境是 ARM64，208 项默认测试中有两项失败，
其中 `display13_tc_lightup` 直接执行 x86 字节而收到非法指令退出。
CI 随后明确使用 Intel runner，并在开始检查 x86_64 ABI，保留全部测试和失败日志输出；
不能把 ARM 上的该次运行计为完整通过。

## 4. 整机与长期验收

未完成。当前实际仍运行下游 0.8.1 DEBUG，VA 组件保持隔离。
没有 30–60 分钟全局修复内存曲线，没有新驱动的启动、双屏、远程视频、睡眠恢复验收。
没有异常帧录像，也没有新的 BCS 现场证据能证明具体根因已修复。
此次 UU 连接采样仍无新增 GPU reset，既不是原始 BCS 卡死的完整复现，也不是候选修复的证据。
新增 600 秒正常桌面只读监听也记录 0 recovery calls / 0 新 GPU 报告，未主动制造高负载。

发布源码和 CI 通过只是让结论可以审查，不能代替这些整机结果。
