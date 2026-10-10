#!/usr/bin/env bash
# tools/test_run.sh NAME [ENV=VALUE...]: a test run of the game that never touches the player's own
# save or settings: the save is copied into out/test-user/ first, the game uses that copy and
# out/test-user/bbport.ini, and tools/mac_bench.sh runs it in the background with short timers
# (BOOT_WINDOWS=1 TITLE_WAIT=3 PRESS_GAP=4; HOLD seconds in the level, default 45). Prints the
# frame summary and any STOP or fault line. Refuses to start while another game is running.
#   tools/test_run.sh base
#   tools/test_run.sh rec BB_RECOMP_LIB=$PWD/recomp/out/recomp/librecomp.dylib
set -euo pipefail
cd -- "$(dirname -- "$0")/.."
name=${1:?usage: tools/test_run.sh NAME [ENV=VALUE...]}
shift
pgrep -x bb-probe > /dev/null && { echo "A game is already running: not starting another." >&2; exit 1; }
data="$HOME/Library/Application Support/bloodborne_mac/user"
test_user=$PWD/out/test-user
mkdir -p "$test_user"
rm -rf -- out/test-user/savedata
[[ -d $data/savedata ]] && cp -R "$data/savedata" "$test_user/"
[[ -f $test_user/bbport.ini || ! -f $data/../bbport.ini ]] || cp "$data/../bbport.ini" "$test_user/bbport.ini"
BOOT_WINDOWS=1 TITLE_WAIT=3 PRESS_GAP=4 LEVEL_WAIT=${LEVEL_WAIT:-200} HOLD=${HOLD:-45} \
    tools/mac_bench.sh "$name" BB_USER_DIR="$test_user" BB_CONFIG="$test_user/bbport.ini" \
    "BB_GPU_USER_DIR=$data" "$@" > "out/test_$name.txt" 2>&1 || true
echo "$name: $(tail -2 "out/test_$name.txt" | head -1)"
log=$(ls -t out/bench_"$name"_*.log 2>/dev/null | head -1)
[[ -n $log ]] && grep -m3 "^Recompiled: [0-9]* of\|STOP\|Host fault\|Guest fault" "$log" || true
