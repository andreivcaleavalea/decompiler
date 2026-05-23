#include "CopyPropagation.h"

#include <string>

void Decompiler::Optimizations::propagateCopies(Graph& cfg)
{
    while (propagateOnce(cfg)) {
    }
}

bool Decompiler::Optimizations::propagateOnce(Graph& cfg)
{
    for (auto& block : cfg.blocks) {
        for (size_t i = 0; i < block.instructions.size(); ++i) {
            const auto& instruction = block.instructions[i];
            if (!canPropagate(instruction)) {
                continue;
            }

            const std::string destName = instruction.operands[0].value;
            const size_t uses          = countUsesOfName(cfg, destName);

            if (uses == 0) {
                block.instructions.erase(block.instructions.begin() + i);
                return true;
            }

            if (uses == 1) {
                const IROperand source = instruction.operands[1];
                block.instructions.erase(block.instructions.begin() + i);
                replaceFirstUseOfName(cfg, destName, source);
                return true;
            }
        }
    }
    return false;
}

bool Decompiler::Optimizations::canPropagate(const IRInstruction& instruction)
{
    if (instruction.type != IRType::ASSIGN && instruction.type != IRType::LOAD && instruction.type != IRType::LOAD_CONST) {
        return false;
    }
    if (!IRProperties::hasOperandAt(instruction, 1)) {
        return false;
    }
    const auto& src = instruction.operands[1];
    if (src.type != OpType::REG && src.type != OpType::IMM) {
        return false;
    }

    const auto& dest = IRProperties::operandAt(instruction, 0);
    if (dest.type != OpType::REG || dest.kind != OperandKind::SsaTemp) {
        return false;
    }

    return true;
}

size_t Decompiler::Optimizations::countUsesOfName(const Graph& cfg, const std::string& name)
{
    size_t count = 0;
    for (const auto& block : cfg.blocks) {
        for (const auto& instruction : block.instructions) {
            if (IRProperties::isReturn(instruction.type)) {
                count += 2;
                continue;
            }
            for (size_t index = 0; index < instruction.operands.size(); ++index) {
                if (IRProperties::usesOperandAt(instruction, index) && instruction.operands[index].value == name) {
                    ++count;
                }
            }
        }
    }
    return count;
}

bool Decompiler::Optimizations::replaceFirstUseOfName(Graph& cfg, const std::string& name, const IROperand& replacement)
{
    for (auto& block : cfg.blocks) {
        for (auto& instruction : block.instructions) {
            for (size_t index = 0; index < instruction.operands.size(); ++index) {
                if (IRProperties::usesOperandAt(instruction, index) && instruction.operands[index].value == name) {
                    instruction.operands[index] = replacement;
                    return true;
                }
            }
        }
    }
    return false;
}
