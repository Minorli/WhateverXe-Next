# 给 Codex（G83 上）：部署 TGLDisplayProbe 并采集 UU 闪烁证据

背景：外部审查者（Claude）已提交 `probe/TGLDisplayProbe/`，回应 Deployment.md 中 "S1 尚未构建" 列出的边界要求。
继续遵守 AGENTS.md：中文汇报、不 spawn、不自动重启、不杀 WindowServer、不改镜像布局。安装和重启都要先得到维护者明确同意。
原始寄存器值、内核地址、反汇编只放 `local/`；仓库里只写摘要。

## 0. 先审查，再动手

1. 执行 `git pull`，阅读 `probe/TGLDisplayProbe/README.md`、`TGLDisplayProbeLogic.hpp` 开头的安全约束，以及 `docs/ClaudeReview-20261113-Display.md`。
2. 对照你在 Deployment.md 写的 S1 门槛，逐条核对探针是否满足：
   - 只读计数器
   - 不能把文件偏移当作运行时地址
   - power domain 访问资格
   - 睡眠 / 唤醒
   - 读不到时报 unavailable，不能填 0
   - 第一版不写 W1C（默认不写；`-tgldp-count` 需要单独 opt-in）
   - sticky 位开机就已置位的处理（`StickyAtFirstSample`）
3. 发现问题就直接修，但**必须同时补测试**，并在提交说明里写清楚改变了什么语义。不要只写文档。

## 1. 在 G83 上构建并静态核对（不安装）

1. `cd probe/TGLDisplayProbe && make test && make`，记录 kext SHA。审查者用 macOS 27.2 SDK 交叉编译的产物不要用。
2. 运行 `kmutil libraries -p build/TGLDisplayProbe.kext`（或同类诊断），确认 OSBundleLibraries 在 26.7.3 上能解析。
   - 尤其要确认 `registerPrioritySleepWakeInterest`、`IOService::copyMatchingService`、`IOPCIDevice::extendedConfigRead16` 这几个符号能找到；
   - 有问题就修 Info.plist 或源码。
3. 核对实机 `AppleIntelTGLGraphicsFramebuffer` 的完整 SHA 是否等于 `115a4616e862…888e`。
   - 用本机 `nm` / `otool` 复核 `TGLDP::Donor` 里的常量：vptr `0xCBA80`、`gMetaClass 0x138B80`、5 个 vtable 槽（0 / 7 / 183 / 184 / 185），以及计数器偏移 `0xE6CD8 / E0 / E8 / F0 / 0xE6BC8`。
   - 再确认它们都落在同一个 `__DATA` 段内。

## 2. 部署方案（交给维护者批准，由维护者选择手动重启的时机）

- 在 OpenCore `Kernel → Add` 注入。boot-args 第一轮**不加任何 tgldp 参数**，也就是默认只读、不写寄存器。
- 回退：
  - 先确认一个**真实可用的回退入口**：可以启动的备份 EFI（例如 U 盘），或者能在 OpenCore 菜单里改 boot-args（`-tgldp-off`）。仅有文档里的备份不算回退入口。
  - 也可以把这一项设为 `Enabled=false`。
- 不改 WEX、Graphics bundle、VA 隔离和镜像布局。

## 3. 重启后的检查（只读）

```sh
kmutil showloaded | grep TGLDisplayProbe
ioreg -rw0 -c TGLDisplayProbe -k TGLDisplayProbe
```

确认以下几项：
- `DonorCounterState == 1`（原生计数器已通过资格校验）；
- `PipeAFirstSampled == 1`；
- 记录 `PipeAStickyAtFirstSample`；
- `SkippedDC`、`SkippedPW1Off`、`UnavailableAllOnes`、`UnavailablePCI` 的增长速度。`SkippedDC` 一直增长说明 DC 状态是开着的，探针在这种情况下看不到 pipe 寄存器，必须报告这一点；
- `Writes == 0`；
- `First*` 寄存器快照：BW_BUDDY、DCPR2、DBuf、PSR、IMR。和 i915 的期望值对照：TLB timer 字段为 8，DCPR2 的 bit27:24 全部置位，DBuf tracker 为 8，PSR 的 bit31 为 0。

## 4. UU 采集（不需要录屏）

- 在本机写一个每秒运行一次的循环：读取探针字典里的 `NowNs`、`PipeAUnderruns`、`PipeALastNs`、`DonorUnderrunPipeA`、`DonorUnderrunEntry`、`Paused` 和各个 `Skipped*`，加上墙钟时间，追加写入 `local/`。现在读 ioreg 只会触发探针自己的 serializer，开销很小。
- 维护者打开 UU 观看 10–15 分钟。每看到一次闪烁，只需告诉你大概时间，或者在终端里敲回车打个时间戳。
- 同时继续使用你已有的 tgl-capture ACK 采集，做交叉对照。
- 再测一次睡眠 / 唤醒：确认 `Paused` 恢复为 0、恢复后没有异常，并记录唤醒后的寄存器快照有没有变化（`WASnapshotChanges`）。

## 5. 按结果决策（每一轮只改一个 boot-arg，探针 kext 的 SHA 保持不变）

| 结果 | 下一步 |
| --- | --- |
| 闪烁时刻 `PipeAUnderruns` 或 `DonorUnderrunPipeA` 同步增长 | **欠载确认。** 对照第 3 步的快照：哪一项和 i915 不符，就提出 `tgldp-wa` 单 bit 轮次，顺序为 1 → 4 → 8。每轮 15 分钟，看闪烁次数和欠载计数是否下降。bit 2（page mask）必须先通过 SPD 或内存控制器寄存器核实通道配置，不能凭 16+8GB 猜测 |
| `StickyAtFirstSample = 1`，只读模式下没有任何新事件 | 请维护者批准下一次启动加 `-tgldp-count`，用来统计每一次新欠载 |
| 原生计数增长，但 PIPESTATUS 没有增长（或者反过来） | 如实记录。两者覆盖范围不同：原生计数受 IMR 影响，PIPESTATUS 受采样窗口和 DC 状态影响 |
| 闪烁已确认，但两者都没有增长，且 Skipped / Unavailable 都很低 | 欠载基本排除。转向 flip 时序：下游二进制仍然带着每次 flip 写 2 KiB registry 的代码（实测平均 190 次/秒），请把 `a9aaf05` 的等价修改加入给 xiananliu 的合入请求，并评估 `-wex-tgl-prog-off` 在下游是否仍然生效，可以作为零代码对照。然后再进行 no-POSH A/B |

## 6. 汇报

- 在 REVIEW / Findings 里给每条结论标注"成立 / 不成立 / 证据不足"和证据层级。整机数据要写清采样窗口、闪烁次数、计数增量和 Skipped 比例。
- 不要把"探针装上没出问题"当作闪烁修复，也不要把"没有欠载"写成"扫描输出没有问题"。
