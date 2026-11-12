# 原生命令缓冲并发缺陷

## 已复现的范围

2026-11-12，在精确 SHA 为
`1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c`
的官方 MTL 组件上，四个线程同时通过同一个真实 `MTLCommandQueue` 创建 CB。
每轮使用新队列，四个 CB 保持存活且不提交；检查发生在所有创建线程完成之后，
先检查再析构。没有在原生方法内部插入延迟、修改标志或伪造对象状态。

原版与 queue-cache 各 300 轮的记录：

| 指标 | 原版 | 队列缓存实验 |
| --- | ---: | ---: |
| active 数量不等于 4 的轮数 | 1 | 1 |
| 同时标记 only-active 的最大 CB 数量 | 2 | 2 |
| 同一共享缓存被多个存活 CB 借用的轮数 | 0 | 1 |
| 非共享私有指针重复数 | 0 | 0 |
| GPU 提交 | 0 | 0 |
| 退出码 | 8 | 8 |

这些是本次观察次数，不是故障概率承诺。并发调度不确定，单次未触发不能否定已保留的失败。
Apple 的 [Metal command organization 文档](https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Cmd-Organization/Cmd-Organization.html)
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

## 复现与下一步

构建 `make build/posh-queue-check`，分别运行：

```sh
./build/posh-queue-check --concurrent stock
./build/posh-queue-check --concurrent queue-cache
```

两个命令只有原生对象创建与检查，均无 GPU commit。exit 8 表示已观察到不满足不变量；
不能在 CI 中将该值改成成功。普通 `posh-queue-check` 仍验证串行生命周期。
完整脱敏数值见 `validation/queue-cache-tests.json`。

后续所有权设计必须独立于这个有竞态的计数，并覆盖未提交、提交中、错误完成和原生析构。
不能简单在整个原生 init / commit 外包一把锁：需先核实父类队列容量等待、回调重入和
跨线程提交的锁顺序，避免新增阻塞。现阶段没有实现或部署这样的同步修复。

BCS 故障观察与这一实验独立，见 `BCS.md`。CPU/native 对象测试可以证明并发不变量失败，
不能代替实际 GPU 命令、生产者和等待目标的现场证据。
