# 2026-11-16：上游整合与本机部署候选

本轮把图形、TF 和睡眠更新分开准备。审查阶段未改活动系统；随后经维护者明确批准，
仅写入 TF-only 配置以启用 EFI 中已有读卡器，待其手动重启验证。
没有安装新 kext、替换 Library、恢复 VA、改变镜像或重启。
G83 配套整合见 [机器集成记录](https://github.com/Minorli/g83-11gen-hackintosh-tahoe-efi/blob/main/docs/Integration-20261116.md)。
确切输入与输出 SHA 见 [脱敏清单](../validation/integration-20261116.json)。

## 核对结果

- **成立〔Git 源码〕**：官方 main `2bad7171` 相对本仓库已集成的 0.8.2 基线 `4512191b`
  只改变 CHANGELOG.md。本轮吸收新的发行说明，保留本地诊断开关优化；没有新的内核修复需要重复移植。
- **部分成立〔下游发行说明 / 二进制〕**：xiananliu 0.8.2 移除 VA fallback、Metal leak、
  eDP training、VBT 实验和诊断记录器，保留 HDMI clock/SCDC、CDCLK 与背光增强。
  新二进制没有 TGLProgram，更新后 plane-trace 将不可用；先完成当前二进制的闪屏对照。
- **不成立〔完整文件静态比对〕**：Graphics 0.4.4 给本机带来了新的 Metal / framebuffer 闪屏修复。
  两个文件与现用版本完整 SHA 相同；accelerator 的 __TEXT / __DATA 相同，__LINKEDIT 不同，
  Compat 的代码和数据确有变化。原本完整 SHA 守卫继续保留，不用段相同放宽准入。
- **成立〔来源核验〕**：0.4.4 pkg 的 SHA256SUMS 和所核对的完整 bundle 签名通过；
  但标准安装会恢复 VA 并重建 AuxKC。[loverlf / vikenzo196 的 #129](https://github.com/b00t0x/WhateverXe/issues/129)
  记录了 Sequoia / TGL 及 Tahoe / RPL 的 WindowServer GVA 合成转换 abort。
  不是单纯的 2560×1440 上限；本轮没有独立复现其 VA 分支解释，也没有恢复 VA。
- **成立〔CPU / 本机构建〕**：根 `make cpu-test`、SyncFlip 4 组 ASan/UBSan 测试、
  精确 framebuffer 11 个锚点、本机 kext 与 ctl 构建、plane-trace 构建通过；
  修正旧 10.6 ABI 后，nm / kmutil 差集仅有 9 个预期 Lilu 导入，298 个其他符号均解析。
  新增真实 SDK vtable 回归测试通过，修复提交 `450d305`。没有进程内 GPU 或整机 A/B 的新通过项。
- **证据不足〔材料核验〕**：给出的“DeepSeek”链接均为审查输入与采集数据，未找到独立完成的 findings；
  不把这些材料当作另一位审查者认可候选的证据。

## SyncFlip 新发现：模式2的缓存回退门槛

**成立〔静态数据流〕**：原候选 `wrapConfigurePlane` 在 native 返回后改 `plane+0x100`，
只清 async bit9，不恢复缓存；`configurePlane 0x21CF` 对相同参数可走 `0x22A0` 快路径，
不重算 CTL。因此模式2后直接切回0，可能继续沿用清过 bit9 的缓存，污染对照块。
原有4组测试只覆盖单次位操作，没有覆盖 native 参数缓存命中及 2→0 跨块。

**不成立〔静态〕**：当前模式2始终等待真实锁存、缓冲生命周期已被证明安全。
原生 IODelay 循环存在超时并写完成状态的出口；≥16ms 只能提示，不能当作成功锁存证据。

这不是 UU 已定位的根因，是实验载体中的独立缺陷。初审产物1.0.1不准入0/1/2盲测。
后续1.0.2已修正缓存作用域并补回归，见下节；整机准入与根因结论仍分开处理。
不能仅改为 mode=0、清统计或依赖 UU 偶尔触发一次完整 setup 来宣称恢复。

## 部署顺序

1. **TF-only** 从实际 EFI 生成，只启用现有 RealtekCardReader 0.9.7；本机 PCI RTS522A 已匹配。
   经批准已写入活动 EFI，安装后 ocvalidate 与逐文件核验通过，尚待手动重启和插卡验收。
2. **闪屏对照**仍用当前 WEX 和 Metal；SyncFlip 1.0.2 已修正上述回退问题，尚未安装，
   新出现的 RCS/CCS 故障先做短时验收，不直接开始长时盲测。不得以新下游替代同 SHA 对照。
3. **图形 0.8.2 + 0.4.4 无 VA 更新**已在私人目录暂存；它是多组件版本验收，不能用来证明 async 因果。
   有 Library / KC 变化，必须具备可挂载系统卷的回退路径，单有 EFI config 备份不够。
4. **睡眠**只收录 S0ixStandby 0.5.4 / SSDT，公共模板默认禁用，保留 Coffee Lake CPUID。
   上游 G83 的睡眠实测仅 Sequoia；S0ix 源码说明 HDMI 带线唤醒、接电 dark wake 再睡眠可冻结。
   先核对固件 LPS0/LXEN 重名和恢复入口，再成组试 Ice Lake CPUID + SSDT + kext。
   不自动开启合盖/空闲睡眠；硬件 SLP_S0=0 也不能宣称完整 S0ix。

维护者明确只有曾进入过的 UEFI Shell，没有独立启动测试的备用 EFI。
原 SyncFlip 任务要求备用 EFI，因此当前没有安装；EFI-only 变更若改用 Shell 同分区回退，
需维护者明确接受该替代方案。维护者已单独批准 TF-only 的 Shell 同分区回退；
该授权不覆盖 SyncFlip、图形或睡眠安装。所有新候选和实际 EFI 备份均在权限700的本机私人目录。

## TF-only 安装落盘结果

**成立〔落盘核验〕**：活动 EFI 相对 baseline 只有 `OC/config.plist` 改变，
唯一语义变化为 `/Kernel/Add/26/Enabled`（RealtekCardReader false→true）；Friend 仍为 false。
同分区新增 `OC/config.plist.pre-tf-20261116`，内容与原配置逐字节一致。
WEX、企业无线及全部其他 EFI 文件未变，临时配置不存在，活动配置通过 ocvalidate 1.0.8。
重启加载、卡片读取和弹出尚未验证，不能把落盘核验称作 TF 修复通过。
具体 Shell 回退步骤见 G83 集成记录；本机私人目录保存完整核验收据。

## SyncFlip 1.0.2修正与UU并发故障

**成立〔静态 / CPU / 本机构建〕**：模式2改在原生 `syncPlaneUpdates(false)` 生成最终 CTL/SURF
包时临时清 bit9，返回前恢复这一位；不再持久改 configurePlane 的缓存。
依据为 `0x351D/0x3525` 的 CTL 值传递、`0x356E/0x3576` 的 SURF 顺序，
及 `PrepareTriggerPacket 0x92555` 的值复制。保留原11处守卫，新增后共21处。
ModeGate 让模式只在原生事务之间切换，避免一次 flip 前后使用不同模式。
8组逻辑测试覆盖 2→0 缓存快路径、2→1、原生其他位保留、主平面范围、并发切换及部分route失败透传；
持久不恢复缓存的变异在新测试中失败。本机ABI回归、kext/ctl构建与链接检查通过。
只成功安装一个route时，即使请求非零启动模式也不修改事务或plane；两处都成功后才启用修改。

plane-trace 新增 SURF 前最后观察到的 CTL 分类，处理初始未知及缺口；
原 `async_ctl` 保留为全部CTL写入计数，不能用它要求准备阶段也为sync。
**证据不足〔整机〕**：源码验证时尚未安装；后续已获批写入EFI，待手动重启，仍未进行A/B，
不能说闪屏或卡死已解决。

**成立〔历史整机日志〕**：2026-11-16 09:58:14 与11:03:42均有 WindowServer/RCS hang 报告，
分别随后在10:04和11:05启动。第二轮11:03:45、11:03:49同一通道反复进入恢复；
3份CCS报告归属 Windows App、同一上下文，完成stamp没有前进，而末份提交值仍增加。
11:03:30–11:04:45保存的内核窗口至少有3对 GPURestartBegin/End、4次Skipped、1次Deferred。
End只证明软件恢复函数结束，不证明硬件成功恢复。日志没有补齐到强制重启前的所有瞬间，
不能把本轮强制重启写成已证实的watchdog panic。

维护者确认当时UU与Windows App并发；单独Windows App、Google Remote Desktop似乎正常。
**部分成立〔用户对照 / 历史日志〕**：UU或并发负载是当前触发方向；报错归属不等于首次肇因。
RCS→CCS及stamp不前进支持继续核对#109恢复链，但没有证明共享reset域就是首次hang的根因，
也没有证明async导致该hang。#109候选仍未部署，不伪造完成stamp。

先保持短时、单变量负载，不自动再次复现至断电；维护者随后明确接受Shell回退并批准SyncFlip安装。
后续负载分别记录UU单独、Windows App单独、二者并发和Google RD，首个GPU错误即停止，
不能把应用间差异直接当作codec或引擎归因。聚合结果见
[本轮验证](../validation/syncflip-102-20261116.json)，详细事故时间线见
[Incident-20261116.md](Incident-20261116.md)，原始报告只在本机local。

## SyncFlip 1.0.2 已安装，等待维护者手动重启

**成立〔磁盘部署〕：**维护者明确接受已使用过的UEFI Shell作为回退入口后，
已安装精确候选`3d772787d294c4ed29a516de3ba66ee7a3da603f28c9b4f2a8f674c2bbfab5ec`。
唯一既有文件变化为活动config：在WEX后新增Kernel/Add、追加`-tglsync`，未设置tglsyncmode，
启动默认0。新增完整插件两文件与原config备份；其余168个既有EFI文件逐项SHA不变。
安装后ocvalidate 1.0.8通过；未换WEX/Graphics、恢复VA、改镜像或自动重启。

**证据不足〔启动 / GPU〕：**本次启动尚未加载插件，没有切换模式或执行实机A/B。
维护者保存工作后自行重启，先不连接UU/Windows App远端；核对state=routed、mode=0、
显示正常后再开始短时冒烟。持久回退恢复`OC/config.plist.pre-tglsync-20261116-102`。
安装记录见[磁盘核验](../validation/syncflip-install-20261116.json)。

## 来源

- [官方 main](https://github.com/b00t0x/WhateverXe/commit/2bad71718c6b4f9887e2a504a19a24c3593ea1db)、[Graphics0.4.4](https://github.com/b00t0x/WhateverXe-Graphics/releases/tag/v0.4.4)。
- [G83 1.4.6](https://github.com/xiananliu/g83-11gen-hackintosh-tahoe-efi/commit/ed57bf051d97578ce8c1afb8e6f2338335be2de1)、[下游 WEX0.8.2](https://github.com/xiananliu/wex-modified-edition/releases/tag/0.8.2)。
- [S0ixStandby0.5.4](https://github.com/labrouss/S0ixStandby/tree/c52f172b934c0f906fccb74ad2bce0a974ca5bb1)、[HDMI问题#3](https://github.com/labrouss/S0ixStandby/issues/3)。
- [Claude 显示审查](ClaudeReview-20261113-Display.md#76-2026-11-16-复审async-结论成立候选实现与部署路径需要更换claude)；DeepSeek所提供链接对应本仓库 review/2026-11-16 与 plane-trace-async 清单。
