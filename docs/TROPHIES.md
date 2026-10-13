# Local trophy tracking

Unlock calls are recorded in `trophies-v1.log` under the active `BB_USER_DIR`, separate from
`savedata/` and shared shader caches. Each complete line is `v1 UNIX_SECONDS TITLE_ID TROPHY_ID`.
The title comes from runtime game configuration. Valid ids are 0–127 in this local service;
invalid ids return its existing invalid-argument code. Writes are serialized, appended and
synced. File links and non-regular event files are rejected. A failed write is logged without
aborting the game. The launcher imports into the existing profile/title/id unique key and
retains the earliest timestamp. Incomplete trailing records retry at refresh; malformed records
are ignored. Read failures preserve existing database history and report an import error.

A marker at `BB_USER_DIR/savedata/.bb-edited` disables recording. Any marker counts, even an
invalid marker or link. `BB_TROPHIES_ELIGIBLE=0` also disables recording; `=1` cannot override
the marker. Missing markers mean eligible in the current system. This does not detect external
save editing or provide an anti-cheat boundary. The future editor must publish the marker
before changed save contents, retain it through imports/exports, and warn before applying edits.
Launcher backups already copy the complete subtree, including the marker. Restoring a clean
pre-edit backup restores its own eligibility; earned profile history remains in SQLite.

Trophy imports already stay local in `runtime_net_symbol`, including with `BB_ONLINE=1`.
The same gate applies in either mode. No online eligibility restriction is imposed: the
maintainer chose to defer that policy. This adds no PSN trophy synchronization or macOS online
support. The online module remains unavailable in the macOS build.

The launcher shows recorded counts and names/grade/requirement/date details on Home, Profiles
and Saves, separating Bloodborne from The Old Hunters. Its catalog uses public sources; no
metadata or artwork is extracted from the dump. Verified translations fall back to English.
The local runtime awards platinum for CUSA03173 after all 33 other base-game trophies are
recorded, excluding the six DLC trophies. This grouping follows the public Windows-fork
mapping, credited in CREDITS.md. Unsupported title IDs keep numeric details.
It does not infer old unlocks from a save.
Real trophy earning has not been exercised in this development check.

## Checks (2026-10-13)

`build.sh --test` passes on macOS arm64. Synthetic calls through the trophy import check valid
recording, invalid ids, edited markers, environment suppression, override resistance and
refusal to follow an event-file link. Launcher tests check duplicate/earliest timestamps,
incomplete-record retry, profile isolation, edited/clean backup restoration, preservation of
history, deleted-profile event cleanup and nonfatal read errors. All 44 native launcher tests and two helper tests
pass, as do Clippy, frontend checks and build. Browser DOM checks use synthetic data for the
Home count, empty modal, edited warning and unlock/date details; no screenshots were taken.

One untraced isolated copied-save run, `trophies_smoke`, completed a 45-second hold without
faults, GPU assertions or invalid-mutex panics: 46.0 FPS, p90 24.2 ms, game CPU 508%, GPU
utilization 97% and stage-B 4.79 us/draw (last eight log windows). This is a smoke check, not
a performance comparison or proof of an actual gameplay unlock. Historical assertion counts
are unchanged. N4 remains paused.

## Runtime notifications

`runtime_notifications.c` owns one host worker and a standalone launcher-owned helper.
`BB_OVERLAY_HELPER` selects the absolute executable path; `BB_TROPHY_OVERLAY=0` disables
notifications without stopping trophy persistence. The macOS app configures its packaged
sibling helper. Spawn, the 20-second readiness budget, nonblocking pipes and display pacing run
on the worker. Guest unlock calls only queue IDs after successful append/fsync. Startup history
suppresses repeated calls across sessions; unreadable history suppresses notifications rather
than claiming an unseen achievement. No guest or renderer lock is held during spawn.

The 16-entry display queue expires stale events after 30 seconds. Full queues or helper failure
leave durable history intact. SIGPIPE is blocked only on the notification worker. The helper
exits on stdin EOF. SDL window metadata supplies positions/focus every 250 ms; no pixels or
guest memory are read. Wayland notification placement is disabled. Windows integration remains
planned; this bridge has Windows stubs. The launcher overlay doc records platform/test limits.

`BB_TROPHY_OVERLAY_PREVIEW=1` sends five clearly labelled previews through the same host bridge;
it does not generate persisted unlocks. The normal test flow now covers duplicate suppression
including restart, base-only platinum and a fake helper’s readiness, geometry and accepted-event
delivery. Existing edited-save, write-failure and link-refusal checks remain.

## Notification game checks (2026-10-13)

Four English, untraced, isolated copied-save holds alternated notifications off/on. The
windowed pair kept about 1,268 draws per frame and is the valid performance comparison.
The fullscreen pair verified visibility, but development frontend activity affected its
off run and the on run varied from 823 to 1,340 draws per frame. Do not infer an FPS
gain from that pair.

| Run | Mode | FPS | p90 ms | Game CPU % | Stage-B us/draw |
|---|---|---:|---:|---:|---:|
| `trophy_live_off1` | Windowed, off | 44.7 | 25.3 | 498 | 4.83 |
| `trophy_live_on1` | Windowed, on | 42.8 | 26.6 | 491 | 5.08 |
| `trophy_live_off2` | Fullscreen, off (excluded) | 45.8 | 24.6 | 500 | 4.81 |
| `trophy_live_on2` | Fullscreen, on (visual only) | 50.7 | 22.9 | 491 | 5.27 |

The valid pair lost 1.9 FPS (4.3%) and added 1.3 ms to p90 with the helper. All four
runs completed without a fault or GPU assertion. The five fullscreen previews were
visually reported as readable, including grades and The Old Hunters. These were synthetic
previews, not natural unlocks, and did not alter the trophy log. Notifications remain
off by default; persistence and the launcher view remain active.

A later `trophy_focus_final` isolated hold completed at 46.7 FPS and p90 23.2 ms
without a fault, but the chat app remained frontmost in all 40 window samples. The
helper sent no previews, as designed for an unfocused game. This run is inconclusive
for notification focus and is not a matched performance comparison.

A second focus check, `trophy_focus_active`, completed at 46.9 FPS and p90 22.5 ms
without a fault. The helper sent two previews late in the run; VS Code remained
frontmost in all 36 metadata samples. This does not establish game-focused behavior
or a performance change.
