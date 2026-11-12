# WhateverXe-Next maintenance and review contract

This is an independent, publicly shared Intel Xe / macOS research project, intended
to produce small, reviewable contributions to WhateverXe and machine-specific
integration changes for the G83 EFI project. It is not a replacement EFI release.

- Preserve evidence, attribution, exact driver fingerprints and physical-device
  guards. Distinguish CPU/mock tests, in-process real GPU tests, global installation,
  boot validation and long-running acceptance. Never conflate these levels.
- Diagnose POSH allocation ownership, BCS hang/recovery and VA teardown separately.
  Do not patch out panics, fabricate GPU completions or silently drop queued work.
- Do not install archived unconditional-free experiments on the live machine
  merely because a serialized benchmark passes. Exercise early client release,
  asynchronous completion, texture work and sharing before deployment.
- Never commit private EFI identities, serials, credentials, corporate network
  details, raw diagnostic reports, GPU memory dumps, Apple binaries, or full
  disassemblies. The ignored local/ directory is for private evidence only.
- User requested direct execution, not generated administration scripts for them
  to run. Build and diagnostic source belong here; privileged deployment remains
  explicit direct commands with native macOS password entry when needed.
- Preserve iCloud, work software, remote tools and the intentional mirrored
  displays. No automatic reboot, WindowServer termination or destructive root
  unpatch. The VA bundle is already quarantined; do not silently restore it.
- User authorized installing a tested candidate, and future upstream contribution
  is the direction. This is not permission to call an untested candidate stable
  or to submit unrelated changes to the existing enterprise-Wi-Fi PR.
- Do not spawn agents. Keep progress updates concise and in Chinese.
