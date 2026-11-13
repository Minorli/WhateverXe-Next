# 研究产物状态

| 产物 | 状态 |
| --- | --- |
| `0001-bcs-pending-capture.patch` | 诊断增强；CPU 测试与 RELEASE 构建通过，未部署实机；不是卡死修复 |
| 旧 `src/POSHDeferred.mm` | 已删除；历史见 `fd1a78a`。未覆盖的释放语义与隐式 GL 混合加载会干扰原版对照，不再维护 |
| `XePOSHFix-v0.1.0.cpp` / Info.plist | **退役，禁止安装**；unconditional-free 生命周期安全性不足，不纳入构建 |

历史实验保留用于审查失败方案，不包含 Apple 原始代码页、可安装 kext 或自动部署入口。
