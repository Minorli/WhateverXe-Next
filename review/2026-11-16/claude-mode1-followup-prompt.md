# 给 Codex（G83 上）：模式 1 开机核验、反向验证、RCS/CCS 卡死第一阶段（只读）

背景：Claude 复审了 a5062dd。

- 1.0.2 的代码通过复审：
  - 模式 1 的逻辑和 1.0.0 一致；
  - 模式 2 的作用域已缩到 `syncPlaneUpdates(false)` 期间，并对照 0x351D/0x356E 的按值取 CTL/SURF 核对过；
  - ModeGate 和部分 route 透传都成立。
- Claude 本地用 ASan/UBSan 跑了 8 个用例和 21 个锚点，全部通过。
- 证据层级仍是"本机用户观察"：没有盲测计数。
- RCS/CCS 卡死与模式 1 无关，仍未解决。

本轮分三部分：

- A：开机核验，只读；
- B：一次反向验证（约 20 分钟，需要维护者参与）；
- C：RCS/CCS 卡死第一阶段，只做静态分析和证据准备，**不复现**。

规则照旧：

- 中文汇报，不 spawn；
- 不杀 WindowServer，不改镜像，不恢复 VA，不升级 WEX、Graphics 或 Metal；
- 原始寄存器、地址和报告原文只放 `local/`；
- 不自动重启。

## A. 开机核验（维护者手动重启后执行）

```sh
probe/TGLSyncFlip/build/tglsync-ctl status        # 必须 state=routed armed=1 mode=1
log show --last boot --predicate 'eventMessage CONTAINS "tglsync"' | tail
```

1. 核对加载的插件、WEX、framebuffer、accelerator、Metal 的 SHA 和 UUID 是否与 `syncflip-persist-mode1-20261116.json` 一致。boot-args 应只比之前多一个 `tglsyncmode=1`。
2. 维护者开着 UU 时跑一次 60 秒 `tgl-plane-trace`。主平面 SURF 前的 `surf_async_ctl` 应为 0，只允许开机准备阶段残留的 async 值；`forced` 计数应增长。
3. 状态不是 `routed/mode=1` 时，记录状态值后立即汇报。不要自己重启或改配置去"修"。这种情况下闪烁会回来，维护者需要知道。
4. 写 `validation/syncflip-mode1-boot-YYYYMMDD.json`，只放聚合数据。

## B. 反向验证：模式 0 下闪烁是否回来

目的：区分"模式 1 消除了闪烁"和"这段时间恰好没闪"。

### B1. 先修 `ab-run.sh`

现在它在退出或 Ctrl-C 时恢复的是**模式 0**。开机默认已经是 1，这会把系统留在会闪的状态。

- 改成恢复**启动时读到的模式**：解析 `start-status.txt` 里的 `mode=`，解析失败就拒绝开始。
- 新增可选的第 4 个参数，用来显式指定恢复模式。
- 用一个假的 `tglsync-ctl` 桩脚本写测试，覆盖三种情况：
  - 正常结束；
  - Ctrl-C；
  - 中途某块设置模式失败。
- 三种情况都必须恢复到原模式，并跑 `bash -n`。
- README 同步更新。

### B2. 盲测

1. 由你随机生成顺序：4 块，每块 300 秒，两块模式 0、两块模式 1，**不要告诉维护者**。可以是 `1 0 0 1`、`0 1 1 0`、`0 1 0 1` 或 `1 0 1 0`。
2. 请维护者打开 UU，使用会闪的那种远端画面和操作，运行：
   ```sh
   probe/TGLSyncFlip/tools/ab-run.sh local/syncflip/rev1 300 "<顺序>" 1
   ```
   每看到一次闪烁就按一次回车。只按真的看到的闪烁，不确定的不按。
3. 停止条件沿用上一份提示词：彩球超过 2 秒、GPU restart、UU 不可用，任何一项出现都立即 Ctrl-C。结束后系统必须回到模式 1，用 `status` 确认。

### B3. 预先约定的判读

- 只有标记次数可以用来算，主观印象不算。
- 用模式 0 的总标记数和 10 分钟，算出每分钟速率 λ。
- 模式 1 的观测值记为 k，按 Poisson 单侧概率 P(X ≤ k | λ·10) 计算。

| 结果 | 结论 |
| --- | --- |
| 模式 0 ≥ 4 次，模式 1 = 0 次 | 成立〔整机盲测〕：模式 1 消除了本机 UU 闪烁。写出 p 值 |
| 模式 0 ≥ 4 次，模式 1 有明显减少但不为 0 | 部分成立，写出 p 值和两种模式的速率 |
| 模式 0 ≤ 1 次 | 本轮未复现。不能归功给模式 1，也不能否定它。如实记录，不追加轮次，交给维护者决定 |
| 两种模式差不多 | 不成立。可以保持模式 1，但它对卡死率的影响未知；明确写"闪烁未被消除" |

每块都要核对是否真的生效：

- 模式 0 块的 `forced` 不增长；
- 模式 1 块的 `forced` 增长，`plane-trace` 的 `surf_async_ctl` 为 0。

未生效的块不参与计算。结果写进 `validation/` 和 Findings，**不要把已撤回的旧计数混进来**。

## C. RCS/CCS 卡死第一阶段（只读，不复现）

已知事实见 `docs/Incident-20261116.md`：

- 两次卡死都是 WindowServer context 6 的 RCS 先出问题；
- 最后一次 context switch 的原因是 `ELEMENT_SWITCH | WAIT_ON_SCANLINE`。第一次 CS 和 SDE 未完成、最后指令为 PIPE_CONTROL；第二次只有 CS 未完成、NO ACTIVE ELEMENTS；
- 第二次在 RCS reset 之后，Windows App 的 CCS 反复挂起；
- 两次发生时 UU 和 Windows App 都开着。

### C1. 主线索：WindowServer 的 RCS 是否在等一个永远不来的显示事件

**已更正（见 `docs/ClaudeReview-20261116-Mode1-Hang.md` 发现 1）：**`WAIT_ON_SCANLINE` 来自 WEX 的 Gen12→旧格式 CSB 转换，不能证明当前正在等扫描线；本机 accelerator 的 scanline 等待由 inhibit 包住，第二次现场也不是引擎阻塞在等待上。下面 C1 的静态问题仍有用，但请先按审查文档第 0 步解码已有报告。

**Claude 的一处静态发现**：在 framebuffer `115a4616…888e` 里，`DE_RRMR`/`DERRMR`（`0x44050`，Display Engine Render Response Mask）只被写过一次，位置是 `CamelliaTcon2::DisablePsrDC6` 的 0x80422，写入值是 `0xFFFFFFFF`，即全部屏蔽；`0x44054` 也被写成全 1。除此之外，framebuffer 本身不解除任何屏蔽。Camellia 是 Apple 自家面板的 TCON，G83 很可能根本不走这条路径，请核实。

需要做的静态分析：

1. 在**本机实际加载的** accelerator（`567511…`）、Metal 0.4.3 bundle，以及 WEX 0.8.0 上游源码（`driver/WhateverXe`）里搜索：
   - `MI_WAIT_FOR_EVENT`（DW0 = `0x01800000 | 事件位`）；
   - `MI_LOAD_SCAN_LINES_INCL` / `MI_LOAD_SCAN_LINES_EXCL`（`0x09000000` / `0x09800000` 开头）；
   - 对 `0x44050` 的 `MI_LOAD_REGISTER_IMM`；
   - 名字里含 Scanline、WaitEvent、VBlank 的符号，例如 accelerator 里的 `barrierForWaitEvents`。
2. 回答以下问题：
   - 谁在什么条件下发出 scanline 等待？
   - 等的是哪个 pipe，scanline 范围怎么算？
   - 解除 `DERRMR` 屏蔽是谁负责的？
   - 镜像时 WindowServer 选的 pipe 和内屏实际的 pipe 是否可能不一致？
   - PSR、DC5/DC6 或 pipe 关闭期间，scanline 事件是否会停止？
   - 有没有超时？
3. 对照 Linux `i915` 中 `DERRMR` 和 `MI_WAIT_FOR_EVENT` 的用法，作为语义参考。注意 Gen12 和 Gen7 之间的差异，有差异时写明。

### C2. 从两份本机报告里提取能支持或否定 C1 的字段

原文留在 `local/`，只发布分类结论：

- 最后几条指令里有没有 `MI_WAIT_FOR_EVENT` 或 `LOAD_SCAN_LINES`？等的是哪个 pipe？
- RCS 的 INSTDONE 类字段停在哪个单元？
- 两次事件前后 5 分钟，统一日志里有没有以下活动：显示重配置、PSR/DC 进出、热插拔、睡眠/唤醒、镜像变化、UU 或 Windows App 启动/全屏切换。
- CCS 卡死是 RCS reset 之后才出现的（共享 GDRST 方向），还是独立发生的？

### C3. 让下一次卡死留下证据（需要维护者先同意）

先问维护者两件事：

- 能否开启"远程登录"，从另一台机器 SSH 进来？
- 卡死时 SSH 是否还能用？

然后写只读脚本 `tools/hang-capture.sh`，可以在 SSH 或仍能响应的终端里一条命令运行，输出到 `local/hang-YYYYMMDD-HHMMSS/`。它收集：

- 最近 15 分钟的统一日志，用 GPU、IOAccel、WindowServer、WEX、tglsync 谓词过滤；
- 新的 `.gpuRestart` 和 DiagnosticReports 文件；
- `ioreg` 中 WEX、TGLSyncFlip 的属性；
- `tglsync-ctl status`；
- 进程列表，以及 WindowServer、UU、Windows App 的 CPU 和 footprint；
- `pmset -g log` 尾部和显示器列表。

脚本要求：

- 不写寄存器，不 kill 进程，不重启；
- 先用 shellcheck 和 `bash -n` 检查；
- 能 dry-run 输出将要收集的项目。

在 Deployment 里写一张"卡死时怎么做"的清单：

1. 能 SSH 就先跑这个脚本；
2. 记下发生时间和当时开着的应用；
3. 然后再强制重启。

### C4. 现在不做的事

- 不复现卡死，不跑压力负载，不做 Metal 回退 A/B。当前每天发生 0 到 2 次，没有可控的复现率，A/B 做不出结论。
- 不 patch 掉任何 wait，不伪造完成，不放宽超时，不提前部署 gpurec 或 no-POSH。
- 维持当前配置（模式 1）被动观察。模式 1 本身改变了显示路径，有可能影响卡死率，所以观察期内不要再改任何别的变量，好在对照期间是单变量。

## 汇报

逐条给出 A、B、C1、C2 的结论，每条标成立、不成立或证据不足，并注明证据层级。给出：

- C1 的调用链和条件，写到函数和偏移；
- 认为最可能的卡死机制，以及下一步建议做的**一个**单变量实验，只写方案，不执行。

推送前跑 `make cpu-test`。
