# 独立审查提示词：UU 局部闪屏与 async flip 候选

你是一名独立的 macOS / Intel Xe 显示驱动审查者。请审查下面的事实链、源码和
候选实现，判断结论是否成立、哪里证据不足、如何用最小实验证伪。不要预设现有
分析正确；不要以 CPU 测试通过、构建成功或“没有 panic”接受硬件结论。

## 1. 背景与用户症状

- 机器：Dynabook G83，i7-1165G7，Iris Xe 96EU，物理 `8086:9A49 rev01`，
  24 GB DDR4，macOS Tahoe 26.7.3 / Darwin 25.6.0。
- 内屏与外屏均为 2560×1440@60，硬件镜像，30-bit。
- 症状：打开网易 UU 远程并显示远端画面后，屏幕局部区域偶发闪 1–2 帧；
  用户描述为 Dock 和 UU 下半部闪，退出 UU 后消失；系统录屏很难同步捕获。
  Google Remote Desktop 同样场景**不闪**。
- 历史：2026-11-12 起有 BCS 卡死、恢复循环和 WindowServer watchdog；
  2026-11-13 `ig=0x20` 启动后，原生欠载上报链被解除屏蔽，连续两次用户闪烁
  期间 pipe A 欠载计数与 bit31 ACK 均未增长。欠载已降级，但未做帧同步证明。
- **另一起独立故障：**2026-11-16 09:58 和 11:03 各有一次 RCS 卡死，第二次在
  RCS reset 后转为 Windows App 的 CCS 反复挂起，用户手动重启两次。两次和当前
  启动都仍运行实际 ESP 上的旧 0.8.0 kext；仓库里的 0.8.2 未写入 ESP。
  详见 `docs/Incident-20261116.md`。本审查包只处理 async 撕裂；不要把这起
  RCS/CCS 卡死归因于 async 候选或显示审计候选。

## 2. 固定仓库与版本

- 仓库：<https://github.com/Minorli/WhateverXe-Next>，审查 `origin/main`。
- 平面追踪工具：`c2a6421`。
- 强制同步候选：`15b3d90`，边界修正 `34dbf4d`。
- 已部署组件（本机，只读记录）：
  - WhateverXe `df96a2ca…f4ed3`（下游 0.8.0/0.8.1，带 HDMI 等私有增强）
  - Metal bundle `25b6c72e…4faa`（Graphics 0.4.3）
  - framebuffer `115a4616…888e`
  - TGLDisplayProbe 1.0.1
- 仓库源码不等于本机运行的二进制；不要假设本仓库构建保留了下游 HDMI/背光增强。

## 3. 必须阅读

1. `docs/Findings.md` 2026-11-16 节。
2. `docs/ClaudeReview-20261113-Display.md` 第 7.5 节。
3. `validation/plane-trace-async-20261116.json`。
4. `src/TGLPlaneTrace.hpp`、`tools/tgl-plane-trace.mm`。
5. `driver/WhateverXe/WhateverXe/kern_tgl_plane_ctl.hpp`、
   `driver/WhateverXe/tests/tgl_plane_ctl.cpp`、
   `driver/WhateverXe/WhateverXe/kern_xe.cpp` 中 `wrapTGLRegisterWrite` 与
   `wrapTGLFastRegisterWrite` 的调用点。
6. 如条件允许，核对本机 `/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext`
   的 SHA 是否仍为 `115a4616…888e`。原始 `records.csv` 只在维护者本机，不在仓库。

## 4. 现有证据

### 4.1 采集方法

`tools/tgl-plane-trace` 每 250 ms 打开并关闭一次 framebuffer type-1 客户端，
让 WEX 发布既有的 `tglProgramRecord` 环；不写寄存器、不注入进程、不需要重启。
用户看到闪烁时按回车，标记使用与内核记录相同的 mach absolute ns 基。

### 4.2 三次聚合结果

| 场景 | 时长 | SURF/s | CTL 写 | async CTL | 完整 setup | 标记 | 欠载 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 空闲基线 | 11.8 s | 41.08 | 480 | 0 | 0 | 0 | 0 |
| UU | 130.2 s | 60.39 | 7,859 | 4,403 | 829 | 23 | 0 |
| GRD | 58.7 s | 20.76 | 1,218 | 0 | 0 | 0 | 0 |

- UU 标记窗口（−2 s/+0.5 s）：平面 enable/disable 0、POS/SIZE 变化 0、
  `BUF_CFG` 变化 0、group 0 仅 `PIPE_MISC_A`、无 DDB/电源变化、
  `bit31_ack_delta=0`。
- UU 的 43 次快速同 buffer 复用全部在完整 setup 附近；没有稳态浅复用。
- 三次采集的观测环 `lost=0`，发布失败 0。

### 4.3 静态

- framebuffer `115a4616…888e`，`AppleIntelPlane::configurePlane` 文件偏移
  `0x2198`：
  - `0x2229` 默认 `0x80000600`（bit9 async）；
  - `0x224B/0x2252` 在特定 transaction/controller/path 条件下改用
    `0x80000400`（sync）；
  - `0x2257` OR 进 CTL，`0x239E` 存回 plane `+0x100`。
- `syncPlaneUpdates`（`0x33AE`）只切换 CTL bit3，不决定 async。（2026-11-16 复审更正：bit3 是与
  DOUBLE_BUFFER_CTL `0x44500` 配套的 "allow double buffer update disable" 位，见显示审查 7.6。）
- i915 定义 `PLANE_CTL_ASYNC_FLIP = REG_BIT(9)`，并说明除 bit9 外该寄存器是
  双缓冲的；async 地址更新允许在扫描中途生效。
- 实际观测值 `0x82000600/0x82000400` 及带 bit3 的 `0x82000608/0x82000408` 只差 bit9。
  （复审更正：bit10 是 X tiling 字段，不是 sync 位；bit8 不参与。）

### 4.4 候选

`-wex-tgl-no-async`（默认关闭）在 WEX 的普通/快速寄存器写包装中，对
`0x70180/0x71180/0x72180`（pipe A/B/C 主平面 CTL）清除 bit9，保留 bit3 和其余
所有位；计数发布为 `TGLNoAsyncFlip = [enabled, suppressed]`。
CPU 测试覆盖主平面识别、sprite/cursor/LPVP 影子/pipe D 排除、bit3 保留。

## 5. 需要你独立判断的问题

### A. async 解码与静态链

1. bit9 是否确实为 async address update？bit3 的含义是什么，是否会与 async 的
   latch 语义冲突？
2. `configurePlane` 的 async 选择是否只由上述条件决定？是否存在其他写
   PLANE_CTL 的路径（`enablePlane`、`updatePlane`、`syncPlaneUpdates` 的
   trigger packet、LPVP `+0x500` 影子）绕过或重新引入 async？
3. 我们的静态偏移是否可能对应另一版 framebuffer？请给出可复核的字节/符号锚点。

### B. 证据强度与混杂

1. UU 与 GRD 同时改变了应用、帧率（60.39 vs 20.76 SURF/s）和完整 setup 次数
   （829 vs 0）。现有数据能把 async 放在首位到什么程度？
2. UU 的 829 次完整 setup 全部是相同值重写，标记窗口内没有值变化。这是否足以
   排除“重复 setup 本身造成闪烁”？还需要什么字段？
3. 人工标记窗口 −2 s/+0.5 s 对 60 Hz 稀疏事件有多大约束力？有没有不需要
   帧同步录像、只用现有记录就能提高归因强度的方法？
4. “录屏抓不到”能排除合成内容错误吗？如果不能，如何设计区分 async 撕裂与
   合成/GPU 内容错误的实验？

### C. 候选正确性与安全

1. 只在 WEX 包装层清除 bit9、不改 plane 缓存 `+0x100`，是否会让原生软件状态
   与硬件状态不一致？flip 完成、超时、重提交、错误恢复是否依赖该位？
2. trigger-packet 路径和 LPVP 影子未覆盖；在本机构型下是否可能实际被使用？
   如何在不安装候选的情况下判定？
3. 强制 sync 是否可能造成 flip 超时、丢帧、vblank 等待或恢复路径问题？
   需要哪些停止阈值和回退条件？
4. 候选是否应限制为仅在 async 位原本置位时改动？当前实现是否已有足够证据
   保证 bit9 只表示 async 而不参与其他语义？

### D. 下一轮最小 A/B

请给出可直接执行的方案：同一候选二进制 SHA、保留 HDMI/CDCLK/DCOff 等下游增强、
单变量开关、同一 UU 远端/分辨率/帧率/镜像布局、记录字段、停止阈值、回退方式。
要求先确认实际加载的二进制与 SHA，不能把磁盘写入当成生效。

### E. 替代解释

如果不支持 async 假说，请给出更有区分力的替代路径，例如：合成内容错误、
IOSurface/缓冲区 fence 与复用、WindowServer 呈现时序、eDP/HDMI link 或 DMC/
watermark 的非 bit31 故障。每项都要有可执行的最小验证，不接受“继续观察”。

## 6. 输出要求

请按以下结构输出：

1. **结论分级**：成立 / 部分成立 / 证据不足 / 不成立，并标明层级
   〔静态〕〔CPU〕〔整机只读〕〔构建〕。
2. **必须修正的代码或文档问题**，附文件与行号。
3. **候选的最小安全实现**（如有必要，给出差异建议）。
4. **A/B 设计与停止阈值**。
5. **仍然未知的事实**，不要用推测补齐。

## 7. 边界与禁止事项

- 不安装新驱动、不重启、不杀 WindowServer、不改 Apple bundle、不恢复 VA。
- CPU 测试和构建成功不等于硬件行为；没有整机 A/B 不得声称修复。
- 不上传原始 `records.csv`、内核地址、客户数据或本机身份信息。
- 不要为让测试通过而修改期望值，或把缺失数据当成 0。
