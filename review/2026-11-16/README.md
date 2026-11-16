# 2026-11-16 审查包：UU 闪屏与 async flip

**当前独立代码审查：**请使用可直接复制的
[Claude 卡死与负载审查提示词](claude-gpu-hang-review-prompt.txt)，结合
[模式1高负载短采摘要](../../validation/high-load-mode1-20261116.json)。
重点是首次RCS挂起、复位后CCS故障、同步调用契约与诊断热路径；本次交接不授权部署或复现卡死。

**后续审查入口：**维护者已在本机采用TGLSyncFlip 1.0.2事务同步模式1，观察到原闪屏消失，
并要求将`tglsyncmode=1`固化到活动EFI。下次启动默认值尚未验收；RCS／CCS卡死没有证明修复。
请先读[最新聚合](../../validation/syncflip-persist-mode1-20261116.json)及
[REVIEW后续结论](../../docs/REVIEW.md#本设备持久模式1交给外部审查)，再读以下历史调查输入。

这是一份**调查与候选审查包**，不是可安装驱动、稳定版或已确认修复。
目标是让外部审查者判断：现有证据是否支持“主平面 async 地址更新（PLANE_CTL
bit9）是 UU 局部闪屏的主要解释”，以及默认关闭的强制同步候选是否值得进入
整机 A/B。

**同期另有两起 RCS 先行的显示卡死（09:58、11:03），第二次随后出现 CCS 故障，
需与局部闪屏分开审查；两者是否存在共同原因尚未确定。** 证据和部署状态见
[`docs/Incident-20261116.md`](../../docs/Incident-20261116.md)。两者必须分开审查，
不能把同步模式的闪屏改善当作卡死修复，也不能先验断言同步模式对未来卡死率没有影响。

## 固定版本

| 内容 | 标识 |
| --- | --- |
| 仓库 | <https://github.com/Minorli/WhateverXe-Next> |
| 平面追踪工具 | `c2a6421`（`src/TGLPlaneTrace.hpp`、`tools/tgl-plane-trace.mm`） |
| 同步 flip 候选 | `15b3d90`，边界修正 `34dbf4d` |
| 当前 `main` | 审查时以 `origin/main` 为准；本包固定在上述提交 |

## 先读什么

1. `docs/Findings.md` 的 2026-11-16 节：UU 捕获、GRD 对照、静态结论。
2. `docs/ClaudeReview-20261113-Display.md` 第 7.5 节：寄存器偏移与候选边界。
3. `validation/plane-trace-async-20261116.json`：三次捕获的聚合数据，无原始地址。
4. `src/TGLPlaneTrace.hpp` 与 `tools/tgl-plane-trace.mm`：如何解码
   `tglProgramRecord` 环、如何判定 async / 复用深度 / 标记窗口。
5. `driver/WhateverXe/WhateverXe/kern_tgl_plane_ctl.hpp` 与
   `driver/WhateverXe/tests/tgl_plane_ctl.cpp`：候选与 CPU 契约。

## 关键证据（摘要）

| 场景 | 时长 | SURF/s | async CTL | 完整 plane setup | 人工标记 | 欠载 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 空闲基线 | 11.8 s | 41.08 | 0 / 480 | 0 | 0 | 0 |
| UU 远端 | 130.2 s | 60.39 | 4,403 / 7,859 | 829 | 23 | 0 |
| Google Remote Desktop | 58.7 s | 20.76 | 0 / 1,218 | 0 | 0 | 0 |

- UU 的 23 个标记窗口内：平面开关 0、位置/尺寸变化 0、`BUF_CFG` 变化 0、
  group 0 只有 `PIPE_MISC_A`、无 DDB/电源变化、`bit31_ack_delta=0`。
- 快速同 buffer 复用 43 次全部在完整 setup 附近，不是稳态浅复用。
- 原生 `AppleIntelPlane::configurePlane`（framebuffer `115a4616…888e`，偏移
  `0x2198`）默认取 `0x80000600`（async）；只有 flip transaction `+0x2C` bit0
  或 controller/path 特性字节同时成立时才取 `0x80000400`（sync）。
- async 地址更新可在扫描中途生效，因此同一帧上下部分可来自不同缓冲，形成
  局部、单帧、录屏难同步捕获的撕裂；这与用户描述一致，但尚未被像素级证据确认。

## 尚未证明

- async 就是闪烁原因；GRD 与 UU 同时存在应用、帧率（60.39 vs 20.76 SURF/s）
  和完整 setup 次数（829 vs 0）的差异，不是严格单变量。
- 候选 `-wex-tgl-no-async` 能消除闪烁；它未安装、未做硬件 A/B。
- 人工标记与某一次 flip 精确对应；标记窗口为 −2 s/+0.5 s。
- 候选覆盖 trigger-packet 路径或 LPVP 影子寄存器；本窗口未观察到这两类写入。

## 给审查者的任务

请直接使用 [review-prompt.md](review-prompt.md)。重点回答：

1. async 解码（bit9）与静态偏移是否正确，是否遗漏其他 async 生效路径。
2. 现有对照是否足以把 async 放在首位，哪些混杂必须先消除。
3. 强制同步候选的最小正确实现与安全边界；是否会破坏原生 flip 完成/超时语义。
4. 下一轮整机 A/B 的最小设计：同一二进制 SHA、单变量、停止阈值、证据字段。
5. 如果不支持 async 假说，给出更有区分力的替代解释与可执行验证。

## 边界

- 原始 `records.csv` / `summary.json` 含本机时间线与指针类字段，只保留在维护者
  本机 `local/`，未上传。公开包只有聚合统计。
- CPU 测试通过不等于硬件行为；本轮没有安装新驱动或重启。
- 不接受以短测通过、`bit31=0` 或“没有 panic”替代整机验收。
