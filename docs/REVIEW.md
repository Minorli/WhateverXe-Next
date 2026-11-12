# 源码审查入口

本文件给首次审查者和外部代码审查工具提供完整上下文。请区分确定事实、解释假说、
实验结果和未满足的部署条件，不因项目名称含 Next 而假定修复已经完成。

## 阅读顺序

1. `Findings.md`：三个主要问题的证据和反证。
2. `Provenance.md` 与 `Validation.md`：源版本、当前加载版本与测试层级。
3. `src/POSHAtomicClaim.mm`、`src/POSHAtomicCode.hpp` 与 `Concurrency.md`：原子计数候选和同版失败 / 通过对照；
   `src/POSHQueueCache.mm` 与 `POSH.md`：队列所有权候选；
   `src/POSHLifetime.hpp`、`src/POSHDeferred.mm` 是独立的较早原型。
4. `research/0001-bcs-pending-capture.patch` 及对应三个源码文件：BCS 采集增强。
5. `Deployment.md`：不能越过的整机验证门槛。
6. `BCS.md`：等待指令现场采集的限定、拒绝条件和待回答的问题。
7. `Abandonment.md`：同队列弃用的真实失败、阶段不同的候选与晚发恢复循环；
   `validation/abandonment-tests.json` 明确哪些结果不属于最终释放逻辑。

## 希望审查者重点回答

### POSH

- 首先审查已复现的并发失败：`posh-queue-check --concurrent`。原版 active 计数丢失，
  队列实验还会让两个未提交 CB 同时借用缓存。only-active 也进入 GPU 等待 token，
  不能只修 free 分支。新原子候选已通过进程内对照，请核对领取点的寄存器存活、ZF、
  新线性化位置、移除的晚增加，以及 commit 减计数范围；CPU 测试执行实际生成指令。
- 原子领取在 POSH allocation 前发生，分配异常尚无回滚。新增可选 lifecycle 对成功初始化
  的未提交 CB 在完整 native dealloc 后归还计数；异常终止诊断，不能称已经支持回滚。
  请检查 token 保留、commit 先标记、析构重入、缓存身份和原生私有分配的延后删除。
  最后新增的未提交私有块删除路径尚无原生验收，勿把较早的纯计数结果转记给它。
- 跨 commit 的缓存重用是否早于旧 CB / descriptor 最后一次 CPU 访问，或 GPU 消费结束？
  交错提交短测通过不构成完整证明。容量探针没有观测到实际阻塞，覆盖不足需保留。

- 队列缓存是否在返回给调用方前完成发布？匹配的原生 init / dealloc、所有消费者和
  descriptor 清理是否真的支持该共享生命周期？active 数量在 commit 时下降，不能当作 GPU 在飞数量。
- 原生 render encoder 对计数表的重置能否覆盖 compute、blit、多 encoder 和跨提交路径？
  并发创建 CB、尚未完成的旧 CB 析构、错误恢复可能怎样干扰共享缓存？
- 正反两次 IOSurface 对照只验证内存斜率和正确性；呈现时间戳全零，不能计为显示时序通过。
- 后续问题针对 deferred 原型；不要将它们与队列方案混为一种析构补丁。

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
- 22:59:46 起的计算通道故障随后阻塞新 Metal 初始化，原生 commandQueueStop → finishEvent
  → eventTimeout → checkForProgress 在服务终止线程中反复等待。现有首次快照只完整采 RCS，
  该 RCS 全零且 detail ownership 拒绝，不可冒充当前 CCS / 旧 BCS 的等待目标证据。
  恢复调用返回与真正恢复进度应如何分别证明？短测后的失败不能被过程内 exit 0 覆盖。

### 呈现与整机

- UU 远端视频呈现、CVMetalTextureCache / IOSurface 与 WindowServer 合成共同出现，
  但未捕获异常帧。如何分离资源生命周期、平面 / tiling、复制与合成同步问题？
- 不能把 VideoToolbox 出现当成硬解证据，也不能把 CA::OGL 名称当成只使用 OpenGL。
- 上游新构建缺少未公开的机器特定 HDMI / 亮度修改，如何避免替换时引入回归？

## 建议审查输出

按严重性列出文件、代码位置、触发条件、为何错误、最小修改和验证方法。
单独列出待取得证据，不将推测写成已复现事实。不要建议通过关闭系统安全、隐藏 panic、
伪造完成或宽松化 SHA 检查来绕过失败。可直接审查本仓库，无需私人日志或 Apple 二进制。
