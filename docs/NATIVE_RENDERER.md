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
  merges what splits them: eligible clears become the next pass's load action. Tessellation
  and copies were measured here; their remaining costs move to the appropriate backend work.
- **Resources (shared):** shadPS4's texture and buffer caches and detiling, with the
  Vulkan-specific parts behind the backend interface.
- **Shaders (shared):** GCN → shadPS4's intermediate form → SPIR-V (Vulkan) or MSL (Metal), ahead
  of time.
- **Backends:** Vulkan (today's code, moved behind the interface) and Metal. D3D12 is possible
  later. Unleashed Recompiled's render layer (plume, MIT, with Vulkan, D3D12 and Metal backends)
  was evaluated for N4; the decision and gaps are below.

## Progress roadmap

Each bar has 20 blocks, one per 5%. Percentages track the accepted scope of each step, not
remaining time. N1 is closed: deferred clears are accepted, fixed-quad triangles were measured
and left opt-in, and extra copy grouping showed no gain. N4 has a design proposal for review;
renderer implementation has not started.

| Step | Progress | Next deliverable |
|---|---|---|
| N0 · scope | 100% `████████████████████` | Complete |
| N1 · frame planning measurements | 100% `████████████████████` | Closed; native tessellation planned for N5 |
| N2 · shaders ahead of time | 0% `░░░░░░░░░░░░░░░░░░░░` | Inventory shader variants |
| N3 · textures as objects | 0% `░░░░░░░░░░░░░░░░░░░░` | Trace texture creation sites |
| N4 · backend interface | 0% `░░░░░░░░░░░░░░░░░░░░` | Review design, then move Vulkan behind it |
| N5 · Metal backend | 0% `░░░░░░░░░░░░░░░░░░░░` | Implement the interface after N4 |
| N6 · engine render boundary | 0% `░░░░░░░░░░░░░░░░░░░░` | Map engine calls after R4 |

N1 decisions:

1. **Accepted:** eligible color clears use render-pass load actions by default; `BB_DEFER_CLEARS=0`
   restores the old path.
2. **Measured; solved natively in N5:** the opt-in fixed-quad triangle path removes MoltenVK's
   compute stage for eligible light draws, but raises game CPU use and worsens p90. Keep
   `BB_FIXED_QUAD_TRIANGLES=1` opt-in; do not develop a GPU expansion path.
3. **Closed without a change:** grouping buffer copies did not improve the measured trace or
   level hold. Leave the current copy hoist intact.
4. **Next platform work:** preserve the accepted Vulkan behavior while moving renderer operations
   behind N4; validate native Metal tessellation in N5.

## Steps

| | Step | Platforms | Status |
|---|---|---|---|
| N0 | Scoping: where the command stream comes from, where a frame's time goes | all | Done (2026-10-11) |
| N1 | Measure frame planning changes in today's renderer | all | Closed (2026-10-12): clears accepted; tessellation moved to N5; copies unchanged |
| N2 | Shaders ahead of time: every shader in the game's files converted at install (SPIR-V; MSL with N5) | all | |
| N3 | Textures as objects: created when the game creates them (libGnm texture setup, the engine's texture loader), detiled once at install | all | |
| N4 | The backend interface: the Vulkan-specific code of the renderer behind it | all | Design proposed for review; no renderer code yet |
| N5 | The Metal backend, replacing MoltenVK | macOS | |
| N6 | The engine's own render layer as the boundary (needs R4's names and structures) | all | |

N2 and N3 can improve every backend; N4 and N5 remove MoltenVK on macOS.

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
default; the CPU difference warrants watching in later tests. A later three-second default
Metal trace in the clinic covered about 168 frames at 47.3 FPS: `vkCmdClearColorImage` used
11.9 separate encoders per frame (0.33 ms of GPU work). This is a fresh observation of the
accepted default, not a paired estimate of the upscaler fix's effect.

The tessellated light draws were the next N1 measurement. The observed shaders use one control point,
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
with a test for triangle count, winding and covered area. With `BB_FIXED_QUAD_TRIANGLES=1`,
the renderer accepts only the recognized one-control-point
quad shaders, maps simple local-shader outputs to vertex inputs, expands CPU-readable indexed
patches, and draws the fixed coordinate mesh as triangles. Unsupported input formats, GPU-written
inputs, other local-shader effects, or instance offsets use the original tessellation pipeline.
This is an experiment and remains off by default.

Two alternating untraced 60-second clinic pairs measured 45.94/47.21 and 47.86/49.38 FPS
(off/on). Frame-time p90 was 23.76/25.22 and 22.16/22.42 ms; game CPU use was 499.8/529.9
and 503.4/541.7%. A later per-patch, instanced input path reduced upload size, but a short
on run still used 545.8% CPU. An 8-second off/on call-stack sample did not isolate the extra
CPU use. Game-frame captures looked the same at the tested position; a diagnostic run converted
all 65,536 sampled eligible draws without fallback.

A valid three-second Metal trace pair at the clinic (47.3/50.1 FPS during the level holds,
about 168/186 frames in the GPU interval spans) measured 27.4/0 light-draw tessellation compute
encoders per frame, or 0.188/0 ms of their compute work. Other tessellation stayed at
22.0/22.0 compute encoders per frame. The light-target render-pass class fell from 75.5 to
20.7 encoders per frame. Idle inside command buffers was 5.4/4.1 ms per frame; idle between
buffers was 0.6/1.2 ms, for 6.0/5.3 ms total. The observed 0.7 ms total difference is one
traced pair, with a worse between-buffer component, not a stable FPS or idle gain. The first
on attempt failed during loading on an `ExecuteCopyShaderHLE` assertion; its retry completed.
The watched `Unknown data_format=6 and num_format=1` assertion did not occur.
`xctrace` reached its three-second time limit but spent over a minute finalizing the trace;
the earlier interrupted five-second capture lacked a template and could not be exported.
A trial with `--window 3s` retained only about 0.1 s of GPU intervals and was discarded.

This tessellation cost exists on MoltenVK: it runs vertex and hull stages as a compute pass.
[Vulkan executes tessellation control and evaluation stages natively](https://docs.vulkan.org/spec/latest/chapters/tessellation.html)
on Windows and Linux. In N5, Metal can draw these fixed-factor patches with its tessellator,
a constant tessellation-factor buffer, and the domain shader as a post-tessellation vertex
function, without a per-draw factor compute pass ([Apple's tessellation guide](https://developer.apple.com/library/archive/documentation/Miscellaneous/Conceptual/MetalProgrammingGuide/Tessellation/Tessellation.html)).
That backend path still needs validation for patch attributes, winding, factor layout and
shader resources. Stop the triangle experiment here; keep it opt-in and do not build GPU-side
expansion. N1's tessellation item is measured; the native solution belongs to N5.

The existing copy hoist already batches copies before a pass. Grouping its non-overlapping
regions by source and destination buffer was tested but not retained. In three usable Metal
traces, `vkCmdCopyBuffer` encoders were 59.4 and 54.8 per frame without grouping, and 54.5 with
it; the runs varied more than the apparent reduction. The level holds were 46.7 and 48.6 FPS
without grouping, and 46.3 and 47.7 FPS with it (the latter trace failed). Further copy work
needs to reduce render-pass breaks or measured idle, not just command calls.

## N4 backend interface design (proposal for review, 2026-10-12)

This is a design review boundary. No renderer implementation has started. The inventory covers
`renderer_vulkan/` and the buffer and texture caches that own Vulkan resources outside it. The
existing Vulkan path remains the reference implementation while its calls move behind the
interface. No Metal-specific type or policy belongs in the shared core.

### Vulkan use in the current code

| Need | Current owner and Vulkan use | Boundary to expose |
|---|---|---|
| Device and memory | `vk_instance.cpp` chooses features, queues, VMA and MoltenVK extensions; `buffer_cache/buffer.cpp` allocates ordinary or sparse buffers, imports host memory on macOS and obtains buffer device addresses; `texture_cache/image.cpp` creates VMA images. | Device capabilities; buffers and images with explicit memory backing and address support. Keep sparse binding, imported host pointers and MoltenVK residency inside the backend. |
| Views and sampling | `texture_cache/image_view.cpp` creates image views; `texture_cache/sampler.cpp` creates samplers; `vk_runtime.cpp` and the upscaler use image handles directly. | Opaque views with exact mip/layer/aspect ranges, samplers, and image operations. |
| Graphics and compute pipelines | `vk_pipeline_cache.cpp`, `vk_graphics_pipeline.cpp` and `vk_compute_pipeline.cpp` compile/cache SPIR-V modules, create pipeline layouts, set dynamic state and choose tessellation stages. | Shader modules with stage and reflection, pipeline descriptions and backend cache keys; patch topology and control points remain expressible. |
| Descriptors | `vk_graphics_pipeline.cpp`, `vk_compute_pipeline.cpp`, `vk_resource_pool.cpp` and `vk_rasterizer.cpp` allocate or push descriptor sets and bind sampled/storage images, buffers and samplers. | Flat, typed binding slots per shader stage and draw/dispatch, including storage buffers/images, offsets and ranges. |
| Passes and commands | `vk_rasterizer.cpp` builds target views, render area and deferred-clear load actions; `vk_scheduler.cpp` queues `Record` lambdas taking `vk::CommandBuffer`, with copied `RecordData` sidecars, then recorder threads run them; `vk_runtime.cpp` records copies, resolves and transitions. `RecordOrdered` also orders host work. | A compact POD command stream and immutable sidecars, produced by the core and translated to native commands on recorder threads. Pass commands carry attachment load/store actions, views and render area. Keep ordered host work separate from GPU commands. |
| Hazards | `texture_cache/image.cpp` tracks Vulkan image layouts and subresources; `vk_runtime.cpp`, `vk_rasterizer.cpp`, `vk_presenter.cpp` and `vk_scheduler.cpp` emit image/buffer barriers. `vk_barrier_tracker.cpp` tracks overlapping access ranges. | Core declares per-pass and per-command read/write access over exact image subresources or buffer byte ranges. Vulkan owns layout state, layout transitions and barrier selection/emission; Metal owns its synchronization. Only API-neutral dependency ordering can remain shared. |
| Queue and presentation | `vk_scheduler.cpp` owns command recording segments and queue submit; `vk_semaphore.cpp` owns timeline waits; `vk_resource_pool.cpp` recycles command and descriptor pools; `vk_presenter.cpp` and `vk_swapchain.cpp` acquire, present and wait with semaphores/fences. | Recorder/queue submission, timeline-style completion values, swapchain acquire/present and retirement. |
| Queries and diagnostics | `vk_timestamps.cpp` creates timestamp pools and reads results; `vk_gpu_profiler.cpp`, `vk_breadcrumbs.cpp` and `vk_gpu_labels.cpp` record diagnostics. | Optional timestamp and marker capabilities with explicit unsupported results. |

The shader recompiler emits SPIR-V and Vulkan binding layouts today; N2 must also produce MSL
and equivalent resource reflection. N4 must not turn one backend's descriptor numbering or image
layout enum into the shared shader contract.

### Plume evaluation

Inspected [renderbag/plume at `b187abd`](https://github.com/renderbag/plume/tree/b187abdb57dc5822b35bcd6bfad138eadf0d56b2)
(MIT, with Vulkan, D3D12 and Metal implementations). Its
[public interface](https://github.com/renderbag/plume/blob/b187abdb57dc5822b35bcd6bfad138eadf0d56b2/plume_render_interface.h)
and [types](https://github.com/renderbag/plume/blob/b187abdb57dc5822b35bcd6bfad138eadf0d56b2/plume_render_interface_types.h)
are a useful model for resource lifetime, command lists, queues and capability reporting.

| Requirement | Fit in this revision |
|---|---|
| Buffers, textures, views, storage resources, descriptors, copies, barriers, queues, swapchain | Covered by device, command list, descriptor and queue APIs. |
| Buffer device address and timestamp queries | Exposed (`getDeviceAddress`, query pool and `writeTimestamp`), subject to capability checks. |
| Dynamic rendering with explicit load actions | Partial. `setFramebuffer` plus clear calls allow plume's Metal backend to choose clear-on-load internally, but the public API does not express a load/store action per attachment, exact render area or a dynamic rendering pass. Its Vulkan backend uses `vkCmdBeginRenderPass`. This is insufficient for the accepted deferred-clear path without extending the contract. |
| Tessellation | Missing. The public shader stages, pipeline topology and draw calls have no patch/control/evaluation stages or patch draw. N5 needs a constant factor buffer and post-tessellation vertex function for eligible fixed patches; Vulkan still needs native tessellation. |
| Sparse or imported host memory | Missing from public resource creation and queue binding. The current runtime needs sparse arenas on some Vulkan devices and host-imported guest memory on macOS. |
| MoltenVK during transition | No cross-backend interop is required: one backend owns the frame from startup. MoltenVK residency and imported-memory details stay in Vulkan. |
| Subresource barriers and exact image ranges | Partial. Texture views select subresources, but `RenderTextureBarrier` addresses a whole texture and has only a layout, not mip/layer/aspect and access scopes. |
| Recording cost | Its command-list API is useful as a shape for explicit work, but this migration needs POD commands translated in batches on the existing recorder threads, without a virtual call per command. |

**Approved direction:** write a narrow project-owned interface in plume's explicit, capability-
reported style. Do not adopt plume unchanged or fork its three backends: the missing pass,
tessellation and guest-memory contracts cut across its public API and implementations. Reuse
ideas, not source code, and keep the Vulkan adapter over the current renderer while N4 lands.
The detailed contract below is awaiting review before renderer code is written.

### Proposed contract

- `Device` reports capabilities and creates opaque, generation-checked `Buffer`, `Image`,
  `ImageView`, `Sampler`, `ShaderModule`, `Pipeline` and `QueryPool` handles. Descriptions include
  usage, formats, sample counts and precise view ranges. A memory-backing service provides
  ordinary, sparse and host-imported resources where supported; device addresses are an optional
  capability. Vulkan's MoltenVK residency sets stay inside that backend.
- **The recording boundary is a compact, backend-neutral POD command stream.** The core encodes
  fixed-size opcode records for pass begin/end, binding changes, dynamic state, draw (including
  patches), dispatch, copy, resolve, timestamps and labels. Variable-length attachment, region
  and binding arrays live in immutable sidecar storage addressed by offset and count. Records
  contain typed resource handles and values, not native handles, pointers or captured lambdas.
  Chunks preserve producer order and pin referenced resources until GPU completion. The existing
  recorder threads translate whole chunks with one backend selection at startup and a direct
  opcode switch; there is no per-command virtual dispatch. Vulkan emits `vk::CommandBuffer`
  operations, while N5's Metal backend emits native encoders. Ordered host actions such as
  `RecordOrdered` remain in a separate sequenced lane with explicit synchronization points;
  they are not serialized into GPU POD records.
- **Access is part of the stream.** Each pass declares attachment and shader read/write uses;
  each command that touches memory declares its read/write use with the exact image aspect,
  mip and layer range or buffer byte range, plus the pipeline usage needed for synchronization.
  The core may use these declarations to order work. The Vulkan backend owns the image layout
  tracker now in `texture_cache/image.cpp`, chooses layouts and emits image/buffer barriers.
  Metal translates the same access declarations into its own synchronization and encoder
  boundaries. No Vulkan layout enum or barrier command appears in the shared stream.
- **Bindings are flat, typed slots per draw or dispatch and shader stage.** Each binding record
  names buffer (including offset and range), texture view, storage image and sampler slots directly.
  This matches today's Vulkan push descriptors and Metal's direct buffer/texture/sampler slots;
  it avoids a shared descriptor-table or argument-buffer update and a per-command dispatch cost.
  The producer can encode only changed slots within a chunk. The backend validates slot limits
  and keeps descriptor allocation or native binding calls private. Resource arrays occupy
  consecutive slots; a shader that exceeds a backend limit needs an explicit capability path
  or fallback, never a silent remap. In N2, the shader manifest assigns these logical slots
  before SPIR-V and MSL generation. SPIRV-Cross's
  [`MSLResourceBinding` mapping](https://github.com/KhronosGroup/SPIRV-Cross/blob/main/spirv_msl.hpp)
  maps each SPIR-V set/binding and stage to `[[buffer(n)]]`, `[[texture(n)]]` and
  `[[sampler(n)]]`; combined samplers split into texture and sampler slots. Start with
  `argument_buffers = false`, reserve backend slots for push constants and internal data, and
  verify reflected slot ranges and array collisions against the emitted MSL. Argument buffers
  are a later backend extension only if a measured shader requires them; they are not the
  shared binding model.
- `Queue` submits translated chunks with waits/signals and an ordered completion value;
  swapchain acquire/present and resource retirement use that value. A `ShaderPackage` pairs
  backend binaries (SPIR-V or MSL) with stage and binding reflection. Native patch support is
  reported as a capability. The core sees no `Vk*`, `vk::*` or `MTL::*` types.
- **Backend selection occurs once at startup.** Vulkan, including MoltenVK on macOS, remains the
  fallback while Metal is incomplete. There is no mixed-backend frame or private interop
  adapter; each backend owns its resources, recording, residency and presentation.

### Migration order and measurements

At **every** migration boundary, build and run the current Vulkan path, and compare alternating
baseline/change runs at the same game location: FPS, frame-time p90, stage-B CPU time per draw
(the log's `GPU thread us/draw`), and game CPU % (100% is one core). Parse stage-B time from the
frame-stat log; the current bench summary omits it. Keep draw counts with these numbers so the
per-draw cost is interpretable. Check game frames for visual differences and
collect a Metal trace when an encoder or GPU-idle claim is made. Record regressions and negative
results. Linux must compile at every boundary, even when only macOS can be measured locally.

1. Capture the pre-migration Vulkan baseline: build/tests, the game metrics above, and a Metal
   trace on macOS (busy/idle, encoder classes, and the current 11.9 separate clear encoders per
   frame). Preserve this run as the reference for each later slice.
2. Define handles, capabilities and resource descriptions; wrap today's Vulkan allocation,
   views, samplers, sparse/host-imported memory and device addresses. Keep current recording and
   queue behavior under the wrapper; measure before proceeding.
3. Replace `Scheduler::Record`'s `vk::CommandBuffer` lambdas and `RecordData` sidecars with the
   POD stream and offset-based sidecars. First lower each existing operation to equivalent Vulkan
   commands on the current recorder threads, keeping submission order and `RecordOrdered` host
   actions. Then move pass/load descriptions, copies, resolves, bindings and exact access
   declarations into the stream. Move image layout state out of `texture_cache/image.cpp` and
   barrier emission into the Vulkan translator; compare pass/clear counts and the CPU gates after
   each usable slice. This is the central N4 migration, not a new synchronous command path.
4. Move pipeline creation and shader-module selection behind the interface. Preserve the Vulkan
   shader cache, flat push-descriptor behavior and native tessellation; compare compile and
   encoder counts as well as the common gates.
5. Move submission, timeline waits, query/marker and presentation behind queue and swapchain
   interfaces. Validate ordering and frame pacing; retire direct Vulkan calls from shared code
   only after the Vulkan backend matches the baseline.
6. In N5, implement Metal against the same stream and access contract: resources and simple
   passes first, then shaders/pipelines and native fixed-factor tessellation. Select Metal at
   startup only when its required capabilities and implementation are complete; otherwise select
   the still-working Vulkan fallback.

Each slice stays buildable and playable. Do not advance after a visual, stage-B CPU, game CPU,
frame-time or GPU-idle regression without identifying it or explicitly accepting it.

### Upstream bbport merges

Moving `renderer_vulkan/`, `buffer_cache/` and `texture_cache/` behind this boundary will diverge
from bbport's renderer, so later squashed bbport releases can conflict in scheduler, resource,
pipeline and draw code. The merge policy needs a separate decision: keep an upstream-shaped
Vulkan layer and adapt each release at the boundary, or port selected upstream renderer changes
into the project-owned core and backend. Either path needs a record of upstream renderer changes,
conflict review and the same build/visual/performance gates after each merge. Continue the
existing release-merge procedure until the maintainer chooses how to reconcile the divergent
renderer; this design makes no choice between those paths.

### Linux build gate

The app and runtime currently have only `.github/workflows/macos.yml`; neither builds Linux in
CI. The smallest proposed addition is one runtime `ubuntu-latest` compile job: recursive checkout,
install Nix, and run `nix-shell shell.nix --run 'bash build.sh'`. It needs no game dump or GPU run.
Add it before the first N4 code slice so the non-Mac Vulkan path is checked on every change.

N3 hooks the engine's own functions. With the recompiled library loaded, recompiled functions call
each other directly and skip hooks, so the recompiler first has to send calls to hooked functions
through the runtime (bloodborne-recomp).
