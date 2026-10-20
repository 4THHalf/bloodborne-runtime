// SPDX-License-Identifier: GPL-2.0-or-later
// The Metal shadow check (R5 N5, docs/NATIVE_RENDERER.md): with BB_METAL_SHADOW=1, every pipeline
// the Vulkan renderer creates is also built as Metal pipeline states, from the same key and SPIR-V,
// through the Metal backend's PipelineFactory; the log counts what built and why the rest did not.
// Nothing is drawn with them: MoltenVK stays the renderer. The approach is Wooloomooloo2/
// bloodborne_mac_port's M1 shadow builder (its docs/DESIGN_METAL_BACKEND.md).

#pragma once

#include <span>
#include "common/types.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"

namespace Metal::Shadow {

/// BB_METAL_SHADOW=1.
bool Enabled();

/// A Vulkan shader module was created from `spirv` (what the Metal one is compiled from).
void NoteModule(vk::ShaderModule module, std::span<const u32> spirv);

/// A Vulkan graphics pipeline was created: builds its Metal pipeline states. `sdata` holds its
/// vertex inputs (in the fetch shader's order).
void Graphics(const Vulkan::GraphicsPipelineKey& key,
              std::span<const vk::ShaderModule, Vulkan::MaxShaderStages> modules,
              std::span<const Shader::Info* const, Vulkan::MaxShaderStages> infos,
              const Vulkan::GraphicsPipeline::SerializationSupport& sdata,
              bool clip_emulation_fragment);

/// A Vulkan compute pipeline was created: builds its Metal pipeline state.
void Compute(const Vulkan::ComputePipelineKey& key, vk::ShaderModule module);

} // namespace Metal::Shadow
