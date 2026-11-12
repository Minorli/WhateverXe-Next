# 源码审查入口

本文件给首次审查者和外部代码审查工具提供完整上下文。请区分确定事实、解释假说、
实验结果和未满足的部署条件，不因项目名称含 Next 而假定修复已经完成。

## 阅读顺序

1. `Findings.md`：三个主要问题的证据和反证。
2. `Provenance.md` 与 `Validation.md`：源版本、当前加载版本与测试层级。
3. `src/POSHLifetime.hpp`、`src/POSHDeferred.mm` 和 `POSH.md`：POSH 所有权原型。
4. `research/0001-bcs-pending-capture.patch` 及对应三个源码文件：BCS 采集增强。
5. `Deployment.md`：不能越过的整机验证门槛。

## 希望审查者重点回答

### POSH

- 初始化时的私有分配是否在所有路径都保持私有？`m_isOnlyActiveCommandBuffer`
  是否可能改变？与队列的共享块比较，足以排除别名和迁移吗？
- 原生析构前把 `_pPoshUsedAsTarget` 置空，是否破坏析构或父类清理所需的不变量？
- GPU completion 与原生 CPU 清理之后才释放，是否覆盖所有实际消费者？错误完成
  是否真的表示 GPU 不再访问？是否还有 descriptor、驱动 worker 或外部队列持有引用？
- MRC 关联对象、复制的 completion block、局部 retain 在回调与析构并发时是否足以
  保住 token 与控制块？是否有 retain cycle、重入、遗漏回调或不匹配的 allocator？
- 真正的 native dealloc-before-GPU-complete 目前计数为零。如何在不伪造状态的前提下
  覆盖这条路径？没有这项证据不得推荐全局部署。
- 完整文件 SHA、方法 ABI、ivar 尺寸检查还缺什么？静态文件指纹与映射后代码状态
  是不同的检查，应如何建立生产级准入？

### BCS / 恢复

- BCS 使用的 native 提交缓存结构与 owner/backlink 校验，是否需要额外的原生重放证据？
- 前后读一致只证明瞬时采样一致，不能固定内存所有权；现有 nofault 与 guard 是否始终
  在读失败时拒绝把数据解释为有效候选？
- 全局四槽预算是否足以取得第一个 BCS 失败？不能为诊断无限扩容或改变成功提交路径。
- 失败是 stamp、上下文、同步、命令生成还是恢复策略问题？现有证据不足以选择一个。

### 呈现与整机

- UU 远端视频呈现、CVMetalTextureCache / IOSurface 与 WindowServer 合成共同出现，
  但未捕获异常帧。如何分离资源生命周期、平面 / tiling、复制与合成同步问题？
- 不能把 VideoToolbox 出现当成硬解证据，也不能把 CA::OGL 名称当成只使用 OpenGL。
- 上游新构建缺少未公开的机器特定 HDMI / 亮度修改，如何避免替换时引入回归？

## 建议审查输出

按严重性列出文件、代码位置、触发条件、为何错误、最小修改和验证方法。
单独列出待取得证据，不将推测写成已复现事实。不要建议通过关闭系统安全、隐藏 panic、
伪造完成或宽松化 SHA 检查来绕过失败。可直接审查本仓库，无需私人日志或 Apple 二进制。
