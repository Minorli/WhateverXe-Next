# Source and license notices

WhateverXe-Next is independently maintained and is not an official Intel,
Apple, or WhateverXe release. It does not claim authorship of inherited code.

- `driver/WhateverXe/` derives from b00t0x/WhateverXe commit
  `db438fdf4d5fe903f5dfbc65537ed3884e2ea980`, updated through upstream 0.8.2
  `4512191b9643e2391431fbd9639305b61568ee32`. Its BSD-3-Clause notice is preserved
  in `driver/WhateverXe/LICENSE.txt`; local changes to that tree follow the same license.
- `research/0001-bcs-pending-capture.patch` contains changes to that tree and
  follows its BSD-3-Clause license.
- `src/`, root `tests/`, `tools/`, the root Makefile and project documentation
  are provided under the root MIT license. `research/XePOSHFix-v0.1.0.cpp` is
  a retired local MIT experiment, with no generated Apple code page included.
- The work was informed by the public WhateverXe issue discussions, including
  issue 47 (POSH leak and texture-reset concerns) and issue 81 (VA compatibility).
- Native Apple drivers, driver code pages, full disassemblies, firmware payloads,
  private EFI identities and raw machine logs are not distributed here.
- Lilu and MacKernelSDK remain separately obtained build dependencies under
  their original licenses. A local dependency or binary is not relicensed by this project.

Repository and version references are listed in `docs/Provenance.md`.
