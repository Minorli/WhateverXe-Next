# 2026-11-16：上游整合与本机部署候选

本轮把图形、TF 和睡眠更新分开准备，没有安装 kext、替换 Library、恢复 VA、改变镜像或重启。
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
现有4组测试只覆盖单次位操作，没有覆盖 native 参数缓存命中及 2→0 跨块。

**不成立〔静态〕**：当前模式2始终等待真实锁存、缓冲生命周期已被证明安全。
原生 IODelay 循环存在超时并写完成状态的出口；≥16ms 只能提示，不能当作成功锁存证据。

这不是 UU 已定位的根因，是实验载体中的独立缺陷。当前产物留作可复核构建，**不准入0/1/2盲测**。
需要让模式2只影响一次原生硬件编程窗口、返回后恢复缓存，或者提供有生命周期证明的回退方案，
并补 2→0 / 2→1 / 并发切换回归测试后重新构建。
不能仅改为 mode=0、清统计或依赖 UU 偶尔触发一次完整 setup 来宣称恢复。

## 部署顺序

1. **TF-only** 候选从实际 EFI 生成，只启用现有 RealtekCardReader 0.9.7；本机 PCI RTS522A 已匹配。
2. **闪屏对照**仍用当前 WEX 和 Metal，先修正上述 SyncFlip 回退问题；不得以新下游替代同 SHA 对照。
3. **图形 0.8.2 + 0.4.4 无 VA 更新**已在私人目录暂存；它是多组件版本验收，不能用来证明 async 因果。
   有 Library / KC 变化，必须具备可挂载系统卷的回退路径，单有 EFI config 备份不够。
4. **睡眠**只收录 S0ixStandby 0.5.4 / SSDT，公共模板默认禁用，保留 Coffee Lake CPUID。
   上游 G83 的睡眠实测仅 Sequoia；S0ix 源码说明 HDMI 带线唤醒、接电 dark wake 再睡眠可冻结。
   先核对固件 LPS0/LXEN 重名和恢复入口，再成组试 Ice Lake CPUID + SSDT + kext。
   不自动开启合盖/空闲睡眠；硬件 SLP_S0=0 也不能宣称完整 S0ix。

维护者明确只有曾进入过的 UEFI Shell，没有独立启动测试的备用 EFI。
原 SyncFlip 任务要求备用 EFI，因此当前没有安装；EFI-only 变更若改用 Shell 同分区回退，
需维护者明确接受该替代方案。所有新候选和实际 EFI 备份均在权限700的本机私人目录。

## 来源

- [官方 main](https://github.com/b00t0x/WhateverXe/commit/2bad71718c6b4f9887e2a504a19a24c3593ea1db)、[Graphics0.4.4](https://github.com/b00t0x/WhateverXe-Graphics/releases/tag/v0.4.4)。
- [G83 1.4.6](https://github.com/xiananliu/g83-11gen-hackintosh-tahoe-efi/commit/ed57bf051d97578ce8c1afb8e6f2338335be2de1)、[下游 WEX0.8.2](https://github.com/xiananliu/wex-modified-edition/releases/tag/0.8.2)。
- [S0ixStandby0.5.4](https://github.com/labrouss/S0ixStandby/tree/c52f172b934c0f906fccb74ad2bce0a974ca5bb1)、[HDMI问题#3](https://github.com/labrouss/S0ixStandby/issues/3)。
- [Claude 显示审查](ClaudeReview-20261113-Display.md#76-2026-11-16-复审async-结论成立候选实现与部署路径需要更换claude)；DeepSeek所提供链接对应本仓库 review/2026-11-16 与 plane-trace-async 清单。
