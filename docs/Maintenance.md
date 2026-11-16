# 长期维护与G83整机集成

维护者于2026-11-16要求持续跟踪上游，先在自己的两个仓库整合，再验证和部署本机。
本仓库的`main`负责显卡源码、独立插件和脱敏证据；完整EFI由
`Minorli/g83-11gen-hackintosh-tahoe-efi`的`main`维护。

## 上游与更新流程

主要上游为`b00t0x/WhateverXe`与`xiananliu/g83-11gen-hackintosh-tahoe-efi`。
另需检查`xiananliu/wex-modified-edition`和`b00t0x/WhateverXe-Graphics`：本机仍依赖
闭源下游显示增强，源码候选与实际二进制不能混称为“已吸收”。

每次相关维护：

1. 读取AGENTS、Deployment和本机快照；保存现有工作，干净时`git pull --ff-only`。
   核对上游新提交、release、相关issue及回归，记录日期、起止commit和采用/延期原因。
2. 先在自有`main`整合可审查的变化，保留本地修复、精确SHA/字节守卫、许可与来源。
   对vendored源码比较真实差异，不用整包覆盖丢失本机增强，也不自动替换闭源WEX。
3. 做与改动相称的测试和构建。CPU/mock、进程内GPU、落盘、启动和长期验收分层记录；
   不以构建通过代替GPU恢复安全，不为凑结论反复运行会卡死的负载。
4. 提交并推送自有仓库后，交由G83集成具体候选；用准确commit和产物SHA互相引用。
   通用修复用短分支回送上游，整机改动不混入独立无线PR。
5. 部署前准备具体差异、风险、实用回退和验收条件，取得本轮实验安装授权；
   用户安排重启。核对EFI分区，备份私人配置，保留三码、ROM、ACPI/USB和镜像布局。
6. 启动后核对真实加载状态与SHA，做有界验收；失败回退。更新Findings/Deployment和
   两仓库维护记录，不把未部署候选写成当前配置。

没有配置后台自动跟踪或无人值守部署；本文件是后续每次维护的持久约定。
本机项目统一位于`/Users/minorli/Documents/Projects`，私有记忆入口为该目录的
`MAINTENANCE.md`，原始资料留在权限700的`g83-machine-local/`或ignored的`local/`。

## 当前基线与优先事项

[deployments/g83-current](../deployments/g83-current/README.md)保存当前脱敏config、
完整EFI文件指纹和G83固定提交，不再分发另一份完整EFI。
本机仍为下游WEX0.8.1 DEBUG、Graphics0.4.3 Metal、TGLSyncFlip1.0.2模式1；
默认1已写入，但下一次启动尚待验收。VA仍隔离，S0ix和0.8.2候选均未部署。

模式1改善维护者观察到的原UU闪屏；RCS/CCS卡死与恢复失败未解决。
先沿现有事故审查跨引擎依赖、完成通知和重新调度，区分首次挂起和复位后连锁。
性能需分别衡量VA隔离后的CPU解码与旧下游热路径诊断开销。
不伪造完成、不放宽守卫、不杀WindowServer、不自动重启，不恢复VA或主动复现整机卡死。
公司网络不具备SSH条件，用户选择不开远程登录；不要把SSH采集作为推进前提。
