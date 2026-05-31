#include "StackVariableRecovery.h"

#include "WindowsX64.h"

namespace Decompiler
{
namespace
{
    std::string accessSizeSuffix(const uint32_t sizeBytes)
    {
        switch (sizeBytes) {
        case 1:
            return "_byte";
        case 2:
            return "_word";
        case 4:
            return "_dword";
        case 8:
            return "_qword";
        default:
            return {};
        }
    }

    bool isStackSlotOperand(const IROperand& operand)
    {
        if (!operand.isHeapDeref()) {
            return false;
        }
        const auto& memory = operand.heapDeref;
        if (memory.absolute.has_value()) {
            return false;
        }
        if (memory.base.empty()) {
            return false;
        }
        const std::string base = normalizedRegisterBase(memory.base);
        return base == "rbp" || base == "rsp";
    }

    void convertOperand(IROperand& operand, const StackFrame& stackFrame)
    {
        if (!isStackSlotOperand(operand)) {
            return;
        }

        const auto& memory        = operand.heapDeref;
        const StackRegion* region = stackFrame.findRegion(memory.displacement, memory.sizeBytes);
        if (region == nullptr || region->name.empty()) {
            return;
        }

        const int64_t displacement = memory.displacement;
        std::string indexName      = memory.index;
        int32_t indexScale         = memory.scale;

        std::string name = region->name;
        if (region->kind == StackRegionKind::Union) {
            const std::string suffix = accessSizeSuffix(memory.sizeBytes);
            if (!suffix.empty()) {
                name += suffix;
            }
        }

        operand.tag         = OperandTag::StackVar;
        operand.name        = std::move(name);
        operand.stackOffset = region->offset;
        operand.sizeBytes   = memory.sizeBytes != 0 ? memory.sizeBytes : region->elementSize;
        operand.heapDeref   = {};

        if (region->kind == StackRegionKind::Array && !indexName.empty()) {
            operand.arrayIndex      = std::move(indexName);
            operand.arrayIndexScale = indexScale;
        } else if (region->kind == StackRegionKind::Array) {
            const int64_t k         = (displacement - region->offset) / static_cast<int64_t>(region->elementSize);
            operand.arrayIndex      = std::to_string(k);
            operand.arrayIndexScale = static_cast<int32_t>(region->elementSize);
        } else {
            operand.arrayIndex.clear();
            operand.arrayIndexScale = 1;
        }
    }
} // namespace

void recoverStackVariables(Graph& cfg, const StackFrame& stackFrame)
{
    for (auto& block : cfg.blocks) {
        for (auto& instruction : block.instructions) {
            if (instruction.address != 0 && stackFrame.initStoreAddrs.count(instruction.address) > 0) {
                instruction.type = IRType::NOP;
                continue;
            }
            for (auto& operand : instruction.operands) {
                convertOperand(operand, stackFrame);
            }
        }
    }
}
} // namespace Decompiler
