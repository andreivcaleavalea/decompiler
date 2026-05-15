#pragma once
#include <string>
#include <vector>

#include "DecompilerTypes.h"
#include "IRTypes.h"

namespace Decompiler {
    std::vector<IRInstruction> asm_to_ir(const std::vector<AssemblyInstruction> &instructions);

    std::vector<IRInstruction> lift(const AssemblyInstruction &instruction);
    std::vector<std::string> split_operands(const std::string &operands);

    std::vector<IRInstruction> lift_mov(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_add(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_sub(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_cmp(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_test(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_and(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_or(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_xor(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_shl(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_shr(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_sar(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_inc(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_dec(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_neg(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_not(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_imul(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_mul(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_div(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_push(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_pop(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_lea(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_sext(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_nop(uint64_t address, const std::vector<std::string> &operands);

    std::vector<IRInstruction> lift_jmp(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_call(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_ret(uint64_t address, const std::vector<std::string> &operands);
    std::vector<IRInstruction> lift_cjmp(uint64_t address, std::string insn, const std::vector<std::string> &operands);
}
