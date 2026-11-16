#!/bin/bash
# Same-boot blinded A/B for TGLSyncFlip. Run it in the maintainer's own terminal
# while UU Remote shows the remote screen; press Enter at every flicker.
#   tools/ab-run.sh OUTDIR [SECONDS_PER_BLOCK] ["MODE MODE ..."]
# The mode of each block is written to OUTDIR/schedule.txt and is not printed.
# Ctrl-C at any time switches back to mode 0.
set -euo pipefail
HERE=$(cd "$(dirname "$0")/.." && pwd)
REPO=$(cd "$HERE/../.." && pwd)
CTL="$HERE/build/tglsync-ctl"
TRACE="$REPO/build/tgl-plane-trace"
OUT=${1:?usage: ab-run.sh OUTDIR [seconds] ["0 2 0 2 1 0 1 0"]}
SECS=${2:-240}
SCHED=${3:-"0 2 0 2 1 0 1 0"}
[ -x "$CTL" ] && [ -x "$TRACE" ] || { echo "build first: make -C probe/TGLSyncFlip ctl; make build/tgl-plane-trace"; exit 64; }
[ -e "$OUT" ] && { echo "refusing to reuse $OUT"; exit 73; }
mkdir -p "$OUT"; chmod 700 "$OUT"
sudo -v
( while true; do sudo -n true; sleep 50; done ) & KEEP=$!
restore() { sudo -n "$CTL" mode 0 >"$OUT/final-status.txt" 2>&1 || true; kill $KEEP 2>/dev/null || true; }
trap restore EXIT
trap 'echo; echo "interrupted: mode 0 restored"; exit 130' INT TERM
sudo -n "$CTL" status >"$OUT/start-status.txt" 2>&1 || { echo "TGLSyncFlip not routed; see $OUT/start-status.txt"; exit 4; }
i=0
for m in $SCHED; do
  i=$((i+1)); d="$OUT/block$i"; mkdir "$d"
  sudo -n "$CTL" mode "$m" >"$d/mode-set.txt"
  echo "block $i mode=$m start_ns=$(head -1 "$d/mode-set.txt" | cut -d' ' -f1)" >>"$OUT/schedule.txt"
  echo "=== block $i/$(echo $SCHED | wc -w | tr -d ' '): ${SECS}s, press Enter at each flicker ==="
  "$TRACE" -o "$d" -d "$SECS" || echo "trace rc=$? (block $i)" | tee -a "$OUT/schedule.txt"
  sudo -n "$CTL" status >"$d/status-after.txt" 2>&1 || true
  top -l 2 -n 0 -s 1 | grep 'CPU usage' | tail -1 >"$d/cpu.txt" || true
done
echo "done; results in $OUT"
