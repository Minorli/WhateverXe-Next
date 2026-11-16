# 给 Codex（G83 上）：部署 TGLSyncFlip，同一次启动内做 async/sync 盲测 A/B

背景：`docs/ClaudeReview-20261113-Display.md` 第 7.6 节复审了 2026-11-16 的 async 发现。

- 结论成立：UU 在提交非 sync 事务，原生驱动照做，主平面使用 bit9 async 地址更新。
- `-wex-tgl-no-async` **不作为本轮载体**，原因有两个：
  - 它只改硬件 CTL，原生仍会在每次 flip 后用 `IODelay(50)` 忙等，完整 setup 也还在；
  - 部署它需要替换下游二进制。
- 本轮改用独立 Lilu 插件 `probe/TGLSyncFlip`。它不碰正在运行的 WEX，可以运行时切换模式：
  - 0：透传；
  - 1：事务层 sync；
  - 2：plane CTL 层 sync。

维护者已授权本轮**安装插件、修改 EFI 和 boot-args 并重启一次**。其余继续遵守 AGENTS.md：

- 中文汇报，不 spawn；
- 不杀 WindowServer，不改镜像布局，不恢复 VA；
- 原始数据只放 `local/`。
- **本轮不要升级到下游 0.8.2 或 Graphics 0.4.4。**下游 0.8.2 没有 `TGLProgram` 记录器，升级后 plane-trace 无法使用；上游 #129 报告 2560×1440 下有 WindowServer abort。

## 0. 审查与构建（不改系统）

1. `git pull`，阅读 7.6 节、`probe/TGLSyncFlip/README.md`、`TGLSyncFlip.cpp`、`TGLSyncFlipLogic.hpp`。有问题先修，同时补测试。
2. 确认 framebuffer 没有变：
   ```sh
   FB=/Library/Extensions/AppleIntelTGLGraphicsFramebuffer.kext/Contents/MacOS/AppleIntelTGLGraphicsFramebuffer
   shasum -a 256 "$FB"   # 必须是 115a4616…888e
   ```
   如果实际加载的 framebuffer 不在这个路径，用 `kmutil showloaded`、`ioreg` 找到真实位置再算。
3. 测试与构建：
   ```sh
   make cpu-test
   make -C probe/TGLSyncFlip test FB="$FB"     # 必须输出 anchors match
   make -C probe/TGLSyncFlip                   # 用本机 driver/WhateverXe 下的 MacKernelSDK 与 Lilu 1.7.2 DEBUG SDK
   make -C probe/TGLSyncFlip ctl
   make build/tgl-plane-trace
   ```
   记录 `probe/TGLSyncFlip/build/build-manifest.json` 里的 SHA。
4. 检查插件能否链接：
   ```sh
   kmutil libraries -p probe/TGLSyncFlip/build/TGLSyncFlip.kext --undef-symbols
   ```
   未解析符号只允许是 Lilu 的符号（`LiluAPI`、`KernelPatcher`、`_lilu*`）。出现其他符号就停止并汇报。
5. 确认当前 WEX 没有 route 这两个函数：WEX 只在 Display13（ADL-N DSI）路径 route `PerformFlipTransaction`。用 `ioreg -l` 检查，`WEXDisplay13*` 相关的 ready 属性都不应为真。插件自己在 route 前也会检查函数序言，别人先 route 过时状态会是 `shape-mismatch`。
6. 记录基线：
   - 当前 WEX 二进制 SHA，应为 `df96a2ca…f4ed3`；
   - Lilu 版本；
   - 完整 boot-args。

## 1. 安装（重启前必须让维护者确认）

1. 备份 EFI 的 `config.plist`，写成 `config.plist.pre-tglsync-YYYYMMDD`。确认维护者手上有能启动的备用 EFI（U 盘或第二个条目），没有就停下来问。
2. 把 `TGLSyncFlip.kext` 复制到 `EFI/OC/Kexts/`。在 `Kernel → Add` 里加一项，放在 Lilu **之后**（WEX 前后都可以）：
   - `BundlePath=TGLSyncFlip.kext`
   - `ExecutablePath=Contents/MacOS/TGLSyncFlip`
   - `PlistPath=Contents/Info.plist`
   - `Arch=x86_64`
   - `MinKernel=25.0.0`，`MaxKernel=25.99.99`
   - `Enabled=true`
3. 在 boot-args 末尾**追加** `-tglsync`，其余参数一字不改。不要加 `tglsyncmode`，启动后默认是模式 0。
4. 用 `ocvalidate` 校验，保存 diff 到 `local/`。
5. **询问维护者是否现在重启**，得到确认后再重启。

## 2. 启动后核对（只读）

```sh
probe/TGLSyncFlip/build/tglsync-ctl status    # state=routed armed=1 mode=0
log show --last boot --predicate 'eventMessage CONTAINS "tglsync"' | tail
mkdir -p local/syncflip/probe && ./build/tgl-plane-trace -o local/syncflip/probe -d 3 --no-marks; echo rc=$?   # 必须 0
```

- `state` 不是 `routed` 时，记下状态值（`shape-mismatch`、`symbol-missing`、`route-failed`、`not-seen`），停止并汇报。插件此时不起作用，系统等同于对照组。
- 确认 WEX 和 framebuffer 的 SHA 与第 0 步一致，HDMI 和背光行为正常。

冒烟测试（UU 可以不开，只观察桌面）：

```sh
sudo probe/TGLSyncFlip/build/tglsync-ctl mode 2; sleep 30
probe/TGLSyncFlip/build/tglsync-ctl status
sudo probe/TGLSyncFlip/build/tglsync-ctl mode 1; sleep 30
sudo probe/TGLSyncFlip/build/tglsync-ctl mode 0
```

每次切换后检查 `log show --last 2m` 里有没有 GPU restart 或 WindowServer watchdog，并记录每个模式的耗时直方图。出现以下任一情况就停在模式 0 并汇报：

- 模式 2 的 `max_ms` 大于 50；
- `≥16` 档超过 flips 的 1%；
- 出现卡顿或 GPU 错误。

## 3. 盲测 A/B（维护者在自己的终端运行）

1. 由你生成一个平衡的随机顺序：8 块，每块 240 秒，0、1、2 都要有，0 至少 3 块，开头和结尾都是 0。例如 `"0 2 1 0 2 1 2 0"`。**不要告诉维护者每块是什么模式。**
2. 请维护者打开 UU 远程，显示与 11-16 相同的远端画面和操作，在自己的终端运行：
   ```sh
   probe/TGLSyncFlip/tools/ab-run.sh local/syncflip/ab1 240 "<你的顺序>"
   ```
   开始时输入一次 sudo 密码。每看到一次闪烁就按一次回车。
   - 脚本每块都会切换模式、运行 `tgl-plane-trace`，记录 status 和 CPU；
   - 屏幕上只显示块编号；
   - 退出或 Ctrl-C 都会切回模式 0。
3. 出现以下任一情况，让维护者立即按 Ctrl-C（会切回模式 0），并照实记录：
   - WindowServer 卡住超过 2 秒，或出现彩球；
   - 日志里有 GPU restart；
   - UU 输入明显延迟到无法使用；
   - 闪烁明显变多。

## 4. 判读（每条写"成立 / 不成立 / 证据不足"）

先核验每块是否生效，用每块的 `summary.json` 和 `status-after.txt`：

- 模式 0 的块：`planes[0].async_ctl` 应大于 0，说明 UU 确实在发 async 事务。
- 模式 1、2 的块：`planes[0].async_ctl` 必须等于 0。
  - 模式 1 的块：`forced` 应增长。
  - 模式 2 的块：`plane_cleared` 应增长。
  - 不满足的块记为"未生效"，不参与比较。

然后按模式汇总：

- 标记次数、每分钟标记数、所有块覆盖情况；
- `full_plane_setups` 或 `ctl_changes`；
- `PerformFlipTransaction` 耗时直方图；
- CPU 的 sys%。

| 结果 | 结论 |
| --- | --- |
| 模式 1 和 2 的每分钟标记数都接近 0，模式 0 仍在每分钟数次 | async 扫描是原因（成立〔整机 A/B〕）。下一步讨论默认修复采用哪种模式 |
| 只有模式 1 接近 0，模式 2 照旧 | 原因是 sync/async 切换导致的完整 setup 或 sync 节奏，而不是硬件 async 扫描本身 |
| 模式 1、2 与模式 0 无差别 | async 假说不成立。转向合成内容或缓冲区内容（IOSurface、fence），不要继续调平面寄存器 |
| 只有某一块异常，或各模式内部差异很大 | 证据不足。给出各块的原始计数，再加一轮 |

统计要求：给出每种模式的总标记数和总分钟数，并用模式 0 的每分钟标记数算 Poisson 期望，写出模式 1、2 观测值的单侧概率。不要只写"明显减少"。

## 5. 汇报与回滚

- 写 `validation/tglsyncflip-ab-YYYYMMDD.json`，只放聚合数据：
  - 各组件 SHA、插件 SHA、顺序；
  - 每块的模式、时长、标记数、`async_ctl`、forced/plane_cleared、耗时直方图、sys%、异常。
  - 不写原始 CTL/SURF 值、指针或 records.csv。
- 在 Findings 和显示审查里补一节结论，标明证据层级。
- A/B 结束后系统保持模式 0（运行时等同于未装插件）。持久回滚的方法：从 boot-args 删掉 `-tglsync`，或在 `Kernel → Add` 里把条目设为 `Enabled=false`，下次启动生效。是否保留插件由维护者决定。
- 如果模式 1 或 2 确认有效，**不要自己把它设成默认**。先汇报，由我和维护者决定是默认启用插件，还是把同一逻辑合入下游。
