# BCS 等待现场诊断

这是故障观察代码，不是卡死修复。代码位于
`driver/WhateverXe/WhateverXe/kern_tgl_hang_bcs_wait.hpp`，入口是已有的原生 ring dump
失败分支，仅接受 BCS 对应的寄存器、ring、context engine、owner 与 backlink。
沿已限定布局读取 ring buffer、LRC 和软件 PPGTT，要求 context ID 与 saved hardware ID 一致。

在 saved head 附近读取 16 DWORD 窗口，支持 ring wrap。最多保存 4 个精确
`0x0e009002` 候选。这是 Gen8+ PPGTT polling / unsigned-GTE 等待包；未知 compare、
GGTT 和 token 变体不按此格式解释。窗口搜索不是已执行指令边界证明，更不是 GPU 当前 PC。
位定义参考 Linux i915 的 `drivers/gpu/drm/i915/gt/intel_gpu_commands.h`；未复制其实现代码。

每个候选记录比较值、GPU 目标、软件映射前后、CPU 可读值前后，以及每次值是否满足比较。
只通过已经存在的 direct map 做 nofault 读取，要求软件 root 与上下文 LRC root 一致。
窗口、身份和映射变化均显式保留为非零状态，不把不完整数据标作有效。
CPU 值自然变化不是采样失败。即使 CPU 看到满足比较，也不能推定 GPU 已看到相同值。

不新增 MMIO、forcewake、映射、锁、正常提交 hook 或等待完成操作。
沿用全局四次失败预算，每个属性小于 4KiB；不无限累积现场。
私有采集器接收 `TGLHangBCSWait0..3`，原始数据含地址和客户内存，禁止公开上传。

CPU / sanitizer 测试覆盖普通等待、ring 尾部跨界、等待值前进、拒绝其他引擎 / 上下文 / 类 / 根、
缺失 PTE、direct-map 边界、读取失败与采样间 metadata / 指令 / PTE 变化。
这些测试不能证明硬件在等待哪一条指令，也不能替代已部署版本的 native 现场。

需要用新现场回答：生产者是否完成但 BCS 仍在等？目标是否落在有效且保持稳定的映射？
是目标值未前进、翻译 / 可见性问题，还是恢复本身使现场失去原始状态？
旧报告不足以确定其中一个。采集代码未修改这些行为，因此不得将其命名为卡死修复。
