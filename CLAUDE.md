# bloodborne-runtime

Runs Bloodborne (PS4, CUSA03173 1.09) outside a PS4: the loader, the PS4 system libraries
(`src/runtime_*.c`), and the renderer (`gpu/`, derived from shadPS4, Vulkan through MoltenVK on
macOS). Built on bbport, whose releases are merged here. The game's code runs through bbcpu from
the `recomp/` submodule (bloodborne-recomp), or as its recompiled library (`BB_RECOMP_LIB`).

## Layout

- `src/probe.c` (the program, `out/bb-probe`), `src/runtime*.c`, `src/platform.{c,h}` (Linux and
  macOS differences: `bb_shared_memory`, the `BB_UC_*` signal-context macros, the image base).
- `gpu/`: `shadps4/` (video core, shader recompiler), `shim/` (window, overlay menu, guest hooks,
  Metal residency), `layer/`, `cp/`, `games/` (game profile). Built by CMake into `libbbgpu`.
- `scripts/`: `prepare.py`, `link_modules.py` (boot image), `patches.py`, `game_check.py`, `mods.py`,
  `launcher_api.py`; `scripts/macos/build-deps.sh` (MoltenVK, SDL3, FFmpeg, ... into `deps/`).
- `run.sh` (runs the game; the app calls it), `build.sh`, `patches/Bloodborne.xml`.
- `tools/`: `mac_bench.sh` (background run, frame statistics; `SAMPLE=1`, `TRACE=1`),
  `test_run.sh` (the same with a copy of the player's save: use this one), `grab.sh`, `press.sh`
  (pad input), the Metal trace readers (`mst_*.py`), `fsr4cap/`.
- `docs/`: `guide/building.md`, `guide/how-it-works.md`, `MACOS_PERFORMANCE.md`, `CREDITS.md`,
  upstream's notes (many in Russian).

## Build, run, test

Tests use English (`BB_LANGUAGE=1 BB_UI_LANGUAGE=en`, and English launcher/helper UI).
The isolated game runner supplies those defaults; a later explicit language investigation
may override them at the maintainer’s request.

```sh
bash build.sh                                   # macOS: builds deps/ the first time (~1 h)
BB_GAME_DIR=~/Downloads/PS4/Bloodborne/CUSA03173 bash run.sh
tools/test_run.sh NAME [VAR=VALUE...]           # background run with a copy of the save
cd tests && python3 -m unittest discover -p 'test_*.py'
```

The user's game dump is `~/Downloads/PS4/Bloodborne/CUSA03173`; `deps/` takes about an hour to
build and records absolute paths (a moved or copied `deps/` must be rebuilt). Metal
System Traces need `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`; what helped, what
did not and what made things worse is in `docs/MACOS_PERFORMANCE.md` (read it before trying a
performance idea).

`test_probe.LoaderTests` fails on macOS (its images use 4 KiB-aligned segments; Macs have 16 KiB
pages): known, not a regression. A healthy run in the Hunter's Dream is ~58–62 FPS on the user's
M3 Pro; FPS is bound by rendering, so CPU-side changes are measured in CPU time.

## Command buffers

The game's main thread and GXRenderThread both record into the main graphics command buffer while
an area loads; `src/runtime_cmdbuf.c` holds a per-buffer lock across the game's command buffer
methods (bbcpu guards; `BB_CB_GUARD=0`: off). `BB_CB_PROBE=1` reports any buffer two threads use at
once, with their names and callers: the first thing to run on a "PM4 type 0" or "Bad PM4 packet".

## Local trophies

`docs/TROPHIES.md` describes profile-owned event recording and edited-save gating.
Always keep trophy imports local, even with the online module enabled. Check `.bb-edited`
inside the selected save subtree before granting an unlock; a launcher environment flag cannot
re-enable an edited save. Preserve earned history across restores. Online access restrictions
for edited saves are undecided; do not add one without the maintainer's decision.

## Native renderer

`docs/NATIVE_RENDERER.md`: milestone R5, the engine's graphics on each platform's own API (steps
N0–N6, N0 and N1 done). Multiplatform: shared core, Vulkan and Metal backends; nothing Metal-only.
N4's design is approved and Linux CI passes. The first buffer-resource slice is opt-in
(`BB_N4_BUFFER_DESC=1`); uncapped and capped A/B game gates pass in Central Yharnam. Handle
issuance, completion-based retirement and imported backing are next.
N4 is paused after this slice at the maintainer's request: opening-video stability comes first.

## Rendering bugs

Motion-vector graphics pipeline keys are rejected during cache preload, before stage metadata
is read. Metadata can describe a plain permutation and bypass the shader-level motion check;
motion shaders also contain session-local buffer addresses. Rebuild those pipelines at first
use. Ordinary pipelines still preload; no cache-format version changed. This ports
Supermedo/bloodborne_pc `f33e7617` by João Pedro Valadares. See the renderer port log for checks
and limits. It is not an established fix for the historical video or sampler assertions.
The warm-cache copied-save check and one isolated opening completed without new assertions;
372 pipelines preloaded and the maintainer reported perfect playback. Motion vectors were off,
so the motion-cache failure itself was not reproduced here. Historical counts are unchanged.

The Windows-fork texture upload-baseline port remains opt-in (`BB_IMAGE_UPLOAD_BASELINE=1`).
Two uncapped pairs favored off; later capped comparisons did not show a repeatable CPU
regression. `BB_IMAGE_UPLOAD_HASH_STATS=1` measures aggregate calls, bytes and elapsed
hash-path time without guest addresses or contents. Four recurring large writable mip
checks cost about 0.05–0.06 ms per frame at 40 FPS in Central Yharnam. The larger uncapped
difference is unresolved. An uncapped trace pair showed similar encoder counts and no
increase in GPU busy time on; post-trace holds measured 47.2/46.9 FPS off/on. Use
`tools/mst_passes.py TRACE --complete-frames` for counted normalization. Retain full checks
and the off default. Details are in
`docs/MACOS_PERFORMANCE.md`. N4 remains paused.

Bisect bbport's optimizations first: `BB_TOGGLE_FILE` holds a mask of switches (bits 0–19, 30,
32–50; re-read every 250 ms); run with all off, then halve groups with one fixed mask per run.
The user watches and says what they see. Example: the cutscene flashing was bit 42
(`WRITE_DATA` pipelined on the recording thread).

If a game run hangs in an indirect dispatch, first retry with `BB_INDIRECT_GUARD=0`:
Bloodborne-Android reported a hang after a few minutes with upstream's guard enabled on its
device. This has not been observed here.

`docs/STABILITY.md` records the opening-video watch list. On 2026-10-12, five alternating
isolated new-save trials each reproduced the same null-read fault (address `0xd2`) in released
v0.5.1 (2/5, runtime `0d7e88d`) and current main (4/5, `727c2b2`). It is an older bug; later
changes might affect its likelihood. Earlier development logs have 11 crashes in 17 movie
trials, plus one stalled guard experiment. Main and GXRenderThread have faulted; the new release
comparison's named faults were on main. The earlier command-buffer probes found those two
users of the main buffer, no third movie-thread writer and no overlapping buffer memory.
Wider and narrower guards did not remove the fault, so neither was retained.
Four initial binding-state probe trials completed without capturing the cause; those probes
were removed. A subsequent filtered load/store and host-write trace caught GXRenderThread
clearing the shared graphics input state while main had installed the movie binding. Movie
frames and render task-list executions now hold the same buffer lock across their state changes
and draws (`BB_MOVIE_STATE_GUARD=0`: old coverage). All 10 guarded openings completed, versus
3/5 faults in alternating controls. Capped 40 FPS gameplay CPU averaged 395.7/397.7% off/on;
uncapped FPS was 48.5/48.6 in one pair. These guards cover translated calls; direct calls in the
optional recompiled library still need guard-aware dispatch. N4 remains paused after its slice.
Use `tools/test_opening_video.sh NAME [VAR=VALUE...]` for release QA of this isolated new-save
route. It uses one Up press for Next. The ordinary copied-save holds skip the movie.

Three logs contain `GPU [Debug] <Critical> vk_shader_hle.cpp:242 ExecuteCopyShaderHLE: Assertion Failed!`:
`n4_trace_on` while loading, and `video_on1`/`video_off2` during playback. One `r5scope` log
contains `Unknown data_format=6 and num_format=1`; one `n4_baseline` log contains
`Unknown data_format=15 and num_format=4`. Build/mode details and run timestamps are in the
stability document. None of these GPU assertions recurred in the release/main comparison or the video-fix
acceptance and gameplay batches. Keep counting them. Do not record guest code or memory in docs or commits.

The maintainer also reported two bright white flashes in the opening video before character
creation on 2026-10-12; playback completed. This visual defect is unresolved. Seven subsequent observed openings all completed without
faults/assertions; four had a reported white flash. Clear, draw-pipeline and preparation-worker
pairs did not establish a cause. `BB_AVPLAYER_COPY_PROBE=1` counts possible overlaps with
registered host-copy sources before a decoded-frame write, without logging frame data.
Stability docs record the results and diagnostic limits; do not infer visual correctness from
crash-free playback.

One additional optimization-baseline trial, `flash_opts_off1`, stopped during the video with
`GPU [Debug] <Critical> liverpool_to_vk.cpp:394 MipFilter: Unreachable code!` (five occurrences: `flash_opts_off1`,
`flash_clock_after1`, translated-walker `flash_srt_off1`, `flash_resume1` and
`video_lifecycle_on1`).
Repeated baselines with the command-buffer probe completed; flash counts were unavailable.
Clock review found an unsynchronized audio timestamp/anchor pair. The clock now uses a mutex
and steady host time; standalone tests also pass under ThreadSanitizer.
`BB_AVPLAYER_TIMING_PROBE=1` measures frame lag/waits. `BB_SAMPLER_PROBE=1` identifies invalid
sampler provenance and flattened bounds; the translated failure had valid indices, live
sampler data and a draw snapshot. The portable comparison completed, but was not visually
observed. Release acceptance remains open. Do not call the
white flashes fixed on the strength of the clock unit tests.

On 2026-10-13, `flash_enqueue_on1` and `flash_pthread_on1` faulted on `Mv_AudioPlayThread`
after `Logical Error. Invalid mutex.(EINVAL)`. The second reproduced with `BB_CB_PROBE=0`
and logged `ensure_mutex` returning EINVAL; its playback had looked perfect before shutdown.
The paired pipeline-off runs completed and were reported as perfect. `BB_PTHREAD_PROBE=1`
logs EINVAL operations; mutex validation now distinguishes null handle slots from destroyed
handles (that distinction is not yet game-validated). `BB_SAMPLER_PROBE=1` also checks invalid
filters at draw enqueue, to distinguish stage-A input from later binding. The enqueue check caught the fifth sampler assertion before stage B used the snapshot. Keep these shutdown panics separate from the earlier null-read and GPU
assertions. Stability docs list all five new trials and their limits.
`video_queue_before3` also faulted on the audio thread after apparently perfect playback. Its
trace confirmed `mutex_lock` on a destroyed handle: main destroyed that slot 13.374 ms before
the invalid use, before a movie audio-thread return was logged. There are now three such
shutdown panics. The last periodic lag was only 36 ms. The 128-record destruction history
and `Mv_` lifecycle trace are opt-in under `BB_PTHREAD_PROBE=1`; absence of a record is not
proof of safety. Strict `BB_SUBMIT_LOCK=gpu` trials completed, but one still flashed twice,
so no submission-lock default was changed. Stability docs record the full comparisons.
The AV queue race is repaired: size, selection and pop are locked; no unlocked front reference
escapes. `avplayer-queue-test` runs in `build.sh --test` and passes ThreadSanitizer, whereas the
original queue reproduced a data race. `video_queue_after1` completed and logged audio return
before main detached it; its visual report is unavailable. This is a source-correctness repair,
not acceptance of the white-flash or thread-state shutdown fixes. Keep the release gate open.
The repeated repaired opening `video_queue_after2` completed with clean return-before-detach
ordering and was reported as perfect playback. Both repaired trials completed (2/2), with only
the repeat visually observed. Linux and macOS CI passed for `f86fabf`. These checks are positive;
the wider sampler and premature-destruction investigations remain open.

Pre-release QA on 2026-10-13 added five default-renderer isolated openings, all completed and
observed as successful, plus two self-contained downloaded-package openings (warm and empty
project cache), also observed as perfect. There were no faults, GPU assertions or invalid-mutex
panics. One extra package opening inherited development MoltenVK through the harness and is
excluded from self-contained acceptance; the repeat corrected the driver selection. A
90-second packaged copied-save hold passed at 47.3 FPS, p90 22.9 ms, sampled CPU 511% and
GPU-thread time 4.68 us/draw. Runtime tests and app CI passed. These are positive acceptance
checks, not proof that all rare historical faults are fixed; keep the existing watch counts.
The project cache was empty in the cold check, but OS/Metal caches were retained. Full
packaged-launcher first-setup/Gatekeeper UI was not exercised by these isolated game tests.
No version bump/tag occurred, and N4 remains paused. See the pre-release section in STABILITY.

## Upstream merges

bbport releases come as squashed commits; remote `upstream`
(https://github.com/deadinside28/bloodborne_pc). Before the first N4 renderer code slice, its
HEAD was checked on 2026-10-12 and still matched release 0.51. Once N4 code starts, merge
non-renderer changes (runtime, patches, scripts, game settings and launcher) as before. Review
changes in `renderer_vulkan/`, `buffer_cache/`, `texture_cache/` and `shader_recompiler/` for each
release and port useful ones by hand into the shared core or Vulkan backend; record ported,
skipped and deferred decisions in `docs/upstream/RENDERER_PORTS.md`. Check what assumes Linux or
native x86:

- no `MAP_FIXED_NOREPLACE`, `memfd_create`, `fallocate`, `gregs`/`REG_RIP` on macOS: use
  `bb_shared_memory`, `bb_shared_memory_zero`, `BB_UC_RIP`/`BB_UC_RSP`;
- the image is not at 0x800000000: use `BB_IMAGE_BASE`, and image offsets in game profiles;
- host code cannot run x86 stubs or call guest functions directly under bbcpu: such features stay
  int3 traps or are off on macOS (upstream's guest-hook detours, the game-menu pages);
- libc++ needs complete types where GCC does not.

Then build, run the game a few times (`tools/test_run.sh`), update `docs/CREDITS.md` ("merged up
to"), `docs/upstream/README*.md` and the renderer port log, and bump the submodule in
bloodborne-mac. On macOS the
"Auto" memory model picks the 0.3 one (non-AMD GPU).

## Working rules (all three repositories)

- **The project is three repositories**, nested as submodules:
  [bloodborne-mac](https://github.com/bloodborne-native/bloodborne-mac) (the app) ⊃ `runtime/` =
  [bloodborne-runtime](https://github.com/bloodborne-native/bloodborne-runtime) ⊃ `recomp/` =
  [bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp). Commit in the repository
  that owns the file, push it, then bump the submodule pointer in the parent (runtime → recomp,
  mac → runtime) and push that too. Submodules are checked out at a commit (detached HEAD):
  before editing inside `runtime/` or `runtime/recomp/`, run `git checkout main` (and
  `git pull`) there. Separate clones of each repository side by side work the same way.
- **Before any push, check `git remote get-url origin`** (a clone once still pointed at a local
  repository and a force-push landed there). Push to `main`; ask before force-pushing anything
  that existed before this session.
- **Commits:** a short subject, a body that says what changed and why (numbers when measured), and
  the trailer the session asks for (`Co-Authored-By: ...`).
- **Credits:** this work is built on [bbport](https://github.com/deadinside28/bloodborne_pc) by
  deadinside28 and on [shadPS4](https://github.com/shadps4-emu/shadPS4); keep them credited where
  they are (README, docs/CREDITS.md, the wikis). Code taken from another project or fork is
  credited in the same commit: CREDITS.md, the wiki's credits page, the commit message and a short
  code comment.
- **No game code, ever:** no decompiled or generated game code, no game files, no records of game
  memory in any repository. Docs and wikis talk about the recompiler only (not about decompiled
  code or private folders).
- **Testing the game:** run it in the background with `runtime/tools/test_run.sh NAME [VAR=...]`
  (bloodborne-runtime's `tools/test_run.sh`): it copies the player's save first and never touches
  the real one. Check `pgrep -x bb-probe` first; one game at a time. Test windows open on the
  user's screen: say so before long batches of runs (bisections), and stop when asked.
- **Never capture the user's screen.** Pictures come from the game itself: `tools/grab.sh NAME`
  (its frame) or `tools/grab.sh --screen NAME` (with the in-game menu). Screenshots in English
  (`BB_LANGUAGE=1 BB_UI_LANGUAGE=en`).
- **Shell:** no `rm -rf` on variable paths (literal paths only); no `xargs ... sh -c '...'` (write
  a script file instead).
- **Docs:** plain, precise English; numbers measured, not guessed. When a milestone or a measured
  number changes, update the docs and the wiki status pages (progress bars: 20 blocks, one per 5%).

The packaged 0.6.0 launcher first-profile/Home check subsequently passed on 2026-10-13 with
synthetic data: the maintainer confirmed the active Release QA profile and version 0.6.0;
SQLite foreign-key checks were clear. Clean-machine Gatekeeper/upgrade UI remains untested.
This is launcher QA; no renderer code or game runtime behavior changed after the game batch.

App 0.6.0 is published from app bdf9589 with runtime 08bffeb, launcher 2e13892 and recompiler
ac2931c. Release CI 38399743268 passed and the prepared changelog/DMG were published. The
previous stability and packaged-launcher checks apply; N4 remains paused and historical rare
fault counts remain on the watch list.
