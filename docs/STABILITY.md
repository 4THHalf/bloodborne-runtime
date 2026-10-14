# Opening-video stability

## Release 0.6.0 (2026-10-13)

[Bloodborne for Apple Silicon 0.6.0](https://github.com/bloodborne-native/bloodborne-mac/releases/tag/v0.6.0)
was published from app `bdf9589`, launcher `2e13892`, runtime `08bffeb` and recompiler `ac2931c`.
[Release CI](https://github.com/bloodborne-native/bloodborne-mac/actions/runs/38399743268)
passed and published the Apple Silicon DMG with the prepared changelog. The accepted game and
packaged-launcher checks below cover this release; historical faults and untested clean-machine
Gatekeeper/upgrade behavior remain documented. N4 remains paused.

## Latest check (2026-10-13)

The pre-release repeat passed five observed default-renderer openings and two self-contained
package openings, including an empty project-cache run. Packaged copied-save gameplay passed
a 90-second hold without faults or assertions. Historical rare faults remain on the watch
list; clean-machine launcher/Gatekeeper UI was not covered. Details and limits are in
[Pre-release checks](#pre-release-repeat-and-downloaded-package-checks-2026-10-13).

## Pipeline-cache preload guard (2026-10-13)

The guard from Supermedo/bloodborne_pc `f33e7617` rejects motion-vector graphics pipeline
keys before loading shader metadata. Such metadata can describe a plain permutation;
motion shaders also contain session-local addresses. Rebuild those pipelines at first use.
Ordinary graphics and compute pipelines still preload; no cache-format change was needed.
Credit and source: [renderer port log](upstream/RENDERER_PORTS.md#motion-vector-pipeline-preload-guard).

`build.sh --test` passed on macOS arm64. Two untraced isolated runs used the default renderer:

| Run | Check | Result |
|---|---|---|
| `mv_cache_warm` | Copied-save warm-cache startup and 45-second hold | Completed; 44.8 FPS, p90 24.7 ms, sampled CPU 486%, GPU 97%, stage-B 4.63 us/draw. |
| `mv_cache_opening` | Fresh-save opening using the warm shared shader cache, with `BB_GPU_LOG=info` | Preloaded 372 pipelines; playback completed and the maintainer reported perfect playback. The game's own frame dump showed character creation. |

Neither log contained a fault, GPU assertion or invalid-mutex panic. Historical assertion
counts are unchanged. Motion vectors were off, so this is a startup/playback smoke check;
it does not reproduce the foreign motion-cache failure or validate motion output. No
performance gain or white-flash fix is claimed. N4 remains paused.

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
timing changes passed shell syntax checks. Subsequent pipeline-off and pipeline-on runs
both entered the movie with these shorter waits.

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
This was the first occurrence of a new sampler assertion, not one of the earlier SurfaceFormat
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

### Follow-up on 2026-10-13

The faster navigation reached playback in `flash_resume1` (`1013_102457`), but that default
pipeline-on run hit the same MipFilter assertion: the fourth occurrence. The sampler probe
identified a table-sourced mip word, valid flattened indices, and a draw snapshot. The user
reported a crash. The last periodic timing record had 768 requests, 577 shown frames, no
empty-queue waits or frames over 100 ms late, maximum lag 52 ms and lead 12 ms.

`BB_SAMPLER_PROBE=1` now checks invalid filters both when stage A queues a draw and when
stage B binds its sampler. The phase is logged along with the existing descriptor-path
summary, without descriptor contents or addresses. This is diagnostic only; invalid samplers
still assert. The added enqueue check has not yet captured an invalid value.

The next same-build pair kept the command-buffer probe enabled:

| Run | Draw pipeline | Outcome | User observation |
| --- | --- | --- | --- |
| `flash_enqueue_off1` (`1013_102814`) | Off | Normal playback stop, no fault | Perfect playback, no flashes or stutter |
| `flash_enqueue_on1` (`1013_103037`) | On | Movie audio-thread panic near playback end | Low frame rate, stutter, no observed flashes; audio finished before video |

The on run sustained about 25–26 FPS during the later movie windows. Its last periodic record
had 1,152 requests and shown frames, all more than 100 ms late, with maximum lag **7,335 ms**.
The off run's last periodic record had 1,536 requests, 1,149 shown frames, no frames over
100 ms late, maximum lag 43 ms and lead 14 ms. These are periodic records, not full-movie
counts. Neither run reported a registered host-copy overlap or an invalid sampler.

After the user's playback observation, the on run logged
`Logical Error. Invalid mutex.(EINVAL)` and a signal-11 fault on `Mv_AudioPlayThread`.
This differs from the earlier null-read and GPU assertions. The returning pthread operation
is not established. `BB_PTHREAD_PROBE=1` now logs the operation and guest thread on an
`EINVAL` result, without handles or guest memory; return values are unchanged. Build and
runtime tests passed, including the clock and fixed-quad tests; the usual macOS guest-TLS
and 4-KiB loader tests were skipped.

A further same-build off/on pair disabled the command-buffer probe in both modes and enabled
`BB_PTHREAD_PROBE=1`:

| Run | Draw pipeline | Outcome | User observation |
| --- | --- | --- | --- |
| `flash_pthread_off1` (`1013_103442`) | Off | Normal playback stop, no fault | Perfect playback |
| `flash_pthread_on1` (`1013_103710`) | On | Same movie audio-thread panic after playback observation | Perfect playback before the later fault |

The off run's last periodic record had 1,536 requests, 1,170 shown frames, 72 frames more
than 100 ms late, maximum lag 534 ms and lead 13 ms. The on run's last periodic record had
1,536 requests, 1,167 shown frames, 28 frames over 100 ms late, maximum lag 1,574 ms and lead
16 ms. Earlier periodic records in both modes had no frames over 100 ms late. Neither run
reported an invalid sampler or registered host-copy overlap.

The on run logged `Pthread probe: ensure_mutex returned EINVAL, thread Mv_AudioPlayThread`
before the same invalid-mutex panic and signal-11 fault. This establishes that the shutdown
fault does not require the command-buffer probe. The diagnostic did not distinguish a null
handle slot from a destroyed handle. The revised diagnostic names the calling mutex operation
and distinguishes those two validation failures; this revision passed the runtime tests but
has not yet been exercised in a faulting game run. Host `thread_join` uses `pthread_join`,
so source review did not find a stub that returns before thread completion. Caller-chain analysis identified the blocking guest mutex lock in the movie audio thread
state check. It does not establish who destroyed that state or whether a null slot was passed.
Thread stop, completion and destruction ordering need a focused trace next; analysis output
remains outside Git and the wiki.

All five trials used fresh isolated saves and no Metal trace. These are playback observations
and fault diagnostics, not gameplay performance gates. There are now four MipFilter assertions
and two movie audio-thread invalid-mutex panics in the recorded investigation. No new
ExecuteCopyShaderHLE or SurfaceFormat assertion appeared in this batch. The enqueue sampler
probe did not trigger, so it has not established where that corruption starts. N4 and release
acceptance remain paused; these results do not establish a white-flash fix.

### Queue and shutdown follow-up (2026-10-13)

`BB_PTHREAD_PROBE=1` now traces entry, return after TLS cleanup, join and detach for guest
threads whose names start with `Mv_`. It also retains the last 128 successful mutex-destruction
records. On an invalid use of a destroyed slot, it can report the most recent recorded
destructor's thread and elapsed time. This bounded diagnostic does not extend mutex lifetime,
change errors or log handles, addresses or guest memory. A missing record proves nothing.

All trials below used isolated fresh saves, no Metal trace and `BB_CB_PROBE=0`. The first
five used the same pre-queue-repair GPU library, with shutdown and sampler probes enabled.

| Run | Draw pipeline / submission lock | Outcome | User observation |
| --- | --- | --- | --- |
| `video_lifecycle_off1` (`1013_110955`) | Off / decode | Normal stop, audio entry returned before main detached it | Perfect playback |
| `video_lifecycle_on1` (`1013_111227`) | On / decode | MipFilter assertion during the movie | Crash |
| `video_lock_gpu1` (`1013_111429`) | On / GPU completion | Normal stop and return-before-detach | Two flashes, at beginning and end |
| `video_lock_decode1` (`1013_111700`) | On / decode | Normal stop and return-before-detach | Perfect playback |
| `video_lock_gpu2` (`1013_111946`) | On / GPU completion | Normal stop and return-before-detach | Slight initial stutter, then fine playback |

The sampler assertion is the fifth occurrence. `video_lifecycle_on1` captured the invalid
filter **at stage-A enqueue**, before stage B bound it. Both reports described a table-sourced
mip word, valid flattened indices and stage 0, sampler 1. Binding used prepared values and a
draw snapshot. This occurrence did not originate from later corruption of the queued draw
snapshot; the table lifetime and population before enqueue still need investigation. There
was no periodic video-timing record before this failure.

Strict GPU-completion locking did not eliminate flashes. The three alternating lock-mode
trials all completed, so they do not establish a fault-rate improvement. Their last periodic
maximum lag was 342/62/575 ms for GPU/decode/GPU locking. No default was changed on this
comparison; these are playback diagnostics, not gameplay performance gates.

The queue-control retry, `video_queue_before2` (`1013_112637`), loaded the saved pre-fix GPU
library (hash prefix `2c687e6d14f3`) and completed normally. Its playback was not watched.
`video_queue_before1` never launched: prebuilt mode selected the packaged `bin/bb-probe`, which
is absent in this development checkout. It is excluded from game results. The retry supplied
the explicit development executable; the helper stamped the actually loaded GPU library.

The maintainer requested a repeat. `video_queue_before3` (`1013_112859`) looked perfect, then
faulted during shutdown. It captured:

```text
Pthread probe: mutex destruction by main 13.374 ms before invalid use
Pthread probe: mutex_lock returned EINVAL (destroyed handle), thread Mv_AudioPlayThread
Logical Error. Invalid mutex.(EINVAL)
```

No movie audio-thread return or detach was logged before the fault. The last periodic video
record had 1,536 requests, 1,145 shown frames, no frames over 100 ms late, maximum lag 36 ms
and lead 13 ms. A large video backlog is therefore not required for this shutdown fault.
This is the third recorded audio-thread invalid-mutex panic. The next target is the destructor
and thread-stop ordering that allows main to destroy the state while the audio thread can
still use it. Caller analysis is for understanding only; no guest code or memory is published.

#### Queue repair and its limits

Source review found unsynchronized `std::queue::size()` reads in `AvPlayerQueue::Size`,
including decoder, frame-availability and controller-event checks. `Pop` also tested for
emptiness before taking the mutex, and video selection held an unlocked front reference.
A standalone single-producer/consumer/observer stress test reproduced a ThreadSanitizer data
race in the original queue. The same stress test passed after locking queue size and pop.

The repaired queue locks every container access. `PopIf` tests and removes the front under one
lock; video timing selects a due frame there, then owns it after unlocking. Audio fetch also
owns the popped frame before recycling its previous buffer and checks for an empty result.
Future frames remain queued. This repairs queue synchronization; it does not establish that
all source state, stop callbacks or borrowed guest-frame lifetimes are safe.

`avplayer-queue-test` is in `build.sh --test`, alongside the clock and fixed-quad tests. It
passed conditional selection, move-only FIFO ownership, a 20,000-item producer with two
competing consumers and a size observer, and concurrent clear/pop with exactly-once buffer
destruction. The integrated test passed under ThreadSanitizer with no reported races. The
full build/test flow passed with the usual macOS guest-TLS and 4-KiB loader skips.

`video_queue_after1` (`1013_113141`) loaded the repaired GPU library (hash prefix
`9601e6924fde`) with the same pipeline-on, decode-lock settings as the repeated pre-fix
control. It completed normally; audio entry returned before main detached the thread. Its
last periodic record had 1,536 requests, 1,189 shown frames, 10 frames over 100 ms late,
maximum lag 152 ms and lead 14 ms. Visual feedback is unavailable at this checkpoint.
The pre-fix control's apparently perfect playback followed by a shutdown fault is evidence
that visual observation alone is insufficient. One repaired opening does not prove a fix for
the shutdown panic or flashes. This repair is retained for the verified queue race; the
premature thread-state destructor and pre-enqueue sampler input remain release blockers.

#### Observed queue-repair repeat (2026-10-13)

At the maintainer's request, `video_queue_after2` (`1013_121409`) repeated the repaired build
with the same pipeline-on, decode-lock settings and a fresh isolated save. The maintainer
reported **perfect playback**. Playback stopped normally, the movie audio entry returned
before main detached it, and the full run ended without faults or GPU assertions.

Its last periodic timing record had 1,536 requests, 1,159 shown frames, no empty-queue waits,
377 future-frame waits, 32 frames more than 100 ms late, maximum lag 197 ms and lead 15 ms.
These are partial timing counters, not full-movie counts or a gameplay performance comparison.

Both repaired openings completed (**2/2**); only this repeat was visually observed. Linux and
macOS CI passed for runtime `f86fabf`. This is a positive observed check, but two completions
and one observed playback do not close the sampler and shutdown investigations or establish
release acceptance.

### Pre-release repeat and downloaded package checks (2026-10-13)

The runtime build/test flow passed on `ae638a4` (queue repair `f86fabf`). The online-module,
guest-TLS and 4-KiB loader checks retained their documented macOS skips. Five further openings
used fresh isolated saves, default renderer settings, no Metal trace and the same local GPU
library (hash prefix `9601e6924fde`). Timing and movie-thread diagnostics were enabled;
the command-buffer probe was disabled. They all completed without faults or GPU assertions,
and all five were observed as successful playback.

| Run (log stamp) | Observation | Last periodic maximum lag | Audio exit/detach log order |
| --- | --- | --- | --- |
| `release_video1` (`1013_124338`) | Perfect playback | 40 ms | Exit before detach |
| `release_video2` (`1013_124643`) | Perfect again | 39 ms | Exit before detach |
| `release_video3` (`1013_124951`) | Perfect again | 41 ms | Exit before detach |
| `release_video4` (`1013_125258`) | Perfect again | 38 ms | Detach before exit |
| `release_video5` (`1013_125615`) | Another success | 44 ms | Exit before detach |

Each last periodic record had 1,536 requests, no empty-queue waits and no frames more than
100 ms late. These remain partial counters, not full-movie totals. Detach does not require an
already-returned thread: trial 4 completed normally, with no invalid-mutex panic. Its order is
recorded rather than described as identical to the other four. A game-only frame capture in
trial 5 confirmed the character-creation menu; no desktop capture was used.

#### Downloaded CI artifact

[App CI run 38391850077](https://github.com/bloodborne-native/bloodborne-mac/actions/runs/38391850077)
passed for app `7643ae5`, launcher `cf9226d` and runtime `ae638a4`. Its downloaded artifact was
`bloodborne_mac-v0.5.1-41-g7643ae5-arm64.dmg`; this is a development package, not a new tagged
release. The DMG mounted read-only, deep/strict signature verification passed, and 13 Mach-O
executables/libraries had only system or relative library references. Bundled bash 5.3.0 and
Python 3.13.16 ran successfully.

Game tests still used `tools/test_run.sh` (the fresh-save helper delegates to it). A temporary
`BB_PROBE` wrapper re-entered the package's `run.sh` using its bundled bash, Python and probe,
with isolated writable data and the helper's isolated save/config. Its PATH contained the
bundle and system tools, and development dynamic-library overrides were removed. The running
process's executable, GPU library, Vulkan loader and MoltenVK paths were checked with `lsof`.

| Run (log stamp) | Cache / driver | Result | Observation |
| --- | --- | --- | --- |
| `release_package_video` (`1013_130102`) | Warm / development MoltenVK inherited by harness | Completed, no fault; excluded from self-contained package acceptance | Perfect |
| `release_package_video2` (`1013_130533`) | Warm / all checked binaries and rendering libraries from the DMG | Completed, normal audio exit before detach, no fault/assertion | Perfect |
| `release_package_cold` (`1013_130842`) | Empty project cache / bundled driver | Completed, normal audio exit before detach, no fault/assertion | Perfect again; compilation felt fast |

The first wrapper retained the outer harness's `VK_DRIVER_FILES`. Loaded-library inspection
caught the development MoltenVK; the repeated checks explicitly selected the bundle manifest.
That first result is recorded, but does not establish independence from development libraries.
The two corrected package openings both reached character creation, also checked with game-only
frames. Their last periodic maximum lag was 35/30 ms, with no empty waits or frames more than
100 ms late. All seven accepted openings in this batch were observed as successful playback.

The project-cache directory did not exist before the cold run. Logged compilation windows
counted 71 shader/pipeline compilations before the movie (53.1 ms), eight during it (5.8 ms),
and 68 after it at character creation (90.6 ms). These are summed frame-window counters.
macOS/Metal's own caches were retained: this covers an empty project cache on this Mac, not a
new operating-system account or an erased driver cache.

#### Packaged copied-save gameplay

`release_package_gameplay` (`1013_131232`) used an isolated copy of the existing save, the
same self-contained downloaded runtime and a 90-second untraced hold. Timing/thread probes
were off. The user reported perfect loading/playback; there was no fault or assertion.
Loaded executable and rendering-library paths again pointed inside the mounted DMG.

| Output / scene | Mean FPS | Median frame time | Frame-time p90 | Sampled game CPU | GPU utilization | GPU thread time |
| --- | --- | --- | --- | --- | --- | --- |
| 1920×1080 / 1130×636, uncapped | 47.3 | 20.5 ms | 22.9 ms | 511% of one core | 98% | 4.68 us/draw |

The frame summary covers 4,159 frames; no hold time was spent in frames over 150 ms.
GPU-thread time is the mean of the last 18 loaded-scene windows. This is a single stability
smoke check, not an A/B comparison or evidence of a speed change.

No new `ExecuteCopyShaderHLE`, SurfaceFormat or MipFilter assertion appeared in these runs,
and no movie audio-thread invalid-mutex panic recurred. Historical counts remain three
copy-shader assertions, one SurfaceFormat 6/1, one SurfaceFormat 15/4, five MipFilter assertions
and three audio-thread panics. This repeat gate is positive; it does not prove that every rare
sampler/lifetime failure is fixed. Keep those faults on the watch list.

The version remains 0.5.1 and no release was tagged. These tests exercise the packaged game
entry point through the isolation harness, not the packaged launcher's full first-setup UI or
Gatekeeper flow on a clean machine. Launcher unit/browser checks were recorded separately.
N4 remains paused until the maintainer chooses to resume it.

## Investigation boundary

Use Ghidra to understand the null pointer's callers and the state they read, then check the
runtime contracts and ordering they depend on. Analysis output, guest code, stack contents and
memory records stay outside Git and the wiki. A fix must pass the runtime tests and repeated
isolated opening-video trials before it is accepted. N4 stays paused until stability is resolved
or the maintainer chooses the release's known-issue policy.

## Packaged launcher release check (2026-10-13)

The 0.6.0 package's native launcher was opened with a fresh synthetic data directory selected
by `BB_LAUNCHER_DATA_DIR`; player saves/settings were not used. The maintainer created
“Release QA” and confirmed Home, the active profile and version 0.6.0. The synthetic SQLite
database contained the profile and had no foreign-key violations. All 39 launcher native
tests, frontend checks/build and Clippy passed. The local DMG build passed deep/strict signature
verification. This closes the first-profile/Home check on this Mac; clean-machine Gatekeeper
behavior and a complete upgrade UI on another Mac remain unverified. The runtime binaries
are the same ones used in the preceding game acceptance batch.

## Stable image storage (2026-10-13)

Ported Supermedo/bloodborne_pc `3a7cde8`, authored by João Pedro Valadares, after upstream
shadPS4. `SlotVector` previously moved live images when its capacity grew, while
`FindDepthTarget` and `ResolveDepthOverlap` retained references across an insertion. The pool
now keeps addresses fixed, and those paths reacquire references after insertion.

`bash build.sh --test` passes on macOS arm64. The normal-flow storage test forces 50,000
non-movable, 64-byte-aligned entries across 2 MiB page boundaries, checks iteration and reuse,
and verifies that every constructed object is destroyed exactly once. It also passes
AddressSanitizer and UndefinedBehaviorSanitizer. Linux CI includes the same standalone test.

The isolated copied-save run `stable_images1` held gameplay for 90 seconds: 48.2 FPS, frame-time
p90 21.8 ms, sampled game CPU 504% and GPU utilization 98%. The 18 steady-state log windows
averaged 4.35 us/draw on stage B. No fault, GPU assertion or invalid-mutex panic
appeared. Visual correctness was not independently assessed. This single smoke test is not an
A/B performance result or proof of the cause of any historical video/sampler failure.
The existing watch counts are unchanged; N4 remains paused.

## Texture upload baseline checks (2026-10-13)

Four isolated copied-save holds (`imgbase_off1`, `imgbase_on1`, `imgbase_off2`, `imgbase_on2`)
completed without faults, GPU assertions or invalid-mutex panics. The Supermedo upload-baseline
port remains opt-in because both untraced pairs favored off for FPS/p90 and stage-B time.
The whole-image fast path now excludes newer GPU buffer writes in both modes. Normal-flow
and sanitizer regression tests pass with synthetic data. The first pair's game captures
showed no visible difference; the second on capture was black near shutdown and is excluded.
This batch did not exercise character creation, opening-video playback or precise-readback
gameplay. Historical assertion counts remain unchanged; N4 remains paused. Results and limits:
[macOS performance](MACOS_PERFORMANCE.md#texture-upload-baseline-experiment-2026-10-13).

Four further copied-save holds (`hashdiag_off`, `hashdiag_on`, `hashgate_off`, `hashgate_on`)
completed without faults, GPU assertions or invalid-mutex panics. They used 45-second holds
capped at 40 FPS; counters were enabled only in the diagnostic pair. Hash-path cost and
capped CPU comparisons are recorded in
[macOS performance](MACOS_PERFORMANCE.md#hash-cost-diagnosis-and-capped-repeat-2026-10-13).
Baseline capture remains opt-in. No new opening-video or visual acceptance was performed,
and historical watch counts are unchanged.

The uncapped GPU comparison (`hashgpu_off`, `hashgpu_on`) saved both Metal traces and
completed both 45-second post-trace holds without faults, GPU assertions or invalid-mutex
panics. Complete-frame normalization showed similar encoder counts and no increase in GPU
busy time with baseline capture on. This is not visual or opening-video acceptance.
Baseline capture remains opt-in, and historical watch counts are unchanged. Full measurements:
[macOS performance](MACOS_PERFORMANCE.md#uncapped-gpu-trace-comparison-2026-10-13).

## Local trophy tracking check (2026-10-13)

The `trophies_smoke` copied-save run completed a 45-second hold without faults, GPU assertions
or invalid-mutex panics. Synthetic runtime tests verify event recording and edited-save
suppression; the game hold did not earn a trophy. Historical assertion counts are unchanged.
[Protocol, checks and limits](TROPHIES.md).

## Central Yharnam geometry after area transitions (2026-10-13–14)

The maintainer reported large stretched surfaces near the bridge's two werewolves after
visiting several areas, including transitions through the nearby house and an area with new
enemies. Restarting restored normal geometry at the same spot. The initial report was after
10–15 minutes; the isolated reproduction shows that elapsed time alone is not the trigger.
Standing still is not sufficient reproduction coverage.

The copied-save runs used English, the normal 30 FPS preset, FSR 3.1, a 960×540 startup scene
patch and 1920×1080 output, live resolution enabled, and `BB_CB_PROBE=1` with a 1,000 us
probe window. The maintainer played the transitions. No game-memory contents or captures
are included here; the game-frame dumps remain in ignored test output.

| Run | Draw pipeline | Result |
|---|---|---|
| `geom_pipe_on1` | On | Setup attempt stopped early: the direct runner omitted the launcher's startup scene patch. Excluded. |
| `geom_pipe_on2` | On at startup | Corruption reported at hold sample 424 (roughly seven minutes). Disabling the pipeline live did not repair it; also bypassing the fetch-shader and memory-region caches did not repair it. Ended early after reproduction. |
| `geom_pipe_off1` | Off from startup | Corruption reported at hold sample 138 (roughly two minutes), immediately after entering an area with new enemies. Ended early after reproduction. |

Both matched setups reproduced stretched geometry in game-frame dumps without a guest fault
or GPU assertion. The command-buffer probe reported no shared-buffer or overlapping-memory
uses. The runs followed user-driven routes and the on run included live diagnostic changes;
they are not an FPS/CPU performance comparison. Pipeline-off is not a workaround. Cache bypass
not repairing an already corrupted scene does not rule those paths out as earlier causes.

`BB_IMAGE_ALIAS_PROBE=1` adds a bounded count/size diagnostic for image-to-buffer alias
transfers. `FindImageFromRange` can accept one image whose size differs from the binding, and
the tiling copy uses the image's size inside a much larger destination allocation. This is
a candidate to investigate, not an established cause or a changed copy policy. The diagnostic
is off by default and logs no addresses or game-memory contents. `build.sh --test` passes.

The size diagnostic did not identify an oversized transfer. `geom_alias_on1` reported missing
geometry, but no confirmed vertex explosion before the window was closed; it is not a clean
15-minute acceptance run. In `geom_alias_off1`, with the pipeline off from startup, the
maintainer reported stretched scenery at hold sample 548. The latest counter at onset was
32,768 transfers, zero larger than the binding; the diagnostic also logs the first eight
oversized transfers immediately, and none were reported. The captured interior view did not
clearly show the affected scenery, so the visual classification relies on the maintainer's
confirmation. This does not exclude stale equal-sized aliases or incorrect resource lifetimes.
The owned run was stopped early after the report, not counted as a completed benchmark.

`geom_gpu_lock1` tests `BB_SUBMIT_LOCK=gpu` with the pipeline still off and the same size
probe and scene settings. It waits for GPU completion instead of releasing the submission
lock at decode. The maintainer reported recurrence around hold sample 259, more commonly when entering or
leaving dark places. A game-frame dump confirms a stretched triangle. The latest size-probe
count was 16,384 transfers with zero oversized copies; there was no new fault or GPU assertion.
The run was stopped early after reproduction. GPU-completion locking is not a workaround.
Dark-place transitions are a reproduction clue, not proof of a lighting-shader defect.

`geom_hoist_off1` next disables buffer-copy hoisting (`BB_HOIST_COPIES=0`), retaining the
pipeline-off and GPU-completion-lock settings. The off run also reproduced at hold sample 259, after
entering a place with more enemies. A game-frame dump confirms stretched scenery. The latest
size counter was 16,384 transfers, zero oversized; no fault or GPU assertion was reported.
The run was stopped early. Disabling copy hoisting is not a workaround.

`geom_texture_safe1` next disables the image-description cache, lock-free upload check,
image lookup cache, deferred uploads, texture-binding memo, image-update fast path, view memo,
texture-binding helper and texture-set memo from startup (disabled mask 35,211,215,635,936).
Other settings match `geom_hoist_off1`. This is a diagnostic group, not a proposed fix;
a positive result must be repeated against control and the group narrowed. Result pending.
No default or copy policy has changed.

### bbport vertex-explosion fixes reviewed (2026-10-14)

Upstream was fetched and remains at 0.51 (`3c57da4`). Commit
[`2ae28fe`](https://github.com/deadinside28/bloodborne_pc/commit/2ae28fe1158cabfd2695dcc30b7bcb014c68c52b)
changed the default readback mode from Disabled to Relaxed: its explanation is stale
GPU-written vertex data read by FaceGen. This commit is already an ancestor of runtime main,
and `gpu/shim/core/emulator_settings.h` still defaults to Relaxed. The controlled geometry
runs did not pass a readback override. Reapplying this setting is not a new fix.

A later direct-recording copy-order repair is also present: `SmallGuestCopy` executes a copy
immediately when recording is not deferred, before commands consuming it. Upstream
[issue 39](https://github.com/deadinside28/bloodborne_pc/issues/39) associated this problem with
`BB_VK_RECORD_THREAD=0`; the geometry runs have not selected that mode. The layer's later
stale-mirror/writeback repairs are present too, but that sparse/mirror memory path is not used
by the imported guest pool on this Mac.

The readback fix's synchronization requirement is relevant even with shared memory. Our
unified path records GPU-write completion ticks and uses them to avoid copying unfinished
GPU-written inputs into the constant stream. However, `BufferCache::ReadMemory` and
`InvalidateMemory` return early in unified mode, and unified written bindings do not take the
old memory tracker's GPU-modified marking path. Sharing bytes removes the need to copy them
back; it does not establish that the GPU finished before a guest CPU read or reuse. This is a
concrete difference to investigate, not a demonstrated cause of the stretched scenery.
Submission-level GPU locking already failed to prevent the symptom, so a fix cannot be
accepted solely on a stronger global wait. Audit per-range CPU/GPU access and completion
ordering, including alias writes, before adding a workaround. No renderer code was changed
by this review and no upstream fix is claimed newly applied.

### Unified CPU-access audit and diagnostic (2026-10-14)

Unified written buffer bindings note `WriteTicks`, but return before
`MemoryTracker::ForEachUploadRange` marks GPU-modified pages. The old tracker invokes
readback on a CPU invalidation of such a page, and Precise mode additionally protects reads.
The unified `ReadMemory` and `InvalidateMemory` paths return without either copying or waiting.
The existing tick check in `IsRegionGpuModified` affects renderer bindings; it does not itself
intercept guest CPU loads. The libc write preparation hook is conditional on existing trap
reasons, and its post-write notification is conditional on the write-watch bitmap. Simply
adding a wait to `ReadMemory` therefore cannot cover all ordinary CPU reads or writes.

`BB_UNIFIED_ACCESS_PROBE=1` counts read/write notifications that reach those unified early
returns while the range's newest recorded GPU write has not completed. It refreshes the
Vulkan timeline completion value before reporting pending work. Logs contain counts and
sizes only, no guest addresses or contents. Notifications may arrive before or after a host
write; overlaps are evidence of pending work, not proof of simultaneous stores. Direct guest
loads/stores without a notification, physical aliases with different virtual addresses, GPU
writes not included in the tick table and GPU reads are outside this diagnostic's coverage.
`build.sh --test` passed, with the existing platform skips.

`geom_unified_probe1` used: copied-save area transitions, pipeline off, ordinary
copy-hoisting/cache/submission-lock settings, with both aggregate probes enabled. No waiting
policy or default has changed. The preceding texture-shortcut run ended with a requested
window close; without a visual report, it is not clean-route acceptance.

The maintainer reported recurrence in `geom_unified_probe1`. It had completed its 900-sample
hold before the report arrived: 29.7 FPS, p90 33.3 ms, sampled CPU 426%, no new assertion or
command-buffer overlap. The onset time is unknown. At least three write notifications
coincided with unfinished writes in the coarse tick table; the logged access size was 8 bytes.
No read notifications were counted. These notifications can overlap a 64 KiB tracking
region without overlapping the GPU's declared binding, so they do not demonstrate a race.
The latest image-size count was 65,536 transfers, zero oversized. The attempted post-exit
capture is excluded: `grab.sh` could previously convert an older raw dump after its trigger
wait timed out. It now requires a new dump; the mislabeled image was removed.

The probe now separately checks a bounded, mutex-protected history of the latest 4,096 GPU
write bindings for exact byte-range overlap, and reports replaced-history entries. It refreshes
completion before counting a pending write. Exact overlap here means overlap with a declared
binding, not proof that the shader wrote every byte. A negative result can miss older history,
unnotified CPU accesses and different virtual aliases. `build.sh --test` passes.
`geom_unified_exact1` repeats the same settings with a 1,800-sample limit and the refined probe;
its result is pending. No synchronization workaround or default change has been applied.

The refined control `geom_unified_exact1` reproduced around hold sample 431 after the
maintainer broke scenery during prolonged play. A fresh game-frame dump confirms severe
stretched geometry. At the report the probe had logged seven coarse pending overlaps, all
with zero exact overlap in retained declared binding history; no CPU reads over unfinished
bindings were reported. The history had replaced over 2.8 million entries, so it cannot
exclude older or unnotified accesses. The latest size count was 16,384 transfers, zero
oversized. No new assertion or command-buffer overlap appeared. The owned run was stopped
early after reproduction. Breaking scenery joins area transitions as a useful trigger.

### Opt-in unified CPU synchronization experiment (2026-10-14)

`BB_UNIFIED_CPU_SYNC=1` requires `BB_READBACKS=2` and reuses the old memory tracker's
CPU/GPU page-state transitions for unified written buffer bindings. No upload is needed:
the GPU already writes the imported pool. A trapped CPU read/write waits for the newest
write tick, submitting an open chunk when needed, then drops the GPU-modified protection.
Guest-thread waits are sent through the GPU command thread, following the existing readback
path; a CPU write additionally marks the page CPU-modified. The switch is off by default.

The experiment also records/tracks image-to-buffer tiling writebacks. Previously, that
unified write did not enter `WriteTicks` (the old follow-up `NoteGpuWrite` only applied to
sparse guest-in-place memory). The diagnostic-only path now includes those writebacks in
its exact binding history without changing the ordinary tick table. This changes diagnostic
coverage; it is not retrospective evidence about the seven coarse overlaps above.

`build.sh --test` passes. `geom_unified_sync1` is pending, matching the pipeline-off control
with the two aggregate probes, plus the synchronization switch and Precise mode. This is a
conservative correctness experiment and may add substantial page-fault/wait cost. It is not
a proposed default or an accepted fix. Page-level tracking, virtual-address aliases, shader
writes outside declared bindings and untracked GPU read lifetimes remain limits to audit.
Normal relaxed/default behavior remains unchanged.

`geom_unified_sync1` stalled while loading, before the hold. A two-second host sample showed
preparation workers in `PageManager` → `BufferCache::ReadMemory`, awaiting GPU-thread commands
while holding the pipeline-cache lock. The GPU command thread awaited that cache lock in
pipeline refresh. This is a dependency cycle introduced by the conservative protection
experiment, not a successful geometry test. The probe had observed pending exact read binding
overlaps, but preparation-worker accesses are renderer reads, not evidence of guest FaceGen
reads. The owned run was stopped; no default changed.

`geom_unified_sync2` retries the same experiment with `BB_PREP_WORKERS=0` to remove those
speculative lock holders. Result pending. Any positive route result needs a matching
workers-off control and repeated alternation before attributing improvement to synchronization.
Do not enable the experiment with preparation workers until their read/fault contract is fixed.

`geom_unified_sync2` reached the level but stalled while teleporting to Hunter's Dream,
reported around hold sample 346. The process remained alive; no fault or GPU assertion was
logged. A two-second host sample showed the GPU command thread waiting on the texture-cache
mutex, while the game thread was inside `TextureCache::InvalidateMemory` → XXH3 hashing in
a fault handler. Two texture hash paths still read the protected guest view directly. The
sample identifies a cache-lock/protected-read hazard, not a completed geometry check. The
owned run was stopped. Do not classify this as a process crash or add it to assertion counts.

`MarkAsMaybeDirty` and the MaybeCpuDirty refresh hash now use the existing upload hash helper,
which reads the backing view in Precise mode. Ordinary Relaxed hashing remains a direct read.
The synchronization experiment now refuses to enable unless `BB_READBACKS=2`,
`BB_PREP_WORKERS=0` and `BB_DRAW_PIPE=0` are explicit. `build.sh --test` passes.
`geom_unified_sync3` retries the same settings with the hash-path repair; first repeat the
teleport before long-route acceptance. Result pending. These repairs do not establish that
shared-buffer synchronization fixes vertex explosion. A workers-off, Precise-mode control
with only `BB_UNIFIED_CPU_SYNC=0` is required if the candidate survives the route.
