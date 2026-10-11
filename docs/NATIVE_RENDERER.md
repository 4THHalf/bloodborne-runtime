# Native renderer

> [!NOTE]
> [Documentation](README.md). The CPU side of the native port (the recompiler) is in
> [bloodborne-recomp](https://github.com/bloodborne-native/bloodborne-recomp/blob/main/docs/RECOMPILATION.md);
> this is its milestone R5.

Goal: the engine's graphics drawn with each platform's own GPU API, with nothing translated while
the game runs: no MoltenVK on macOS, no shaders compiled during play, no PS4 hardware emulated.
The renderer stays multiplatform: one core, shared by every platform, and thin backends for
Vulkan (Windows, Linux) and Metal (macOS).

## Principles

1. **Multiplatform.** Nothing works on Metal only. Planning a frame, tracking resources and
   converting shaders happen in the shared core; a backend only speaks its API.
2. **Always playable.** Every step is behind a switch, with today's renderer (shadPS4's, through
   MoltenVK on macOS) as the fallback until the new path draws everything.
3. **Measured.** Each step is measured in GPU time per frame (Metal System Traces on macOS) and in
   CPU time, against the path it replaces.
4. **Ahead of time where possible.** Like the CPU code, shaders and texture layouts are converted
   once, on the player's machine, from their own copy of the game.

## What was measured (2026-10-11)

**Where the command stream comes from.** Bloodborne links libGnm statically: the game itself writes
the PS4 command packets (PM4), through the libGnm command buffer methods. bbport hooked every one
of them (`BB_GNM_OBSERVE=1`, on Linux): 100% of the packets the game submits come from 99 known
libGnm methods. The packets are a lossless record of the API calls, so reading them is equivalent
to intercepting the calls ([upstream's notes](EMULATION_REMOVAL_PLAN.ru.md), in Russian). The
front end stays the packet reader; there is no need for a second one at the libGnm calls.

**Where a frame's time goes on macOS** (Hunter's Dream, M3 Pro, ~48 FPS during the trace: 311
frames of a Metal System Trace and a 10 s CPU sample):

| GPU, per frame | |
|---|---|
| Busy | 14.9 ms |
| Idle inside command buffers (between encoders) | 4.9 ms |
| Idle between command buffers | 1.0 ms |
| Metal encoders | ~420 |
| of which: separate clear passes | 18 (0.6 ms of work) |
| of which: compute passes for tessellated draws (MoltenVK runs the vertex and hull stages as compute) | 47 (0.3 ms of work) |
| of which: buffer copies | 61 (0.5 ms of work) |

Every clear, tessellated draw and copy between draws ends a render pass; on Apple's tile-based
GPUs each new pass reloads and stores its attachments, and the GPU idles between encoders. The
idle time (5.9 ms, 28% of the frame) is the largest single item.

| CPU, cores busy | |
|---|---|
| The game's code | ~3.8 |
| The renderer: reading packets, preparing and recording draws | ~1.2 |
| MoltenVK and the Metal driver, recording | ~0.6 |
| System threads (submission, driver work queues) | ~1.0 |

The game is GPU-bound, so the CPU numbers matter for power and heat; the frame rate is in the
GPU's idle time and in what each pass costs.

## Design

- **Front end (shared):** the packet reader (shadPS4's command processor and bbport's parallel
  draw preparation), unchanged.
- **Frame planning (shared):** the core sees a frame's passes before recording them, and
  merges what splits them: clears become the next pass's load action, constant tessellation
  becomes ordinary triangles, copies move out of passes and are batched. It works for every
  backend.
- **Resources (shared):** shadPS4's texture and buffer caches and detiling, with the
  Vulkan-specific parts behind the backend interface.
- **Shaders (shared):** GCN → shadPS4's intermediate form → SPIR-V (Vulkan) or MSL (Metal), ahead
  of time.
- **Backends:** Vulkan (today's code, moved behind the interface) and Metal. D3D12 is possible
  later. Unleashed Recompiled's render layer (plume, MIT, with Vulkan, D3D12 and Metal backends)
  is to be evaluated before writing our own interface.

## Steps

| | Step | Platforms | Status |
|---|---|---|---|
| N0 | Scoping: where the command stream comes from, where a frame's time goes | all | Done (2026-10-11) |
| N1 | Frame planning in today's renderer: clears as load actions, constant tessellation as triangles, copies batched | all | In progress: deferred clears done (2026-10-11) |
| N2 | Shaders ahead of time: every shader in the game's files converted at install (SPIR-V; MSL with N5) | all | |
| N3 | Textures as objects: created when the game creates them (libGnm texture setup, the engine's texture loader), detiled once at install | all | |
| N4 | The backend interface: the Vulkan-specific code of the renderer behind it | all | |
| N5 | The Metal backend, replacing MoltenVK | macOS | |
| N6 | The engine's own render layer as the boundary (needs R4's names and structures) | all | |

N1 to N3 also make today's renderer faster on every platform; N4 and N5 remove MoltenVK.

The first N1 change defers color clears until their next use. A pass that draws over the full
cleared image uses a clear load action; other uses record the clear before accessing the image.
It is on by default, with `BB_DEFER_CLEARS=0` as the off switch. Four alternating traced runs in
the clinic (off/on/off/on, same save and position) measured 46.7/47.3/44.6/47.9 FPS during the
level hold. Separate color-clear encoders fell from 17.8/16.8 to 14.7/13.6 per frame. Idle
inside command buffers was 5.0/4.7 ms off and 4.7/4.3 ms on. The on traces had variable idle
between command buffers (1.6 and 3.7 ms, against 0.8 and 1.3 ms off); their total GPU time is
not a stable estimate of this change. Game-generated frames at the same position looked alike.
The next N1 items are the tessellated light draws and buffer-copy batching.

N3 hooks the engine's own functions. With the recompiled library loaded, recompiled functions call
each other directly and skip hooks, so the recompiler first has to send calls to hooked functions
through the runtime (bloodborne-recomp).
