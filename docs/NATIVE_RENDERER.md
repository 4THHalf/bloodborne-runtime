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

## Progress roadmap

Each bar has 20 blocks, one per 5%. The percentages track completed deliverables in each step,
not a prediction of remaining development time. N1 has four deliverables; deferred clears
have passed visual, traced, and three-pair untraced checks. The fixed quad coordinate mesh is implemented
and checked in isolation, but the triangle draw path is not complete, so it does not advance N1.

| Step | Progress | Next deliverable |
|---|---|---|
| N0 · scope | 100% `████████████████████` | Complete |
| N1 · frame planning | 25% `█████░░░░░░░░░░░░░░░` | Fixed tessellation as triangles |
| N2 · shaders ahead of time | 0% `░░░░░░░░░░░░░░░░░░░░` | Inventory shader variants |
| N3 · textures as objects | 0% `░░░░░░░░░░░░░░░░░░░░` | Trace texture creation sites |
| N4 · backend interface | 0% `░░░░░░░░░░░░░░░░░░░░` | Define shared resource and command interfaces |
| N5 · Metal backend | 0% `░░░░░░░░░░░░░░░░░░░░` | Implement the interface after N4 |
| N6 · engine render boundary | 0% `░░░░░░░░░░░░░░░░░░░░` | Map engine calls after R4 |

N1 acceptance gates:

1. **Done:** defer eligible color clears to render-pass load actions; compare frames and Metal
   traces with the old path.
2. **Next:** replace only eligible fixed, one-control-point quad tessellation with ordinary
   triangles. Detect the factor pattern from the hull shader, carry local-shader attributes and
   domain-shader resources into a vertex shader, expand indexed patches, and compare the image
   and trace against tessellation. Keep the old path for every unmatched shader and as an off
   switch.
3. **Then:** move or defer copies that still break render passes; keep dependency checks for
   source and destination ranges. Simple grouping by buffer pair showed no gain.
4. **Finish:** repeat alternating game runs on both backends where available, make each proven
   path the default, and keep rollback switches.

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
It is on by default, with `BB_DEFER_CLEARS=0` as the off switch. The first four alternating
traced runs in the clinic (off/on/off/on) measured 46.7/47.3/44.6/47.9 FPS. Separate color-clear
encoders fell from 17.8/16.8 to 14.7/13.6 per frame. The on traces had variable idle between
command buffers (1.6 and 3.7 ms, against 0.8 and 1.3 ms off), so an untraced check was needed.

`BB_DEFER_CLEAR_REASONS=1` counts why a pending clear was recorded. At 16,384 clears, the first
diagnostic run took 3,469 as load actions and recorded 12,908. Of those, 7,399 were blocked by
the upscaler's broad enabled check, 3,752 by image access and 1,757 by copies. Testing the actual
color redirect instead raised taken clears to 10,815 and reduced separate clears to 5,564 at the
same count. The remaining reasons were 3,761 shader reads and 1,803 copies; those uses need the
clear recorded first. View, render-area, multi-level/layer, reduced-pass and backing-swap reasons
were zero in this scene. The separate-clear counter is sampled while a few clears remain pending.

Six alternating runs without Metal tracing, with 90-second level holds, measured 46.7/47.8,
46.4/47.5 and 47.2/49.1 FPS (off/on pairs). Frame-time p90 was 22.4/21.9, 22.6/22.1 and
22.2/21.2 ms. Game CPU use was 494/505, 496/498 and 498/503% (100% is one core). On improved
FPS and p90 in each pair, while CPU use averaged about six percentage points higher. Game-frame
captures at the same position showed no visible difference. Deferred clears remain enabled by
default; the CPU difference warrants watching in later tests.
The tessellated light draws are the next N1 target. The observed shaders use one control point,
pass its attributes through the hull stage, and use fixed integer quad factors (outer 3, inner
1, or all 1). A triangle path must preserve the domain shader's coordinates, winding and
resources while moving its work into a vertex stage. This needs a shader-stage rewrite; changing
only the draw topology would change the image. The [Vulkan tessellation rules](https://docs.vulkan.org/spec/latest/chapters/tessellation.html)
allow implementation-dependent triangle connections; visual comparison is required for the
prebuilt mesh.

The shader recompiler now recognizes the two fixed hull factor variants by their operations,
stores the result in shader metadata, and can emit the three observed domain shader variants as
vertex SPIR-V modules for diagnosis (`BB_FIXED_QUAD_PROBE=1`). All three modules passed
`spirv-val` for Vulkan 1.3. A shared quad mesh supplies the coordinates for both factor pairs,
with a test for triangle count, winding and covered area. The renderer does not select these
modules yet: local-shader output attributes must be carried into the vertex inputs and indexed
patches must be expanded before a visual or performance comparison is valid.

The existing copy hoist already batches copies before a pass. Grouping its non-overlapping
regions by source and destination buffer was tested but not retained. In three usable Metal
traces, `vkCmdCopyBuffer` encoders were 59.4 and 54.8 per frame without grouping, and 54.5 with
it; the runs varied more than the apparent reduction. The level holds were 46.7 and 48.6 FPS
without grouping, and 46.3 and 47.7 FPS with it (the latter trace failed). Further copy work
needs to reduce render-pass breaks or measured idle, not just command calls.

N3 hooks the engine's own functions. With the recompiled library loaded, recompiled functions call
each other directly and skip hooks, so the recompiler first has to send calls to hooked functions
through the runtime (bloodborne-recomp).
