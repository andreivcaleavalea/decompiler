#include "IRPatternRecognition.h"

#include <optional>

#include "types/CallingConvention.h"
#include "types/IRProperties.h"

namespace Decompiler
{
namespace
{
    // Returns the index of the last instruction before fromIndex that defines regName.
    std::optional<size_t> findLastDef(const std::vector<IRInstruction>& instrs, const size_t fromIndex, const std::string& regName)
    {
        const std::string base = normalizedRegisterBase(regName);
        if (base.empty() || fromIndex == 0) {
            return std::nullopt;
        }

        for (size_t i = fromIndex; i-- > 0;) {
            const auto& inst = instrs[i];
            if (IRProperties::createsNewValue(inst) && normalizedRegisterBase(inst.operands[0].value) == base) {
                return i;
            }
        }
        return std::nullopt;
    }

    // Returns the index of the last 1-operand MUL instruction before fromIndex.
    // A 1-op MUL (imul rdx) is the marker for a 128-bit signed multiply used in 64-bit division optimization.
    std::optional<size_t> findLast1OpMulBefore(const std::vector<IRInstruction>& instrs, const size_t fromIndex)
    {
        if (fromIndex == 0) {
            return std::nullopt;
        }

        for (size_t i = fromIndex; i-- > 0;) {
            if (instrs[i].type == IRType::MUL && instrs[i].operands.size() == 1) {
                return i;
            }
        }
        return std::nullopt;
    }
} // namespace

// Detects the compiler's 64-bit division-by-constant optimization (128-bit magic multiply) and
// replaces the final subtraction with an explicit MOD instruction.
//
// Pattern detected (compiler output for x % K where x is 64-bit):
//   ASSIGN rax, x                  ← copy dividend to rax
//   LOAD_CONST rdx, magic          ← load magic constant
//   MUL rdx                        ← 1-op: 128-bit signed multiply rdx:rax = rax * rdx
//   SAR rdx, rdx, shift            ← extract quotient
//   [ASSIGN rax, rdx]
//   SAR rax, rax, 63               ← sign bit
//   SUB rdx, rdx, rax              ← sign correction
//   MUL reg_q, rdx, K             ← quotient * divisor K
//   SUB x, x, reg_q               ← x - quotient*K = x % K  ← detected here
void recognizeOptimizedDivision(std::vector<IRInstruction>& instructions)
{
    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& inst = instructions[i];

        // Step 1: Find SUB dest, dest, reg_q (x86 SUB always has dest == implicit src1)
        if (inst.type != IRType::SUB || inst.operands.size() < 3) {
            continue;
        }
        if (inst.operands[2].type != OpType::REG) {
            continue;
        }

        const std::string destBase = normalizedRegisterBase(inst.operands[0].value);
        const std::string regQ     = inst.operands[2].value;

        // Step 2: Last def of reg_q must be 3-op MUL with immediate K (quotient * divisor)
        const auto mulDefIdx = findLastDef(instructions, i, regQ);
        if (!mulDefIdx.has_value()) {
            continue;
        }

        const auto& mulInst = instructions[*mulDefIdx];
        if (mulInst.type != IRType::MUL || mulInst.operands.size() < 3 || mulInst.operands[2].type != OpType::IMM) {
            continue;
        }

        const std::string divisorK = mulInst.operands[2].value;

        // Step 3: Find the 1-op MUL (128-bit imul) that marks this as a division optimization
        const auto oneopMulIdx = findLast1OpMulBefore(instructions, *mulDefIdx);
        if (!oneopMulIdx.has_value()) {
            continue;
        }

        // Step 4: Verify rax was loaded from the dividend (dest) before the 1-op MUL
        const auto raxDefIdx = findLastDef(instructions, *oneopMulIdx, "rax");
        if (!raxDefIdx.has_value()) {
            continue;
        }

        const auto& raxDef = instructions[*raxDefIdx];
        if (raxDef.type != IRType::ASSIGN && raxDef.type != IRType::LOAD && raxDef.type != IRType::SEXT) {
            continue;
        }
        if (raxDef.operands.size() < 2) {
            continue;
        }
        if (normalizedRegisterBase(raxDef.operands[1].value) != destBase) {
            continue;
        }

        // All checks passed: replace SUB dest, dest, reg_q with MOD dest, dest, K
        instructions[i].op          = "mod";
        instructions[i].type        = IRType::MOD;
        instructions[i].operands[1] = instructions[i].operands[0];
        instructions[i].operands[2] = ImmediateOperand(divisorK);
    }
}

} // namespace Decompiler
