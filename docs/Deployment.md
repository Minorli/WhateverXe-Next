# 整机部署与回退门槛

当前没有可推荐安装的 WhateverXe-Next 发行版。历史已构建的 BCS 组件仅增加诊断，
POSH 原型只在诊断进程中生效。不要将它们组合命名为“内存 / 闪屏 / 卡死完全修复”。
队列缓存实验已在真实并发构造测试中发生共享存储别名，属于已知失败，禁止全局部署。
后续 lifecycle 短测通过后又出现实机计算通道恢复循环；最新未提交私有分配释放路径
尚未完成 native 验证。不得将短测成功、恢复函数返回或构建成功作为整机通过。

2026-11-13 单独采用了官方 Graphics 0.4.3 的 Metal 文件，已备份旧 bundle，完成
隔离进程对照、签名检查和新启动 UU 的加载核对。没有安装本仓库内核候选；
维护者随后重启，已确认 WindowServer 与 UU 都加载新版。短测中的分配增长减少，
但 Dock 与 UU 下半部闪屏仍然出现，整机验收未通过。**后续精确静态复核发现，
stock 队列下新版对非空 POSH 私有块的删除条件与旧两字节实验相同；内核 POSH 仍开启。
因此撤回将这次加载视为低风险验收的前提，当前组合列为部署阻塞，建议先回退 Metal。**
本轮只核验备份并提供回退步骤，没有执行回退或重启。

### db7b36e 跟进：部署结论补充

**成立〔静态 / CPU〕：**现 no-POSH 候选仍在 POCS image 分配后清 flag；native free
在 flag 开/关两种情况下都遗漏 +0xA0 释放。恢复 flag 不是修复；提前禁止分配还会碰到
copyContextImage 无条件取 +0xA0 的消费者。不能把“no-POSH”标为已解决所有 context 内存问题。

**不成立〔静态〕：**局部返回 false 就使整机恢复路径安全。host/GuC scheduler 忽略
beginReset 的结果，继续 resetRing / stamp 重提交 / endReset；host 最后仍 submitExecList。
返回 false 还跳过 beginReset 的页表重建。完整错误隔离未完成之前，不把本候选安装状态当验收。

**成立〔静态〕：**当前 Metal 本身含按进程分类的 PoshDisable 覆盖；正常初始化下，
UU 与 WindowServer 都走禁用 POSH VS 设置的路径，旧 MTL 也有相同逻辑。
这不等于内核 POCS context 已关闭，也不等于 native POSH token 全部停止编码。
因此纠正风险表述：内核 POSH Enabled 日志不能证明 UU 必然执行 POSH shader；
析构分支相同不能证明与 #47 有相同的完整执行路径。
**证据不足〔整机安全〕：**该既有策略或新增 kernel no-POSH 已让所有异步释放安全。
维持当前“未验收 / 新候选不部署”的结论；旧 bundle 回退作为撤销释放行为的选项保留，
不是已定位闪烁因果的治疗。本轮没有执行回退。

### 显示 S0 之后的 S1/S2门槛

S0已执行，结果见 [REVIEW](REVIEW.md#d852aa9-跟进显示-s0-已命中用户反馈窗口)。
用户反馈闪烁时没有新的bit31 ACK，不将其当作欠载已被排除，也不直接进入寄存器修补。

**S1尚未构建或安装。**独立探针需要同时解决两种读取的边界：

1. 原生coverage计数：只读加载后framebuffer数据，核对精确SHA/布局、计数指令锚点与映射范围；
   不能把文件偏移直接当已重定位内存地址，也不能在kext退出后保留指针。
   这不会调用原生错误处理或改变计数；没有新计数仍不证明未被屏蔽的硬件状态。
2. 100ms PIPESTATUS/配置寄存器：先证明具体显示power domain的访问资格与同步生命周期。
   BAR可映射、屏幕亮着或IOPCIDevice处于D0，都不足以单独保证pipe/PSR寄存器一直可读。
   失去资格、睡眠/唤醒转换或设备终止期间停止读取并报告unavailable，不能用0填充。
   第一版不写W1C、不读清零寄存器；启动即为1的sticky位只能记“首次观察到旧/未知时刻状态”，
   不能每100ms累计成一次新欠载，也不能把它与之后每次闪烁机械对应。

保持独立于闭源下游、不hook原生函数的目标；若无法在这个边界内建立MMIO读取资格，
应先交付内存计数部分并清楚标明覆盖缺口，而不是部署任意BAR轮询。
探针的具体源码、构建SHA、权限/休眠边界和已核验回退入口准备好后，再请维护者批准安装并选择手动重启。
目前没有新探针可批准安装，Documents里的EFI备份也不等于OpenCore菜单里已有可启动回退项。

**S2仍未准入：**只有观察到有效欠载且配置值/平台条件不符时，才逐项提出写入候选。
BW_BUDDY、DCPR_2、DBUF tracker分开验证，不在一个“修复”开关内同时改三组寄存器后声称定位了机制。
先核实内存控制器配置；不根据16+8GB的SMBIOS标签猜双通道值。
无欠载记录也不排除flip/buffer复用等扫描输出问题。全程不自动重启或终止WindowServer。

### B2：用户确认后的短时分流

**状态更新：**维护者已确认 slow2 的 3.700–3.733 秒是目标闪烁，并自行验证拔掉 HDMI 后内屏仍闪。
维护者不能同步录屏，因此不再以同步视频作为推进必经条件，转入下文 S0 计数与显示侧检查。
其余窗口位置、帧率实验仍需确认；本轮没有代为执行。全程保留 Dock 位置，每轮只改一项，
恢复基线后再做下一项。当前不要求重启，也不重开 UU 或改 bundle/defaults。

1. **同步拍摄，约2分钟。**手机≥120fps慢动作拍物理屏，系统同时录屏；
   开始时录入同一可见时间标记，结束保留原视频与实际时间戳。
   以手机实际拍到一次用户认定的闪烁为有效窗口，再逐帧对齐录屏；
   两者都有异常优先查内容/合成，两者不同优先查呈现/扫描及 buffer 复用。
   若开启录制后不再闪，只记“未复现、录制改变条件”，不能记“录屏证明合成正确”。
   slow2 的明显下沿变化已由维护者确认就是目标异常；
   它没有同步录屏，不替代本项。
2. **临时拔 HDMI，内屏约2分钟。**保持同一 UU 远端、同一动态内容，记录内屏是否仍闪；
   然后插回并核对恢复原镜像。分辨率如自动改变须记录，不能忽略这个伴随变量。
   维护者已报告单内屏仍闪。它否定“必须连接 HDMI 才触发”，没有证明上游候选满足内屏背光/训练依赖；仍不尝试上游 kext 引导。
3. **移动 UU 窗口，约2分钟。**仅将 UU 拖离 Dock，不移动 Dock；分别观察
   Dock、UU 下半部和窗口边界是否仍异常。若存在远端自身更新，应在另一客户端/远端核对。
   结束恢复窗口位置。Dock 仍闪支持更大范围的合成/呈现问题，不能单凭这一点排除跨窗口损伤传播。
4. **帧率60→30，约2分钟。**保持远端、画质、分辨率及镜像不变，仅修改 UU 帧率，
   记录实际是否达到目标帧率及手机命中的异常次数/时长，结束恢复60。
   若该设置还改变码率或解码路径，需记录，不能称绝对单变量。

由诊断端保存每轮起止时间、gpuRestart增量、可用恢复计数、WindowServer footprint与温度；
不存在的 registry 属性记 unavailable。视频原片仅保留本机。
首次恢复循环、明显无响应或原有停止阈值触发即停止该轮负载并保存证据，不继续压力复现。
未同步命中的录屏不能证明合成正确；不据此直接关闭 PSR/FBC 或修改 flip/stamp 同步。

## #109 后续候选：仅构建，待审查与单独确认

**部分成立〔CPU / 构建〕：**上游 0.8.2 + 内核 no-POSH / nomc / gpurec 候选已构建，
修正了 nomc 部分写入回退和 no-POSH 错误传播；没有安装、改 EFI、恢复 VA 或重启。
完整 SHA 见 [候选记录](../validation/issue109-candidate-20261113.json)。
**不成立〔部署〕：**用构建成功证明 HDMI/背光下游兼容、GPU 生命周期或长期稳定性。
下游同名增强开关未获等价覆盖证明，不能直接替换当前 EFI。

下面是待维护者另行确认的计划，不是已执行实验。先取得保留现用下游增强的集成版本，
或者完成独立插件的共存审查；A 与 B 必须使用**同一个候选二进制 SHA**。
当前下游与整个新上游二进制直接比较会同时改变很多变量，不能归因于 no-POSH。
Graphics 0.4.3 的既有风险尚未解除，是否先恢复旧完整 bundle 需单独确定；每组 A/B 固定同一 Metal SHA。

| 轮次 | 单变量 / 启用项 | 目标和前提 |
| --- | --- | --- |
| A | 同一集成候选，无新增 #109 boot-arg | 建立受控基线，保留镜像与既有必要参数 |
| B | A + `-wex-tgl-no-posh` | 先验证闪烁 / POSH 假设；不要同时启用 nomc、gpurec 或 Metal 补丁 |
| 后续 R | 经审查的同一基线 + `-wex-tgl-gpurec` | 单独验证恢复机制；已有 `-disablegfxfirmware`、host/CSB/recovery 准入必须成立；不含 stampfix |
| 后续 M | R + `-wex-tgl-nomc` | 仅增加 CCS 关闭，检验共享 reset 域假说；它还保留高地址预留，与单纯属性=0不同 |

`-wex-tgl-gpurec-halt` 暂不在首轮计划内；完整钩子序列化和长等待未验证。
旧 `-wex-mtl-poshfix` 未纳入候选，不能制定当前0.4.3直接叠加的 C 轮。

每个 A/B 各 15 分钟：固定同一 UU 远端、同一画面 / 分辨率 / 帧率 / 镜像布局；
另以 Google Remote Desktop 同画面作应用对照，但 codec 尚未核实，不能直接称“硬解/软解对照”。
手机 ≥120fps 固定机位拍闪烁，先核实导出保留帧率；系统录屏可能改变触发条件，应单独标记。
记录 gpuRestart 增量、`TGLNoPOSHLive`（成功/失败次数）、`TGLGpuRecLive`（若该轮启用），
WindowServer physical footprint 每10秒及预热后 MiB/min 斜率、CPU die 温度。
只读计数不可用必须写 unavailable；不得用0替代，安装 Ready 也不是硬件验收。

首次恢复循环、stamp 停滞、明显无响应、no-POSH LayoutMismatch 或新硬件错误即停止负载；
其余内存 / 温度阈值沿用下方判据。不连续触发直到强制断电，不自动重启或杀 WindowServer。
回退试验开关可删除新增 boot-arg；若更换了整个 kext，必须恢复事先验证的原 kext 与配置。
删除 boot-arg **不会**回退整个0.8.2二进制，也不会恢复已替换的 Metal。
需要维护者选择手动重启时机并核对实际加载的 SHA，不能把磁盘写入等同于生效。

## 2026-11-13：0.4.3 Metal 风险结论

**成立〔静态〕：**文件 `25b6c72e…c4faa` 的 `0x7ED7B` 跳至 `0x2000`。
新代码从 CB 的 `_pIGCommandQueue` 取队列，再取队列的 `m_pPoshUsedAsTarget`，
与 CB 的 `_pPoshUsedAsTarget` 比较；不相等就在 `0x7ED89` 调用 delete。
`0x7ED76` 先排除了 NULL CB 指针。stock queue 在 `0x3CB07` 将共享字段置零；
完整原生代码交叉引用只有初始化、读取和析构，没有发布非空共享块的路径。
队列 init/dealloc 和 CB init 与旧版逐字节相同。因此，在未被外部 hook 改写的
stock 队列下，每个到达这段析构的非空私有块都被释放，only-active 不再构成保留条件。
这证明释放条件相同，不等于已经证明 GPU 还在读取该 CPU 分配。

**不成立〔静态 + 既有整机观察〕：**“新版按所有权比较，所以 POSH 开启时可以低风险验收”。
本次启动 `13:16:21.964 +0800` 的内核日志仍有 `POSH Enabled`；本机没有加载
`-wex-tgl-no-posh` 的实现。指针归属检查没有证明所有异步消费者的生命周期。
[SaqibAlMalik 的 #109](https://github.com/b00t0x/WhateverXe/issues/109) 明确把
MTL 释放开关依赖于内核 no-POSH 实际启用；
[Aqpcet 在 #47 的对照](https://github.com/b00t0x/WhateverXe/issues/47#issuecomment-6717895458)
报告 9A49 上两字节释放补丁出现纹理负载恢复风暴。这是其他机器的风险证据，
不是本机 0.4.3 已导致卡死的实验证明。本机旧事故先于此次 0.4.3 部署，不能倒置因果。

**不成立〔静态〕：**把 #109 的旧 MTL 补丁直接叠加到 0.4.3。
旧 `1e5e4f5b…53c6c` 的 `0x7ED6B` 43 字节匹配；0.4.3 的同窗口不匹配，
全文件也不存在原始模式。保持守卫拒绝，不修改模式、不按版本名称放行。
完整字节表与来源见 [REVIEW](REVIEW.md#2026-11-13上游-109-准入核对任务-1)。

### 已重启后的观察与停止判据

本次已加载新版只是部署事实，不是通过项；不为观察继续强制触发 UU / HLS hang。
若经维护者另行同意保留观察或回退后观察，使用同一画面、分辨率、帧率和镜像设置：

- 先确认磁盘完整 SHA 与 WindowServer/UU 实际映射文件一致；两版 UUID 相同，
  仅凭 UUID、磁盘文件或 boot-arg 存在不能确认加载。
- 记录新增 gpuRestart、通道/context、submitted/returned 是否推进、真实恢复后进度。
  第一次新 reset / 硬件错误、恢复循环或 stamp 停滞即停止负载并保留日志；
  不以 resetOK=1 或 calls=returns 代替恢复。当前下游无 `TGLGpuRecLive`，应记录不可用。
- WindowServer 用同口径 physical footprint，每 10 秒一次，区分 2 分钟预热和
  10 分钟恒定画面区间，报告 MiB/min 斜率、样本数、背景活动；停止后再看是否回落。
  连续 3 分钟超过 100 MiB/min、明显 UI 失去响应、重复画面损坏或 CPU die
  持续 15 秒超过 90°C，均作为本次实验的保守停止阈值，不作为硬件额定上限。
- 任何停止项出现即判该轮失败，不用“没 panic”或短时间 footprint 平稳覆盖。
  10 分钟通过也只代表该窗口；原始恢复安全与长期验收仍需单独批准的实验。

### 从旧完整 bundle 回退（仅步骤，本轮未执行）

旧备份完整 bundle 已通过 `codesign --verify --deep --strict`，主文件 SHA 为
`1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c`。
回退会恢复已知内存泄漏基线，目的是撤销新释放行为，**不是修复全部卡死**。
恢复前保存工作，结束测试负载，由维护者安排手动重启；不杀 WindowServer。

下列命令只供审查回退动作，执行时逐条检查成功再继续；若目标暂存/保留目录已经存在，
先核对其内容，不覆盖。使用完整旧 bundle，不重新签名，不改 EFI，不恢复 VA：

```sh
sudo -n ditto --rsrc --extattr --acl \
  /Users/minorli/Documents/Projects/g83-machine-local/Metal-before-043-20261113/AppleIntelTGLGraphicsMTLDriver.bundle \
  /Library/GPUBundles/.MTL-restore-20261113
sudo -n codesign --verify --deep --strict /Library/GPUBundles/.MTL-restore-20261113
sudo -n shasum -a 256 /Library/GPUBundles/.MTL-restore-20261113/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver
sudo -n install -d -m 700 /Users/minorli/Documents/Projects/g83-machine-local/Metal-043-before-rollback-20261113
sudo -n mv /Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle \
  /Users/minorli/Documents/Projects/g83-machine-local/Metal-043-before-rollback-20261113/AppleIntelTGLGraphicsMTLDriver.bundle
sudo -n mv /Library/GPUBundles/.MTL-restore-20261113 /Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle
sudo -n codesign --verify --deep --strict /Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle
sudo -n shasum -a 256 /Library/GPUBundles/AppleIntelTGLGraphicsMTLDriver.bundle/Contents/MacOS/AppleIntelTGLGraphicsMTLDriver
```

若第二次 mv 失败且正式路径缺失，立即将保留目录中的新版 bundle 移回正式路径，
不要在缺失或校验失败状态下重启。文件恢复成功后，既有进程仍可能映射新版，
需维护者手动重启，再核对实际映射；不自动重启或热切换 WindowServer。
删除 boot-arg **不能**回退已替换的 0.4.3 Metal。

## 进入整机测试之前

1. 对 POSH 完成真实所有权与错误路径审查。队列缓存方案需独立验证跨提交共享、并发编码和错误恢复。
   deferred 方案已退役，不能作为部署候选。
   对 BCS 取得符合当前指纹的语义和故障现场证据。CPU mock 不替代这些证据。
2. 确认物理 PCI 身份、系统 build、每个运行组件 SHA、已应用补丁链和实际加载 UUID。
3. 对比当前机器特定下游功能。现用亮度、HDMI、训练等下游源码未公开，直接替换上游
   构建可能丢失这些行为。需逐项重现或保留兼容，不能静默舍弃。
4. 备份可启动 EFI、配置、相关 bundle 与版本清单；确认可实际进入的恢复介质或引导入口。
   仅存在 UEFI Shell 文件不代表救援入口已验证。
5. 保存工作，由维护者选择重启时机；一次只改一个问题相关的组件，保留显式禁用和回退方式。

## 验收分层

- CPU / mock：守卫、边界、所有权状态机与竞态。
- 单进程真实 GPU：数据和像素正确性、异步与失败完成、内存斜率。
- 冷启动与正常桌面：确认候选确实加载，检查亮度、显示连接和新增故障。
- 30–60 分钟固定场景：桌面、浏览、镜像屏幕、远端视频分别记录 CPU、内存增长、
  体感、温度、GPU restart 与错误。
- 长期与恢复：多次启动、拔插、显示熄屏、后续再单独验证系统睡眠与休眠。

出现新 GPU reset、画面损坏、stamp 不前进或恢复循环即停止负载并回退，
先保存证据，不通过重复触发直到强制断电来证明问题。

## 当前机器保护状态

UU GUI 可临时退出做对照，后台服务和配置保留；保留工作软件、同步和有意镜像布局。
VA bundle 保持隔离，不以重装图形包自动恢复它。没有杀 WindowServer 或自动重启步骤。
旧 XePOSHFix 0.1.0 两字节候选已退役，不能作为部署捷径。
