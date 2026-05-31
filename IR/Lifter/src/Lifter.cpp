#include "IR.h"

#include <cctype>
#include <cstdlib>

#include "WindowsX64.h"
#include "ConditionCode.h"
#include "IRInstruction.h"
#include "IROperand.h"
#include "IRProperties.h"
#include "IRType.h"
#include "InstructionInfo.h"

namespace Decompiler
{
namespace
{
    std::string trim(const std::string& value)
    {
        const auto first = value.find_first_not_of(' ');
        if (first == std::string::npos) {
            return {};
        }
        const auto last = value.find_last_not_of(' ');
        return value.substr(first, last - first + 1);
    }

    size_t memorySizeBytes(const std::string& value, const size_t bracketPos)
    {
        const std::string prefix = trim(value.substr(0, bracketPos));
        if (prefix.starts_with("byte ptr"))
            return 1;
        if (prefix.starts_with("word ptr"))
            return 2;
        if (prefix.starts_with("dword ptr"))
            return 4;
        if (prefix.starts_with("qword ptr"))
            return 8;
        if (prefix.starts_with("xmmword ptr"))
            return 16;
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

    void applyMemoryTerm(HeapDerefData& memory, std::string token, const int sign)
    {
        token = trim(std::move(token));
        if (token.empty()) {
            return;
        }

        const auto star = token.find('*');
        if (star != std::string::npos) {
            memory.index  = normalizedRegisterBase(trim(token.substr(0, star)));
            int64_t scale = 1;
            if (parseIntegerToken(trim(token.substr(star + 1)), scale)) {
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
            return;
        }

        if (memory.base.empty()) {
            memory.base = normalizedRegisterBase(token);
        } else if (memory.index.empty()) {
            memory.index = normalizedRegisterBase(token);
        }
    }

    std::optional<HeapDerefData> parseMemoryAddress(const std::string& value)
    {
        const auto lb = value.find('[');
        const auto rb = value.find(']');
        if (lb == std::string::npos || rb == std::string::npos || rb <= lb) {
            return std::nullopt;
        }

        HeapDerefData memory;
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
        if (mnemonic == "je" || mnemonic == "jz")
            return ConditionCode::E;
        if (mnemonic == "jne" || mnemonic == "jnz")
            return ConditionCode::NE;
        if (mnemonic == "jl" || mnemonic == "jnge")
            return ConditionCode::L;
        if (mnemonic == "jle" || mnemonic == "jng")
            return ConditionCode::LE;
        if (mnemonic == "jg" || mnemonic == "jnle")
            return ConditionCode::G;
        if (mnemonic == "jge" || mnemonic == "jnl")
            return ConditionCode::GE;
        if (mnemonic == "jb" || mnemonic == "jnae" || mnemonic == "jc")
            return ConditionCode::B;
        if (mnemonic == "jbe" || mnemonic == "jna")
            return ConditionCode::BE;
        if (mnemonic == "ja" || mnemonic == "jnbe")
            return ConditionCode::A;
        if (mnemonic == "jae" || mnemonic == "jnb" || mnemonic == "jnc")
            return ConditionCode::AE;
        if (mnemonic == "jns")
            return ConditionCode::GE;
        if (mnemonic == "js")
            return ConditionCode::L;
        return ConditionCode::None;
    }

    IROperand parseOperand(const std::string& raw)
    {
        const std::string value = trim(raw);
        if (value.empty()) {
            return RegisterOperand(value);
        }

        if (value.find('[') != std::string::npos) {
            IROperand op;
            op.tag  = OperandTag::HeapDeref;
            op.name = value;
            if (auto memory = parseMemoryAddress(value); memory.has_value()) {
                op.heapDeref = std::move(*memory);
            }
            return op;
        }

        if (std::isdigit(static_cast<unsigned char>(value[0])) || (value.size() > 1 && value[0] == '0' && value[1] == 'x') || value[0] == '-') {
            return ImmediateOperand(value);
        }

        return RegisterOperand(normalizedRegisterBase(value));
    }

    std::vector<std::string> splitOperands(const std::string& operands)
    {
        std::vector<std::string> parts;
        std::string current;
        int depth = 0;

        for (const char c : operands) {
            if (c == '[')
                depth++;
            if (c == ']')
                depth--;
            if (c == ',' && depth == 0) {
                parts.push_back(trim(current));
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            parts.push_back(trim(current));
        }
        return parts;
    }

    void resolveMemoryOperand(IROperand& operand, const uint64_t instructionAddress, const uint16_t instructionSize)
    {
        if (!operand.isHeapDeref()) {
            return;
        }

        auto& memory = operand.heapDeref;
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

    std::vector<IRInstruction> liftInplaceBinary(const uint64_t address, const std::vector<std::string>& operands, const std::string& op, const IRType type)
    {
        if (operands.size() < 2) {
            return {};
        }
        const IROperand dest = parseOperand(operands[0]);
        const IROperand src2 = parseOperand(operands[1]);
        return { { op, type, { dest, dest, src2 }, address } };
    }

    std::vector<IRInstruction> liftShift(const uint64_t address, const std::vector<std::string>& operands, const std::string& op, const IRType type)
    {
        if (operands.empty()) {
            return {};
        }
        const IROperand dest = parseOperand(operands[0]);
        const IROperand src2 = operands.size() >= 2 ? parseOperand(operands[1]) : ImmediateOperand(1);
        return { { op, type, { dest, dest, src2 }, address } };
    }

    std::vector<IRInstruction> liftUnary(const uint64_t address, const std::vector<std::string>& operands, const std::string& op, const IRType type)
    {
        if (operands.empty()) {
            return {};
        }
        const IROperand dest = parseOperand(operands[0]);
        return { { op, type, { dest, dest }, address } };
    }

    std::vector<IRInstruction> liftMov(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.size() < 2) {
            return {};
        }

        IROperand dest = parseOperand(operands[0]);
        IROperand src  = parseOperand(operands[1]);

        if (dest.isHeapDeref()) {
            return { { "store", IRType::STORE, { dest, src }, address } };
        }
        if (src.isHeapDeref()) {
            return { { "load", IRType::LOAD, { dest, src }, address } };
        }
        if (src.isImmediate()) {
            return { { "load_const", IRType::LOAD_CONST, { dest, src }, address } };
        }
        return { { "assign", IRType::ASSIGN, { dest, src }, address } };
    }

    std::vector<IRInstruction> liftFloatBinary(const uint64_t address, const std::vector<std::string>& operands, const std::string& op, const IRType type)
    {
        if (operands.size() >= 3) {
            return { { op, type, { parseOperand(operands[0]), parseOperand(operands[1]), parseOperand(operands[2]) }, address } };
        }
        return liftInplaceBinary(address, operands, op, type);
    }

    uint32_t scalarFloatWidth(const std::string& typeToken)
    {
        if (typeToken == "ss") {
            return 4;
        }
        if (typeToken == "sd") {
            return 8;
        }
        return 0;
    }

    void applyConvertWidth(IROperand& operand, const uint32_t width)
    {
        if (width == 0) {
            return;
        }
        operand.sizeBytes = width;
        if (operand.isHeapDeref()) {
            operand.heapDeref.sizeBytes = width;
        }
    }

    std::vector<IRInstruction> liftConvert(const uint64_t address, const std::string& mnemonic, const std::vector<std::string>& operands)
    {
        if (operands.size() < 2) {
            return {};
        }
        const size_t srcIndex = operands.size() >= 3 ? 2 : 1;

        IROperand dest = parseOperand(operands[0]);
        IROperand src  = parseOperand(operands[srcIndex]);

        std::string form = mnemonic;
        if (form.starts_with("v")) {
            form = form.substr(1);
        }
        form = form.substr(3);
        if (form.starts_with("t")) {
            form = form.substr(1);
        }

        const auto separator = form.find('2');
        if (separator != std::string::npos) {
            applyConvertWidth(src, scalarFloatWidth(form.substr(0, separator)));
            applyConvertWidth(dest, scalarFloatWidth(form.substr(separator + 1)));
        }

        return { { "convert", IRType::CONVERT, { dest, src }, address } };
    }

    std::vector<IRInstruction> liftMovFloat(uint64_t address, const std::vector<std::string>& operands, const size_t sizeBytes)
    {
        if (operands.size() < 2) {
            return {};
        }

        IROperand dest = parseOperand(operands[0]);
        IROperand src  = parseOperand(operands[1]);

        if (dest.isHeapDeref()) {
            dest.heapDeref.sizeBytes = sizeBytes;
            return { { "store", IRType::STORE, { dest, src }, address } };
        }
        if (src.isHeapDeref()) {
            src.heapDeref.sizeBytes = sizeBytes;
            return { { "load", IRType::LOAD, { dest, src }, address } };
        }
        if (src.isImmediate()) {
            return { { "load_const", IRType::LOAD_CONST, { dest, src }, address } };
        }
        return { { "assign", IRType::ASSIGN, { dest, src }, address } };
    }

    std::vector<IRInstruction> liftAdd(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.size() < 2) {
            return {};
        }
        const IROperand dest = parseOperand(operands[0]);
        const IROperand src2 = parseOperand(operands[1]);
        return { { "add", IRType::ADD, { dest, dest, src2 }, address } };
    }

    std::vector<IRInstruction> liftSub(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.size() < 2) {
            return {};
        }
        const IROperand dest = parseOperand(operands[0]);
        const IROperand src2 = parseOperand(operands[1]);
        return { { "sub", IRType::SUB, { dest, dest, src2 }, address } };
    }

    std::vector<IRInstruction> liftCmp(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.size() < 2) {
            return {};
        }
        return { { "cmp", IRType::CMP, { {}, parseOperand(operands[0]), parseOperand(operands[1]) }, address } };
    }

    std::vector<IRInstruction> liftTest(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.size() < 2) {
            return {};
        }
        return { { "test", IRType::TEST, { {}, parseOperand(operands[0]), parseOperand(operands[1]) }, address } };
    }

    std::vector<IRInstruction> liftAnd(uint64_t address, const std::vector<std::string>& operands)
    {
        return liftInplaceBinary(address, operands, "and", IRType::AND);
    }

    std::vector<IRInstruction> liftOr(uint64_t address, const std::vector<std::string>& operands)
    {
        return liftInplaceBinary(address, operands, "or", IRType::OR);
    }

    std::vector<IRInstruction> liftXor(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.size() < 2) {
            return {};
        }
        const IROperand dest = parseOperand(operands[0]);
        const IROperand src  = parseOperand(operands[1]);
        if (operands[0] == operands[1]) {
            return { { "load_const", IRType::LOAD_CONST, { dest, ImmediateOperand(0) }, address } };
        }
        return { { "xor", IRType::XOR, { dest, dest, src }, address } };
    }

    std::vector<IRInstruction> liftShl(uint64_t address, const std::vector<std::string>& operands)
    {
        return liftShift(address, operands, "shl", IRType::SHL);
    }

    std::vector<IRInstruction> liftShr(uint64_t address, const std::vector<std::string>& operands)
    {
        return liftShift(address, operands, "shr", IRType::SHR);
    }

    std::vector<IRInstruction> liftSar(uint64_t address, const std::vector<std::string>& operands)
    {
        return liftShift(address, operands, "sar", IRType::SAR);
    }

    std::vector<IRInstruction> liftInc(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        const IROperand dest = parseOperand(operands[0]);
        return { { "add", IRType::ADD, { dest, dest, ImmediateOperand(static_cast<uint64_t>(1)) }, address } };
    }

    std::vector<IRInstruction> liftDec(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        const IROperand dest = parseOperand(operands[0]);
        return { { "sub", IRType::SUB, { dest, dest, ImmediateOperand(static_cast<uint64_t>(1)) }, address } };
    }

    std::vector<IRInstruction> liftNeg(uint64_t address, const std::vector<std::string>& operands)
    {
        return liftUnary(address, operands, "neg", IRType::NEG);
    }

    std::vector<IRInstruction> liftNot(uint64_t address, const std::vector<std::string>& operands)
    {
        return liftUnary(address, operands, "not", IRType::NOT);
    }

    std::vector<IRInstruction> liftImul(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.size() >= 3) {
            return { { "smul", IRType::SMUL, { parseOperand(operands[0]), parseOperand(operands[1]), parseOperand(operands[2]) }, address } };
        }
        if (operands.size() == 2) {
            const auto dest = parseOperand(operands[0]);
            return { { "smul", IRType::SMUL, { dest, dest, parseOperand(operands[1]) }, address } };
        }
        if (operands.size() == 1) {
            return { { "smul", IRType::SMUL, { parseOperand(operands[0]) }, address } };
        }
        return {};
    }

    std::vector<IRInstruction> liftMul(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        if (operands.size() == 1) {
            return { { "mul", IRType::MUL, { parseOperand(operands[0]) }, address } };
        }
        const auto dest = parseOperand(operands[0]);
        return { { "mul", IRType::MUL, { dest, dest, parseOperand(operands[1]) }, address } };
    }

    std::vector<IRInstruction> liftIdiv(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        const IROperand divisor = parseOperand(operands[0]);
        return {
            { "smod", IRType::SMOD, { RegisterOperand("rdx"), RegisterOperand("rax"), divisor }, address },
            { "sdiv", IRType::SDIV, { RegisterOperand("rax"), RegisterOperand("rax"), divisor }, address },
        };
    }

    std::vector<IRInstruction> liftDiv(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        const IROperand divisor = parseOperand(operands[0]);
        return {
            { "mod", IRType::MOD, { RegisterOperand("rdx"), RegisterOperand("rax"), divisor }, address },
            { "div", IRType::DIV, { RegisterOperand("rax"), RegisterOperand("rax"), divisor }, address },
        };
    }

    std::vector<IRInstruction> liftPush(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        return { { "push", IRType::PUSH, { {}, parseOperand(operands[0]) }, address } };
    }

    std::vector<IRInstruction> liftPop(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        return { { "pop", IRType::POP, { parseOperand(operands[0]) }, address } };
    }

    std::vector<IRInstruction> liftLea(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.size() < 2) {
            return {};
        }

        const IROperand dest = parseOperand(operands[0]);
        const IROperand src  = parseOperand(operands[1]);

        if (!src.isHeapDeref()) {
            return { { "lea", IRType::LEA, { dest, src }, address } };
        }

        const HeapDerefData& mem = src.heapDeref;

        if (mem.ripRelative || mem.base == "rbp" || mem.base == "rsp") {
            return { { "lea", IRType::LEA, { dest, src }, address } };
        }

        if (!mem.index.empty() && mem.scale > 1 && dest.name == mem.base) {
            return { { "lea", IRType::LEA, { dest, src }, address } };
        }

        if (mem.base.empty() && mem.index.empty()) {
            const std::string addrStr = mem.absolute.has_value() ? std::to_string(*mem.absolute) : std::to_string(mem.displacement);
            return { { "load_const", IRType::LOAD_CONST, { dest, ImmediateOperand(addrStr) }, address } };
        }

        std::vector<IRInstruction> result;
        bool destHasValue = false;

        if (!mem.index.empty()) {
            const IROperand indexReg = RegisterOperand(mem.index);
            const int scale          = mem.scale <= 0 ? 1 : mem.scale;

            if (scale == 2) {
                result.push_back({ "shl", IRType::SHL, { dest, indexReg, ImmediateOperand(static_cast<uint64_t>(1)) }, address });
                destHasValue = true;
            } else if (scale == 4) {
                result.push_back({ "shl", IRType::SHL, { dest, indexReg, ImmediateOperand(static_cast<uint64_t>(2)) }, address });
                destHasValue = true;
            } else if (scale == 8) {
                result.push_back({ "shl", IRType::SHL, { dest, indexReg, ImmediateOperand(static_cast<uint64_t>(3)) }, address });
                destHasValue = true;
            } else if (scale > 1) {
                result.push_back({ "mul", IRType::MUL, { dest, indexReg, ImmediateOperand(static_cast<uint64_t>(scale)) }, address });
                destHasValue = true;
            }

            if (!mem.base.empty()) {
                const IROperand baseReg   = RegisterOperand(mem.base);
                const IROperand scaledIdx = destHasValue ? dest : indexReg;
                result.push_back({ "add", IRType::ADD, { dest, baseReg, scaledIdx }, address });
                destHasValue = true;
            } else if (!destHasValue) {
                result.push_back({ "assign", IRType::ASSIGN, { dest, indexReg }, address });
                destHasValue = true;
            }
        }

        if (mem.displacement != 0) {
            const IROperand lhs = destHasValue ? dest : RegisterOperand(mem.base);
            if (mem.displacement > 0) {
                result.push_back({ "add", IRType::ADD, { dest, lhs, ImmediateOperand(static_cast<uint64_t>(mem.displacement)) }, address });
            } else {
                result.push_back({ "sub", IRType::SUB, { dest, lhs, ImmediateOperand(static_cast<uint64_t>(-mem.displacement)) }, address });
            }
            destHasValue = true;
        }

        if (result.empty()) {
            result.push_back({ "assign", IRType::ASSIGN, { dest, RegisterOperand(mem.base) }, address });
        }

        return result;
    }

    std::vector<IRInstruction> liftSext(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.size() < 2) {
            return {};
        }
        return { { "sext", IRType::SEXT, { parseOperand(operands[0]), parseOperand(operands[1]) }, address } };
    }

    std::vector<IRInstruction> liftNop([[maybe_unused]] uint64_t address, [[maybe_unused]] const std::vector<std::string>& operands)
    {
        return {};
    }

    std::vector<IRInstruction> liftSetcc(uint64_t address, const std::string& mnemonic, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        const ConditionCode cond = conditionFromSetMnemonic(mnemonic);
        if (cond == ConditionCode::None) {
            return {};
        }
        return { { "setcc", IRType::SETCC, { parseOperand(operands[0]) }, address, cond } };
    }

    std::vector<IRInstruction> liftJmp(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        return { { "jmp", IRType::JMP, { parseOperand(operands[0]) }, address } };
    }

    std::vector<IRInstruction> liftCall(uint64_t address, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        return { { "call", IRType::CALL, { parseOperand(operands[0]) }, address } };
    }

    std::vector<IRInstruction> liftRet(uint64_t address, [[maybe_unused]] const std::vector<std::string>& operands)
    {
        return { { "ret", IRType::RET, {}, address } };
    }

    std::vector<IRInstruction> liftCjmp(uint64_t address, const std::string& insn, const std::vector<std::string>& operands)
    {
        if (operands.empty()) {
            return {};
        }
        return { { "cjmp_" + insn, IRType::CJMP, { parseOperand(operands[0]) }, address, conditionFromMnemonic(insn) } };
    }

    std::vector<IRInstruction> liftInstruction(const AssemblyInstruction& instruction)
    {
        auto ops            = splitOperands(instruction.operands);
        const auto mnemonic = InstructionInfo::canonicalizeMnemonic(instruction.mnemonic);

        std::vector<IRInstruction> result;

        if (mnemonic == "mov") {
            result = liftMov(instruction.address, ops);
        } else if (mnemonic == "add") {
            result = liftAdd(instruction.address, ops);
        } else if (mnemonic == "sub") {
            result = liftSub(instruction.address, ops);
        } else if (mnemonic == "jmp") {
            result = liftJmp(instruction.address, ops);
        } else if (mnemonic == "call") {
            result = liftCall(instruction.address, ops);
        } else if (mnemonic == "ret") {
            result = liftRet(instruction.address, ops);
        } else if (!mnemonic.empty() && mnemonic[0] == 'j') {
            result = liftCjmp(instruction.address, mnemonic, ops);
        } else if (mnemonic == "cmp") {
            result = liftCmp(instruction.address, ops);
        } else if (mnemonic == "test") {
            result = liftTest(instruction.address, ops);
        } else if (mnemonic == "and") {
            result = liftAnd(instruction.address, ops);
        } else if (mnemonic == "or") {
            result = liftOr(instruction.address, ops);
        } else if (
              mnemonic == "xor" || mnemonic == "pxor" || mnemonic == "xorps" || mnemonic == "xorpd" || mnemonic == "vpxor" || mnemonic == "vxorps" ||
              mnemonic == "vxorpd") {
            result = liftXor(instruction.address, ops);
        } else if (mnemonic == "shl" || mnemonic == "sal") {
            result = liftShl(instruction.address, ops);
        } else if (mnemonic == "shr") {
            result = liftShr(instruction.address, ops);
        } else if (mnemonic == "sar") {
            result = liftSar(instruction.address, ops);
        } else if (mnemonic == "inc") {
            result = liftInc(instruction.address, ops);
        } else if (mnemonic == "dec") {
            result = liftDec(instruction.address, ops);
        } else if (mnemonic == "neg") {
            result = liftNeg(instruction.address, ops);
        } else if (mnemonic == "not") {
            result = liftNot(instruction.address, ops);
        } else if (mnemonic == "imul") {
            result = liftImul(instruction.address, ops);
        } else if (mnemonic == "mul") {
            result = liftMul(instruction.address, ops);
        } else if (mnemonic == "idiv") {
            result = liftIdiv(instruction.address, ops);
        } else if (mnemonic == "div") {
            result = liftDiv(instruction.address, ops);
        } else if (mnemonic == "cdq" || mnemonic == "cltd" || mnemonic == "cqo") {
            const uint64_t signShift = mnemonic == "cqo" ? 63 : 31;
            result = { { "sar", IRType::SAR, { RegisterOperand("rdx"), RegisterOperand("rax"), ImmediateOperand(signShift) }, instruction.address } };
        } else if (mnemonic == "cwde" || mnemonic == "cwtl") {
            result = { { "sext", IRType::SEXT, { RegisterOperand("rax"), RegisterOperand("rax") }, instruction.address } };
        } else if (mnemonic == "cdqe" || mnemonic == "cltq") {
            result = { { "sext", IRType::SEXT, { RegisterOperand("rax"), RegisterOperand("rax") }, instruction.address } };
        } else if (mnemonic == "push") {
            result = liftPush(instruction.address, ops);
        } else if (mnemonic == "pop") {
            result = liftPop(instruction.address, ops);
        } else if (mnemonic == "lea") {
            result = liftLea(instruction.address, ops);
        } else if (mnemonic == "movsx" || mnemonic == "movsxd") {
            result = liftSext(instruction.address, ops);
        } else if (mnemonic == "movzx") {
            result = liftMov(instruction.address, ops);
        } else if (mnemonic == "movd" || mnemonic == "vmovd") {
            result = liftMov(instruction.address, ops);
        } else if (mnemonic == "movq" || mnemonic == "vmovq") {
            result = liftMov(instruction.address, ops);
        } else if (mnemonic == "movss" || mnemonic == "vmovss") {
            result = liftMovFloat(instruction.address, ops, 4);
        } else if (mnemonic == "movsd" || mnemonic == "vmovsd") {
            result = liftMovFloat(instruction.address, ops, 8);
        } else if (mnemonic == "movaps" || mnemonic == "vmovaps" || mnemonic == "movups" || mnemonic == "vmovups") {
            result = liftMovFloat(instruction.address, ops, 4);
        } else if (
              mnemonic == "movapd" || mnemonic == "vmovapd" || mnemonic == "movupd" || mnemonic == "vmovupd" || mnemonic == "movdqa" || mnemonic == "vmovdqa" ||
              mnemonic == "movdqu" || mnemonic == "vmovdqu") {
            result = liftMovFloat(instruction.address, ops, 8);
        } else if (mnemonic == "nop" || mnemonic == "leave" || mnemonic == "endbr64") {
            result = liftNop(instruction.address, ops);
        } else if (mnemonic.size() >= 4 && mnemonic.starts_with("set")) {
            result = liftSetcc(instruction.address, mnemonic, ops);
        } else if (mnemonic == "addss" || mnemonic == "addsd" || mnemonic == "vaddss" || mnemonic == "vaddsd") {
            result = liftFloatBinary(instruction.address, ops, "add", IRType::ADD);
        } else if (mnemonic == "subss" || mnemonic == "subsd" || mnemonic == "vsubss" || mnemonic == "vsubsd") {
            result = liftFloatBinary(instruction.address, ops, "sub", IRType::SUB);
        } else if (mnemonic == "mulss" || mnemonic == "mulsd" || mnemonic == "vmulss" || mnemonic == "vmulsd") {
            result = liftFloatBinary(instruction.address, ops, "mul", IRType::MUL);
        } else if (mnemonic == "divss" || mnemonic == "divsd" || mnemonic == "vdivss" || mnemonic == "vdivsd") {
            result = liftFloatBinary(instruction.address, ops, "div", IRType::DIV);
        } else if (
              mnemonic == "cvtss2sd" || mnemonic == "cvtsd2ss" || mnemonic == "vcvtss2sd" || mnemonic == "vcvtsd2ss" || mnemonic == "cvtsi2sd" ||
              mnemonic == "cvtsi2ss" || mnemonic == "vcvtsi2sd" || mnemonic == "vcvtsi2ss" || mnemonic == "cvttss2si" || mnemonic == "cvttsd2si" ||
              mnemonic == "cvtss2si" || mnemonic == "cvtsd2si" || mnemonic == "vcvttss2si" || mnemonic == "vcvttsd2si") {
            result = liftConvert(instruction.address, mnemonic, ops);
        } else if (
              mnemonic == "ucomiss" || mnemonic == "ucomisd" || mnemonic == "vucomiss" || mnemonic == "vucomisd" || mnemonic == "comiss" ||
              mnemonic == "comisd" || mnemonic == "vcomiss" || mnemonic == "vcomisd") {
            result = liftCmp(instruction.address, ops);
        } else {
            IROperand dest = ops.size() > 0 ? parseOperand(ops[0]) : IROperand{};
            IROperand src  = ops.size() > 1 ? parseOperand(ops[1]) : IROperand{};
            result         = { { "[unknown] " + mnemonic, IRType::UNKNOWN, { dest, src }, instruction.address } };
        }

        resolveMemoryOperands(result, instruction);
        return result;
    }
} // namespace

std::vector<IRInstruction> transformASMToIR(const std::vector<AssemblyInstruction>& instructions)
{
    std::vector<IRInstruction> result;

    for (const auto& instr : instructions) {
        auto irs = liftInstruction(instr);
        result.insert(result.end(), irs.begin(), irs.end());
    }

    return result;
}

} // namespace Decompiler
