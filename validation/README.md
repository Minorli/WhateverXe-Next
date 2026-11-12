# Validation artifacts

These JSON files contain selected, non-identifying numeric summaries and source
fingerprints, not raw logs or driver payloads.

- `driver-build-manifest.json`: historical RELEASE build of the BCS diagnostic change;
  outputs were not installed. Paths inside the manifest are source-relative labels.
- `driver-tests.json`: complete names and exit codes for the historical 216-test run.
  Seven native replay entries refused an incompatible full driver fingerprint.
- `gpu-short-tests.json`: selected historical process-local runs, before removal of
  legacy probe entry points and stricter argument parsing. No new GPU tests were run
  after the machine showed instability. The POSH source fingerprint is included.

Current checkout compilation, CLI rejection and CPU model checks are described in
`docs/Validation.md`. A CI CPU/mock pass does not replace the unresolved native,
boot and real workload acceptance.
