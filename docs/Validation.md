# 验证记录

日期：2026-11-12。以下保留精确的通过边界；原始日志不分发。
机器与实际加载指纹见 Provenance。

## 1. CPU / mock 与 native replay

继承驱动完整测试运行 216 项：209 项 exit 0，7 项在完整 accelerator SHA 检查处拒绝。
拒绝项为：

```text
dsi_backing_adapter_native
dsi_backing_native
xelp_forcewake_irq_native
tgl_rpcs_native
small_gt_urb_native
tgl_tlb_preparser_native
tgl_semaphore_tlb_native
```

这 7 项不能计为通过。多个测试预期 accelerator SHA
`bd1a21f5e622aadabdab6441f6b5c38ed73fa3053c783fdf0fb7e4b309f273d0`，
当前官方 v0.4.1 Tahoe payload 与本机均为 Provenance 所列另一 SHA。
本项目没有降低守卫或把新 SHA 直接加入允许列表。

变更关联的七项 CPU 测试（pending、select、detail、prestop、recovery、graph、ggtt）
单独复核 exit 0。pending 新增八种 BCS 接纳、身份拒绝和采样竞态情形。
真实 BCS hung 时的新采集能力尚未在本机部署验证。

根 POSHLifetime 模型在 ASan / UBSan 下覆盖两种事件顺序、重复完成、缺失完成和
1000 次 CPU / GPU 事件线程竞态。模型通过不能证明 Apple 私有对象的真实生命周期。

## 2. 真实 GPU 短测

这些是同日已有的有界测试记录；在用户再次报告闪屏和风扇加速后，停止新增 GPU 压力测试。
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
本次公开整理移除了它们的运行入口；整理后的诊断代码只重建，不在当前不稳定会话重跑 GPU 工作。

## 3. 构建

BCS 诊断增强后的 RELEASE 构建已成功生成两组件：

| 组件 | SHA-256 |
| --- | --- |
| WhateverXe | `2a3a609ba3b2f5328413ad9b3b167fb8dbef0891acb3ce23900d21c7c94e4a51` |
| AppleIntelTGLCompat | `a9cc21f1dc4775f9625d036abe9c1a868cf8cdbffe0b117da09eab0e74f673e1` |

构建含继承的 warning，不宣称 warning-free。构建清单见 `validation/driver-build-manifest.json`。
这些二进制没有安装到当前 EFI，也没有在该机器实际启动。

公开整理后的根诊断工具已重新构建，CPU 模型与提交数量边界测试通过。
8 个非法参数或旧 `fix` / `binaryfix` 入口在创建 Metal / GL 设备之前以 exit 2 拒绝。
只读采集器完成 1 秒请求的轻量 smoke，报告目录可读，基线后新增 GPU 报告为 0；
检查该路径不向 GPU 提交工作。未重新运行真实 GPU 探针。

## 4. 整机与长期验收

未完成。当前实际仍运行下游 0.8.1 DEBUG，VA 组件保持隔离。
没有 30–60 分钟全局修复内存曲线，没有新驱动的启动、双屏、远程视频、睡眠恢复验收。
没有异常帧录像，也没有新的 BCS 现场证据能证明具体根因已修复。

发布源码和 CI 通过只是让结论可以审查，不能代替这些整机结果。
