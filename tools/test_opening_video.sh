#!/usr/bin/env bash
set -euo pipefail
# Opens a game window. Uses test_run.sh with a fresh user directory, never the player's save.
cd -- "$(dirname -- "$0")/.."
name=${1:?usage: tools/test_opening_video.sh NAME [VAR=VALUE...]}
shift
[[ $name =~ ^[A-Za-z0-9_-]+$ ]] || { echo 'Use letters, digits, underscores or hyphens in NAME.' >&2; exit 2; }
if pgrep -x bb-probe >/dev/null; then
    echo 'A game is already running.' >&2
    exit 2
fi
isolated="$PWD/out/stability-users/$name"
if [[ -e $isolated ]]; then
    echo "Refusing to reuse a new-save directory: $isolated" >&2
    exit 2
fi
mkdir -p "$isolated"
python3 - "$name" "$@" <<'STAMP'
import sys, json, subprocess, hashlib
from pathlib import Path
root=Path.cwd()
def source(path):
    def git(*args): return subprocess.check_output(['git','-C',str(path),*args])
    diff=git('diff','HEAD','--binary')
    return {'commit':git('rev-parse','HEAD').decode().strip(), 'status':git('status','--porcelain').decode(), 'diff_sha256':hashlib.sha256(diff).hexdigest()}
Path('out', 'stability_'+sys.argv[1]+'.json').write_text(json.dumps({'runtime':source(root),'recompiler':source(root/'recomp'),'switches':sys.argv[2:]},indent=2)+'\n')
STAMP
LEVEL_WAIT=${LEVEL_WAIT:-140} HOLD=${HOLD:-5} tools/test_run.sh "$name" "$@" "BB_USER_DIR=$isolated" > "out/video_$name.txt" 2>&1 &
run_pid=$!
log=''
for ((i=0;i<120;i++)); do
    log=$(ls -t out/bench_"$name"_*.log 2>/dev/null | head -1 || true)
    if [[ -n $log ]] && rg -q "save data 'SPRJ0005' mounted" "$log"; then break; fi
    if ! kill -0 "$run_pid" 2>/dev/null; then break; fi
    sleep 1
done
if [[ -z $log ]] || ! rg -q "save data 'SPRJ0005' mounted" "$log"; then
    wait "$run_pid" || true
    printf '%s invalid: no new-game mount; log=%s
' "$name" "$log"
    exit 3
fi
python3 - "$name" <<'BINARIES'
from pathlib import Path
import hashlib, json, sys
p=Path('out', 'stability_'+sys.argv[1]+'.json')
m=json.loads(p.read_text())
m['host_binaries']={str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in
                   (Path('out/bb-probe'),Path('recomp/out/libbbcpu.a')) if f.exists()}
p.write_text(json.dumps(m,indent=2)+'\n')
BINARIES
sleep 15
tools/press.sh down 0.1
sleep 0.6
tools/press.sh cross 0.1
sleep 20
tools/press.sh up 0.1
sleep 0.8
tools/press.sh cross 0.1
sleep 10
if pgrep -x bb-probe >/dev/null && ! rg -q "Mv_AudioPlayThread.*created" "$log"; then
    tools/grab.sh "${name}_navigation" || true
fi
if [[ ${VIDEO_GRAB:-0} == 1 ]]; then
    while kill -0 "$run_pid" 2>/dev/null; do
        if rg -q 'Stop: Could not stop playback: already stopped' "$log" && pgrep -x bb-probe >/dev/null; then
            sleep 3
            tools/grab.sh "${name}_creation" || true
            break
        fi
        sleep 1
    done
fi
wait "$run_pid" || true
python3 - "$name" "$log" <<'REPORT'
import sys,re,json
from pathlib import Path
name,path=sys.argv[1:]
s=Path(path).read_text(errors='replace')
result={'run':name,'log':str(Path(path).resolve()),'movie_entered':bool(re.search(r"Mv_AudioPlayThread.*created",s)), 'playback_stop':bool(re.search(r"Stop: Could not stop playback: already stopped",s)),'faults':[x for x in s.splitlines() if re.search(r"^(Fault|Guest fault|Host fault) \(signal|^  thread |ExecuteCopyShaderHLE.*Assertion Failed|Unknown data_format=|^STOP:",x)]}
print(json.dumps(result))
if not result['movie_entered']:
    raise SystemExit(3)
if result['faults'] or not result['playback_stop']:
    raise SystemExit(1)
REPORT
