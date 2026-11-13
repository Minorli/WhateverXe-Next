# POSH 所有权研究

## 当前进展：原子领取候选

已实现独立的 `POSHAtomicClaim`，修复原生“读零值→设置 only-active→普通增加”竞态，
并将提交减少改为原子操作；没有改析构、GPU 完成或释放逻辑。最终同版原版对照再次
出现 2 个 only-active；候选每种模式通过 300 轮对象检查和 800 次真实渲染 / 交错提交。
完整设计、回归结果与剩余风险见 `Concurrency.md` 和 `validation/atomic-claim-tests.json`。

下面保留未修正计数时队列缓存的真实失败，不因新候选短测通过而删除。
组合候选仍未全局安装：原子领取修复不等于跨提交缓存租约、错误恢复或整机卡死修复。

## 队列缓存实验：并发验收失败

**已确认部署阻塞：四线程并发创建 CB 时，共享缓存会被同时借用。**
300 轮、每轮 4 个存活且未提交的原生 CB：stock 出现 1 次 active 计数错误，
并出现两个 CB 同时标记 only-active，但私有指针没有别名；queue-cache 同样出现 1 次
计数错误，并在 1 轮中让两个存活 CB 借用同一块可变缓存。两者以 exit 8 保留失败。
没有 GPU 提交，不能把这个结果冒称已复现 BCS 挂死。

该竞态否定了“重叠 CB 必然私有”的部署前提。串行 A/B 的内存改善仍有效，但候选
不得进入通用进程或 WindowServer。测试在 native 创建完成后检查四个 CB；未伪造
active 数量、标志、指针或完成状态。实际入口是 `posh-queue-check --concurrent stock`
与 `posh-queue-check --concurrent queue-cache`；这是故障复现，失败应保留。

精确指纹的静态分析还确认，only-active 标志不只控制析构：
`IGRenderPoshEncoder::waitCSSemaphore` 将其写入 POSH wait token 的 bit 2；
内核 `IntelMTLRenderFunctions::executePosh` 的该 token 分支会结合队列历史状态决定
是否清空一个等待命令。因此不得为释放内存直接翻转标志，或把计数错误当作无害统计。
该调用链尚未与本机 BCS 故障现场闭合，不能据此确定原始卡死根因。

`src/POSHQueueCache.mm` 在原生 `MTLIGAccelCommandQueue` 初始化完成、尚未返回调用者时，
分配其缺失的 `m_pPoshUsedAsTarget` 缓存。分配大小为 `0x12010`，使用与原生私有分配
匹配的 C++ allocator，并按原生初始化两个计数数组与 overflow 标志。

固定指纹的原生实现存在以下不配对路径：only-active 命令缓冲从队列借用缓存；
队列指针为空时退回私有分配，但析构仍按 only-active 标志跳过释放。
该队列指针在原生初始化中被置空，未找到原生发布真实分配的路径。
候选补齐队列所有权，而不改变 CB 标志、CB 析构、提交或完成语义。
串行构造时重叠的 CB 继续使用原生私有分配；并发构造不满足这一保证。
队列缓存由原生队列析构释放。

准入同时要求诊断进程名称、Metal registry ID 对应的物理设备选择属性、完整 MTL 文件 SHA、
四个方法的来源路径、映射地址和完整方法字节、构造 ABI、字段类型与对象范围。
仅匹配代码前缀、只看伪装的 Metal 设备名称或路径存在均不足以进入候选。
安装必须在单线程诊断设置阶段、创建队列之前完成。

100 个真实原生队列的未提交 CB 生命周期测试通过：首个 CB 借用，第二个重叠 CB 私有，
先释放客户端队列引用时缓存所有者仍存活，最后 100 个队列均析构。无 GPU 提交。
独立 IOSurface 渲染的正向和反向 300 帧对照均抑制线性内存增长，像素无差异。
串行通过不能抵消上述真实并发失败；错误恢复、跨提交复用和系统合成也未验收。

该实现仍限制在诊断进程，未注入 UU 或 WindowServer，也未修改系统 bundle。
它不是当前可安装的系统级驱动。队列方案仍需独立证明共享缓存
在编码、提交、descriptor 清理和并发之间的生命周期。

## 已删除：双事件延迟释放原型

`POSHDeferred`、`POSHLifetime` 及专用 CPU 测试已从活动源码删除，最后保留版本是
`fd1a78a`。原型的 native inflight 析构、descriptor 别名与错误完成语义没有验收；
GL/Metal 混合探针还会隐式启用它，使该负载无法作为原驱动的干净对照。
历史短测结果继续保留在 `Validation.md`，不再为已退役原型增加测试或实现。

## 退役实验

`research/XePOSHFix-v0.1.0.cpp` 是历史的两字节 unconditional-free 实验。
它会移除析构中的分支，未证明全部 GPU 消费者已完成。上游讨论存在纹理负载恢复回归，
因此不提供可安装二进制，不纳入默认构建，不再作为部署候选。
所需 Apple 原始代码页未分发。

根 `metal-memory-check` 已移除旧 `fix`、`binaryfix` 和 `deferred` 运行选项，保留 stock 与
受限诊断用途的 queue-cache。
这项清理在旧对照结果之后完成；原始结果指向当时的测试版本，不能冒称新代码已重新做 GPU 验证。
