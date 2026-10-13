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

The launcher shows recorded counts and id/timestamp details on Home, Profiles and Saves.
It does not infer old unlocks from a save, embed a trophy catalog, or award a derived platinum.
Real trophy earning has not been exercised in this development check.

## Checks (2026-10-13)

`build.sh --test` passes on macOS arm64. Synthetic calls through the trophy import check valid
recording, invalid ids, edited markers, environment suppression, override resistance and
refusal to follow an event-file link. Launcher tests check duplicate/earliest timestamps,
incomplete-record retry, profile isolation, edited/clean backup restoration, preservation of
history, deleted-profile event cleanup and nonfatal read errors. All 43 native launcher tests
pass, as do Clippy, frontend checks and build. Browser DOM checks use synthetic data for the
Home count, empty modal, edited warning and unlock/date details; no screenshots were taken.

One untraced isolated copied-save run, `trophies_smoke`, completed a 45-second hold without
faults, GPU assertions or invalid-mutex panics: 46.0 FPS, p90 24.2 ms, game CPU 508%, GPU
utilization 97% and stage-B 4.79 us/draw (last eight log windows). This is a smoke check, not
a performance comparison or proof of an actual gameplay unlock. Historical assertion counts
are unchanged. N4 remains paused.
