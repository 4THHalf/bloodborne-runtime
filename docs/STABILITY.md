# Opening-video stability

## Status (2026-10-12)

The new-save opening video can fault before character creation in released v0.5.1 as well as
current main. N4 is paused after its first buffer-resource slice while this is investigated.
The next app release needs a verified fix or an explicit known issue. No release has been tagged.

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
buffer memory. A timing or shared-state race remains a hypothesis, not a confirmed cause.

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
guard experiment stalled. The root cause remains unresolved.

## Investigation boundary

Use Ghidra to understand the null pointer's callers and the state they read, then check the
runtime contracts and ordering they depend on. Analysis output, guest code, stack contents and
memory records stay outside Git and the wiki. A fix must pass the runtime tests and repeated
isolated opening-video trials before it is accepted. N4 stays paused until stability is resolved
or the maintainer chooses the release's known-issue policy.
