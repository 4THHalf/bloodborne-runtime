# macOS performance

Measurements on an Apple M3 Pro (18-core GPU, 18 GB), macOS 27, MoltenVK, native arm64 build,
in the clinic at the start of the game (`tools/mac_bench.sh`: boots, loads the save, holds,
reports the frame times). GPU breakdowns come from Metal System Traces (`TRACE=1`,
`BB_PASS_LABELS=1`, `tools/mst_passes.py`, `tools/mst_sequence.py`, `tools/mst_shaders.py`).

## Where it stands (2026-10-09)

| 1080p output | FPS |
|---|---|
| Native 1080p | ~31 |
| 720p, plain upscale | ~44–45 |
| FSR 3.1 preset 2 (1130×636), character motion vectors off | ~44 |
| FSR 3.1, character motion vectors on | ~36–37 |

At native 1080p the GPU is the limit: ~25 ms of work and 6–8 ms of idle time per frame, with
the game waiting on the previous frame for most of each frame. At 720p a large part of the work
does not scale with resolution (~10 ms: vertex work, shadow maps, small passes, copies, idle).

GPU time per frame at native 1080p:

| Work | ms |
|---|---|
| G-buffer pass | ~7.2 (vertex ~2.6) |
| Lighting (RGBA16F + depth) | ~4.1 |
| Compute dispatches | ~2.7 |
| Post-processing passes | ~5 |
| Image clears (separate passes) | ~1.2 |
| Copies (buffers and images) | ~2 |
| Idle between Metal encoders | 6–8 |

## Changes that helped

Four alternating Metal-traced runs in the clinic on 2026-10-11 measured deferred color clears
(`BB_DEFER_CLEARS`, default on; `=0` disables them): 46.7 and 44.6 FPS off, 47.3 and 47.9 FPS
on during the level hold. The median frame times were 21.2/21.5 ms off and 20.9/20.7 ms on;
game CPU was 489%/499% off and 480%/495% on (100% is one core). Separate color-clear encoders
fell from 17.8/16.8 to 14.7/13.6 per frame. The trace's idle inside command buffers was
5.0/4.7 ms off and 4.7/4.3 ms on. Idle between command buffers varied from 0.8 to 3.7 ms
across these traces, so the FPS and frame times are better measures of the net effect.

The original implementation recorded about 70% of pending clears on their own in a diagnostic
run. `BB_DEFER_CLEAR_REASONS=1` showed that a broad check for an enabled upscaler accounted for
7,399 of 12,908 separate clears at the first 16,384 clears. Checking whether the color target
was actually redirected raised load-action clears from 3,469 to 10,815 and reduced separate
clears from 12,908 to 5,564. The remaining resolves were 3,761 shader reads and 1,803 copies;
both require the clear to be recorded before access. Other reason buckets were zero in this
clinic run.

Three alternating off/on pairs without tracing, each with a 90-second hold, measured:

| Pair | Off FPS / p90 / game CPU | On FPS / p90 / game CPU |
|---|---|---|
| 1 | 46.7 / 22.4 ms / 494% | 47.8 / 21.9 ms / 505% |
| 2 | 46.4 / 22.6 ms / 496% | 47.5 / 22.1 ms / 498% |
| 3 | 47.2 / 22.2 ms / 498% | 49.1 / 21.2 ms / 503% |

On improved FPS and p90 in each pair. Game CPU use averaged 502% on versus 496% off; this
small increase should be checked again if later renderer changes affect CPU cost. Game-frame
captures from the same place showed no visible difference. The default remains on, and
`BB_DEFER_CLEARS=0` disables it.

| Change | Effect |
|---|---|
| Color clears deferred to the next render pass's load action (`BB_DEFER_CLEARS`) | After narrowing the upscaler check: 46.4–47.2 → 47.5–49.1 FPS in three untraced pairs; 3–4 fewer clear encoders per frame before this refinement |
| GPU memory kept resident in a Metal residency set (`gpu/shim/bbport_metal_residency.*`) | 9–14 FPS with 400–650 ms hitches → 16.8 FPS steady (Rosetta build) |
| No SPIR-V `NoContraction`: SPIRV-Cross turned it into unoptimizable `[[clang::optnone]]` add/multiply helpers in every shader | 16.7 → 30.4 FPS |
| Copy-shader copies recorded before the render pass instead of splitting it (`BB_HOIST_COPIES`), batched | ~1–2 ms |
| Upscaler presets at 1080p output render the game smaller from the start (the live path kept post-processing at 1080p) | FSR preset: ~31 → ~37–44 FPS |
| Larger direct memory only above 1080p output (9152 MiB ran the clinic at 7 FPS on 18 GB) | 7 → 36 FPS with a preset |
| Motion-vector positions stored with one plain store instead of four atomic exchanges | FSR + motion vectors 32.7 → 34.2 FPS |
| Character motion vectors off by default on macOS | FSR 36 → 42 FPS |

## Measured without gain

- Fast transcendental math (no `SignedZeroInfNanPreserve`, `BB_SHADER_INFNAN`).
- MoltenVK fast math, argument buffers off, prefill, more command buffers, Metal heaps,
  `BB_FRAMES_AHEAD=2`, anisotropic filtering off.
- Skipping draws that render into nothing (~13 a frame).
- Rect lists as triangles (`BB_RECTS_AS_TRIANGLES`): the game's heavy tessellated draws are real
  tessellation, not rect lists.
- Grouping already hoisted copy regions by buffer pair: 59.4/54.8 `vkCmdCopyBuffer` encoders per
  frame in two off traces versus 54.5 in one on trace. The off runs spanned the apparent gain;
  level-hold FPS was 46.7/48.6 off and 46.3/47.7 on. The grouping was reverted.
- Motion vectors: a smaller target (RGBA16F), no history stores, gating still objects. The cost
  is the motion variant's vertex outputs: a variant without them (`BB_MOTION_LITE=1`,
  measurement) runs as fast as no motion vectors.
- KosmicKrisp (Mesa's Vulkan on Metal 4): renders correctly, ~3× slower than MoltenVK.

## Made things worse

- A CPU fence on swapchain acquire.
- Waiting on the CPU for each frame to be ready before presenting it (`BB_PRESENT_CPU_WAIT`):
  "Bad PM4 packet" crashes while a save loaded; reverted.

Measuring: the GPU timestamp profiler does not work on MoltenVK, and `ioreg`'s "Device
Utilization" overstates how busy the GPU is; use Metal System Traces (`TRACE=1` in
`tools/mac_bench.sh`, read with `tools/mst_summary.py` / `tools/mst_passes.py` /
`tools/mst_shaders.py`; `xctrace` needs `DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer`).

## Where more could be gained

1. **Idle time between Metal encoders, 6–8 ms a frame at 1080p.** Each switch between render,
   compute and blit work leaves the GPU idle (~630 encoders a frame).
   - The game's tessellated light draws (~48 a frame) become a compute pass plus a render-pass
     restart each on MoltenVK: ~2–3 ms (removing them entirely: 31 → 34 FPS). Their hull shader
     uses constant factors (outer 3, inner 1), so they could be pre-tessellated into ordinary
     triangles (merging the LS, HS and DS stages in the recompiler). An opt-in fixed-quad
     triangle path (`BB_FIXED_QUAD_TRIANGLES=1`) converted the observed draws and improved
     mean FPS in two alternating 60-second pairs, but p90 worsened in both and game CPU use
     rose by 30–38 percentage points. It remains off by default pending a CPU fix and a valid
     Metal trace.
   - Deferred color clears initially removed 3–4 separate clear encoders a frame in the clinic.
     The narrowed upscaler check removes more, but encoder counts have not been retraced.
   - Small buffer copies between passes (~1–1.5 ms): batch further.
2. **Vertex work, ~6 ms a frame at native.** Vertex shaders read constants from storage buffers
   one value at a time; uniformly indexed read-only buffers declared as constant buffers might
   let Metal preload them. Some tiny passes (32×32, 64×64, 256×256 targets) are almost only
   vertex work (~1.5 ms).
3. **FSR overhead, ~3 ms over a plain upscale** (~1.7 ms the FSR passes, the rest idle time).
4. **Cheaper character motion vectors**, if wanted back: pass only the previous position and
   rebuild the current one from the pixel position (exact where the game's viewport is the
   render size: startup presets and native).

Expected: native 1080p in the mid-to-high 30s; FSR presets around 50 FPS. 60 FPS at native 1080p
would need much less GPU work than the game issues.
