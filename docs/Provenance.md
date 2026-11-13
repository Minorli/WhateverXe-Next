# 来源、指纹与构建

## 源码来源

| 来源 | 固定版本 / 用途 |
| --- | --- |
| [b00t0x/WhateverXe](https://github.com/b00t0x/WhateverXe) | `db438fdf4d5fe903f5dfbc65537ed3884e2ea980`；`driver/WhateverXe` 的完整归档基线 |
| [macintelk/WhateverXe](https://github.com/macintelk/WhateverXe) | 调查参考 `7c76ce232ff533b116c84085020187c9c267c464`；未声称完整集成此分支 |
| [wex-modified-edition](https://github.com/xiananliu/wex-modified-edition) | 本机现用预编译下游；0.8.1 release 内部仍为 0.8.0，无对应下游修改源码 |
| [WhateverXe-Graphics](https://github.com/b00t0x/WhateverXe-Graphics) | 对照官方 v0.4.1 pkg，SHA `7f7ce3b924ad421cff0c4f30534598858474b8c9539c4b889a461bc1d5fed02c` |
| [WhateverXe issue 47](https://github.com/b00t0x/WhateverXe/issues/47) | POSH 泄漏假说与纹理恢复回归参考 |
| [WhateverXe issue 81](https://github.com/b00t0x/WhateverXe/issues/81) | VA 兼容范围参考 |

BCS 变更相对**未修改的 git archive 基线**生成，涉及
`WhateverXe/kern_tgl_hang_pending.hpp`、`WhateverXe/kern_xe.cpp`、`tests/tgl_hang_pending.cpp`。
完整差异见 `research/0001-bcs-pending-capture.patch`。构建输出、缓存与本机依赖不在源码快照中。

## 当前实机加载与运行组件

以下表格记录调查起点的加载文件。2026-11-13 12:18 左右已将磁盘上的 Metal
executable 单独替换为官方 Graphics `v0.4.3` 的 Tahoe 版本：
`25b6c72ee23fb71f91384982fb591d6d1e514c73be3fea99831b4559d0ec4faa`。
其 bundle 其余文件与本机逐项匹配，完整签名验证通过。既有 WindowServer 仍映射表中旧版，
新启动的 UU 映射新版，已通过 inode 区分；UUID 在两版相同。
内核、framebuffer、EFI 与隔离的 VA 没有因此更换。整机效果待重启验收，结果见 `Validation.md`。

| 文件 | SHA-256 |
| --- | --- |
| 下游 WhateverXe | `df96a2cac5c68fb5a1540045da53b9871729561bba1c4714e03ae700466f4ed3` |
| AppleIntelTGLGraphics | `567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e` |
| AppleIntelTGLGraphicsMTLDriver | `1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c` |

对应 UUID：

- WhateverXe：`674F04E8-5AC6-3296-84D8-7F4FBB44E54D`
- AppleIntelTGLGraphics：`BA3AA1C0-FE6B-33B3-9D85-73F848394E3D`

实机为 i7-1165G7 / 96EU / 24GB、物理 Intel `8086:9A49 rev01`、
macOS 26.7.3（25G342）/ Darwin 25.6.0、OpenCore 1.0.8、Lilu 1.7.2。
UUID 是二进制构建标识，不是机器身份。没有发布 serial、硬件 UUID、MAC 或用户账号。

官方 v0.4.1 的 Tahoe accelerator 与表中 SHA 相同；首次 7 项 native replay 的旧指纹拒绝
不构成本机文件被意外修改的证据。核实官方 payload 并加入精确完整 SHA 后，216 项
已全部通过。新增 BCS wait 单项另行通过，未冒称 217 项整套重跑。
官方 pkg 只解包对比，没有在该轮重新安装。

## 构建步骤与复现限制

根诊断工具：macOS、Clang、Foundation / Metal / IOKit / OpenGL 系统 SDK。
`make all` 构建只读采集器，`make gpu-probes` 单独构建图形探针。
CPU 模型：`make cpu-test`。Linux CPU 模型可使用支持 C++17 与线程的编译器，
但真实 GPU 工具和 kext 必须使用对应 macOS 工具链。

内核构建需另行取得 [Lilu](https://github.com/acidanthera/Lilu) 的开发资源和
[MacKernelSDK](https://github.com/acidanthera/MacKernelSDK)，放入下列被忽略目录或显式传路径：

```sh
cd driver/WhateverXe
python3 tools/build.py --release --lilu Lilu.kext --sdk MacKernelSDK --output build/reviewed
python3 tools/test.py
```

无 accelerator 参数时是默认 CPU / mock 测试。只有持有合法、本地且精确匹配的运行组件时，
才添加 `--accelerator /path/to/AppleIntelTGLGraphics` 做可选 native replay。
不要下载不同来源的 Apple 文件以迎合测试；不要修改 SHA 让测试表面通过。

当前记录的构建清单保存源码与输出 hash；尚未实现可跨工具链逐字复现的构建。
已有构建的所有外部依赖 commit / 工具链版本未完整记录，这是复现材料的已知缺口，
不能把上述指令和已有产物 SHA 宣称为任何机器上必然相同的结果。

继承源码的 README / CHANGELOG 保留其自己的版本语义。本项目品牌不改写原作者标识、
bundle identifier、ABI floor 或来源许可证。细则见根 NOTICE。
