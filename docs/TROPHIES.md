# Local trophies

[← Documentation](README.md)

Trophies are recorded locally, per launcher profile. Nothing is sent to PSN, with or without the
online module.

## Recording

When the game unlocks a trophy, the runtime appends a line to `trophies-v1.log` in the active
`BB_USER_DIR` (next to `savedata/`, apart from shared caches):

```
v1 UNIX_SECONDS TITLE_ID TROPHY_ID
```

Writes are serialized, appended and synced; links and non-regular files are refused; a failed
write is logged and the game goes on. Valid ids are 0–127. The runtime prints
`trophy N unlocked`, and the launcher imports the log into its database (profile, title and id
unique; the earliest timestamp kept). Incomplete trailing lines are retried later; malformed
ones are ignored.

Platinum (CUSA03173) is awarded locally once the 33 other base-game trophies are recorded; The
Old Hunters' six are separate. Old unlocks are not inferred from a save.

## Edited saves

A file `savedata/.bb-edited` (any file there, even an invalid one) stops recording.
`BB_TROPHIES_ELIGIBLE=0` stops it too; `=1` cannot override the marker. This is not anti-cheat:
it does not detect saves edited elsewhere. The save editor (future) must write the marker before
the changed save, and keep it through imports and exports. Backups copy the marker; restoring a
clean backup restores eligibility, and earned history stays in the launcher's database. Whether
edited saves may go online is the maintainer's decision, still open.

## Notifications

`src/runtime_notifications.c` starts the launcher's notification helper (`BB_OVERLAY_HELPER`,
set by the app) on one worker thread and sends it accepted unlock ids and the game window's
position and focus every 250 ms; no pixels or game memory. Unlocks are queued only after their
line is synced. A 16-entry queue drops events older than 30 seconds; a failed helper never
affects the recorded history. Already-recorded trophies are not shown again.

The helper shows a notice only while the game's window is in front, and gives focus straight
back to the game. `BB_TROPHY_OVERLAY=0` turns notices off (the launcher's Advanced → Launcher →
Trophy notifications); one measured pair lost 1.9 FPS (44.7 against 42.8) with them on.
`BB_TROPHY_OVERLAY_PREVIEW=1` sends five labelled previews without recording anything.

The launcher's trophy catalog uses public names and original summaries; no trophy files,
artwork or keys are taken from the game.
