# POSH 生命周期原型

## 问题与当前实现

研究对象是已固定指纹的 AppleIntelTGLGraphicsMTLDriver 中，命令缓冲私有 POSH 分配。
原型在诊断进程里替换特定 init / dealloc 方法，不注入 WindowServer，不修改磁盘 bundle。
全局实现尚未完成，不能把该原型直接作为系统级修复安装。

初始化时仅在以下条件满足时建立 token：完整文件 SHA 一致、目标方法来自对应 bundle、
方法参数 ABI 和 ivar 范围通过检查、only-active 标志为真、分配非空、
分配不是队列共享指针、`malloc_size` 精确等于 77,824 bytes。

token 由命令缓冲关联对象和完成回调分别保留。完成回调仅捕获 token，不捕获命令缓冲。
析构前再次检查身份；确认仍为私有块后置空该指针，执行原生析构，再标记 CPU cleanup done。
GPU completion 独立标记 GPU done。两个事件均到达后，以 atomic exactly-once 调用 deleter。

```text
native cleanup done ─┐
                     ├─ both observed ─ atomic disposal, once
GPU completion ─────┘
```

未提交的缓冲不可能已将工作交给 GPU，原型在该路径记录 uncommitted。
缺失完成回调时保守泄漏；控制对象析构不伪造 GPU 完成。
这避免用“超时后直接释放”掩盖未解决的 GPU 所有权。

## 已验证

- CPU 顺序：CPU 先 / GPU 先、重复事件、缺失完成、1000 次并发竞态。
- ASan / UBSan 下 CPU 模型测试通过。
- 真实 GPU 的空提交、计算、绘制、纹理与混合复制短测抑制内存增长，未见数据不一致。

## 部署阻塞项

1. 真实 GPU 短测中 `dealloc_inflight=0`。调用者提前释放不等于原生对象已经提前析构，
   关键“GPU 尚未完成时原生析构”路径尚未被覆盖。
2. detach 对原生清理、descriptor 与父类不变量的影响尚未完全证明。
3. `malloc_size` 与私有标志不构成完整的所有权证明；别名、迁移、共享变化需审查。
4. MTL error completion 对所有硬件故障语义是否足够安全仍需核对。
5. Objective-C block / 关联对象 / native object 生命周期，需覆盖并发、错误、遗漏与重入。
6. 原型校验静态文件和方法来源，生产实现还需验证实际映射、物理硬件和补丁链，
   不能直接把同一套 offset 应用到不同组件版本。
7. 不得在未满足上述条件时注入系统合成进程，以短测代替长期验证。

## 退役实验

`research/XePOSHFix-v0.1.0.cpp` 是历史的两字节 unconditional-free 实验。
它会移除析构中的分支，未证明全部 GPU 消费者已完成。上游讨论存在纹理负载恢复回归，
因此不提供可安装二进制，不纳入默认构建，不再作为部署候选。
所需 Apple 原始代码页未分发。

根 `metal-memory-check` 已移除旧 `fix` 和 `binaryfix` 运行选项，仅保留 stock 与 deferred。
这项清理在旧对照结果之后完成；原始结果指向当时的测试版本，不能冒称新代码已重新做 GPU 验证。
