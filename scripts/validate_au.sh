#!/usr/bin/env bash
# Runs Apple's AU validation against every installed Dine component (or the codes given as arguments).
set -euo pipefail
killall -9 AudioComponentRegistrar 2>/dev/null || true
CODES=("$@")
[ ${#CODES[@]} -eq 0 ] && CODES=(Lmdr Lmvo Lmky Lmma Lmgt Lmba Lmfx)
status=0
for code in "${CODES[@]}"; do
    echo "=== auval -v aufx $code Lvmx"
    auval -v aufx "$code" Lvmx || status=1
done
exit $status
