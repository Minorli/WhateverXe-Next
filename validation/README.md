# Validation artifacts

These JSON files contain selected, non-identifying numeric summaries and source
fingerprints, not raw logs or driver payloads.

The later watchdog reboot is recorded separately in
[`review/2026-11-13`](../review/2026-11-13/README.md), including sanitized excerpts,
the symbolic lock dependency, repository/PR status and an independent review prompt.
It does not invalidate the historical numeric observations, but it does prevent
interpreting their immediate success as whole-system acceptance.

- `driver-build-manifest.json`: historical RELEASE build of the BCS diagnostic change;
  outputs were not installed. Paths inside the manifest are source-relative labels.
- `driver-tests.json`: complete names and exit codes for the 216-test run after
  independently verifying the official Tahoe full-file identity. All passed;
  the added BCS-wait sanitizer test passed separately, outside that full run.
- `driver-bcs-wait-build-manifest.json`: a later RELEASE diagnostic build including
  bounded semaphore wait witnesses. It was not installed or boot-tested.
- `queue-cache-tests.json`: native serial lifetime checks, forward/reverse IOSurface
  memory comparisons, presentation timing refusal, and **known failing concurrent
  construction**. Queue-cache is blocked from global deployment despite serial gains.
- `atomic-claim-tests.json`: later same-build stock/candidate native concurrency,
  actual emitted CPU instructions, concurrent render/stream work and two-order
  IOSurface memory comparisons. Process-local validation only; no global install.
- `abandonment-tests.json`: staged uncommitted-buffer accounting checks, earlier
  counter-only native/sanitizer/GPU results, and the subsequent real recovery
  loop. The final private-allocation disposal path is build-checked only; its
  native validation was blocked before installation. This is not an acceptance.
- `gpu-short-tests.json`: selected historical process-local runs, before removal of
  legacy probe entry points and stricter argument parsing. Later controlled GPU
  comparisons are recorded separately in `queue-cache-tests.json`.

Current checkout compilation, CLI rejection and CPU model checks are described in
`docs/Validation.md`. A CI CPU/mock pass does not replace the unresolved native,
boot and real workload acceptance.
