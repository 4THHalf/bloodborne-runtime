// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/logging/classes.h"
#include <array>
#include <optional>
#include <ranges>
#include <span>
#include "shader_recompiler/frontend/control_flow_graph.h"
#include "shader_recompiler/frontend/decode.h"
#include "shader_recompiler/frontend/structured_control_flow.h"
#include "shader_recompiler/frontend/translate/translate.h"
#include "shader_recompiler/ir/passes/ir_passes.h"
#include "shader_recompiler/ir/post_order.h"
#include "shader_recompiler/ir/program.h"
#include "shader_recompiler/profile.h"
#include "shader_recompiler/recompiler.h"

namespace Shader {

// A deliberately narrow gate for replacing quad tessellation with triangles. The domain
// shader and the vertex input layout are checked separately when selecting the fast path.
static u8 FixedQuadOuter(const IR::Program& program, const RuntimeInfo& runtime_info) {
    if (program.info.sw_stage != SwStage::TessellationControl ||
        runtime_info.sw.tcs.tess_type != AmdGpu::TessellationType::Quad ||
        runtime_info.sw.tcs.num_input_control_points != 1 ||
        !runtime_info.sw.tcs.IsPassthrough() || program.blocks.size() != 2 ||
        program.blocks.back()->Instructions().size() != 1 ||
        program.blocks.back()->Instructions().front().GetOpcode() != IR::Opcode::Epilogue) {
        return 0;
    }
    constexpr std::array factors{
        IR::Patch::TessellationLodLeft, IR::Patch::TessellationLodTop,
        IR::Patch::TessellationLodRight, IR::Patch::TessellationLodBottom,
        IR::Patch::TessellationLodInteriorU, IR::Patch::TessellationLodInteriorV,
    };
    std::array<float, factors.size()> values{};
    std::array<bool, factors.size()> seen{};
    for (const IR::Inst& inst : program.blocks.front()->Instructions()) {
        if (inst.GetOpcode() != IR::Opcode::SetPatch) continue;
        const auto patch = inst.Arg(0).Patch();
        const auto it = std::ranges::find(factors, patch);
        if (it == factors.end()) continue;
        const size_t index = std::distance(factors.begin(), it);
        const auto value = inst.Arg(1);
        if (seen[index] || value.Type() != IR::Type::F32) return 0;
        seen[index] = true;
        values[index] = value.F32();
    }
    if (!std::ranges::all_of(seen, [](bool value) { return value; })) return 0;
    if (values[4] != 1.f || values[5] != 1.f) return 0;
    for (u8 outer : {u8{1}, u8{3}}) {
        if (std::ranges::all_of(std::span{values}.first(4),
                                [outer](float value) { return value == outer; })) {
            return outer;
        }
    }
    return 0;
}

using FixedQuadComponent = InfoPersistent::FixedQuadComponent;

static std::optional<FixedQuadComponent> FixedQuadScalar(IR::Value value, u32 depth);

static std::optional<FixedQuadComponent> FixedQuadVectorComponent(IR::Value value, u32 component,
                                                                  u32 depth) {
    if (depth > 16 || component >= 4) return std::nullopt;
    const IR::Inst* inst = value.TryInst();
    if (!inst) return std::nullopt;
    switch (inst->GetOpcode()) {
    case IR::Opcode::CompositeConstructF32x4:
        return FixedQuadScalar(inst->Arg(component), depth + 1);
    case IR::Opcode::CompositeShuffleF32x4: {
        const auto selector = inst->Arg(component + 2);
        if (selector.Type() != IR::Type::U32 || selector.U32() >= 8) return std::nullopt;
        const u32 index = selector.U32();
        return FixedQuadVectorComponent(inst->Arg(index < 4 ? 0 : 1), index % 4, depth + 1);
    }
    default:
        return std::nullopt;
    }
}

static std::optional<FixedQuadComponent> FixedQuadScalar(IR::Value value, u32 depth) {
    if (depth > 16) return std::nullopt;
    if (value.Type() == IR::Type::F32) {
        return FixedQuadComponent{.constant = value.F32()};
    }
    const IR::Inst* inst = value.TryInst();
    if (!inst) return std::nullopt;
    switch (inst->GetOpcode()) {
    case IR::Opcode::GetAttribute: {
        const IR::Attribute attr = inst->Arg(0).Attribute();
        const IR::Value component = inst->Arg(1);
        if (attr < IR::Attribute::Param0 || attr >= IR::Attribute::Param0 + IR::NumParams ||
            component.Type() != IR::Type::U32 || component.U32() >= 4) {
            return std::nullopt;
        }
        return FixedQuadComponent{.source_attr = u8(u32(attr) - u32(IR::Attribute::Param0)),
                                  .source_comp = u8(component.U32())};
    }
    case IR::Opcode::CompositeExtractF32x4: {
        const auto component = inst->Arg(1);
        if (component.Type() != IR::Type::U32) return std::nullopt;
        return FixedQuadVectorComponent(inst->Arg(0), component.U32(), depth + 1);
    }
    default:
        return std::nullopt;
    }
}

static void MapFixedQuadLocalOutputs(IR::Program& program, const RuntimeInfo& runtime_info) {
    Info& info = program.info;
    if (info.hw_stage != HwStage::Local || info.sw_stage != SwStage::Vertex ||
        program.blocks.size() != 2 ||
        program.blocks.back()->Instructions().size() != 1 ||
        program.blocks.back()->Instructions().front().GetOpcode() != IR::Opcode::Epilogue ||
        runtime_info.hw.ls.ls_stride == 0 ||
        runtime_info.hw.ls.ls_stride > IR::NumParams * 16) {
        return;
    }
    const u32 count = (runtime_info.hw.ls.ls_stride + 15) / 16;
    std::array<bool, IR::NumParams * 4> seen{};
    for (const IR::Block* block : program.blocks) {
        for (const IR::Inst& inst : block->Instructions()) {
            if (inst.GetOpcode() != IR::Opcode::SetAttribute) {
                if (inst.MayHaveSideEffects() &&
                    inst.GetOpcode() != IR::Opcode::Prologue &&
                    inst.GetOpcode() != IR::Opcode::Epilogue) return;
                continue;
            }
            const auto attr = inst.Arg(0).Attribute();
            const auto component = inst.Arg(2);
            if (attr < IR::Attribute::Param0 || attr >= IR::Attribute::Param0 + count ||
                component.Type() != IR::Type::U32 || component.U32() >= 4) {
                return;
            }
            const u32 index = (u32(attr) - u32(IR::Attribute::Param0)) * 4 + component.U32();
            if (seen[index]) return;
            const auto mapped = FixedQuadScalar(inst.Arg(1), 0);
            if (!mapped) return;
            seen[index] = true;
            info.fixed_quad_outputs[index] = *mapped;
        }
    }
    if (std::ranges::none_of(seen, [](bool value) { return value; })) return;
    info.fixed_quad_output_count = u8(count);
}

IR::BlockList GenerateBlocks(const IR::AbstractSyntaxList& syntax_list) {
    size_t num_syntax_blocks{};
    for (const auto& [_, type] : syntax_list) {
        if (type == IR::AbstractSyntaxNode::Type::Block) {
            ++num_syntax_blocks;
        }
    }
    IR::BlockList blocks{};
    blocks.reserve(num_syntax_blocks);
    for (const auto& [data, type] : syntax_list) {
        if (type == IR::AbstractSyntaxNode::Type::Block) {
            blocks.push_back(data.block);
        }
    }
    return blocks;
}

void EmitControlFlowGraph(IR::Program& program, Pools& pools, Gcn::CFG& cfg,
                          RuntimeInfo& runtime_info, const Profile& profile) {
    Gcn::Translator translator{program.info, runtime_info, profile};
    for (auto& block : cfg) {
        const u32 start = block.begin_index;
        const u32 size = block.end_index - start + 1;
        auto* ir_block = pools.block_pool.Create(pools.inst_pool);
        ir_block->cfg_block = &block;
        block.ir_block = ir_block;
        translator.Translate(ir_block, block.begin, block.cond,
                             std::span{program.ins_list}.subspan(start, size));
        program.blocks.push_back(ir_block);
    }
    translator.EmitPrologue(program.blocks.front());
    ASSERT_MSG(!program.info.translation_failed, "Shader translation has failed");
    for (auto& block : cfg) {
        auto* ir_block = block.ir_block;
        if (block.branch_true) {
            auto* true_block = block.branch_true->ir_block;
            ir_block->AddBranch(true_block);
        }
        if (block.branch_false) {
            auto* false_block = block.branch_false->ir_block;
            ir_block->AddBranch(false_block);
        }
    }
    program.post_order_blocks = Shader::IR::PostOrder(program.blocks.front());
}

IR::Program TranslateProgram(const std::span<const u32>& code, Pools& pools, Info& info,
                             RuntimeInfo& runtime_info, const Profile& profile) {
    // Ensure first instruction is expected.
    constexpr u32 token_mov_vcchi = 0xBEEB03FF;
    if (code[0] != token_mov_vcchi) {
        LOG_WARNING(Render_Recompiler, "First instruction is not s_mov_b32 vcc_hi, #imm");
    }

    Gcn::GcnCodeSlice slice(code.data(), code.data() + code.size());
    Gcn::GcnDecodeContext decoder;

    // Decode and save instructions
    IR::Program program{info};
    program.ins_list.reserve(code.size());
    while (!slice.atEnd()) {
        program.ins_list.emplace_back(decoder.decodeInstruction(slice));
    }

    // Clear any previous pooled data.
    pools.ReleaseContents();

    // Create control flow graph
    Common::ObjectPool<Gcn::Block> gcn_block_pool{64};
    Gcn::CFG cfg{gcn_block_pool, program.ins_list};
    EmitControlFlowGraph(program, pools, cfg, runtime_info, profile);

    // On NVIDIA GPUs HW interpolation of clip distance values seems broken, and we need to emulate
    // it with expensive discard in PS.
    Shader::InjectClipDistanceAttributes(program, runtime_info);

    // Run optimization passes on unstructured graph
    if (!profile.support_float64) {
        Shader::Optimization::LowerFp64ToFp32(program);
    }
    Shader::Optimization::SsaRewritePass(program);
    Shader::Optimization::ConstantPropagationPass(program.post_order_blocks);
    if (info.sw_stage == SwStage::TessellationControl) {
        Shader::Optimization::TessellationPreprocess(program, runtime_info);
        Shader::Optimization::HullShaderTransform(program, runtime_info);
    } else if (info.sw_stage == SwStage::TessellationEval) {
        Shader::Optimization::TessellationPreprocess(program, runtime_info);
        Shader::Optimization::DomainShaderTransform(program, runtime_info);
    }
    Shader::Optimization::RingAccessElimination(program, runtime_info);
    Shader::Optimization::ReadLaneEliminationPass(program);
    Shader::IR::DumpProgram(program, info, "pre-res-discover.");
    auto resources = Shader::Optimization::ResourceDiscoverPass(program, profile);
    Shader::Optimization::FlattenExtendedUserdataPass(program);
    Shader::IR::DumpProgram(program, info, "pre-res-patch.");
    Shader::Optimization::ResourcePatchingPass(program.info, resources, profile);
    Shader::Optimization::LowerBufferFormatToRaw(program);
    Shader::Optimization::SharedMemorySimplifyPass(program, profile);
    Shader::Optimization::SharedMemoryToStoragePass(program, runtime_info, profile);
    Shader::Optimization::LowerUserClipPlanes(program, runtime_info);
    Shader::Optimization::PhiSimplificationPass(program);
    Shader::Optimization::InverseBallotEliminationPass(program);
    Shader::Optimization::LowerHardwareIntrinsics(program);
    Shader::IR::DumpProgram(program, info, "pre-lower-phi.");

    // Prepare for structurization by clearing flow graph and lowering phis
    for (auto* ir_block : program.blocks) {
        ir_block->imm_predecessors.clear();
        ir_block->imm_successors.clear();
        ir_block->ssa_state.Reset();
    }
    Shader::Optimization::LowerPhisToRegsPass(program);

    // Structurize control flow graph and create program.
    program.syntax_list = Shader::Gcn::BuildASL(pools, cfg, info);
    program.blocks = GenerateBlocks(program.syntax_list);
    program.post_order_blocks = Shader::IR::PostOrder(program.syntax_list.front().data.block);

    // Run optimization passes on structured graph
    Shader::Optimization::SsaRepairPass(program);
    Shader::Optimization::SsaRewritePass(program);
    Shader::Optimization::SharedMemoryBarrierPass(program, runtime_info, profile);
    Shader::Optimization::DeadCodeEliminationPass(program);
    Shader::Optimization::LowerWave64BallotPass(program, runtime_info, profile);
    Shader::Optimization::ConstantPropagationPass(program.post_order_blocks);
    Shader::Optimization::DeadCodeEliminationPass(program);
    if (info.sw_stage == SwStage::Fragment && profile.needs_integer_interpolation_fix) {
        Shader::Optimization::InterpolatedIntegerPass(program);
    }
    Shader::Optimization::CollectShaderInfoPass(program, profile);
    info.fixed_quad_outer = FixedQuadOuter(program, runtime_info);
    MapFixedQuadLocalOutputs(program, runtime_info);
    Shader::IR::DumpProgram(program, info);

    return program;
}

} // namespace Shader
