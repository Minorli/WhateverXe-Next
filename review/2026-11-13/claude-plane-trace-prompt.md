# 给 Codex（G83 上）：核实欠载排除，采集 UU 闪烁时的平面编程

背景：外部审查者（Claude）在 `docs/ClaudeReview-20261113-Display.md` 第 7 节写明了以下内容：
- `ig=0x20` 打开了完整的原生 pipe 欠载上报链；
- 结合你的 ig20 数据，pipe A 欠载**基本排除**，只剩启动时间这一个前提待核实；
- 新的方向是平面编程和缓冲区时序。本轮新增了只读采集器 `tools/tgl-plane-trace`。

继续遵守 AGENTS.md：
- 中文汇报，不 spawn；
- 不重启，不杀 WindowServer，不改镜像布局、EFI 或 boot-args；
- 本轮**不需要安装任何东西**；
- 原始记录只放 `local/`。

## 0. 先审查

1. `git pull`。阅读第 7 节、`src/TGLPlaneTrace.hpp`、`tools/tgl-plane-trace.mm`。
2. 用本机 framebuffer 独立复核 7.1 的偏移：`0x3F236`、`0x757EE..0x7582D`、`0x7349A` 中 `0x44408` bit31 → `0x40`、`0x7407C..0x74090`，以及三个 IMR 读改写函数确实不会重新屏蔽 bit31。
   - 有出入就直接改文档，并写清证据层级。
3. 采集器有问题就修，**同时补测试**（`make build/tgl-plane-trace-test && ./build/tgl-plane-trace-test`）。

## 1. 关闭"欠载"这一项（只读）

- 用 `sysctl kern.boottime` 确认当前这次启动早于 18:01:04。确认后，在 ig20 记录里把 18:01:04、18:05:13 两次闪烁标为"处于 ig=0x20 启动内"。
- 现在再读一次 `DonorUnderrunEntry`、`DonorUnderrunPipeA` 和 `TGLStreamErrorCount`。它们都是开机以来的累计值，这次读数覆盖用户此后看到的每一次闪烁。
- 两者仍为 0、6 时，把结论更新为"pipe A 欠载：不成立〔静态＋整机〕"。如果有增长，就照实写，并停止下面的平面分析，先汇报。
- 读 IGPU 的 PCI revision-id：用 `ioreg -rxn IGPU -k revision-id`，或者 `ioreg -l` 里 00:02.0 的 `revision-id`。只记录这个数字，供第 4 步判断 Wa_14010477008 的适用性。

## 2. 构建并确认记录器存在

```sh
make build/tgl-plane-trace build/tgl-plane-trace-test && ./build/tgl-plane-trace-test
mkdir -p local/plane-trace/probe && ./build/tgl-plane-trace -o local/plane-trace/probe -d 3 --no-marks; echo rc=$?
```

- `rc=3` 表示下游没有发布 `TGLProgram1Count`：可能被关掉了，或者版本不含记录器。如实汇报，并检查 boot-args 里是否有 `-wex-tgl-prog-off`，然后停止。
- 看 `summary.json` 里的 `ring1.lost`。持续大于 0 时，把 `-i` 降到 150；仍有丢失就用 100，并记录覆盖率。

## 3. 两段采集（不录屏）

1. **对照（UU 关闭，其余照常）**，60 秒：
   `./build/tgl-plane-trace -o local/plane-trace/noUU -d 60 --no-marks`
2. **UU 远程打开并显示远端画面**，10 分钟，请**维护者在自己的终端**运行，每看到一次闪就按一下回车：
   `./build/tgl-plane-trace -o local/plane-trace/UU1 -d 600`
   - 如果只能由你代按：用 FIFO 作为 stdin（`mkfifo`，再用 `sleep 100000 >fifo &` 保持写端打开）。收到"闪了"就 `echo >fifo`，并加 `--before-ms 20000`，因为消息到达有延迟。报告时写明标记精度。
3. 把两段的 `summary.json` 留在 `local/`。

## 4. 判读（逐条写"成立 / 不成立 / 证据不足"）

| 观察 | 含义与下一步 |
| --- | --- |
| 只有 UU 段出现 plane 2/3 的 `ctl_enable_writes` 或 `enable_transitions` | 有 overlay 参与。记录 `rects_at_surf`（是否位于左下或下半部）、`format`、`tiling`、`rc_decomp`、`mc_decomp`，以及 toggle 与标记的对应关系。在 framebuffer 中静态查找控制 overlay plane 数量的属性或开关，**只汇报，不改**。 |
| `mc_decomp_ctl` 或 `rc_decomp_ctl` 大于 0 | 显示端在读压缩表面。对照 i915 的 Wa_14010477008（TGL 显示 stepping A0–C0 不能用媒体解压）和第 1 步的 revision-id |
| plane 1 的 `post_arm_writes` 大于 0 | 记录 `post_arm_by_reg` 和 `post_arm_callers`（镜像偏移）。我会把它们符号化，再判断是否需要在 SURF 之后补 arm |
| `reuse_depth_hist` 基本落在 0/1 档，且 `min_reuse_ns` 接近一帧 | 缓冲轮换太浅，flip 完成可能过早。记下来，下一轮再加 vblank 时间戳 |
| UU 段 `buf_cfg_changes` 大于 0，或 group0 里有 `PWR_WELL_*` / `TRANSCONF` 变化 | 运行时发生了 DDB 或电源重分配。逐条列出时间和标记的距离 |
| `gamma10.uploads_seen_in_window` 随 UU 增长 | 说明 gamma 路径很热。下游仍在每次上传时做 LUT 回读（0.8.0 代码）。把本轮 `wrapTGLGamma10` 门控加入下游合入请求 |
| 每个标记都 `covered=true`，上面各项都没有异常 | 平面编程这一侧证据不支持。回到 GPU 完成时序（no-POSH A/B 等），不要强行下结论 |

## 5. 汇报

- 在 `validation/plane-trace-20261113.json` 写脱敏摘要：
  - 窗口长度、样本数、`ring1` 的 delta/lost、标记数及其覆盖情况；
  - 每个 plane 的 surf/s、enable/transition、格式和压缩标志、rects、post-arm 计数和寄存器名；
  - group0 的变化计数，gamma 次数。
  不写原始 CTL/SURF 数值、线程或指针。caller 偏移可以写。
- 在 REVIEW / Findings 里给每条结论标注证据层级。不要把"没看到异常"写成"显示没有问题"。
- 默认 gamma 门控只有 CPU 测试，没有实机效果，不能当作闪烁修复。
