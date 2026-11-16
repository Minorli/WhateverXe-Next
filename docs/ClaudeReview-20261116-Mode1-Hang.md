# Claude 独立审查：首次 RCS 挂起、CCS 恢复失败、模式 1 契约与高负载（2026-11-16）

> **2026-11-16 本机复核补注（保留外部作者原文）：**1.3 的标签来源归因不成立，
> 原生报告实际读硬件执行列表状态的保存副本；WEX legacyEntry 是另一条路径。
> 第2节的时序结论也需收窄：channel29进度错误早于首个记录到的restart，默认映射
> 为CCS，但故障时flags未取得，尚不能确认该默认映射当时生效。已接受的semaphore
> 等待保留及重新提交路径经6个CPU正例与24个拒绝反例核对，没有直接丢任务。
> 详见 [本机复核及边界](Incident-20261116.md#1624-后续复核标签来源任务保留与跨引擎时间线)。
> 以上没有证明M2或整机卡死已修复。

## 审查基线

- **HEAD**：`20c685628503be8a00bbfcaffea58f3869171cd2`（`main`，包含 `claude-gpu-hang-review-prompt.txt` 和 `high-load-mode1-20261116.json`）。
- **Apple 二进制**：均为**精确版本**，从官方 WhateverXe-Graphics 发行包中解出，只做静态分析，未上传。
  - accelerator `AppleIntelTGLGraphics` 为 `567511be…6e`（0.4.1 包内的 Tahoe 版本）；
  - framebuffer 为 `115a4616…888e`；
  - MTLDriver 为 `25b6c72e…4faa`。
  - 文中偏移都是文件偏移，等于 vm 偏移。
- **部署的 WEX**（`df96a2ca…`）是闭源下游，**没有对应源码**。凡是引用 `driver/WhateverXe` 的地方，都标为"未部署候选"。
- **已读文件**：
  - 根目录与 `driver/WhateverXe` 的 `AGENTS.md`；
  - `Incident-20261116.md`；
  - 两份 `validation` JSON（mode1 固化、高负载）；
  - `REVIEW.md` 中的 A1–A3；
  - `kern_tgl_gpurec.hpp`，`kern_tgl_csb.hpp`；
  - `kern_xe.cpp` 中 gpurec 和 CSB12 的生产 wrapper（约 15399–15800 行）；
  - TGLSyncFlip 的源码。
- **运行的测试**：无。本文没有改代码。

## 发现 1（最高优先级）：`WAIT_ON_SCANLINE` 不能当作"RCS 在等扫描线"，也和第二次的现场不吻合

**结论：部分成立〔静态，部署的 accelerator 与 framebuffer〕；根因仍证据不足。**

### 1.1 本机 accelerator 里唯一的 scanline 等待发出者

本机 accelerator `567511be` 里能发出 scanline 等待的只有一处：`IGAccelSurface::generateSync`（`0x82AE6`）。

**调用条件**：由 `IGAccelSurface::submitSwapFlush` 在 `0x8250D` 调用，目标是正在扫描输出的表面（`isActiveScanout` / full-screen surface），即"往前台缓冲 blit、避免撕裂"这一类操作。发出前还要同时满足以下门控：

- pipe `+0x73D4==1` 且 `isActive`；
- accel `+0xC78` bit1 被置位，`+0x128C` 不为 0；
- 调试参数 5 未设置；
- 非缩放模式。

**写进命令流的内容**（通过 `IntelAccelerator::addSyncEvent`，顺序如下）：

1. `LRI DE_RRMR(0x44050)`，解除对应 pipe scanline 事件的屏蔽：
   - pipe0 写 `0xB077EFEE`；
   - pipe1 写 `0xB077EEEF`；
   - pipe2 写 `0xB077AFEF`；
   - 随后接一条 `SRM` 回读。
2. 两次 `MI_LOAD_SCAN_LINES_INCL`（`0x09000000`），或在整屏情况下用 `EXCL`（`0x09800000`）。pipe 选择位在 bit19/bit21。范围由 bounds 的 y/高度换算，并截到帧高。
3. **当 `accel+0x1190 & 0x18000 == 0` 时**，`synchronousCtxSwitch(…,1)` 写一条 `LRI 0x2244 = 0x00080008`，即 `CTX_CTRL` 的 "inhibit synchronous context switch"。
4. `MI_WAIT_FOR_EVENT`：pipe A/B/C 分别为 `0x01800001`、`0x01800100`、`0x01804000`。**没有超时。**
5. `synchronousCtxSwitch(…,0)`，解除 inhibit。
6. `LRI DE_RRMR = 0xB077EFEF`（静止屏蔽值）+ `SRM`。

**其他位置的情况**：

- `IGAccelDisplayPipe::generateFlip`（`0x7FF0F` / `0x803F5`）是 GPU 命令流里的 `MI_DISPLAY_FLIP`（`0x0A000001`，async 位在 bit22）加 flip-pending 等待（`0x01800002` 等），同样用 inhibit 包住，同样自己解除和恢复 `DE_RRMR`。
- 0.4.3 MTLDriver 里**没有** `MI_WAIT_FOR_EVENT` 的显示事件编码，也没有 `0x44050`（按字节常量统计为 0 次）。

### 1.2 更正我上一份提示词里关于 `DE_RRMR` 的推论

`DE_RRMR` 的解除屏蔽**由 accelerator 在命令流里自己负责**：等待前用 LRI 解除，等待后恢复。framebuffer 里只有 `CamelliaTcon2::DisablePsrDC6` 在 `0x80422` 写全 1，这不能说明"没人解除屏蔽"。

- 这条 Camellia 路径要满足 `fb+0xB48==3`，又属于 Apple TCON，G83 是否会走到仍未证实。
- 只有当某个 CPU 侧写者在 LRI 解除屏蔽之后、等待结束之前，又把 `DE_RRMR` 写回全屏蔽，才会让等待永远收不到事件。目前没有证据显示存在这样的写者。

### 1.3 能用来证伪的矛盾

`WAIT_ON_SCANLINE` 并不是 Apple 原生从硬件直接读出来的：

- 部署的 WEX 把 Gen12 CSB 转换成供体使用的旧格式。仓库候选 `kern_tgl_csb.hpp` 的 `legacyEntry` 里，switch detail==3 对应 `0x100`（WAIT_ON_SCANLINE），detail==4 对应 `0x80`（semaphore），`0x4` 对应 ELEMENT_SWITCH。
- 闭源部署版本应当做了同类转换，**但未核对**。

另一方面，`0x18000` 就是 `SingleChannelIndex` 的两位（`populateAccelConfig`：`shll $0xf`；RefFirmware 调度器会强制设为 1）。plist 默认值是 0，WEX 在 TGL 上用 `-disablegfxfirmware` 选的是 host 调度器（`0x2800000`），不是 RefFirmware。

所以**如果本机 `SingleChannelIndex==0`**，`generateSync` 和 `generateFlip` 的等待都被 inhibit 包住，不应该产生"因 scanline 等待而切换 context"。那么报告里的 WAIT_ON_SCANLINE 只能来自以下三种情况之一：

- (a) 本机该值实际不为 0；
- (b) Gen12 detail 到旧格式标志的映射错了，真实原因是别的等待，比如 semaphore；
- (c) inhibit 在 Gen12 上不生效。

### 1.4 两次现场不是同一种机制

- **第二次**：RCS `NO ACTIVE ELEMENTS`、`HEAD==TAIL`、只有 CS 未完成。这**不像**"引擎停在 `MI_WAIT_FOR_EVENT` 上"：那种情况下 context 仍是 active element，并且 `RING_CTL` 的等待位会是 1。它更像下面的**机制 M2**：context 6 因为某种等待被切换出去之后，软件再也没有把它重新提交；或者它被挂在一个永远不会满足的依赖上。
- **第一次**：有 pending 命令，CS 和 SDE 都未完成，最后指令是 `PIPE_CONTROL`。这更像 3D 管线或资源层面的挂起，即竞争解释 **E1**，WAIT_ON_SCANLINE 只是历史上的切换原因。

### 三种候选机制及区分方法

区分所需的数据**已经在本机 `local/` 的原始报告里**，不需要复现。Apple 的 `IGHardwareRingBuffer::dumpRingStatus`（`0x440C4`）会打印以下字段：

- 每个 ring 的摘要：`- waiting on a semaphore`（状态 bit2）或 `- waiting on an event`（bit3）；
- `RING_CTRL` 的值，bit11 是 "waiting"，bit10 是 "semaphore waiting"；
- 状态 bit3 为 1 时，额外打印 `This ring is stuck waiting on an event!` 和 `DE_RRMR = 0x…`（`0x44416..0x4445B`）。

| 机制 | 现场特征 | 下一步 |
| --- | --- | --- |
| M1：引擎正在等显示事件 | `RING_CTRL` bit11=1，或有 "stuck waiting on an event" 行；`DE_RRMR` 为某个 pipe 的解除值 | 查该 pipe 当时是否在运行，以及 `generateSync` 所用 plane index 和硬件 pipe 是否一致（镜像时） |
| M1'：事件被屏蔽 | 同上，但 `DE_RRMR` 为 `0xB077EFEF` 或全 1 | 找出在等待期间覆盖 `DE_RRMR` 的 CPU 写者 |
| M2：wait-switch 之后没有重新提交 | `RING_CTRL` bit11=0、没有 active element；WEX 记录的最后一条 ctx6 CSB 为 ElementWait/ActiveToIdleWait，detail 为 3 或 4，之后再没有 `to=ctx6` | 审查 WEX CSB 转换对 detail 3/4 的处理，以及 `SingleChannelIndex` |
| E1：3D/资源挂起 | ACTHD 落在 batch 内，INSTDONE 中 SDE 等单元忙，EIR/IPEIR 或页错误非零 | 转向 POSH/Metal 资源生命周期，即 REVIEW 的 A1–A2 |

另外还需要补两个字段：

- **`accel+0x1190` 的低 32 位**。部署版本可能已经发布了它：候选源码里 `tglObserveAccelerator(…,8,…)` 和 `TGLHostRecord q[44]` 都记录了这个值，请在 ioreg 或 `local/` 的历史里找。
- **WEX 记录的原始 Gen12 CSB 双字**（候选源码中为 `q[13..16]`）。

### 最小修复建议

现在不改代码。原因：这三种机制对应三种完全不同的修法，在数据出来之前改任何一处都是盲改。

- 如果是 M2，最小修复点在 WEX 的 CSB 转换：一个因等待而被切走、尚未完成的 context，必须由原生路径保留在等待队列里，或者在事件发生后重新提交。不能把它当作完成，也不能直接丢掉。
- 如果是 M1，最小修复是让 `generateSync` 的门控反映真实的硬件 pipe 状态。原生在 pipe 未激活、缩放模式、调试参数 5 这几种情况下本来就不发这条等待，所以这只是去掉一条用于防撕裂的呈现等待，不是跳过完成等待。
- 两种都需要先在 CPU 上写回归测试。

## 发现 2：#109 共享复位只能解释第二次事故后面的 CCS，不能解释首次 RCS

**结论：成立〔静态〕；以下针对未部署候选 `kern_tgl_gpurec.hpp` / `kern_xe.cpp:15399`。**

### 能解释与不能解释的部分

- RCS 和 CCS 共用 GDRST bit1。原生 `resetGraphicsEngine` 只回放本引擎的列表，所以 RCS reset 之后，CCS 的 `RCU_MODE` CCS enable 和 NONPRIV 都回到默认值。这和"RCS 第二次 reset 之后，Windows App 的 CCS context185 才反复挂起"的时间顺序一致。
- 它解释**不了**首次 RCS 挂起。两次事故里 RCS 都是先出问题的。

### 候选代码的问题

1. **兄弟引擎的列表快照取自 reset 之前。** 只用 `HWS_PGA!=0` 判断快照是否可信。如果兄弟引擎已经被更早的一次共享 reset 清掉，而它的 `HWS_PGA` 又被恢复过，快照就可能是坏值。
   - `extras` 有 `healthy()`（检查 RCU_MODE）作守卫，whitelist 有常量兜底，但**列表其余寄存器没有守卫**。
   - 修法：只在 `goodOK` 第一次成立时，也就是首个干净启动期快照，才更新 `g.good`，之后只读不写。
2. **全局单例状态，不能重入。** `g.pendingOwner`、`g.pendingBar` 是全局单例。如果 RCS 和 CCS 的 `resetGraphicsEngine` 可能并发（调度器的锁没有静态证明），这些状态会互相覆盖。
   - 需要找到原生调用者 `eventTimeout` 和 `processGPUReset` 所持有的锁。找不到之前，应当用原子标志拒绝并发进入，第二个进入者直接走原生路径。
3. **兄弟引擎的重放可能不幂等**，这一条是推断。`processGPUReset(sibling)` 会按 LRC 镜像重新提交兄弟引擎当时的 active element。GDRST 并没有先保存这个 context，镜像里的 `RING_HEAD` 是上一次切换时保存的旧值，可能导致命令被重复执行。
   - 这和 CCS "returned stamp 不变、submitted 增加"的现象相容，**但未证实**。
   - `-wex-tgl-gpurec-halt`（先 REQUEST_RESET，等到 READY）方向正确，但它同样不保存 context。
4. **"恢复成功"只能用 stamp 是否前进来判断**，`resetOK`/`ok` 不算。候选里没有 stampfix，这是对的。

### 最小修复与回归测试

- **最小修复草图**：做第 1、2 条，即锁定首个干净快照并拒绝并发重入，暂时不动重放。
- **回归测试**：
  - 用 CPU 模拟两次连续共享 reset，第二次时兄弟引擎的寄存器已经被清掉，断言写回的是首个干净快照；
  - 模拟两个线程同时进入，断言后进入的线程走原生路径。

### 部署前提

部署这个候选需要替换闭源 WEX，不是单变量改动。在发现 1 有结论之前**不建议**部署。

## 发现 3：模式 1 的契约，已核对的部分和缺口

**结论：未发现缺陷〔静态，部署的 framebuffer〕；调用方语义证据不足。**

### 已核对

- `PerformFlipTransaction` 在 `0x2BD3F..0x2BD4C` 把整份 params（`0xB8` 字节）**复制**到 fb 自己的 `fb+0x4010` 缓冲。
  - 所以模式 1 置的 bit0 会留在这份副本里。`RestoreTransactions`（`0x33E4E`，由 `setAttribute` 在恢复显示时调用）回放的就是这份副本。
  - 后果是恢复路径会以 sync 方式回放。这是保守的方向，不会造成错误。
  - 原生不保留调用方的 params 指针；我们恢复的那一位只影响调用方自己的结构。
- `callPlatformFunction`（`0x2ACD5`）只把返回码存进 `fb+0x4A50` 的状态槽；`0xE00002D8` 会让 `fb+0x4A80` 置 1。完成状态通过 `IsTransactionComplete` 读取 `fb+0x4008`（`0x2AD2F`）。
  - 在 sync 路径下，`fb+0x4008` 由中断置位；在 async 路径下，由忙等之后置为 -1。
  - 也就是说，模式 1 让原本走 async 的事务变成"中断完成"。只要调用方会读取完成状态，语义就不会变。
- **耗时尾部**：模式 1 里 ≥16 ms 的条目和约 17.1 ms 的最大值，对应原生在接近帧边界时调用 `hwWaitForVBlank` 等满一帧，属于正常的 vblank 解释。模式 0 里的 ≥16 ms 条目则是 async 忙等达到 333×50 µs 上限后超时。直方图只覆盖 `PerformFlipTransaction` 本身，不代表端到端延迟。
- **ModeGate**：只固定模式，不负责保护 params 或 plane 缓存。这两者在原生实现里由调用方各自的路径持有，我们没有引入新的共享写入，所以不需要额外加锁。

### 缺口

对于原本请求 async 的事务，IOAcceleratorFamily2 的调用方返回后是否**马上释放或复用旧的扫描缓冲**，而不等 `IsTransactionComplete`？这段代码不在这三个 Apple 二进制里。

- 如果确实不等，模式 1 会带来"缓冲已被复用但还在扫描"的窗口。但这类问题应当表现为闪烁增多，和观察到的现象相反。
- **判定方法**：从本机的 kernel collection 提取 `IOAccelDisplayPipe2` 里调用 `IsTransactionComplete` 的函数窗口，只取那一段即可。

### 对"模式 1 与卡死无关、它无害"的更正

这个说法未经证明。

- 模式 1 解释不了安装之前的两次事故。
- 但它改变了 flip 的时序，也改变了 WindowServer 是否走 `submitSwapFlush` 向前台 blit 的条件，因而可能改变 `generateSync` 的发出频率。这一点未测。

## 高负载

- **主体是解码**：VA 被隔离以后，UU 的 HEVC 流由 VTDecoder 的 VCP 路径在 CPU 上处理，再经 `CVMetalTextureCache` 交给合成。WindowServer 54% 和 VTDecoder 28% 的单核占用与此一致。这不是 GPU 即将崩溃的信号，也不能归因到模式 1。
- **部署的旧下游的额外开销**：每次 `updatePlane` 前后各替换一份 2 KiB 的 registry 快照。这会拿全局 IORegistry 锁，再加一次 OSData 分配；全机约 190 条记录/秒。
  - a9aaf05 的门控就是最小修复，但它需要替换闭源 WEX。
  - 代价是失去 plane-trace 证据，所以应当在闪烁和卡死结论出来以后再做。
  - 没有固定负载的 A/B，不能说它能降温或修复卡死。

## 对旧材料的更正

其中第 2、3 条出自我写的 `claude-mode1-followup-prompt.md`，已在该文件中同步修正。

1. "两次都只有 CS 未完成"：不准确。第一次还有 SDE 未完成，最后指令是 `PIPE_CONTROL`。
2. "保持模式 1（它无害）"：没有证据，改为"保持模式 1，但它对卡死率的影响未知"。
3. "`WAIT_ON_SCANLINE` 说明 context 执行了 scanline 等待，与现场一致"：不成立。见发现 1：这个标签来自 WEX 的 CSB 转换；本机发出者用 inhibit 包住等待；第二次现场也不是引擎阻塞在等待上的样子。
4. "没有新恢复、事件数继续增长，说明稳定"：不成立。CSB 事件数不是按客户端统计的 stamp 进度。
5. "没有欠载，说明扫描输出没有问题"：不成立。当前没有 MMIO power lease，观测通道本身有限。

## 唯一推荐的下一步单变量实验（只写方案，不执行）

**第 0 步：零风险解码，不算实验。** 从已有的两份 RCS 报告、CCS 报告和 WEX 历史中提取以下字段，按发现 1 的表格判定属于 M1、M1'、M2 还是 E1：

- `RING_CTRL` 的 bit11 和 bit10；
- "stuck waiting on an event" 行和 `DE_RRMR` 的值；
- 是否有 active element；
- ACTHD/BB 和 INSTDONE；
- 原始 Gen12 CSB 双字；
- `accel+0x1190` 的低 32 位。

原始值留在 `local/`，只发布分类结论。

**实验：在 TGLSyncFlip 1.0.3 里只增加一个透传观测 route。**

- **保持不变**：WEX、Metal、framebuffer、EFI 其余部分、镜像，以及模式 1。
- **改变的唯一变量**：用同样的守卫（符号、基址+偏移、字节锚点、失败时关闭）在 accelerator `567511be` 的 `IGAccelSurface::generateSync`（`0x82AE6`）上加一个透传 route。
  - 它只计数，不改参数和返回值：按 plane index 0/1/2/其他统计调用次数，记录门控是否通过；
  - 记录 inhibit 分支是否被跳过，即 `accel+0x1190 & 0x18000` 是否不为 0；
  - 发布 `accel+0x1190` 的低 32 位。
- **观察**：日常 UU 加 Windows App 的使用中，WindowServer 是否在发 scanline 等待、等的是哪个 pipe、等待是否没被 inhibit。如果计数为 0，或者 inhibit 一直生效，M1 就被证伪，主线转向 M2 或 E1。
- **停止条件**：出现任何新的 GPU restart、彩球超过 2 秒，或 route 状态不是 routed。
- **回退**：恢复 1.0.2 的 kext（SHA 为 `3d772787…`）并手动重启。
- 维护者批准后才能安装和重启。
