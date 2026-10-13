# Opening-video stability

## Status (2026-10-12)

The new-save opening video faults before character creation in released v0.5.1 and in main
without the new state guard. A shared-state reset race has been identified and guarded. Five
alternating controls produced three null faults; all five guarded trials completed playback.
The guard is now the default. Five further translated-walker trials completed playback, and
capped and uncapped gameplay gates passed with the small CPU cost recorded below.
N4 remains paused after its first buffer-resource slice. No release has been tagged.

## Release versus main

The app's `v0.5.1` tag points to runtime `0d7e88d2798d99d08193c9999610bdec95e3f9df`.
That commit and its exact submodule revisions were built in a detached worktree with its own
`out/` and a symlink to the main clone's `deps/`. The release sources were unmodified. Current
main was `727c2b2e88039fd5a15d800fdd13cdccf8409c71`. The recompiler submodule revisions differ
only in documentation; its CPU code is the same.

Five trials per build alternated release/main. Every trial used `tools/test_run.sh`, a fresh
isolated user directory and the same configuration. No real save was written. Runs were
untraced and used each build's defaults: the translated SRT walker on the release, portable on
main, and the N4 resource seam off. Menu navigation was corrected where needed; two attempts
that never entered the movie were excluded. Playback entry and completion were checked in the
AvPlayer log. This is a reproduction check, not a precise estimate of failure probability.

| Pair | v0.5.1 run | Result | Current-main run | Result |
|---|---|---|---|---|
| 1 | `stab_release1` | Playback completed | `stab_main1_retry` | Null-read fault; main thread |
| 2 | `stab_release2_retry` | Playback completed | `stab_main2` | Null-read fault; main thread |
| 3 | `stab_release3` | Null-read fault | `stab_main3` | Null-read fault; main thread |
| 4 | `stab_release4` | Null-read fault | `stab_main4` | Null-read fault; main thread |
| 5 | `stab_release5` | Playback completed | `stab_main5` | Playback completed |

Release faulted in **2/5** trials and main in **4/5**. All six faults were the same read through
a null pointer (fault address `0xd2`), with matching main-thread caller chains where thread
names were available. The untouched release does not print the guest thread name. No
`ExecuteCopyShaderHLE` or `SurfaceFormat` assertion appeared in this batch.

The fault is older than the fork fixes, deferred clears and the portable SRT default. These
results do not exclude later changes affecting its likelihood. The higher main count is not
enough evidence to identify a particular regression or justify reverting one of those changes.

Excluded navigation attempts: `stab_main1` and `stab_release2`. Neither entered the movie;
neither is counted as successful playback.

## Earlier development trials

The earlier 2026-10-12 logs contain 17 trials that entered the movie: 9 null-read faults,
2 `ExecuteCopyShaderHLE` assertions, 1 stalled guard experiment and 5 completed playbacks.
Those trials used different development trees and diagnostics, so their aggregate is not a
controlled release comparison. Two additional command-buffer guard placements failed to remove
the null fault; neither was retained. The wider placement coincided with the stall.

In the earlier ten-run translated/portable comparison, null faults occurred in 2/5 and 3/5
trials, with one copy-shader assertion each way. Command-buffer probes saw the main and graphics
threads use the same buffer close together, but no third movie-thread writer or overlapping
buffer memory. At that stage, a timing or shared-state race was a hypothesis, not a confirmed cause.

## GPU assertion inventory

The retained benchmark logs contain three copy-shader assertions and one occurrence of each
reported surface-format assertion. Historical logs did not stamp the complete source tree;
where a development tree was dirty, its exact revision cannot be recovered from the log.

| Assertion | Run | Build or mode recorded |
|---|---|---|
| `GPU [Debug] <Critical> vk_shader_hle.cpp:242 ExecuteCopyShaderHLE: Assertion Failed!` | `n4_trace_on` (`1012_102807`) | Pre-fork development build; `BB_FIXED_QUAD_TRIANGLES=1`; stopped while loading |
| Same copy-shader assertion | `video_on1` (`1012_120514`) | `196b791` development tree with fault-thread reporting; portable SRT; opening video |
| Same copy-shader assertion | `video_off2` (`1012_120658`) | Same development tree; translated SRT; opening video |
| `Unknown data_format=6 and num_format=1` | `r5scope` (`1011_134814`) | Pre-N1 scoping build with a Metal trace and sample; exact tree not recorded |
| `Unknown data_format=15 and num_format=4` | `n4_baseline` (`1012_122820`) | Pre-N4-resource development build; deferred clears on, translated SRT; after a Metal trace |

Both assertion types are followed by `STOP: GPU library assertion failed (see GPU log above)`.
Their relationship to the opening-video null fault is not established. Keep counting them by
build and run during the investigation.

## Caller and state investigation

Read-only Ghidra analysis followed the movie update through its draw wrapper to the graphics
state writer that faults. The movie installs a source binding in the shared graphics context,
draws, then restores the previous state. The main and graphics threads both use that context.
The existing command-buffer lock protects packet methods; this alone does not establish the
ordering of the surrounding state updates. No incorrect PS4 library return value has been
identified. The AvPlayer frame retrieval path was checked, but this does not prove all playback
contracts are correct.

Four additional isolated movie trials used temporary binding-state probes on current main:

| Run | Walker | Probe version | Result |
|---|---|---|---|
| `stab_probe_portable` (`1012_153404`) | Portable | Entry, draw and setter probes with normal-state logging | Playback completed |
| `stab_probe_translated` (`1012_153841`) | Translated | Added state-writer entry probe; normal-state logging | Playback completed |
| `stab_probe_light1` (`1012_154434`) | Portable | Same sites; log invalid states only, direct atomic reads | Playback completed |
| `stab_probe_light2` (`1012_154956`) | Translated | Same lightweight probes | Playback completed |

The source binding was initialized in the sampled normal calls. No invalid binding was
reported, and no copy-shader or surface-format assertion appeared in these four runs. The
diagnostics changed between the first two trials and change timing and translation boundaries,
so these are investigation results, not a controlled failure-rate comparison or a verified fix.
They leave both a shared-state ordering problem and CPU translation behavior to investigate.
The temporary probes were removed; no new guard or playback workaround was retained.

The next useful diagnostic must distinguish the state at the writer's load from the register
value at the fault, while disturbing execution less than the entry probes. A guard should be
extended only once its protected state and lock ordering are established; the previous wider
guard experiment stalled. At that stage the root cause remained unresolved; the subsequent
filtered write trace below identified the intervening reset.

## Confirmed shared-state reset

Five subsequent diagnostic trials (`faultstate1`, `faultstate2`, `bindtrace1`, `bindtrace2`,
`bulktrace1`) reproduced the null fault. Fault-only reads first established the exact
instruction and that the input binding could change between the fault and inspection.
Filtered inline load/store recording then captured the main thread installing the movie's
valid binding and later loading zero from the same address. Host memory-write recording
identified the intervening operation: GXRenderThread cleared the shared graphics input state
with memset while executing a render task list. In `bulktrace1`, that reset was recorded about
33 microseconds after the movie's binding store and about 105 microseconds before its load.
Counter frequency was verified locally; record timestamps are taken after accesses and do not
freeze the other threads.

This establishes a shared-state race for the captured null fault. It does not establish the
cause of the independent copy-shader and surface-format assertions.
The movie source binding was initialized in this capture. Packet-method locks do not
protect the state snapshot, direct binding stores, state reset and restoration around draws.

The candidate `BB_MOVIE_STATE_GUARD=1` guards two complete transactions using the existing
recursive lock for their graphics command buffer: a movie frame and a render task-list
execution. Both include state updates and draws; the task-list scope includes the bulk reset.
The context reaches these functions through different arguments, so each resolves the correct
context before choosing the lock.

Five untraced pairs alternated the guard off/on with the portable walker. The controls had
two main-thread null reads at `0xd2`, one graphics-thread null read at address zero and two
completed videos. All five guarded runs completed playback without faults, stalls or GPU
assertions. Game-frame captures in the first and last guarded runs verified the Contract
character-creation screen. The guard is now the default; `BB_MOVIE_STATE_GUARD=0` restores the
old packet-method coverage. The existing `BB_CB_GUARD=0` disables all these guards.

| Pair | Guard off | Result | Guard on | Result |
|---|---|---|---|---|
| 1 | `txn_off1` (`1012_194409`) | Null read; main | `txn_on1` (`1012_194528`) | Completed; Contract screen checked |
| 2 | `txn_off2` (`1012_194843`) | Null read; main | `txn_on2` (`1012_195013`) | Completed |
| 3 | `txn_off3` (`1012_195306`) | Completed | `txn_on3` (`1012_195558`) | Completed |
| 4 | `txn_off4` (`1012_195849`) | Null read; GXRenderThread | `txn_on4` (`1012_200003`) | Completed |
| 5 | `txn_off5` (`1012_200254`) | Completed | `txn_on5` (`1012_200548`) | Completed; Contract screen checked |

Five further default-guard trials with `BB_SRT_INTERP=0` completed playback: `fallback1`
(`1012_201127`), `fallback2` (`1012_201418`), `fallback3` (`1012_201710`), `fallback4`
(`1012_202002`) and `fallback5` (`1012_202257`). The last used the reusable
`tools/test_opening_video.sh` and a game-frame capture verified character creation. In total,
all **10/10 guarded openings** completed, against **3/5 faults in the alternating controls**.
No copy-shader or surface-format assertion appeared in these fifteen acceptance trials.
These counts measure this opening route, not every movie or the direct calls made by the
optional recompiled game library. The latter still needs guard-aware calls, as already required
by the recompiler's R5 work.

### Gameplay cost

Three off/on pairs used the copied current save in Central Yharnam, without tracing, `HOLD=90`
and `BB_FPS_LIMIT=40`. All six measured 40.0 FPS and 25.0 ms frame-time p90, with about 1,268
draws/frame. Stage-B values below average the last 18 frame-stat windows; CPU percentages are
the benchmark's averages of one core. Neither build faulted or asserted.

| Pair | Off game CPU | On game CPU | Off GPU thread µs/draw | On GPU thread µs/draw |
|---|---:|---:|---:|---:|
| `statecpu_off1` / `statecpu_on1` | 396% | 395% | 3.75 | 3.79 |
| `statecpu_off2` / `statecpu_on2` | 396% | 400% | 3.66 | 3.69 |
| `statecpu_off3` / `statecpu_on3` | 395% | 398% | 3.65 | 3.69 |

Mean CPU was 395.7% off and 397.7% on: a measured increase of 2.0 percentage points of one core,
about 0.5% relative. Stage B increased about 0.04 µs/draw (1.0%). FPS and p90 were unchanged.
The fix is kept for stability; its measured cost is recorded rather than rounded to zero.

One additional untraced uncapped pair checked that the limit was not hiding a throughput loss:

| Run | FPS | Frame-time p90 | Game CPU | GPU thread µs/draw |
|---|---:|---:|---:|---:|
| `statefull_off` (`1012_204249`) | 48.5 | 21.7 ms | 504% | 4.37 |
| `statefull_on` (`1012_204500`) | 48.6 | 21.6 ms | 507% | 4.39 |

Both completed without faults or assertions. The small differences are one pair's results,
not evidence of a speed gain.

### Repeatable opening-video check

On macOS, run `tools/test_opening_video.sh NAME [VAR=VALUE...]`. It delegates to
`tools/test_run.sh`, creates a fresh isolated user directory and refuses to reuse it, stamps
source revisions/diff hashes and host-binary hashes, and uses one Up press to reach Next in
setup. The isolated directory override is last, so caller switches cannot redirect writes to
the real save. `VIDEO_GRAB=1` captures the game's frame after natural playback completion.
The script returns failure for a fault or incomplete playback, and separately identifies
navigation attempts that never entered the movie. Tell the user before batches and check
`pgrep -x bb-probe` first. Never capture the user's screen.

Navigation waits for the bench driver's title inputs to finish, then uses two-second menu
settle times instead of the previous 15- and 20-second pauses. `VIDEO_MENU_WAIT` and
`VIDEO_SETUP_WAIT` can increase those delays for slow boots. After the final press it polls
for movie entry for up to ten seconds, returning immediately when entry is logged. These
timing changes passed shell syntax checks; game navigation has not yet been rechecked.

The user reported poor video frame rate before crashes. The last pacing windows in
`faultstate1` and `faultstate2` had p99 values of 118.05 and 51.36 ms. Cold pipeline compilation
still causes hitches: one guarded movie window compiled two pipelines for 348.1 ms and reported
a 403.0 ms worst frame. The state guard addresses the captured null fault; these observations
do not establish that all playback pacing problems are solved.

The optional diagnostics are `BB_JIT_FAULT_MAP=1` (exact instruction mapping without added
execution instructions), `BB_JIT_WATCH` (filtered inline loads/stores plus host memory writes),
`BB_FAULT_READS` (safe pointer-chain reads after a fault), and `BB_WRITE_LOG_ADDRESS` (a complete-
qword filter for the existing GPU write log). The GPU write log found no full-qword writes
covering the watched field in `bulktrace1`. Run metadata stamps source revisions and local-diff hashes; diagnostic
output and private analysis stay outside Git and the wiki. The synthetic CPU tests cover exact
fault mapping, slow accesses, watched values and ring wrap. This version of the CPU code passed
4,000 interpreter/JIT comparison cases and 6,018 instruction fuzz cases with zero failures or
crashes; runtime tests also pass.

### White flashes during opening-video playback

On 2026-10-12, the maintainer reported two bright white flashes in the prerecorded opening
video before character creation during launcher playtesting. Playback finished. This is a
visual defect report, not a reproduced crash or a verified fix. The exact playtest binary and
switches were not stamped by the isolated test harness. A read-only check of the current
launcher `last.log` found playback start/stop messages and neither watched GPU assertion;
that log alone cannot establish the cause or identify the flashed frames.

Two subsequent isolated untraced openings reproduced the visual defect with the crash guard
held on and `BB_PIPELINED_WRITE_DATA=0`: `flash_clr_off1` (clear deferral off) and
`flash_clr_on1` (on) both completed without faults or watched assertions. The maintainer saw
one white flash in each, at different moments. This pair does not identify clear deferral as
the cause; it does not establish that its timing is unaffected. A source-video luminance check
used the local FFmpeg libraries: 1,216 decoded frames, 40.68 seconds, no frame with mean luma
above 190/255. That excludes a full-frame near-white source image at that threshold, not every
localized bright effect. Source decoding briefly overlapped the on run; these trials are visual
checks, not a performance comparison. No source frames or video assets are committed.

A second pair held clear deferral on and set `BB_UI_TRACE=1` in both modes:
`flash_pipe_off1` disabled the draw pipeline; the maintainer saw no white flash but reported
stuttering. `flash_pipe_on1` enabled it; the maintainer described playback as perfect. Neither
log recorded a UI-composition gap or a shader/pipeline compilation window after movie entry.
One clean run in each mode does not identify the draw pipeline as the cause or establish a fix.
The earlier clear-mode pair had one flash each at different moments. The defect is intermittent.

A third pair held the draw pipeline and clear deferral on: `flash_prep_off1` used zero
speculative preparation workers (one mid-video flash and slight stuttering reported), while
`flash_prep_on1` restored the normal six workers (no flash, heavy stuttering at the beginning
reported). Disabling preparation workers therefore does not remove the defect either. All six observed
openings completed without faults or watched GPU assertions; three had a reported flash.

The earlier stale-constant cutscene fix already leaves `BB_PIPELINED_WRITE_DATA` off by
default. Do not assume this report has the same cause. Continue observed alternating tests
with one setting changed per pair and the movie-state guard enabled. Speculative draw-preparation
workers and video-buffer reuse/upload ordering are the next paths to isolate. `BB_AVPLAYER_COPY_PROBE=1` adds counts of video-frame writes whose ranges overlap the
scheduler's registered host-copy sources. It is off by default and records no addresses or
frame contents. Slots are coarse and may be conservative; completed copies can remain
registered until a wait updates the bookkeeping. A positive count is a possible overlap,
not proof of an executing copy. A zero count does not cover future registrations or queued
draw reads. It does not synchronize or change buffer reuse.

The rebuilt diagnostic run `flash_copy_probe1` completed without faults or watched GPU
assertions. The maintainer saw a flash at the very end before character creation, with no
stuttering. The last periodic record counted 1,152 decoded-frame writes and zero possible
registered-copy overlaps; no positive overlap report appeared before playback stopped.
This check did not capture a registered host-copy overlap. It does not rule out future queued
draw reads or reuse/freeing before command processing reaches the video draw. Next inspect
resource-table snapshots and guest-visible completion ordering around video shutdown.

Across these seven observed openings, four had a reported white flash. None crashed or hit
one of the watched assertions. The visual defect is not fixed, and these counts do not prove
a regression against released v0.5.1.

### Broader optimization baseline and clock investigation

The documented optimization mask disables bits 0–19, 30 and 32–50
(`2251796593508351`); it does not disable the movie-state guard or clear deferral.
`flash_opts_off1` entered playback and failed with
`GPU [Debug] <Critical> liverpool_to_vk.cpp:394 MipFilter: Unreachable code!`.
This is a new sampler assertion, one occurrence so far, not one of the earlier SurfaceFormat
or ExecuteCopyShaderHLE assertions. The mask was corrected during boot before the video;
this trial is not a performance baseline. The next runs used the exact mask from startup.

`flash_opts_off2` repeated the baseline with `BB_CB_PROBE=1 BB_CB_PROBE_US=1000` and
completed without a fault; the maintainer did not watch it. `flash_opts_off3` repeated the
same setup and completed; heavy stuttering was reported, but the flash count was not specified.
Probe reports named main and GXRenderThread; no third movie-thread writer was found.
Do not count either unknown flash result as a visual pass.

The maintainer suggested audio/video synchronization as a possible explanation. Source review
found unsynchronized reads and writes of the audio timestamp and host-time anchor in the old
clock. A video request could observe a mismatched pair. Signed elapsed time was also added to
an unsigned timestamp without guarding negative values. The new `PlaybackClock` uses one mutex
for the anchor snapshot, samples a steady clock under that lock, handles pause/resume and
restart, and clamps negative elapsed time and arithmetic overflow. This fixes a concrete clock
data race; it does not establish that the white flash is fixed.

`BB_AVPLAYER_TIMING_PROBE=1` records counts of shown frames, empty-queue waits, future-frame
waits, inactive calls and frames more than 100 ms late, plus maximum lag/lead. It is off by
default and records no frame contents or addresses. The normal `build.sh --test` flow includes
`avplayer-clock-test`; it passed audio anchoring, pause/resume, restart, unsigned-boundary and
concurrent snapshot checks, also under ThreadSanitizer. Runtime tests passed.

For the clock A/B, the pre-fix host GPU library is preserved outside the checkout and selected
only for the isolated control process. The opening helper now stamps the loaded GPU library's
path and hash, rather than assuming the current build was loaded. `flash_clock_before1` was
verified to load the preserved library and completed without a fault. Its visual result is
unavailable. `flash_clock_after1` loaded the new clock build and failed during the movie with the same
MipFilter assertion (second occurrence at that point). Its last timing record had 896 video requests,
754 shown frames, zero empty-queue waits, 142 future-frame waits, zero frames more than 100 ms
late, maximum lag 54 ms and maximum lead 16 ms. No SRT load fault was logged. The data do not
show a large A/V backlog before this assertion; they do not establish visual acceptance of the
clock fix. `BB_SAMPLER_PROBE=1` now identifies prepared/live descriptor origin and flattened
index bounds for an invalid mip filter, without logging descriptor words or addresses. The
translated/portable SRT comparison uses the same new clock and diagnostics in both modes.
`flash_srt_off1` (translated) also failed with the same MipFilter assertion, the third occurrence.
The sampler probe reported filter 3, stage 0, sampler 1, live rather than prepared descriptor
values, a draw snapshot, and indices within the 28-word flattened buffer. This establishes
that this assertion is not exclusive to the portable walker. `flash_srt_on1` (portable)
completed without faults; its visual result is unavailable. Its last periodic timing record
had 1,408 requests, 1,147 shown frames, no empty-queue waits or frames over 100 ms late,
261 future-frame waits, maximum lag 56 ms and maximum lead 15 ms. These measurements do not
show a major clock backlog in that trial; they do not establish a visual fix.

The clock race repair is retained as a source-correctness fix with passing tests, including
ThreadSanitizer on the standalone clock test. The opening video remains a release gate:
invalid sampler provenance and resource lifetime need investigation, followed by observed
visual checks and packaged-app testing. No renderer optimization has been disabled by default
as a workaround, and no release has been tagged.

Decoder-thread
invalidation calls the rasterizer, but `DrainDrawPipe` only drains on stage A; cache locking and
copy lifetime must be checked before changing this contract. Successful playback and
crash-free runs are insufficient to accept a visual fix. N4 remains paused.

## Investigation boundary

Use Ghidra to understand the null pointer's callers and the state they read, then check the
runtime contracts and ordering they depend on. Analysis output, guest code, stack contents and
memory records stay outside Git and the wiki. A fix must pass the runtime tests and repeated
isolated opening-video trials before it is accepted. N4 stays paused until stability is resolved
or the maintainer chooses the release's known-issue policy.
