# WhateverXe-Next

**Intel Xe graphics compatibility and stability research for macOS.**

面向 Tiger Lake / Iris Xe 的独立维护项目，围绕命令缓冲生命周期、GPU 队列、
内存增长和呈现稳定性推进可审查的驱动改进。基于 WhateverXe，保留来源和许可，
目标是形成能够独立审查、回归并回送上游的改动。

**状态：实验开发。当前没有已验证的全局内存、闪屏或卡死修复，也没有可安装发行版。**
本仓库的构建没有部署到维护者正在使用的桌面；本地短测通过不能作为系统稳定性承诺。
原生并发计数缺陷已有进程内原子修复并通过有界对照；队列缓存的跨提交生命周期与
整机故障仍未验收，当前禁止全局部署。历史并发失败证据完整保留。
后续又复现同队列弃用后的计数缺口；新生命周期候选尚未完成验证，
计算任务之后出现的实机恢复循环已保留为失败证据，详见[弃用与验证边界](docs/Abandonment.md)。

## 当前内容

| 目录 | 内容与验证边界 |
| --- | --- |
| `driver/WhateverXe` | 固定上游源码基线、失败现场采集与边界修正；CPU 回归与整机验证分别记录 |
| `src` | 原子计数、弃用生命周期、POSH 队列缓存候选；仅在诊断进程研究 |
| `tests` | CPU 生命周期验证，以及 Metal / GL 的有界正确性与内存对照探针 |
| `tools` | 有时限、只读 IORegistry / GPU 重启报告监测器；采集数据必须留在本地 |
| `docs` | 问题证据、设计、验证记录、来源与审查入口 |
| `research` | 可回送上游的源码差异，以及明确退役的旧实验记录 |

首个研究平台是 i7-1165G7、Iris Xe 96EU、物理 `8086:9A49 rev01`、
macOS Tahoe 26.7.3 / Darwin 25.6.0。这里的硬件记录不是对全部 Xe 平台的支持承诺。
G83 的 EFI 与整机设置在[独立集成仓库](https://github.com/Minorli/g83-11gen-hackintosh-tahoe-efi)维护。

## 阅读与审查

- [2026-11-13 源码深审进展](docs/DeepReview-20261113.md)：已证实的代码缺陷、下游 HDMI／时钟差异、测试汇总修正和未完成的因果验证。
- [多引擎停止前观察](docs/HaltObservation-20261113.md)：新增有界 CPU 记录、POSH 消费者链与尚未部署的验证边界。
- [2026-11-13 独立审查包](review/2026-11-13/README.md)：最新异常重启、脱敏关键日志、推送清单与可直接交给外部审查者的详细提示词。
- [审查入口](docs/REVIEW.md)：建议阅读顺序、关键疑点及部署阻塞项，适合人工或 Claude 等代码审查。
- [问题证据](docs/Findings.md)：内存、BCS 卡死、远程呈现闪屏与 VA 故障分别记录。
- [POSH 生命周期设计](docs/POSH.md)：所有权、CPU / GPU 完成、已知风险和未覆盖路径。
- [验证记录](docs/Validation.md)：CPU / mock、真实 GPU 短测、构建及整机验收边界。
- [构建与来源](docs/Provenance.md)：固定源码提交、依赖、二进制指纹与许可。
- [部署与回退门槛](docs/Deployment.md)：何时才允许进入整机验证。

## 本地构建

在有 Xcode Command Line Tools 的 macOS 上：

```sh
make all
make cpu-test
```

`make all` 只构建只读采集工具。`make cpu-test` 运行参数边界、原子指令和监测解析测试，
不安装驱动、不重启、不访问其他进程。图形探针会向真实 GPU 提交工作，
用 `make gpu-probes` 单独构建。在出现闪屏、卡死或恢复失败的机器上应先保存现场并阅读部署门槛。
旧 deferred 释放原型及其专用模型已删除；GL/Metal 混合探针使用原生生命周期。

只读采集示例：

```sh
./build/tgl-capture local/capture 60
```

允许 1–3600 秒，目录权限 0700、快照 0600；内容可能包含内存地址及客户数据，
不要上传原始快照。没有可读报告目录或没有自定义诊断属性，不代表 GPU 一定正常。

内核组件按 [固定来源与构建说明](docs/Provenance.md)构建。
`driver/WhateverXe/README.md` 是继承源码自己的使用说明，不表示本项目已验收相同支持范围。

## 贡献与许可

请提交最小复现、精确版本、脱敏结果及明确的验收层级。
不得以抑制 panic、伪造 GPU 完成或降低驱动指纹检查来制造“通过”。
通用改动优先拆成上游可审查的独立变更；未公开的下游修改不会被假设已经包含在源码中。

自有 `src/`、根 `tests/`、`tools/` 与文档采用 [MIT](LICENSE)；
继承的 WhateverXe 源码保留 [BSD-3-Clause](driver/WhateverXe/LICENSE.txt)。
第三方代码、固件及商标属于各自权利人，详见 [NOTICE](NOTICE.md)。
