# 未提交缓冲的计数与内存

这里的实现仍是诊断进程候选。没有系统安装。2026-11-12 22:59:46 起又出现真实 GPU
恢复循环，因此本轮不能作为卡死、闪屏或整机稳定验收通过；新内存释放路径尚未完成原生验证。

## 已复现：同一队列的活动计数不归还

在 `Concurrency.md` 固定的 MTL SHA 上，使用同一个真实队列顺序创建并丢弃 32 个 CB。
每次排空 autorelease pool，并用关联对象的析构观察者确认 CB 已销毁。没有 GPU 提交。

| 同一探针的顺序阶段 | stock | 先前 atomic | lifecycle 候选 |
| --- | ---: | ---: | ---: |
| 已销毁 CB | 32 | 32 | 32 |
| 析构后计数非零的轮数 | 32 | 32 | 0 |
| 后续 only-active 判断错误 | 31 | 31 | 0 |
| 最终 active | 32 | 32 | 0 |

原生 init 增加计数，commit 减少计数，dealloc 没有减少。原子增减修复了并发更新，
没有补上未提交析构。因此不能仅以先前的并发创建 / 提交测试通过判断生命周期完整。

## 实现边界

`installPOSHAtomicClaim(registryID, true)` 保留原有指纹、设备、完整方法 / 页校验，
额外校验队列和 POSH 字段及 init 标量 ABI。默认参数仍只启用先前的原子计数方案。
安装只能在创建队列前单线程进行，不能在已运行队列上从 atomic 升级到 lifecycle。

每个成功的 native init 关联一个 token，独立保留队列，记录 commit 是否进入。commit
入口先标记，再调用仍包含原子减计数的原生方法。这里不依赖 Metal status 的稍后更新，
也不在正常提交上再减一次。重复 commit 或缺少 token 会终止诊断，不伪造成功。

dealloc 先保留 token，完整调用原生析构。原生 descriptor、父类和关联对象的清理都返回后，
仅通过保留的 token / 队列，对未进入 commit 的 CB 做一次原子减计数。之后绝不读取已销毁 CB。
关联对象的析构可能重新创建 CB，因此归还不能提前到原生析构之前。

原生 init 在领取前后都可能异常。当前没有证明指令级的异常回滚所有权；遇到 native /
跟踪初始化异常会终止诊断进程，不根据并发队列的计数猜测是否应回滚。这不是分配异常修复。
外部预先缓存 IMP、其他构造入口及真实应用行为没有验收，不能据此全局部署。

### 必须同时处理的内存问题

只归还计数会让无共享缓存的每个顺序 CB 再次成为 only-active，而原生析构在该分支跳过
POSH delete。这会增加原生私有分配泄漏，不能将纯计数候选独立发布为内存修复。

最终源码额外捕获 only-active 且不属于队列缓存的原生私有分配，并检查分配尺寸及析构前
的指针 / 标志 / 共享身份。只有 **没有进入 commit 且完整 native dealloc 已返回** 时才删除，
随后归还计数。没有翻转 only-active，没有提前释放 descriptor 消费者仍可访问的内存。
共享缓存不在此路径中删除。已提交的私有块仍保留原生行为；其 GPU / CPU 双生命周期另行审查。

这段新增释放逻辑目前只有构建验证，不能把此前纯计数版本的 native / sanitizer / GPU
结果转记到它名下。计划新增的 512 次同队列弃用检查会同时检查 malloc 增长、所有权计数和
private tracked / freed。首次尝试卡在 Metal 设备初始化，尚未安装候选，也没有进入该检查。

## 本轮已经完成的验证

纯计数 lifecycle 与 queue-cache-lifecycle 两个模式各通过：

- 同队列顺序、交叠、8 次关联析构重入及 4 线程弃用，共 307 个 claim / 归还 / token 析构；
- 300 × 4 并发对象创建，连同串行阶段共 1400 个 claim / 归还 / token 析构；
- 上述同队列 307 个对象的 ASan / UBSan 构建，无报告；这不对 Apple 未插桩代码作完整保证；
- 真实 GPU 测试：16 次普通编码后弃用，16 次同一组缓冲 / 私有纹理先弃用再提交，
  200 次分批渲染、200 次交错提交、2 次容量空提交。各模式共 418 次真实提交，
  0 数据差异、0 GPU 错误；450 个 token 最终全部析构。

原版与先前 atomic 在资源复用测试中第一次弃用后 active=1，探针停止，未提交该组后续工作；
此前容量探针各有 2 次提交。该失败不能改为成功。

但是，短测在 22:58:08 前结束，22:59:46 起系统出现新恢复循环。不能排除与前面候选
工作负载的关系，也不能因报告的 Application 是系统进程就认定由它独立造成。
候选的长期 / 整机验收不通过。完整脱敏记录见 `validation/abandonment-tests.json`。

## 故障后检查与复现

新增 `GPUHealth.hpp` 在初始化 Metal 前读取内存型 `TGLRecoveryLive`。
缺失 / 非零的恢复记录均拒绝探针；需要干净启动再比较。GPU 探针在温度检查点也重查该记录。
它不修改恢复逻辑，不请求 GPU 工作，不以缺失数据推定健康。
本次已在故障状态验证其 exit 14，未再次进入 Metal 设备初始化。

干净启动、持续 SMC 监测和独立故障监听就绪后，才运行：

```sh
make all cpu-test
./build/posh-abandon-check stock
./build/posh-abandon-check atomic
./build/posh-abandon-check lifecycle
./build/posh-abandon-check queue-cache-lifecycle
./build/posh-queue-check --concurrent lifecycle
./build/posh-queue-check --concurrent queue-cache-lifecycle
./build/posh-concurrent-gpu lifecycle 50 POWER_LOG --reused-abandon
./build/posh-concurrent-gpu queue-cache-lifecycle 50 POWER_LOG --reused-abandon
```

这些命令是维护者的复现入口，不会安装系统组件。缓存的跨提交所有权、分配异常回滚、
原始 BCS 等待目标 / 生产者，以及新计算通道恢复现场仍未闭合。
