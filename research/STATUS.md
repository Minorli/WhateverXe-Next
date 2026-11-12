# 研究产物状态

| 产物 | 状态 |
| --- | --- |
| `0001-bcs-pending-capture.patch` | 诊断增强；CPU 测试与 RELEASE 构建通过，未部署实机；不是卡死修复 |
| `src/POSHDeferred.mm` | 测试进程内生命周期原型；有短测证据，关键 native inflight 析构未覆盖 |
| `XePOSHFix-v0.1.0.cpp` / Info.plist | **退役，禁止安装**；unconditional-free 生命周期安全性不足，不纳入构建 |

历史实验保留用于审查失败方案，不包含 Apple 原始代码页、可安装 kext 或自动部署入口。
