# 整机部署与回退门槛

当前没有可推荐安装的 WhateverXe-Next 发行版。已构建的 BCS 组件仅增加诊断，
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
