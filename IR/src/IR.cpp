#include "IR.h"

#include <cctype>
#include <cstdlib>

namespace Decompiler
{
namespace
{
    std::string trim_operand(std::string value)
    {
        const auto first = value.find_first_not_of(' ');
        if (first == std::string::npos) {
            return {};
        }
        const auto last = value.find_last_not_of(' ');
        return value.substr(first, last - first + 1);
    }

    std::vector<IRInstruction> lift_inplace_binary(const uint64_t address, const std::vector<std::string>& operands, const std::string& op, const IRType type)
    {
        if (operands.size() < 2) {
            return {};
        }
        const IROperand dest = parse_operand(operands[0]);
        const IROperand src2 = parse_operand(operands[1]);
        return { { op, type, { dest, dest, src2 }, address } };
    }

    std::vector<IRInstruction> lift_shift(const uint64_t address, const std::vector<std::string>& operands, const std::string& op, const IRType type)
    {
        if (operands.empty()) {
            return {};
        }
        const IROperand dest = parse_operand(operands[0]);
        const IROperand src2 = operands.size() >= 2 ? parse_operand(operands[1]) : ImmediateOperand(1);
        return { { op, type, { dest, dest, src2 }, address } };
    }

    std::vector<IRInstruction> lift_unary(const uint64_t address, const std::vector<std::string>& operands, const std::string& op, const IRType type)
    {
        if (operands.empty()) {
            return {};
        }
        const IROperand dest = parse_operand(operands[0]);
        return { { op, type, { dest, dest }, address } };
    }

    void resolveMemoryOperand(IROperand& operand, const uint64_t instructionAddress, const uint16_t instructionSize)
    {
        if (operand.type != OpType::MEM || !operand.memory.has_value()) {
            return;
        }

        auto& memory = *operand.memory;
        if (memory.ripRelative) {
            const int64_t rip = static_cast<int64_t>(instructionAddress) + instructionSize;
            memory.absolute   = static_cast<uint64_t>(rip + memory.displacement);
            return;
        }

        if (memory.base.empty() && memory.index.empty() && memory.displacement >= 0) {
            memory.absolute = static_cast<uint64_t>(memory.displacement);
        }
    }

    void resolveMemoryOperands(std::vector<IRInstruction>& instructions, const AssemblyInstruction& source)
    {
        for (auto& instruction : instructions) {
            for (auto& operand : instruction.operands) {
                resolveMemoryOperand(operand, source.address, source.size);
            }
        }
    }

    size_t memorySizeBytes(const std::string& value, const size_t bracketPos)
    {
        const std::string prefix = trim_operand(value.substr(0, bracketPos));
        if (prefix.starts_with("byte ptr")) {
            return 1;
        }
        if (prefix.starts_with("word ptr")) {
            return 2;
        }
        if (prefix.starts_with("dword ptr")) {
            return 4;
        }
        if (prefix.starts_with("qword ptr")) {
            return 8;
        }
        if (prefix.starts_with("xmmword ptr")) {
            return 16;
        }
        return 0;
    }

    bool parseIntegerToken(const std::string& token, int64_t& out)
    {
        if (token.empty()) {
            return false;
        }
        char* end = nullptr;
        out       = std::strtoll(token.c_str(), &end, 0);
        return end != token.c_str() && *end == '\0';
    }

    void applyMemoryTerm(IRMemoryAddress& memory, std::string token, const int sign)
    {
        token = trim_operand(std::move(token));
        if (token.empty()) {
            return;
        }

        const auto star = token.find('*');
        if (star != std::string::npos) {
            memory.index  = normalizedRegisterBase(trim_operand(token.substr(0, star)));
            int64_t scale = 1;
            if (parseIntegerToken(trim_operand(token.substr(star + 1)), scale)) {
                memory.scale = static_cast<int>(scale);
            }
            return;
        }

        int64_t displacement = 0;
        if (parseIntegerToken(token, displacement)) {
            memory.displacement += sign * displacement;
            return;
        }

        if (token == "rip") {
            memory.ripRelative = true;
        }

        if (memory.base.empty()) {
            memory.base = normalizedRegisterBase(token);
        } else if (memory.index.empty()) {
            memory.index = normalizedRegisterBase(token);
        }
    }

    std::optional<IRMemoryAddress> parseMemoryAddress(const std::string& value)
    {
        const auto lb = value.find('[');
        const auto rb = value.find(']');
        if (lb == std::string::npos || rb == std::string::npos || rb <= lb) {
            return std::nullopt;
        }

        IRMemoryAddress memory;
        memory.sizeBytes = memorySizeBytes(value, lb);

        std::string current;
        int sign                 = 1;
        const std::string inside = value.substr(lb + 1, rb - lb - 1);
        for (const char ch : inside) {
            if (ch == '+' || ch == '-') {
                applyMemoryTerm(memory, current, sign);
                current.clear();
                sign = ch == '-' ? -1 : 1;
            } else {
                current += ch;
            }
        }
        applyMemoryTerm(memory, current, sign);
        return memory;
    }

    ConditionCode conditionFromSetMnemonic(const std::string& mnemonic)
    {
        if (mnemonic == "sete" || mnemonic == "setz")
            return ConditionCode::E;
        if (mnemonic == "setne" || mnemonic == "setnz")
            return ConditionCode::NE;
        if (mnemonic == "setl" || mnemonic == "setnge")
            return ConditionCode::L;
        if (mnemonic == "setle" || mnemonic == "setng")
            return ConditionCode::LE;
        if (mnemonic == "setg" || mnemonic == "setnle")
            return ConditionCode::G;
        if (mnemonic == "setge" || mnemonic == "setnl")
            return ConditionCode::GE;
        if (mnemonic == "setb" || mnemonic == "setnae" || mnemonic == "setc")
            return ConditionCode::B;
        if (mnemonic == "setbe" || mnemonic == "setna")
            return ConditionCode::BE;
        if (mnemonic == "seta" || mnemonic == "setnbe")
            return ConditionCode::A;
        if (mnemonic == "setae" || mnemonic == "setnb" || mnemonic == "setnc")
            return ConditionCode::AE;
        return ConditionCode::None;
    }

    ConditionCode conditionFromMnemonic(const std::string& mnemonic)
    {
        if (mnemonic == "je" || mnemonic == "jz") {
            return ConditionCode::E;
        }
        if (mnemonic == "jne" || mnemonic == "jnz") {
            return ConditionCode::NE;
        }

        if (mnemonic == "jl" || mnemonic == "jnge") {
            return ConditionCode::L;
        }
        if (mnemonic == "jle" || mnemonic == "jng") {
            return ConditionCode::LE;
        }
        if (mnemonic == "jg" || mnemonic == "jnle") {
            return ConditionCode::G;
        }
        if (mnemonic == "jge" || mnemonic == "jnl") {
            return ConditionCode::GE;
        }
        if (mnemonic == "jb" || mnemonic == "jnae" || mnemonic == "jc") {
            return ConditionCode::B;
        }
        if (mnemonic == "jbe" || mnemonic == "jna") {
            return ConditionCode::BE;
        }
        if (mnemonic == "ja" || mnemonic == "jnbe") {
            return ConditionCode::A;
        }
        if (mnemonic == "jae" || mnemonic == "jnb" || mnemonic == "jnc") {
            return ConditionCode::AE;
        }
        return ConditionCode::None;
    }

} // namespace

IROperand parse_operand(const std::string& raw)
{
    const std::string value = trim_operand(raw);
    if (value.empty()) {
        return RegisterOperand(value);
    }

    if (value.find('[') != std::string::npos) {
        return { OpType::MEM, value, parseMemoryAddress(value) };
    }

    if (std::isdigit(static_cast<unsigned char>(value[0])) || (value.size() > 1 && value[0] == '0' && value[1] == 'x') || value[0] == '-') {
        return ImmediateOperand(value);
    }

    return RegisterOperand(normalizedRegisterBase(value));
}

std::vector<IRInstruction> asm_to_ir(const std::vector<AssemblyInstruction>& instructions)
{
    std::vector<IRInstruction> result;

    for (const auto& instr : instructions) {
        auto irs = lift(instr);
        result.insert(result.end(), irs.begin(), irs.end());
    }

    return result;
}

std::vector<IRInstruction> lift(const AssemblyInstruction& instruction)
{
    auto ops = split_operands(instruction.operands);
    std::vector<IRInstruction> result;

    const auto mnemonic = InstructionInfo::canonicalizeMnemonic(instruction.mnemonic);

    if (mnemonic == "mov") {
        result = lift_mov(instruction.address, ops);
    } else if (mnemonic == "add") {
        result = lift_add(instruction.address, ops);
    } else if (mnemonic == "sub") {
        result = lift_sub(instruction.address, ops);
    } else if (mnemonic == "jmp") {
        result = lift_jmp(instruction.address, ops);
    } else if (mnemonic == "call") {
        result = lift_call(instruction.address, ops);
    } else if (mnemonic == "ret") {
        result = lift_ret(instruction.address, ops);
    } else if (!mnemonic.empty() && mnemonic[0] == 'j') {
        result = lift_cjmp(instruction.address, mnemonic, ops);
    } else if (mnemonic == "cmp") {
        result = lift_cmp(instruction.address, ops);
    } else if (mnemonic == "test") {
        result = lift_test(instruction.address, ops);
    } else if (mnemonic == "and") {
        result = lift_and(instruction.address, ops);
    } else if (mnemonic == "or") {
        result = lift_or(instruction.address, ops);
    } else if (mnemonic == "xor") {
        result = lift_xor(instruction.address, ops);
    } else if (mnemonic == "shl" || mnemonic == "sal") {
        result = lift_shl(instruction.address, ops);
    } else if (mnemonic == "shr") {
        result = lift_shr(instruction.address, ops);
    } else if (mnemonic == "sar") {
        result = lift_sar(instruction.address, ops);
    } else if (mnemonic == "inc") {
        result = lift_inc(instruction.address, ops);
    } else if (mnemonic == "dec") {
        result = lift_dec(instruction.address, ops);
    } else if (mnemonic == "neg") {
        result = lift_neg(instruction.address, ops);
    } else if (mnemonic == "not") {
        result = lift_not(instruction.address, ops);
    } else if (mnemonic == "imul") {
        result = lift_imul(instruction.address, ops);
    } else if (mnemonic == "mul") {
        result = lift_mul(instruction.address, ops);
    } else if (mnemonic == "idiv" || mnemonic == "div") {
        result = lift_div(instruction.address, ops);
    } else if (mnemonic == "cdq" || mnemonic == "cltd" || mnemonic == "cqo") {
        result = {};
    } else if (mnemonic == "push") {
        result = lift_push(instruction.address, ops);
    } else if (mnemonic == "pop") {
        result = lift_pop(instruction.address, ops);
    } else if (mnemonic == "lea") {
        result = lift_lea(instruction.address, ops);
    } else if (mnemonic == "movsx" || mnemonic == "movsxd") {
        result = lift_sext(instruction.address, ops);
    } else if (mnemonic == "movzx") {
        result = lift_mov(instruction.address, ops);
    } else if (mnemonic == "nop" || mnemonic == "leave" || mnemonic == "endbr64") {
        result = lift_nop(instruction.address, ops);
    } else if (mnemonic.size() >= 4 && mnemonic.starts_with("set")) {
        result = lift_setcc(instruction.address, mnemonic, ops);
    } else {
        IROperand dest = ops.size() > 0 ? parse_operand(ops[0]) : IROperand{};
        IROperand src  = ops.size() > 1 ? parse_operand(ops[1]) : IROperand{};
        result         = { { "[unknown] " + mnemonic, IRType::UNKNOWN, { dest, src }, instruction.address } };
    }

    resolveMemoryOperands(result, instruction);
    return result;
}

std::vector<std::string> split_operands(const std::string& operands)
{
    std::vector<std::string> parts;
    std::string current;
    int depth = 0;

    for (char c : operands) {
        if (c == '[')
            depth++;
        if (c == ']')
            depth--;
        if (c == ',' && depth == 0) {
            parts.push_back(trim_operand(current));
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(trim_operand(current));
    }
    return parts;
}

std::vector<IRInstruction> lift_mov(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 2)
        return {};

    IROperand dest = parse_operand(operands[0]);
    IROperand src  = parse_operand(operands[1]);

    if (dest.type == OpType::MEM) {
        return { { "store", IRType::STORE, { dest, src }, address } };
    }

    if (src.type == OpType::MEM) {
        return { { "load", IRType::LOAD, { dest, src }, address } };
    }

    if (src.type == OpType::IMM) {
        return { { "load_const", IRType::LOAD_CONST, { dest, src }, address } };
    }

    return { { "assign", IRType::ASSIGN, { dest, src }, address } };
}

std::vector<IRInstruction> lift_add(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 2)
        return {};

    IROperand dest = parse_operand(operands[0]);
    IROperand src  = parse_operand(operands[0]);
    IROperand src2 = parse_operand(operands[1]);

    return { { "add", IRType::ADD, { dest, src, src2 }, address } };
}

std::vector<IRInstruction> lift_sub(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 2)
        return {};

    IROperand dest = parse_operand(operands[0]);
    IROperand src  = parse_operand(operands[0]);
    IROperand src2 = parse_operand(operands[1]);

    return { { "sub", IRType::SUB, { dest, src, src2 }, address } };
}

std::vector<IRInstruction> lift_cmp(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 2) {
        return {};
    }

    return { { "cmp", IRType::CMP, { {}, parse_operand(operands[0]), parse_operand(operands[1]) }, address } };
}

std::vector<IRInstruction> lift_test(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 2) {
        return {};
    }

    return { { "test", IRType::TEST, { {}, parse_operand(operands[0]), parse_operand(operands[1]) }, address } };
}

std::vector<IRInstruction> lift_and(uint64_t address, const std::vector<std::string>& operands)
{
    return lift_inplace_binary(address, operands, "and", IRType::AND);
}

std::vector<IRInstruction> lift_or(uint64_t address, const std::vector<std::string>& operands)
{
    return lift_inplace_binary(address, operands, "or", IRType::OR);
}

std::vector<IRInstruction> lift_xor(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 2) {
        return {};
    }

    const IROperand dest = parse_operand(operands[0]);
    const IROperand src  = parse_operand(operands[1]);
    if (operands[0] == operands[1]) {
        return { { "load_const", IRType::LOAD_CONST, { dest, ImmediateOperand(0) }, address } };
    }

    return { { "xor", IRType::XOR, { dest, dest, src }, address } };
}

std::vector<IRInstruction> lift_shl(uint64_t address, const std::vector<std::string>& operands)
{
    return lift_shift(address, operands, "shl", IRType::SHL);
}

std::vector<IRInstruction> lift_shr(uint64_t address, const std::vector<std::string>& operands)
{
    return lift_shift(address, operands, "shr", IRType::SHR);
}

std::vector<IRInstruction> lift_sar(uint64_t address, const std::vector<std::string>& operands)
{
    return lift_shift(address, operands, "sar", IRType::SAR);
}

std::vector<IRInstruction> lift_inc(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.empty()) {
        return {};
    }

    const IROperand dest = parse_operand(operands[0]);
    return { { "add", IRType::ADD, { dest, dest, ImmediateOperand(1) }, address } };
}

std::vector<IRInstruction> lift_dec(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.empty()) {
        return {};
    }

    const IROperand dest = parse_operand(operands[0]);
    return { { "sub", IRType::SUB, { dest, dest, ImmediateOperand(1) }, address } };
}

std::vector<IRInstruction> lift_neg(uint64_t address, const std::vector<std::string>& operands)
{
    return lift_unary(address, operands, "neg", IRType::NEG);
}

std::vector<IRInstruction> lift_not(uint64_t address, const std::vector<std::string>& operands)
{
    return lift_unary(address, operands, "not", IRType::NOT);
}

std::vector<IRInstruction> lift_imul(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() >= 3) {
        return { { "mul", IRType::MUL, { parse_operand(operands[0]), parse_operand(operands[1]), parse_operand(operands[2]) }, address } };
    }
    if (operands.size() == 2) {
        const auto dest = parse_operand(operands[0]);
        return { { "mul", IRType::MUL, { dest, dest, parse_operand(operands[1]) }, address } };
    }
    if (operands.size() == 1) {
        return { { "mul", IRType::MUL, { parse_operand(operands[0]) }, address } };
    }
    return {};
}

std::vector<IRInstruction> lift_mul(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.empty()) {
        return {};
    }
    if (operands.size() == 1) {
        return { { "mul", IRType::MUL, { parse_operand(operands[0]) }, address } };
    }

    const auto dest = parse_operand(operands[0]);
    return { { "mul", IRType::MUL, { dest, dest, parse_operand(operands[1]) }, address } };
}

std::vector<IRInstruction> lift_div(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.empty()) {
        return {};
    }

    return { { "div", IRType::DIV, { RegisterOperand("eax"), RegisterOperand("eax"), parse_operand(operands[0]) }, address } };
}

std::vector<IRInstruction> lift_push(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.empty()) {
        return {};
    }

    return { { "push", IRType::PUSH, { {}, parse_operand(operands[0]) }, address } };
}

std::vector<IRInstruction> lift_pop(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.empty()) {
        return {};
    }

    return { { "pop", IRType::POP, { parse_operand(operands[0]) }, address } };
}

std::vector<IRInstruction> lift_lea(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 2) {
        return {};
    }

    return { { "lea", IRType::LEA, { parse_operand(operands[0]), parse_operand(operands[1]) }, address } };
}

std::vector<IRInstruction> lift_sext(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 2) {
        return {};
    }

    return { { "sext", IRType::SEXT, { parse_operand(operands[0]), parse_operand(operands[1]) }, address } };
}

std::vector<IRInstruction> lift_nop(uint64_t address, const std::vector<std::string>& operands)
{
    return {};
}

std::vector<IRInstruction> lift_setcc(uint64_t address, const std::string& mnemonic, const std::vector<std::string>& operands)
{
    if (operands.empty()) {
        return {};
    }
    const ConditionCode cond = conditionFromSetMnemonic(mnemonic);
    if (cond == ConditionCode::None) {
        return {};
    }
    return { { "setcc", IRType::SETCC, { parse_operand(operands[0]) }, address, cond } };
}

std::vector<IRInstruction> lift_jmp(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 1) {
        return {};
    }

    IROperand target = parse_operand(operands[0]);

    return { { "jmp", IRType::JMP, { target }, address } };
}

std::vector<IRInstruction> lift_call(uint64_t address, const std::vector<std::string>& operands)
{
    if (operands.size() < 1) {
        return {};
    }

    IROperand dest = parse_operand(operands[0]);

    return { { "call", IRType::CALL, { dest }, address } };
}

std::vector<IRInstruction> lift_ret(uint64_t address, const std::vector<std::string>& operands)
{
    return { { "ret", IRType::RET, {}, address } };
}

std::vector<IRInstruction> lift_cjmp(uint64_t address, std::string insn, const std::vector<std::string>& operands)
{
    if (operands.size() < 1) {
        return {};
    }

    IROperand dest = parse_operand(operands[0]);

    return { { "cjmp_" + insn, IRType::CJMP, { dest }, address, conditionFromMnemonic(insn) } };
}
} // namespace Decompiler
