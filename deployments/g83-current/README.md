# G83 当前部署基线（2026-11-16）

这里保存本机活动EFI的脱敏config与完整文件指纹；完整EFI位于
`Minorli/g83-11gen-hackintosh-tahoe-efi`，准确提交由`efi-manifest.json`的
`full_EFI_commit`固定。与该提交中的`EFI/OC/config.plist`逐字节相同。
本仓库不再复制一套完整EFI，避免显卡研究与整机集成各自漂移。

## 当前组合

- 下游WEX0.8.1 DEBUG（内部0.8.0），保留本机HDMI/背光增强与TGLProgram记录器。
- TGLSyncFlip1.0.2，当前routed/armed/mode1；启动参数含
  `-tglsync tglsyncmode=1`，写入默认1后的下一次启动待核验。
- TGLDisplayProbe1.0.1，仅原生RAM计数；VA仍隔离。
- AX201企业认证修订保持；TF驱动已绑定、待插卡验收。
- S0ix睡眠、WEX0.8.2及新的Graphics0.4.4未部署。

模式1改善了维护者此前观察到的UU闪屏；没有完整逐次计数。
RCS/CCS卡死未证明修复，不能把保存本机配置当作稳定性验收。
系统accelerator、framebuffer与Metal只提供SHA，不包含Apple二进制。

## 脱敏与校验

仅MLB、SystemSerialNumber、SystemUUID为空，ROM为6字节零；其余配置解析结果与活动EFI
一致。所有公开非config文件SHA与只读活动EFI一致，清单共167个文件。
两仓库config均通过ocvalidate1.0.8，44个启用条目可解析。同步前后活动EFI整树不变，
本轮没有安装、改写活动配置或重启。Unix执行位不参与FAT源文件内容SHA对照。

不能直接用空身份config覆盖私人EFI。部署与回退先读G83提交中的
`docs/Current-EFI.md`、`profiles/tglsyncflip-mode1/README.md`与本仓库
[长期维护流程](../../docs/Maintenance.md)。本机曾进入UEFI Shell，但没有已验证的独立备用EFI。
私人配置备份与原始资料只存本机，不公开。
