# 2026-11-13 独立审查材料

这是故障调查与代码审查包，**不是可安装驱动或稳定性声明**。

1. 将 [ExternalReview-20261113.md](../../docs/ExternalReview-20261113.md) 中的提示词交给审查者。
2. 阅读 [本次重启分析](../../docs/Incident-20261113.md) 和 [关键日志摘录](log-excerpts.txt)。
3. 用 [机器可读证据](incident.json) 与 [仓库 / 推送 / PR 清单](repositories.json) 核对事实。
4. 按提示词核对 `src/`、`tests/`、`driver/WhateverXe/` 与 `validation/`。
5. `stages/POSHAtomicClaim-counter-only.mm.txt` 是本机保存的较早纯计数源码原样副本，
   用于区分已跑短测的阶段与当前新增私有释放逻辑；不是默认编译目标或可部署版本。

代码基准是 `400502276ee36358bc817409b06dc3ec50ffa1bd`；本次材料提交仅增加文档、
脱敏数据和历史自有源码副本，没有更改运行组件。该基准的 GitHub CI
[43246102257](https://github.com/Minorli/WhateverXe-Next/actions/runs/43246102257) 已 success。
CI 是构建 / CPU 测试，实际机器随后发生的异常重启仍是失败，不能互相抵消。

公开包不含原始 panic / IPS / spin、客户或 GPU 内存、完整反汇编、Apple 驱动 payload、
EFI 身份、认证信息、完整 boot-args 或个人应用路径。日志摘录由白名单选择后重新排版，
保留原始语义；符号栈与数字摘要标明其来源，不能冒充完整原始报告。

原始证据另有本机私有归档，未上传。后续研究已合入 `main`，新审查从 main 开始。
本历史包、`repositories.json` 与 `files.sha256` 固定描述
`5851889cb10a6a801be6256dde9090ef287dd56d` 时的材料；需要复核原始清单时 checkout
该提交。它们不描述后来更新的 main，也不为未保存的历史可执行文件提供精确重放保证。

`source-artifacts.sha256` 记录本机保全原始报告的校验值，主机名已统一替换成 `HOST`。
它可在用户另行分享原始证据时核对文件；没有原始文件时，外部审查者只能复核公开
摘要与源码的相容性，不能独立重做全部原始 stackshot 解码。
