# 源码审查入口

本文件给首次审查者和外部代码审查工具提供完整上下文。请区分确定事实、解释假说、
实验结果和未满足的部署条件，不因项目名称含 Next 而假定修复已经完成。

2026-11-13 新增了 [异常重启分析](Incident-20261113.md)、
[脱敏审查包](../review/2026-11-13/README.md) 和
[完整外部审查提示词](ExternalReview-20261113.md)。请先阅读新事故，再复核以下研究。

## 2026-11-13：上游 #109 准入核对（任务 1）

**结论：部分成立〔静态〕。**本机原始 accelerator / 旧 MTL / 隔离 VA 的新增固定锚点
均匹配；**不成立〔静态〕：可把 e0aff60 整包直接套用到当前 0.4.3 Metal。**
新版在唯一 MTL 43 字节窗口失败，原模式全文件出现 0 次。按维护者的停止条件，
本轮停在任务 1，未进行任务 2–7 的后续工作。没有改守卫、移植源码、CPU 测试、构建或安装。

参考源码是 [SaqibAlMalik/WhateverXe e0aff600](https://github.com/SaqibAlMalik/WhateverXe/commit/e0aff60038de42520c078d833e0138a50cde2495)，
经 GitHub commit/blob API 核对 16 个变更文件的完整 Git blob SHA-1。
核对器直接只读本机文件，解析 Mach-O segment 映射与符号表，不载入或执行候选。
本表覆盖该提交**新增**的全部固定字节数组、常量校验和符号偏移，以及三个 rel32 目标复核；
不代表继承的 CPS/host/CSB 路由、运行时对象、内核滑移后 hook 链均已准入。

40 个字节/常量检查中 39 个匹配，16 个固定符号全部匹配。
其中 accelerator 33 项（含 3 项派生 rel32 目标），旧 MTL 1 项，新 MTL 1 项，VA 5 项。
原始反汇编和完整 Apple 文件仅留在 ignored `local/`；这里只发布必要的锚点窗口与语义。
机器可读结果：[upstream-109-anchor-audit-20261113.json](../validation/upstream-109-anchor-audit-20261113.json)。

| 组件 | 完整 SHA-256 | 本轮状态 |
| --- | --- | --- |
| 本机 accelerator | `567511bed855a5165cbc3615f61be7b4fcf607fde4fc5c48567fb127726d496e` | 新增静态锚点匹配 |
| 旧 MTL（完整 bundle 备份） | `1e5e4f5bd76ff567a102125dfa3c74a255934d45c753a88aedcbfac7dab53c6c` | 新增静态锚点匹配 |
| 0.4.3 MTL（当前磁盘） | `25b6c72ee23fb71f91384982fb591d6d1e514c73be3fea99831b4559d0ec4faa` | 不匹配 |
| VA（隔离备份，未恢复） | `a972cdb7ddd56bcf38c3ab71e874b73efc34bc683d30e49f0a1a1919625a726d` | 新增静态锚点匹配 |

#109 没有发布 accelerator 的完整 SHA；因此可以证明本机 `567511…d496e` 匹配其
16.0.32 的全部新增固定检查，不能单凭该检查声称已比较过作者整份 accelerator 文件。
旧 MTL 和 VA 的哈希分别吻合其头文件声明的前后缀，完整本机值如上。

### 偏移 / 期望字节 / 本机字节

偏移是无 slide 的 Mach-O VM 地址；本表各窗口的 file offset 与该地址相同。
`haltHead/Body` 是可选 `gpurec-halt` 的守卫，也包含在核对中。
VA 的 V2 是完整 363 字节窗口，未截断成前缀核对。

| 组件 / 检查 | 偏移 | 期望字节 | 本机字节 | 匹配 |
| --- | --- | --- | --- | --- |
| accelerator / no-posh / allocate | `0x7bb13` | `41 f6 45 6e 10 74 2c` | `41 f6 45 6e 10 74 2c` | 是 |
| accelerator / no-posh / clone | `0x7c15e` | `41 f6 45 6e 10 0f 85 e7 00 00 00 b8 ef ff ef ff 41 23 87 0c 10 00 00 0d 00 00 10 00` | `41 f6 45 6e 10 0f 85 e7 00 00 00 b8 ef ff ef ff 41 23 87 0c 10 00 00 0d 00 00 10 00` | 是 |
| accelerator / no-posh / restore | `0x7c421` | `41 f6 44 24 6e 10 74 31` | `41 f6 44 24 6e 10 74 31` | 是 |
| accelerator / no-posh / reset | `0x7c919` | `e8 74 fa ff ff` | `e8 74 fa ff ff` | 是 |
| accelerator / no-posh / batch | `0x4301f` | `31 c9 83 e0 10 48 c1 e0 10` | `31 c9 83 e0 10 48 c1 e0 10` | 是 |
| accelerator / no-posh / params | `0xb09a4` | `00 00 1c 00 00 00 00 00 00 00 00 00` | `00 00 1c 00 00 00 00 00 00 00 00 00` | 是 |
| accelerator / no-posh / params | `0xb0bc0` | `00 00 1c 00 00 00 00 00 00 00 00 00` | `00 00 1c 00 00 00 00 00 00 00 00 00` | 是 |
| accelerator / gpurec / gdrst | `0x43769` | `c7 80 08 4a 00 00 04 00 04 00 41 8b 4c 24 70 89 88 1c 94 00 00` | `c7 80 08 4a 00 00 04 00 04 00 41 8b 4c 24 70 89 88 1c 94 00 00` | 是 |
| accelerator / gpurec / replay | `0x4384e` | `41 0f b6 74 24 40 48 8b 07 48 8b 55 d0 ff 90 90 01 00 00` | `41 0f b6 74 24 40 48 8b 07 48 8b 55 d0 ff 90 90 01 00 00` | 是 |
| accelerator / gpurec / flags | `0x43625` | `41 0f b6 44 24 40 49 8b 84 c4 b8 08 00 00 8b 88 a4 00 00 00` | `41 0f b6 44 24 40 49 8b 84 c4 b8 08 00 00 8b 88 a4 00 00 00` | 是 |
| accelerator / gpurec / s5 | `0x380dc` | `89 f0 48 8b bc c7 a0 0c 00 00` | `89 f0 48 8b bc c7 a0 0c 00 00` | 是 |
| accelerator / gpurec / rcu | `0x28e7e` | `b9 fe ff fe ff 23 88 00 48 01 00 48 8b 97 90 11 00 00 48 c1 ea 21 83 e2 01 8d 8c 11 00 00 01 00 89 88 00 48 01 00` | `b9 fe ff fe ff 23 88 00 48 01 00 48 8b 97 90 11 00 00 48 c1 ea 21 83 e2 01 8d 8c 11 00 00 01 00 89 88 00 48 01 00` | 是 |
| accelerator / gpurec / nonpriv | `0x28f27` | `c7 80 d0 a4 01 00 34 b1 00 00` | `c7 80 d0 a4 01 00 34 b1 00 00` | 是 |
| accelerator / gpurec / whitelist | `0x28eba` | `f6 87 91 11 00 00 01 74 6e c7 80 d0 24 00 00 00 e1 00 00 c7 80 d4 24 00 00 10 70 00 00 c7 80 d8 24 00 00 34 b1 00 00 c7 80 dc 24 00 00 08 70 00 00 c7 80 e0 24 00 00 80 25 00 00 c7 80 f0 24 00 00 1c 73 00 00 c7 80 f4 24 00 00 44 22 00 00 0f ba a7 e6 0f 00 00 19 73 14 c7 80 f8 24 00 00 51 23 00 10 c7 80 fc 24 00 00 41 23 00 10` | `f6 87 91 11 00 00 01 74 6e c7 80 d0 24 00 00 00 e1 00 00 c7 80 d4 24 00 00 10 70 00 00 c7 80 d8 24 00 00 34 b1 00 00 c7 80 dc 24 00 00 08 70 00 00 c7 80 e0 24 00 00 80 25 00 00 c7 80 f0 24 00 00 1c 73 00 00 c7 80 f4 24 00 00 44 22 00 00 0f ba a7 e6 0f 00 00 19 73 14 c7 80 f8 24 00 00 51 23 00 10 c7 80 fc 24 00 00 41 23 00 10` | 是 |
| accelerator / gpurec / element | `0x3a3b9` | `8b 47 50 48 c1 e0 04 48 8b 5c 07 30 48 85 db 0f 84 5d 01 00 00` | `8b 47 50 48 c1 e0 04 48 8b 5c 07 30 48 85 db 0f 84 5d 01 00 00` | 是 |
| accelerator / gpurec / lutSCI | `0x15f5d` | `48 8d 35` | `48 8d 35` | 是 |
| accelerator / gpurec / lut | `0x15f67` | `48 8d 1d` | `48 8d 1d` | 是 |
| accelerator / gpurec / scheduler | `0x15f7b` | `48 8b b8 50 12 00 00` | `48 8b b8 50 12 00 00` | 是 |
| accelerator / gpurec / haltHead | `0x56978` | `55 48 89 e5 41 57 41 56 41 54 53 41 89 f6 49 89 ff 48 8b 7f 10 be 01 00 00 00 ba 07 00 00 00` | `55 48 89 e5 41 57 41 56 41 54 53 41 89 f6 49 89 ff 48 8b 7f 10 be 01 00 00 00 ba 07 00 00 00` | 是 |
| accelerator / gpurec / haltBody | `0x5699c` | `49 8b 7f 10 44 89 f0 48 6b c0 79 48 8d 0d 82 0d 08 00 44 8b 64 08 3d 48 8b 87 40 12 00 00 42 8b 0c 20 42 c7 04 20 01 00 01 00` | `49 8b 7f 10 44 89 f0 48 6b c0 79 48 8d 0d 82 0d 08 00 44 8b 64 08 3d 48 8b 87 40 12 00 00 42 8b 0c 20 42 c7 04 20 01 00 01 00` | 是 |
| accelerator / gpurec / haltHead | `0x56a2e` | `55 48 89 e5 41 57 41 56 41 54 53 41 89 f6 49 89 ff 48 8b 7f 10 be 01 00 00 00 ba 07 00 00 00` | `55 48 89 e5 41 57 41 56 41 54 53 41 89 f6 49 89 ff 48 8b 7f 10 be 01 00 00 00 ba 07 00 00 00` | 是 |
| accelerator / gpurec / resumeBody | `0x56a4d` | `e8 d8 21 fd ff 49 8b 7f 10 44 89 f0 48 6b c0 79 48 8d 0d cc 0c 08 00 44 8b 64 08 3d 48 8b 87 40 12 00 00 42 8b 0c 20 42 c7 04 20 00 00 01 00` | `e8 d8 21 fd ff 49 8b 7f 10 44 89 f0 48 6b c0 79 48 8d 0d cc 0c 08 00 44 8b 64 08 3d 48 8b 87 40 12 00 00 42 8b 0c 20 42 c7 04 20 00 00 01 00` | 是 |
| accelerator / no-posh / LRC CTX_CTRL identity | `0xc04a8` | `44 22 00 00` | `44 22 00 00` | 是 |
| accelerator / no-posh / LRC POSH_LRCA identity | `0xc0574` | `b0 21 00 00` | `b0 21 00 00` | 是 |
| accelerator / nomc / find | `0x27499` | `21 d8 48 c1 e0 21 48 b9 ff ff ff ff fd ff ff ff` | `21 d8 48 c1 e0 21 48 b9 ff ff ff ff fd ff ff ff` | 是 |
| accelerator / nomc / find | `0x4bf0a` | `f6 80 94 11 00 00 02 74 26 41 f6 46 24 02` | `f6 80 94 11 00 00 02 74 26 41 f6 46 24 02` | 是 |
| accelerator / gpurec / disk CSB opcode + native head (live 11 requires tglCSB12Enabled) | `0x3a3a4` | `b8 07` | `b8 07` | 是 |
| accelerator / gpurec-halt / forcewake call opcode | `0x56997` | `e8` | `e8` | 是 |
| accelerator / gpurec-halt / RCS reset-control descriptor | `0xd776d` | `d0 20 00 00` | `d0 20 00 00` | 是 |
| accelerator / gpurec-halt / CCS reset-control descriptor | `0xd77e6` | `d0 a0 01 00` | `d0 a0 01 00` | 是 |
| accelerator / gpurec-derived / halt native SafeForceWake | `0x56998` | `8e 22 fd ff` | `8e 22 fd ff` | 是 |
| accelerator / gpurec-derived / eventTimeout SCI LUT | `0x15f60` | `1c a3 08 00` | `1c a3 08 00` | 是 |
| accelerator / gpurec-derived / eventTimeout normal LUT | `0x15f6a` | `22 a3 08 00` | `22 a3 08 00` | 是 |
| mtl_old / mtl-poshfix / dealloc Find[43] | `0x7ed6b` | `48 8b 1d 3e b1 41 00 49 8b 3c 1e 48 85 ff 74 1b 48 8b 05 26 b1 41 00 41 80 3c 06 00 75 0d e8 7a fa 01 00 49 c7 04 1e 00 00 00 00` | `48 8b 1d 3e b1 41 00 49 8b 3c 1e 48 85 ff 74 1b 48 8b 05 26 b1 41 00 41 80 3c 06 00 75 0d e8 7a fa 01 00 49 c7 04 1e 00 00 00 00` | 是 |
| mtl_043 / mtl-poshfix / dealloc Find[43] | `0x7ed6b` | `48 8b 1d 3e b1 41 00 49 8b 3c 1e 48 85 ff 74 1b 48 8b 05 26 b1 41 00 41 80 3c 06 00 75 0d e8 7a fa 01 00 49 c7 04 1e 00 00 00 00` | `48 8b 1d 3e b1 41 00 49 8b 3c 1e 48 85 ff 74 1b e9 80 32 f8 ff 90 90 90 90 90 90 90 90 90 e8 7a fa 01 00 49 c7 04 1e 00 00 00 00` | **否：STOP** |
| va_quarantined / va-fix / N1 | `0x1336bf` | `e8 4c 27 ff ff 89 45 e4 48 8b 4d d8 48 81 c1 b0 2c 00 00 48 8b 55 d8 48 8b ba 20 09 00 00 48 89 4f 08 48 81 c2 b0 2c 00 00 48 8b 4d d8 48 8b b9 10 09 00 00 48 89 57 08 8b 45 e4 48 83 c4 30 5d c3` | `e8 4c 27 ff ff 89 45 e4 48 8b 4d d8 48 81 c1 b0 2c 00 00 48 8b 55 d8 48 8b ba 20 09 00 00 48 89 4f 08 48 81 c2 b0 2c 00 00 48 8b 4d d8 48 8b b9 10 09 00 00 48 89 57 08 8b 45 e4 48 83 c4 30 5d c3` | 是 |
| va_quarantined / va-fix / N2 | `0x130e7b` | `e8 a0 56 ff ff 48 8b 45 a0 8b 88 bc 09 00 00 83 c1 00 89 c9 89 ca 48 8b bc d0 10 09 00 00 48 8b 55 f0 48 89 c6 e8 7b 56 ff ff 48 8b 45 a0` | `e8 a0 56 ff ff 48 8b 45 a0 8b 88 bc 09 00 00 83 c1 00 89 c9 89 ca 48 8b bc d0 10 09 00 00 48 8b 55 f0 48 89 c6 e8 7b 56 ff ff 48 8b 45 a0` | 是 |
| va_quarantined / va-fix / N3 | `0x12d301` | `e8 9a 01 00 00 48 89 45 c0 48 8b 45 d0 48 89 45 a8 48 8b 45 a8 48 8b 08 48 89 c7 ff 51 48 48 89 45 a0 48 8b 45 e8 c6 40 20 00 48 83 7d d0 00 0f 85 0c 00 00 00 c7 45 e4 05 00 00 00 e9 32 01 00 00` | `e8 9a 01 00 00 48 89 45 c0 48 8b 45 d0 48 89 45 a8 48 8b 45 a8 48 8b 08 48 89 c7 ff 51 48 48 89 45 a0 48 8b 45 e8 c6 40 20 00 48 83 7d d0 00 0f 85 0c 00 00 00 c7 45 e4 05 00 00 00 e9 32 01 00 00` | 是 |
| va_quarantined / va-fix / V1 | `0x1c16` | `e8 93 c6 23 00 55 48 89 e5 48 8d 05 1e 9a 24 00 8b 00 89 07 48 b8 00 00 00 00 40 00 00 80 48 89 47 04 5d c3` | `e8 93 c6 23 00 55 48 89 e5 48 8d 05 1e 9a 24 00 8b 00 89 07 48 b8 00 00 00 00 40 00 00 80 48 89 47 04 5d c3` | 是 |
| va_quarantined / va-fix / V2 | `0x2c8fc` | `e8 1a 53 fd ff 45 8b 86 28 01 00 00 45 8a be 24 01 00 00 4d 85 c0 0f 84 4f 01 00 00 41 8b 86 60 01 00 00 c1 e8 05 0f b7 c0 b9 e0 07 00 00 29 c1 bf ef 07 00 00 29 c7 85 c9 0f 49 f9 4d 8b 66 08 83 e7 f0 c1 e7 0c 09 c7 41 bd 00 00 ff ff 41 01 fd 49 8d be 9c 42 00 00 31 db 48 8d 05 c3 0d 2f 00 41 b9 ff 00 ff ff 49 ba 01 00 00 80 f0 00 00 00 49 bb 01 00 00 80 0f 00 00 00 45 84 ff 74 7c 41 8b 8c 9e cc 41 00 00 48 8b 10 48 8b 70 08 48 89 77 e8 8b 70 20 89 37 48 8b 70 18 48 89 77 f8 48 8b 70 10 48 89 77 f0 48 89 57 e0 8b 57 ec 44 21 ca 81 ca 00 10 00 00 89 57 ec 41 8b b4 24 68 a8 00 00 c1 e6 10 0f b7 d2 8d 94 16 00 00 ff ff 89 57 ec 44 89 6f f4 48 c7 47 f8 00 00 00 00 c7 07 00 00 00 00 83 f9 02 74 67 83 f9 01 74 68 85 c9 75 68 c7 47 f8 00 00 00 00 eb 5f 48 8b 08 48 8b 50 08 48 89 57 e8 8b 50 20 89 17 48 8b 50 18 48 89 57 f8 48 8b 50 10 48 89 57 f0 48 89 4f e0 8b 4f ec 44 21 c9 81 c9 00 10 00 00 89 4f ec 41 8b 94 24 68 a8 00 00 c1 e2 10 0f b7 c9 8d 8c 0a 00 00 ff ff 89 4f ec 44 89 6f f4 80 4f fb 80 eb 0a 4c 89 57 f8 eb 04 4c 89 5f f8 48 ff c3 48 83 c7 24 4c 39 c3 0f 82 10 ff ff ff` | `e8 1a 53 fd ff 45 8b 86 28 01 00 00 45 8a be 24 01 00 00 4d 85 c0 0f 84 4f 01 00 00 41 8b 86 60 01 00 00 c1 e8 05 0f b7 c0 b9 e0 07 00 00 29 c1 bf ef 07 00 00 29 c7 85 c9 0f 49 f9 4d 8b 66 08 83 e7 f0 c1 e7 0c 09 c7 41 bd 00 00 ff ff 41 01 fd 49 8d be 9c 42 00 00 31 db 48 8d 05 c3 0d 2f 00 41 b9 ff 00 ff ff 49 ba 01 00 00 80 f0 00 00 00 49 bb 01 00 00 80 0f 00 00 00 45 84 ff 74 7c 41 8b 8c 9e cc 41 00 00 48 8b 10 48 8b 70 08 48 89 77 e8 8b 70 20 89 37 48 8b 70 18 48 89 77 f8 48 8b 70 10 48 89 77 f0 48 89 57 e0 8b 57 ec 44 21 ca 81 ca 00 10 00 00 89 57 ec 41 8b b4 24 68 a8 00 00 c1 e6 10 0f b7 d2 8d 94 16 00 00 ff ff 89 57 ec 44 89 6f f4 48 c7 47 f8 00 00 00 00 c7 07 00 00 00 00 83 f9 02 74 67 83 f9 01 74 68 85 c9 75 68 c7 47 f8 00 00 00 00 eb 5f 48 8b 08 48 8b 50 08 48 89 57 e8 8b 50 20 89 17 48 8b 50 18 48 89 57 f8 48 8b 50 10 48 89 57 f0 48 89 4f e0 8b 4f ec 44 21 c9 81 c9 00 10 00 00 89 4f ec 41 8b 94 24 68 a8 00 00 c1 e2 10 0f b7 c9 8d 8c 0a 00 00 ff ff 89 4f ec 44 89 6f f4 80 4f fb 80 eb 0a 4c 89 57 f8 eb 04 4c 89 5f f8 48 ff c3 48 83 c7 24 4c 39 c3 0f 82 10 ff ff ff` | 是 |

磁盘 CSB head 的原生值为 7。上游也接受运行时已由它自己的 CSB12 路由改为 11，
但要求 `tglCSB12Enabled` 同时成立；本轮没有把 11 写成磁盘预期。
`0x56998` 的 rel32 解析至 `SafeForceWake=0x28C2A`；两个 LUT 分别解析至
`0xA0280` / `0xA0290`。运行时是否已被已知 bridge 改写，需要未来独立核对。

### 固定符号检查

| 符号 | 期望偏移 | 本机偏移 | 匹配 |
| --- | --- | --- | --- |
| `__ZN20IGHardwareRingBuffer19resetGraphicsEngineEP17IGHardwareContext` | `0x43576` | `0x43576` | 是 |
| `__ZN16IntelAccelerator13SafeForceWakeEbj` | `0x28c2a` | `0x28c2a` | 是 |
| `__ZN16IntelAccelerator20getDefaultResetValueEj` | `0x2e60a` | `0x2e60a` | 是 |
| `__ZN12IGScheduler515processGPUResetE10IGHwCsTypeP17IGHardwareContext` | `0x380d8` | `0x380d8` | 是 |
| `__ZN12IGScheduler517getActiveContextsE10IGHwCsTypePP17IGHardwareContextPyS3_` | `0x380ec` | `0x380ec` | 是 |
| `__ZNK11IGAccelTask9getStampsEv` | `0x8248` | `0x8248` | 是 |
| `__ZN16IntelAccelerator17signalStampUpdateERK8IGBitSetILm64EE` | `0x2ab4e` | `0x2ab4e` | 是 |
| `__ZN19IGAccelEventMachine12eventTimeoutEi` | `0x15f22` | `0x15f22` | 是 |
| `__ZN16IntelAccelerator17initModeRegistersEv` | `0x28e1a` | `0x28e1a` | 是 |
| `__ZN26IGHardwareCommandStreamer515processGPUResetEv` | `0x3a396` | `0x3a396` | 是 |
| `_rcsResetRegisters` | `0x14b2b0` | `0x14b2b0` | 是 |
| `_ccsResetRegisters` | `0x14aeb8` | `0x14aeb8` | 是 |
| `__ZN16IntelAccelerator19populateAccelConfigEP13IOAccelConfig` | `0x270e8` | `0x270e8` | 是 |
| `__ZN21IntelTGLMemoryManager31newUnifiedAddressRangeAllocatorEP11IGAccelTask` | `0x4bed8` | `0x4bed8` | 是 |
| `__ZN11IGScheduler19haltCommandStreamerE10IGHwCsType` | `0x56978` | `0x56978` | 是 |
| `__ZN11IGScheduler21resumeCommandStreamerE10IGHwCsType` | `0x56a2e` | `0x56a2e` | 是 |

`initModeRegisters` 函数入口是 `0x28E1A`；`0x28E7E` 是内部 RCU 写入锚点，二者不能混称。
上述匹配只建立参考代码位置，不替代任务 2 对寄存器表内容和完整分支的独立语义证明。

### 0.4.3 的精确释放分支

| 地址 | 独立静态核对结果 |
| --- | --- |
| `0x7ED6B–0x7ED79` | 读取 CB `_pPoshUsedAsTarget`；NULL 跳过释放 |
| `0x7ED7B` | 由旧 only-active 测试改为跳至 `0x2000`，余下填 NOP |
| `0x2000` / `0x2007` | 读取 ivar 元数据 `0x499E20`，再从 CB 取 `_pIGCommandQueue` |
| `0x200B` / `0x2012` | 读取共享字段元数据 `0x499C20`，比较队列 `m_pPoshUsedAsTarget` 与 CB 指针 |
| `0x2016` | 不相等则跳至 `0x7ED89`，调用原生 operator delete |
| `0x201C` | 相等则跳至 `0x7ED96`，不释放队列共享指针 |
| `0x3CA8D` / `0x3CB00–0x3CB07` | queue init 清零 ECX，然后将零写入共享字段 |
| `0x7EA1F–0x7EA30` | CB init 读共享字段，为零则跳至 `0x7E9CB` 私有分配 `0x12010` bytes |
| `0x3B897–0x3B8AC` | queue dealloc 读取共享字段，非空时 delete，然后置空 |

旧新版 `__TEXT` 共 43 个**发生变化的字节**（不等于 MTL Find[43] 窗口的 43 字节）：
新 cave 占 `0x2000–0x2020` 共 33 字节，其中 29 字节从零变为非零；
旧分支 `0x7ED7B–0x7ED88` 改动 14 字节。队列 init/dealloc 和 CB init 完全未改。
完整原生 text 的共享字段元数据交叉引用只有上述 init、CB init、queue dealloc 三处；
另有新 cave 一处只读比较。未发现非空赋值。

**成立〔静态〕：**在未被其他 hook 改写的 stock 队列下，共享指针为 NULL 时，
每个到达析构的非空私有 POSH 块必走 delete。这里证明的是条件与 #47 两字节方案等价，
不是证明两段代码所有情况都相同，也不是证明任一释放必然造成 GPU use-after-free。
ivar 在文件中的值可被 Objective-C runtime 按类布局调整；上述判断使用元数据符号身份，
未把文件 ivar 数字冒充进程内最终对象偏移。

**不成立〔静态 + 既有整机日志〕：**仅以所有权比较、短测通过或加载新版，认定
“POSH 仍开启 + 0.4.3”低风险。本机已启动内核明确 POSH enabled；风险决策、观察标准
和完整 bundle 回退步骤见 [Deployment](Deployment.md)。

### 后续任务的门槛与结论边界

| 项目 | 结论 / 层级 | 本轮处理 |
| --- | --- | --- |
| 任务 1 | 部分成立 / 静态 | 原始 donor 匹配；当前新版 MTL 不匹配，触发停止 |
| 任务 2：共享 GDRST、reset 列表、reason 1–6 | 证据不足 / 参考源码与已有事故 | 未独立完成；固定字节匹配不是完整机制证明 |
| 任务 3：EnableMultiContext 属性来源 | 证据不足 / 未开展 | 未认定 DeviceProperties 可用；未改 EFI |
| 任务 4：下游覆盖与请求草稿 | 证据不足 / 未开展 | 未声称上游构建可替代下游，未发送请求 |
| 任务 5：移植、CPU 测试、DEBUG/RELEASE | 不成立 / 静态准入失败 | 当前整包不能直接适用；未构建，无新产物 SHA |
| 任务 6：实机 A/B/C | 不成立 / 源码约束 | 原 A 含 stampfix，不能原样采用；其余方案暂未设计/执行 |
| 任务 7：UU / Google RD 解码及引擎提交 | 证据不足 / 未开展 | 未把 VideoToolbox 当硬解，也未把 Google RD 当 VP8/VP9 软解；无提交计数则记缺失 |

`tglGpuRecEventTimeout` 的 `stamp[0]=submitted` 后接 `signalStampUpdate`，即便限定两次
NULL timeout 且引擎 idle，仍不能证明真实完成，违反本仓库禁止伪造 GPU 完成的约束。
该分支不得移植。获准恢复旧 MTL 基线并继续之后，可先独立研究 gpurec 与 nomc；
仅 gpurec 不承诺解决 orphan stamp 的持锁清理，需要另行核对原生失败/取消路径。

参考代码还有两点需保留：RCS 白名单在 `_rcsResetRegisters` 内，但作者说它不在
`populateResetRegisterList` 的**默认值表**，不能写成所有复位列表均缺少白名单；
nomc 有第二处高地址 reservation 修改，作者没有证明它必要，不能直接等同于单个属性置零。
这两点目前为源码合同阅读结果，不冒充任务 2/3 的本机独立验证。

### 来源与许可

- [#109，SaqibAlMalik](https://github.com/b00t0x/WhateverXe/issues/109)：同机型 CPU / PCI ID 的相关事故与 opt-in 方案。
- [#75，labrouss](https://github.com/b00t0x/WhateverXe/issues/75) 和 Aqpcet 的 TGL-H35 对照：视频相关触发线索。
- [#47，Aqpcet 的回归记录](https://github.com/b00t0x/WhateverXe/issues/47#issuecomment-6717895458)：两字节释放风险。
  其后续 PoshDisable 有效说法已撤回，不作为替代方案。
- [Graphics aa278800](https://github.com/b00t0x/WhateverXe-Graphics/commit/aa27880055cec32967bba5c54ac6ff8bfcf05fb9)：0.4.3 的所有权比较来源。
- e0aff60 的头文件注明 gpurec / stampfix / nomc / no-posh 来自 NootedGreen，
  对应 [JulioNectar XPS13Plus](https://github.com/JulioNectar/Hackintosh-XPS13Plus-IrisXe-Tahoe)
  / NP550XDA 的 `kern_gen11.cpp`；MTL page-in 实现注明来自
  [vikenzo196/WhateverXe d34ded83](https://github.com/vikenzo196/WhateverXe/commit/d34ded83ad9c7bc616bd569bb989691f9a6e2ebf)、
  `c202fcb`、`7726115`。保留这些归属，不改称本项目原创。
- [e0aff60 的 LICENSE.txt](https://github.com/SaqibAlMalik/WhateverXe/blob/e0aff60038de42520c078d833e0138a50cde2495/LICENSE.txt)
  为 BSD-3-Clause，保留 vit9696 版权与免责声明。本轮仅作分析和锚点记录，没有将参考实现
  加入 driver；将来移植仍须逐项保留原文件归属并核对所引用 NootedGreen 源文件许可。

## 阅读顺序

1. `Findings.md`：三个主要问题的证据和反证。
2. `Provenance.md` 与 `Validation.md`：源版本、当前加载版本与测试层级。
3. `src/POSHAtomicClaim.mm`、`src/POSHAtomicCode.hpp` 与 `Concurrency.md`：原子计数候选和同版失败 / 通过对照；
   `src/POSHQueueCache.mm` 与 `POSH.md`：队列所有权候选。
   较早的 deferred 原型已删除；历史审查可从提交 `fd1a78a` 读取。
4. `research/0001-bcs-pending-capture.patch` 及对应三个源码文件：BCS 采集增强。
5. `Deployment.md`：不能越过的整机验证门槛。
6. `BCS.md`：等待指令现场采集的限定、拒绝条件和待回答的问题。
7. `Abandonment.md`：同队列弃用的真实失败、阶段不同的候选与晚发恢复循环；
   `validation/abandonment-tests.json` 明确哪些结果不属于最终释放逻辑。

## 希望审查者重点回答

### POSH

- 首先审查已复现的并发失败：`posh-queue-check --concurrent`。原版 active 计数丢失，
  队列实验还会让两个未提交 CB 同时借用缓存。only-active 也进入 GPU 等待 token，
  不能只修 free 分支。新原子候选已通过进程内对照，请核对领取点的寄存器存活、ZF、
  新线性化位置、移除的晚增加，以及 commit 减计数范围；CPU 测试执行实际生成指令。
- 原子领取在 POSH allocation 前发生，分配异常尚无回滚。新增可选 lifecycle 对成功初始化
  的未提交 CB 在完整 native dealloc 后归还计数；异常终止诊断，不能称已经支持回滚。
  请检查 token 保留、commit 先标记、析构重入、缓存身份和原生私有分配的延后删除。
  最后新增的未提交私有块删除路径尚无原生验收，勿把较早的纯计数结果转记给它。
- 跨 commit 的缓存重用是否早于旧 CB / descriptor 最后一次 CPU 访问，或 GPU 消费结束？
  交错提交短测通过不构成完整证明。容量探针没有观测到实际阻塞，覆盖不足需保留。

- 队列缓存是否在返回给调用方前完成发布？匹配的原生 init / dealloc、所有消费者和
  descriptor 清理是否真的支持该共享生命周期？active 数量在 commit 时下降，不能当作 GPU 在飞数量。
- 原生 render encoder 对计数表的重置能否覆盖 compute、blit、多 encoder 和跨提交路径？
  并发创建 CB、尚未完成的旧 CB 析构、错误恢复可能怎样干扰共享缓存？
- 正反两次 IOSurface 对照只验证内存斜率和正确性；呈现时间戳全零，不能计为显示时序通过。

### BCS / 恢复

- BCS 使用的 native 提交缓存结构与 owner/backlink 校验，是否需要额外的原生重放证据？
- 前后读一致只证明瞬时采样一致，不能固定内存所有权；现有 nofault 与 guard 是否始终
  在读失败时拒绝把数据解释为有效候选？
- 全局四槽预算是否足以取得第一个 BCS 失败？不能为诊断无限扩容或改变成功提交路径。
- 失败是 stamp、上下文、同步、命令生成还是恢复策略问题？现有证据不足以选择一个。
- 22:59:46 起的计算通道故障随后阻塞新 Metal 初始化，原生 commandQueueStop → finishEvent
  → eventTimeout → checkForProgress 在服务终止线程中反复等待。现有首次快照只完整采 RCS，
  该 RCS 全零且 detail ownership 拒绝，不可冒充当前 CCS / 旧 BCS 的等待目标证据。
  恢复调用返回与真正恢复进度应如何分别证明？短测后的失败不能被过程内 exit 0 覆盖。

### 呈现与整机

- UU 远端视频呈现、CVMetalTextureCache / IOSurface 与 WindowServer 合成共同出现，
  但未捕获异常帧。如何分离资源生命周期、平面 / tiling、复制与合成同步问题？
- 不能把 VideoToolbox 出现当成硬解证据，也不能把 CA::OGL 名称当成只使用 OpenGL。
- 上游新构建缺少未公开的机器特定 HDMI / 亮度修改，如何避免替换时引入回归？

## 建议审查输出

按严重性列出文件、代码位置、触发条件、为何错误、最小修改和验证方法。
单独列出待取得证据，不将推测写成已复现事实。不要建议通过关闭系统安全、隐藏 panic、
伪造完成或宽松化 SHA 检查来绕过失败。可直接审查本仓库，无需私人日志或 Apple 二进制。
