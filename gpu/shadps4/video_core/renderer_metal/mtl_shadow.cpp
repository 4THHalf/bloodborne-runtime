// SPDX-License-Identifier: GPL-2.0-or-later
#include "video_core/renderer_metal/mtl_shadow.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <xxhash.h>

#include "video_core/renderer_metal/mtl_device.h"
#include "video_core/renderer_metal/mtl_pipeline_build.h"

namespace Metal::Shadow {

namespace {

struct State {
    Device device;
    PipelineFactory factory{device};
    std::mutex modules_mutex;
    std::unordered_map<u64, std::unique_ptr<ShaderModule>> modules; ///< by VkShaderModule
    std::atomic<u32> graphics{}, rect_lists{}, tessellated{}, geometry{}, compute{}, failed{};
};

State& Get() {
    static State state;
    return state;
}

u64 Handle(vk::ShaderModule module) {
    return u64(static_cast<VkShaderModule>(module));
}

const ShaderModule* Find(vk::ShaderModule module) {
    if (!module) {
        return nullptr;
    }
    auto& s = Get();
    std::scoped_lock lock{s.modules_mutex};
    const auto it = s.modules.find(Handle(module));
    return it != s.modules.end() ? it->second.get() : nullptr;
}

/// The totals, every 100 pipelines and after each failure (the first 50 are explained).
void Report(bool failure, const char* what, const std::string& error) {
    auto& s = Get();
    const u32 failed = s.failed.load();
    if (failure && failed <= 50) {
        std::printf("Metal shadow: %s pipeline failed: %s\n", what, error.c_str());
    }
    const u32 total = s.graphics + s.compute + failed;
    if (failure || total % 100 == 0) {
        std::printf("Metal shadow: %u graphics (%u rect lists, %u tessellated, %u geometry) and "
                    "%u compute pipelines built, %u failed\n",
                    s.graphics.load(), s.rect_lists.load(), s.tessellated.load(),
                    s.geometry.load(), s.compute.load(), failed);
    }
    std::fflush(stdout);
}

} // namespace

bool Enabled() {
    static const bool on = [] {
        const char* env = std::getenv("BB_METAL_SHADOW");
        const bool enabled = env && env[0] == '1';
        if (enabled) {
            std::printf("Metal shadow: each pipeline is also built as Metal pipeline states on %s "
                        "(BB_METAL_SHADOW=1; MoltenVK still draws)\n",
                        Get().device.Name().c_str());
        }
        return enabled;
    }();
    return on;
}

void NoteModule(vk::ShaderModule module, std::span<const u32> spirv) {
    if (!Enabled() || !module) {
        return;
    }
    // Named by its SPIR-V: the MSL disk cache keys on this.
    const u64 hash = XXH3_64bits(spirv.data(), spirv.size_bytes());
    auto metal = std::make_unique<ShaderModule>(std::vector<u32>(spirv.begin(), spirv.end()), hash);
    auto& s = Get();
    std::scoped_lock lock{s.modules_mutex};
    s.modules.insert_or_assign(Handle(module), std::move(metal));
}

void Graphics(const Vulkan::GraphicsPipelineKey& key,
              std::span<const vk::ShaderModule, Vulkan::MaxShaderStages> modules,
              std::span<const Shader::Info* const, Vulkan::MaxShaderStages> infos,
              const Vulkan::GraphicsPipeline::SerializationSupport& sdata,
              bool clip_emulation_fragment) {
    if (!Enabled()) {
        return;
    }
    auto& s = Get();
    std::array<const ShaderModule*, Vulkan::MaxShaderStages> stages{};
    for (u32 i = 0; i < Vulkan::MaxShaderStages; ++i) {
        if (infos[i]) {
            stages[i] = Find(modules[i]);
            if (!stages[i]) {
                ++s.failed;
                Report(true, "graphics", "a stage's SPIR-V was not seen");
                return;
            }
        }
    }
    // The Vulkan vertex inputs in Metal's terms: location, rate and divisor per attribute.
    std::vector<VertexAttribute> attributes;
    for (const auto& attribute : sdata.vertex_attributes) {
        VertexAttribute metal{.location = attribute.location};
        for (const auto& binding : sdata.vertex_bindings) {
            if (binding.binding == attribute.binding) {
                metal.per_instance = binding.inputRate == vk::VertexInputRate::eInstance;
            }
        }
        for (const auto& divisor : sdata.divisors) {
            if (divisor.binding == attribute.binding) {
                metal.divisor = divisor.divisor;
            }
        }
        attributes.push_back(metal);
    }
    const GraphicsPsos psos = s.factory.BuildGraphics(key, stages, attributes, clip_emulation_fragment);
    if (!psos.render) {
        ++s.failed;
        Report(true, "graphics", psos.error);
        return;
    }
    ++s.graphics;
    s.rect_lists += psos.rect_list ? 1 : 0;
    s.tessellated += psos.tessellation ? 1 : 0;
    s.geometry += psos.geometry ? 1 : 0;
    Report(false, "graphics", {});
}

void Compute(const Vulkan::ComputePipelineKey& key, vk::ShaderModule module) {
    if (!Enabled()) {
        return;
    }
    auto& s = Get();
    const ShaderModule* metal = Find(module);
    std::string error = "its SPIR-V was not seen";
    if (!metal || !s.factory.BuildCompute(key, *metal, error)) {
        ++s.failed;
        Report(true, "compute", error);
        return;
    }
    ++s.compute;
    Report(false, "compute", {});
}

} // namespace Metal::Shadow
