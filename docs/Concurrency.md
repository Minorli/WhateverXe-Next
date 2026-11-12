# 原生命令缓冲并发缺陷

## 已复现的范围

2026-11-12，在精确 SHA 为
`1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c`
的官方 MTL 组件上，四个线程同时通过同一个真实 `MTLCommandQueue` 创建 CB。
每轮使用新队列，四个 CB 保持存活且不提交；检查发生在所有创建线程完成之后，
先检查再析构。没有在原生方法内部插入延迟、修改标志或伪造对象状态。

较早原版与未加原子修复的 queue-cache 各 300 轮记录：

| 指标 | 原版 | 队列缓存实验 |
| --- | ---: | ---: |
| active 数量不等于 4 的轮数 | 1 | 1 |
| 同时标记 only-active 的最大 CB 数量 | 2 | 2 |
| 同一共享缓存被多个存活 CB 借用的轮数 | 0 | 1 |
| 非共享私有指针重复数 | 0 | 0 |
| GPU 提交 | 0 | 0 |
| 退出码 | 8 | 8 |

这些是本次观察次数，不是故障概率承诺。并发调度不确定，单次未触发不能否定已保留的失败。
Apple 的 [Metal command organization 文档](https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Cmd-Submiss/Cmd-Submiss.html)
将队列描述为可供多个线程同时编码的线程安全对象，因此这不是故意并发使用同一个 encoder。

## 精确组件中的语义

原生 CB 初始化读取队列 active 数量，以零值设置 only-active，随后增加数量。
原生 commit 在进入父类提交之前减少数量。观察到的这组更新不是一个原子 claim；
该数量也不代表 GPU 在飞的工作数。队列缓存实验依赖这个判断来保证借用互斥，
真实并发结果否定了这一前提。

该标志还进入 `IGRenderPoshEncoder::waitCSSemaphore` 生成的 POSH wait token bit 2。
精确 accelerator 中的 `IntelMTLRenderFunctions::executePosh` 对应分支结合该位和
队列历史字段，决定是否将等待位置清空。它同时涉及资源回收和同步优化，不能按纯粹的
dealloc 所有权位处理。

因此当前不采用以下推断：内存曲线变平就可部署；计数错误只是统计；翻转一个 flag
即可安全修复全部内存与卡死。原始 BCS 报告仍缺等待目标，尚未将这条并发链与其闭合。

## 已实现：原子领取与提交减计数

`src/POSHAtomicClaim.mm` 与 `src/POSHAtomicCode.hpp` 实现进程内的最小计数修复。
在原生零值判断的位置，使用 `lock xadd` 一次完成增加和读取旧值，再根据旧值设置
原有 only-active 字段，保留原生条件跳转所用的 ZF。移除后面的重复增加，并把 commit
中原有的普通减少改为 `lock decl`。没有在原生 init / commit 外包锁，没有新等待或回调，
没有修改 GPU 命令生成、完成状态、析构或释放分支。计数仍在父类 commit 之前下降。

原子领取提前到了 POSH 分配之前，避免两个构造者都观察到零。它仅修复这个领取点的
线性化，不能把该计数当作完整内存租约。未提交 CB 析构不减少计数这一原生行为仍保留。
分配异常的回滚尚未实现，其他原生 telemetry 计数也不在本次修改范围；当前仅为诊断进程候选。

准入包括五个诊断进程名、物理设备选择属性、完整文件 SHA、init / commit / dealloc
映射地址与完整方法字节、整个原始代码页一致、字段类型 / 尺寸 / 对齐，以及 4KiB 页。
校验全部通过后才生成修改，以 COW 复制本进程的一个代码页并恢复 RX；不写系统 bundle。
恢复执行权限失败时终止诊断进程，不能带着部分修改返回。仅允许在创建队列前单线程安装。
组合模式先安装队列缓存，再安装原子计数；不与 deferred 原型组合。

### 最终同版对照

| 真实对象检查，300 轮 × 4 个未提交 CB | stock | atomic | queue-cache-atomic |
| --- | ---: | ---: | ---: |
| 活动计数错误轮数 | 0 | 0 | 0 |
| only-active 最大数量 | **2** | 1 | 1 |
| 共享缓存重复借用轮数 | 0 | 0 | 0 |
| 队列缓存分配 / 析构 | 0 / 0 | 0 / 0 | 400 / 400 |
| GPU 提交 | 0 | 0 | 0 |
| 退出码 | **8** | 0 | 0 |

原版最终仍失败，只是本次没有同时丢计数；不能把计数正确当作独占判断正确。
同一最终 GPU 探针的原版在第 0 轮发现 4 个存活 CB 中有 2 个 only-active，
在提交这批工作之前以 exit 8 停止。此前容量探针有 2 次空提交，应与工作负载计数分开。

atomic 和 queue-cache-atomic 各完成 400 次分批渲染及 400 次交错提交，均 0 字节差异、
0 GPU 错误、最终活动计数 0。工作包括 CPU 图样上传、私有纹理、采样绘制和读回。
交错阶段没有分批屏障，创建与其他线程的 commit / 完成回调重叠，调用者提交后立即释放引用。
只限制在飞深度为 4，不插入人为 GPU 阻塞。加上各自容量探针，真实总提交各 802 次。

请求 maxCommandBufferCount=1 时，第二个构造仍在第一次 commit 前返回；
本机没有观测到实际队列容量等待，不能声称已覆盖“满队列阻塞后恢复”。
CPU sanitizer 测试执行的正是生成后的领取 / 减计数指令，覆盖 300 × 4 个合成对象，
检查 ZF、字段、并发归零和修改区外字节。它不使用 Apple 二进制，也不向 GPU 提交工作。

独立 IOSurface 300 帧 A→B / B→A 对照：stock 增长 22.4791 / 22.4743 MiB，
queue-cache-atomic 增长 0.1955 / 0.2036 MiB，四次逐像素验证均通过。
这证明本次串行内存改善保留，不代表系统显示流畅度、温度或 BCS 修复验收。
完整数值与最终源 / 工具指纹见 `validation/atomic-claim-tests.json`。

## 复现与剩余门槛

构建 `make build/posh-queue-check`，分别运行：

```sh
./build/posh-queue-check --concurrent stock
./build/posh-queue-check --concurrent queue-cache
./build/posh-queue-check --concurrent atomic
./build/posh-queue-check --concurrent queue-cache-atomic
```

以上命令只有原生对象创建与检查，均无 GPU commit。exit 8 表示已观察到不满足不变量；
不能在 CI 中将该值改成成功。普通 `posh-queue-check` 仍验证串行生命周期。
完整脱敏数值见 `validation/queue-cache-tests.json`。

真实 GPU 探针为 `posh-concurrent-gpu MODE ROUNDS POWER_LOG`，MODE 同上，ROUNDS 为
1–100；必须传入持续更新的 powermetrics SMC 日志，样本超过 6 秒或温度达到 75°C 就停止。
该工具只有进程内修改，出现所有权异常时停止提交异常批次，并保留失败退出码。

当前没有系统安装。仍需证明跨 commit 重用缓存时，旧 CB / descriptor / GPU 消费者
都不再访问这块可变内存，并覆盖错误恢复、未提交析构、分配异常和回调重入。
原子计数不能独立回答这些问题，也不能替代原始 BCS 等待目标及生产者证据。

BCS 故障观察与这一实验独立，见 `BCS.md`。CPU/native 对象测试可以证明并发不变量失败，
不能代替实际 GPU 命令、生产者和等待目标的现场证据。
