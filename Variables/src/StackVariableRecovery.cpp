#include "StackVariableRecovery.h"

#include <cctype>
#include <string>

namespace Decompiler
{
namespace
{
    std::string canonicalBase(const std::string& raw)
    {
        std::string value     = raw;
        const auto underscore = value.find('_');
        if (underscore != std::string::npos) {
            value = value.substr(0, underscore);
        }
        for (char& ch : value) {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return value;
    }

    bool isStackPointerBase(const std::string& base)
    {
        return base == "rsp" || base == "esp" || base == "sp";
    }

    bool isFramePointerBase(const std::string& base)
    {
        return base == "rbp" || base == "ebp" || base == "bp";
    }

    std::string makeLocalName(const long long positiveOffset)
    {
        return "var_" + std::to_string(positiveOffset);
    }

    std::string makeArgumentName(const long long positiveOffset, const StackFrameLayout layout)
    {
        if (layout == StackFrameLayout::Win64HomeSlots) {
            if (const auto homeSlotIndex = argumentIndexForWin64HomeSlot(positiveOffset); homeSlotIndex.has_value()) {
                return "arg_" + std::to_string(*homeSlotIndex);
            }
        }
        return "arg_" + std::to_string(positiveOffset);
    }

    std::string stackVariableName(const std::string& base, const long long displacement, const StackFrameLayout layout)
    {
        if (isFramePointerBase(base)) {
            if (displacement <= 0) {
                return makeLocalName(-displacement);
            }
            return makeArgumentName(displacement, layout);
        }
        if (isStackPointerBase(base)) {
            if (displacement < 0) {
                return {};
            }
            return makeLocalName(displacement);
        }
        return {};
    }

    bool isStackSlotOperand(const IROperand& operand)
    {
        if (operand.type != OpType::MEM || !operand.memory.has_value()) {
            return false;
        }
        const auto& memory = *operand.memory;
        if (memory.absolute.has_value()) {
            return false;
        }
        if (!memory.index.empty()) {
            return false;
        }
        if (memory.base.empty()) {
            return false;
        }
        return true;
    }

    void convertOperand(IROperand& operand, const StackFrameLayout layout)
    {
        if (!isStackSlotOperand(operand)) {
            return;
        }

        const std::string base = canonicalBase(operand.memory->base);
        const std::string name = stackVariableName(base, operand.memory->displacement, layout);
        if (name.empty()) {
            return;
        }

        operand.type  = OpType::REG;
        operand.value = name;
        operand.memory.reset();
        operand.kind = OperandKind::StackVar;
    }
} // namespace

void recoverStackVariables(Graph& cfg, const StackFrameLayout layout)
{
    for (auto& block : cfg.blocks) {
        for (auto& instruction : block.instructions) {
            for (auto& operand : instruction.operands) {
                convertOperand(operand, layout);
            }
        }
    }
}
} // namespace Decompiler
