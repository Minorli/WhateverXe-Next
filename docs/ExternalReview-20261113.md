# WhateverXe-Next 独立审查提示词

以下内容可直接交给 Claude、其他代码审查工具或有 macOS 图形驱动经验的审查者。
请同时提供整个研究分支，不能只给默认 main 的首页。

---

你是一名独立的 macOS / Intel Xe 图形驱动与并发内存生命周期审查者。请审查下面的
项目源码、失败证据、验证记录和推送历史，判断当前分析是否成立，找出可证实的代码问题，
提出最小修复和有区分力的验证。不要预设已有分析正确，也不要以测试 exit 0、CI 绿色或
项目叫 Next 为由接受“已经稳定”的结论。你的任务是独立复核，不是润色报告。

## 1. 用户目标和当前严重问题

用户实际在一台 Dynabook G83、Tiger Lake i7-1165G7 黑苹果上日常使用 macOS，
希望解决整机卡死 / 自动重启、UU 远程画面触发的屏幕下半部闪烁、WindowServer
内存增长、卡顿、CPU 高占用、发热与风扇加速，最终尽量接近原生 Mac 的稳定性和体感。

用户观察到显示远端画面后闪屏，退出 UU GUI 后闪屏消失；后台远程服务保留。
Windows App 也曾被怀疑，但用户后续更确认 UU 是触发场景。
这些观察尚未证明具体驱动根因。没有异常帧录像或扫描输出级证据。
镜像双屏是用户有意设置，不应通过取消镜像来冒充修复。

**2026-11-13 最新用户对照：启动 UU 远程“必现”，启动 Google Remote Desktop
“不会出现这个问题”。** 本轮没有主动复跑。尚无重复次数、持续时间、客户端版本与
同一远端 / 编码 / 帧率 / 分辨率的匹配记录；也未把闪屏、卡顿、内存增长和重启分别计数。
此前更具体的观察是显示远端画面后闪屏，本次“启动”具体阶段尚待确认。
请把这组差异列为高优先级调查入口，避免直接推断 Google Remote Desktop 不使用GPU。

2026-11-12 晚上的进程内候选短测结束后，又发生真实 GPU 恢复循环，最终在跨日后
WindowServer watchdog 导致异常重启。因此任何“卡死已经修复 / 可以全局安装”的
结论当前都不成立。短测与后续故障的因果关系尚未证实，也尚未排除。

## 2. 必须读取的仓库和精确版本

- 显卡研究仓库：<https://github.com/Minorli/WhateverXe-Next>
- **目标分支：`research/posh-ownership-bcs-wait-20261112`。**
- 核心源码基准：`400502276ee36358bc817409b06dc3ec50ffa1bd`；后续本次审查包为文档 / 证据提交。
- 截至整理时，默认 main 仍为 `be95a2e6953297c4357cfa29143121fbdd19119c`，
  缺少后续 atomic / lifecycle 等研究，不要误审 main 后报告“找不到源码”。
- 本机主集成仓库：<https://github.com/Minorli/g83-11gen-hackintosh-tahoe-efi>，
  main `409a6c66ac6a9b7af7287b1cf261847bef83e7cd`。
- G83 上游：<https://github.com/xiananliu/g83-11gen-hackintosh-tahoe-efi>。
- 继承图形源码：b00t0x/WhateverXe `db438fdf4d5fe903f5dfbc65537ed3884e2ea980`，
  已完整放在 `driver/WhateverXe`，保留来源和许可。
- 另一个调查参考为 macintelk/WhateverXe `7c76ce232ff533b116c84085020187c9c267c464`，
  不意味着完整集成了该分支。
- 实际机器加载的是 xiananliu/wex-modified-edition 下游预编译版本，
  不是当前研究分支构建。其机器特定亮度 / HDMI 等修改没有完整对应源码，不能假设
  单纯构建继承上游就能保留目前显示功能。

参见 `review/2026-11-13/repositories.json`，其中有全部研究提交、两个上游 PR、
CI 和核对时间。图形研究尚未向 b00t0x/WhateverXe 提 PR。
G83 上游 Wi-Fi PR #2 和 OpenCore / I²C PR #3 均 OPEN、未合并。
它们与 GPU 故障是独立变更，不应把显卡实验加入企业 Wi-Fi PR。

## 3. 硬件和实际加载组件

| 项目 | 本机事实 |
| --- | --- |
| CPU / GPU | i7-1165G7、Iris Xe 96EU |
| 物理 PCI | Intel 8086:9A49 rev01 |
| 内存 | 24 GB |
| 系统 | macOS 26.7.3 build 25G342；Darwin 25.6.0，x86_64 |
| 引导 / 注入 | OpenCore 1.0.8；Lilu 1.7.2 |
| 实际 WEX | 下游发布名 0.8.1 DEBUG，bundle 内部仍显示 0.8.0 |
| VA | AppleIntelTGLGraphicsVADriver.bundle 已隔离；不要擅自恢复 |
| 本次新构建全局部署 | 没有；BCS 诊断 kext 未装，POSH 实验仅作用于测试进程 |

精确 SHA-256：

```text
WhateverXe:
df96a2cac5c68fb5a1540045da53b9871729561bba1c4714e03ae700466f4ed3
AppleIntelTGLGraphics:
567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e
AppleIntelTGLGraphicsMTLDriver:
1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c
```

加载 UUID：WEX `674F04E8-5AC6-3296-84D8-7F4FBB44E54D`；accelerator
`BA3AA1C0-FE6B-33B3-9D85-73F848394E3D`。这些是二进制构建标识，不是机器身份。
官方 WhateverXe-Graphics v0.4.1 的 Tahoe accelerator 与本机完整 SHA 相同，
“与某个旧测试预期 SHA 不同”本身不构成被破坏的证明。
相关补丁状态 `WEXTGLMTLLeakReady=False`；boot-args 中存在泄漏选项不能证明 Tahoe 已应用修复。

## 4. 先看这次重启的证据

阅读 `docs/Incident-20261113.md`、`review/2026-11-13/log-excerpts.txt` 与 `incident.json`。
所有日期为 UTC+8，文件报告时间、采样窗口、panic uptime、重启时间必须区分。

1. 22:58:08 前，**较早纯计数**生命周期候选的短测已结束。
2. 22:59:42.400710，IOAccel checkGPUProgress 首次留存对 channel 25 的 hardware error。
3. 22:59:45 开始反复重启同通道，报告关联 mediaanalysisd、context 1019，受阻引擎标 CCS；
   ring 数据已 idle / disabled，缺少最初未前进指令和生产者证据。
4. 恢复记录 resetOK=1、calls 与 returns 都增加，但同通道不断再报错。
5. WindowServer 两份 watchdog 快照相隔 40 秒，主线程均在 IOSurface / IOAF 内核锁上等待
   kernel_task 的线程 111（IOServiceTerminateThread）。IntelAccelerator / event machine 也等它。
6. 持锁线程精确符号化为 commandQueueStop → finishEvent → eventTimeout →
   IGScheduler5::checkForProgress → timed thread wait。
7. 第二份快照 WindowServer RSS 11,814,912,000 bytes，spin footprint 11.00 GB；指标不能混算。
8. panic 原因是 WindowServer 120 秒未 checkin，2 次 induced crashes 后仍未退出。
   logd / configd / opendirectoryd 同时仍正常 checkin。
9. 当前启动时间 2026-11-13 00:02:44；panic 文件报告时间 00:02:49 是重启后保存时间。
10. 新启动两分钟只读采集没有新增恢复，但这只是短窗口，不能覆盖之前的失败。

请独立判断：哪些事实足以证明直接重启机制？哪些只是推断？恢复路径、持锁清理与最初
GPU 失去进度是否被错误合并成一个根因？如何在不伪造完成的前提下修复错误传播 / 恢复？

## 5. 已复现的其他问题及其边界

### 5.1 POSH 分配增长

stock 独立进程 2000 个空 CB 后 malloc 增长约 148.806 MiB，与每 CB 约 76 KiB 相符。
纹理 / 混合复制也类似。deferred 原型短测抑制增长，但 native dealloc-inflight 计数为 0，
没有覆盖“真实 CPU 析构先于 GPU 完成”的关键顺序。

WindowServer 堆摘要另看到 109,189 个 77,824-byte non-object 分配和仅一个存活 CB。
未取得每个块的分配栈，因此不能把全部大块直接判为同一 POSH 来源。

### 5.2 并发 active / only-active 错误

同队列 4 线程构造，stock 出现两个存活 CB 同时 only-active。较早 queue-cache 实验
还出现两 CB 共享同一可变 POSH。串行内存改善不能抵消此失败。

`src/POSHAtomicCode.hpp` 原子领取在 init 中前移，移除原来晚增加，commit 用原子减。
请检查寄存器存活、ZF、线性化点、调用前后代码、精确 ABI 与页面 COW 校验。
only-active 也参与 POSH GPU semaphore token bit2，不能只为 free 随意翻标志。
**active 在父类 commit 前就下降，不是 GPU 在飞计数。**

该阶段 native 并发 / 真实 GPU 有界对照通过，详见 atomic-claim-tests.json；
并未证明缓存跨提交重用安全或真实 UU 闪屏已修复。

### 5.3 未提交 CB 弃用后 active 不归还

同一真实队列连续创建 / 丢弃 32 个 CB，析构观察者确认均销毁，但 stock 和 atomic
最终 active 都为 32，31 轮 only-active 错误，exit 8，未提交 GPU 工作。

较早 lifecycle 候选：成功 init 关联独立 retain queue 的 token；commit 入口先标记；
完整 native dealloc 返回后，仅通过存活 token 给未提交 CB 归还计数，不再读已销毁 CB。
保持 claim 到关联析构完成，以覆盖重入创建。分配异常没有可证明回滚，诊断进程终止，
不能称支持异常恢复。

较早纯计数阶段每模式 native307、并发1400、sanitizer307 通过；GPU 每模式418提交、
32弃用、450 tokens，短测0数据差异 / 0 GPU错误。**94秒左右后仍有真实恢复故障**。
其阶段源码原样保存在审查包 stages；历史 non-sanitized/GPU 二进制被后续构建覆盖，
不能精确重放，必须保留这项证据缺口。

### 5.4 当前最终源码新增的未提交私有释放

纯计数归还会令顺序 CB 重新 only-active；原生析构跳过此分支的 POSH delete，
因此可能加剧私有泄漏，纯计数方案不可独立发布为内存修复。

当前 `src/POSHAtomicClaim.mm` 又增加了：捕获 only-active 且非共享块、检查 malloc_size
77824 与指针 / 标志 / 共享身份，完整 native cleanup 返回后，只有从未 commit 的私有块
才执行 operator delete，然后归还计数。共享块不删，已提交私有块保留原生行为。

**该最终释放路径只有构建 / CPU 检查；native819 与512次增长检查、native sanitizer、
最终GPU对照均未运行。首次尝试卡在 MTLCreateSystemDefaultDevice，尚未安装候选。**
不允许把前一阶段的307 / 418 / sanitizer结果转记到这段新释放逻辑上。
也不能盲目放宽 malloc_size 来迎合 ASan allocator 的布局。

请重点审查：token / 队列寿命、析构重入、关联清理、commit与销毁顺序、预先缓存IMP、
重复commit、init中途抛错、allocator匹配、共享别名 / 迁移、descriptor消费者和GPU消费者。
请明确哪些条件实际经过原生测试，哪些只存在于模型或假设。

### 5.5 较早 BCS 故障

过去 panic 为 `submitToRing: Work queue failure detected`，准确符号路径含 GL2D blit、
同步事件和stamp，因此调查不能只盯Metal。
18:25–18:28 的16份重启报告中9份BCS hung、7份恢复中idle/disabled；9份IPEHR均
`0x0e009002`，对应PPGTT polling/GTE等待候选。目标、比较值、生产者没有可靠现场。

`kern_tgl_hang_bcs_wait.hpp` 新增有界 nofault 读取，保留head附近候选、LRC / PPGTT根、
前后PTE与等待值，拒绝身份 / 映射变化；只在原生失败路径采集，预算4次，没有新MMIO、
forcewake、映射或成功提交hook。已构建但没部署，不能声称取得真实新现场。
CPU native replay通过不代表真实GPU的同步语义通过。

### 5.6 闪屏与呈现

UU sample 有 VideoToolbox、CVMetalTextureCache、Metal编码、CAMetalLayer present；
WindowServer sample 有IOSurface / QuartzCore合成。VideoToolbox出现不证明硬解，
CA::OGL命名也不证明只走OpenGL。VA仍隔离。
IOSurface像素短测通过，但presentedTime全零、工具明确exit16，不能算扫描输出和时序验收。
退出UU后CPU回落、内存保留，属于触发负载证据，没有证明所有问题都由UU单独造成。

请结合最新 Google Remote Desktop 阴性对照，审查两客户端在解码路径、CVPixelBuffer /
IOSurface格式与stride / tiling、纹理缓存复用与释放、跨队列同步、CAMetalLayer提交节奏、
帧率 / 缩放 / 色彩转换、虚拟显示或捕获路径上的差异。这里列的是调查维度，
没有证明任意一项是实际差异或根因；应先采样，再形成可检验假说。

## 6. 审查阅读顺序与源码定位

1. `docs/Incident-20261113.md`、本次审查包摘录及JSON。
2. `docs/Findings.md`、`Provenance.md`、`Validation.md`、`Deployment.md`。
3. `src/POSHAtomicClaim.mm`、`POSHAtomicCode.hpp`、`POSHQueueCache.mm`、
   `POSHDeferred.mm`、`POSHLifetime.hpp`，以及 `Concurrency.md` / `Abandonment.md`。
4. `tests/posh-queue-check.mm`、`posh-abandon-check.mm`、`posh-concurrent-gpu.mm`、
   `metal-surface-check.mm`、`metal-async-check.mm`、`metal-memory-check.mm`、`gl-blit-check.mm`。
5. `src/GPUHealth.hpp` 与 `tools/tgl-capture.mm`。
6. `driver/WhateverXe/WhateverXe/kern_tgl_recovery.hpp`、`kern_xe.cpp` 中
   wrapTGLRecovery / tglHangPreStopWake / hang capture，以及 `kern_tgl_hang_bcs_wait.hpp`。
7. 相应 `driver/WhateverXe/tests/` 和 `validation/*.json`；检查实际断言、测试是否镜像实现、
   对不在断言范围的行为是否做了过度结论。

检查工具保护的缺口：GPUHealth仅接入3个posh工具，其他Metal / GL探针还未补齐；
目前读取至少16字节就当作识别监测布局，未知布局 / 陈旧采集如何拒绝？
检查和后续GPU调用之间仍会竞态；停止用户态工具也不一定解除内核退出等待。
请不要通过杀WindowServer、缩短等待、忽略panic或伪造GPU完成来绕过。

## 7. 如何验证建议

首先区分六层：静态审查、CPU/mock、精确Apple指令CPU replay、进程内真实GPU、
全局安装与启动、长时间真实负载。请为每条结论标明所属层级。

仅需构建 / CPU测试的入口是 `make all`、`make cpu-test`；继承驱动默认CPU套件入口
是 `python3 tools/test.py`（在driver/WhateverXe目录）。可选native replay需要合法的
本地精确Apple二进制，公共仓库不分发它；缺失时应报告无法验证，不能降低SHA守卫。

不要默认运行真实GPU探针：本机已经发生恢复后整机重启，当前任务是独立审查。
如有硬件条件，请先给出受控计划，再明确原版A / 修改版B、相同依赖与负载、源码与二进制
SHA、冷热启动、温度上限、停止条件、原始证据保全与回退入口。失败应保留。
监测必须覆盖测试本身与**退出后的延迟窗口**；当前故障在短测返回后才暴露。
同时监测GPU恢复、WindowServer内存斜率、帧时序、CPU/功耗/温度，不能只测malloc终点。
单次两分钟健康不能算30–60分钟验收，更不能算长期稳定。

请特别设计能区分以下假说的实验：未提交对象泄漏、已提交私有生命周期、共享缓存提前复用、
BCS等待生产者未完成、映射 / 可见性、CCS提交或恢复失败、持锁清理、合成输出异常。
尽量一次只改变一个变量。不要把改动全部叠加后用“感觉顺滑”替代因果证据。

新增的首选真实场景对照是 UU / Google Remote Desktop：同一远端、同一画面与显示布局，
尽量匹配分辨率 / 帧率 / 编码条件，记录不能匹配的变量。分别定义“闪屏”“内存增长”
“无响应”“GPU reset / watchdog”的判据和时间，不把一项阴性外推为所有故障都阴性。
先给出低风险、有停止条件的设计，不能为了验证“必现”持续压到强制断电。

## 8. 需要你的输出

请用中文给出可供另一名工程师直接行动的报告：

1. 先给出按P0/P1/P2排序的发现表：准确文件与行、触发条件、为什么错误、证据层级、
   影响、最小修复和能证明 / 推翻该修复的测试。没有发现也应如实说明，不凑数量。
2. 对已有每条关键分析分别标“成立 / 部分成立 / 证据不足 / 错误”，说明依据和反例。
3. 分别画出POSH私有 / 共享块的CPU与GPU所有权时序，说明commit计数不等于在飞数量。
4. 解释这次watchdog的直接等待链，指出最初故障与后续恢复 / 清理分别缺什么证据。
5. 列出三项最优先工作，要求范围小、可回退、能区分假说；同时列出不应部署的候选。
6. 有可确认的源码问题时给最小diff或伪代码，不要提供没有论证的硬编码补丁合集。
7. 单列公开数据不足、无法取得Apple二进制、未保存历史二进制等限制；说明它们怎样限制结论。
8. 若建议上游PR，拆开通用修复、诊断增强与G83集成，说明各自依赖和验收，不把未验证候选
   包装成发行版，不混进现有Wi-Fi / OpenCore PR。

可以质疑并推翻当前维护者的判断。请把可证明事实、从源码推导的结论、解释性假说和仍需
新现场的数据分开。最终判断应回答“现在能安全部署什么、不能部署什么、下一项证据是什么”，
而不是承诺仅靠这份材料已经把所有问题解决。

---

本提示词及公开摘录刻意不含原始内存地址、EFI身份、认证资料、完整Apple反汇编或payload。
如确需额外证据，请精确说明所需字段与理由，优先提出可脱敏的最小采集范围。
