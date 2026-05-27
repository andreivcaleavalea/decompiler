#pragma once

#include "IRCategory.h"
#include "IRInstruction.h"
#include "IRType.h"

namespace Decompiler::IRProperties
{
inline bool hasOperandAt(const IRInstruction& instruction, const size_t index)
{
    return index < instruction.operands.size();
}

inline const IROperand& operandAt(const IRInstruction& instruction, const size_t index)
{
    static const IROperand empty{};
    if (!hasOperandAt(instruction, index)) {
        return empty;
    }
    return instruction.operands[index];
}

inline bool isRegister(const IROperand& operand)
{
    return operand.type == OpType::REG && !operand.value.empty();
}

inline IRCategory categoryOf(const IRType type)
{
    switch (type) {
    case IRType::ASSIGN:
    case IRType::LOAD:
    case IRType::LOAD_CONST:
    case IRType::STORE:
    case IRType::LEA:
    case IRType::SEXT:
    case IRType::SETCC:
        return IRCategory::Data;

    case IRType::ADD:
    case IRType::SUB:
    case IRType::MUL:
    case IRType::DIV:
    case IRType::MOD:
    case IRType::NEG:
        return IRCategory::Arithmetic;

    case IRType::AND:
    case IRType::OR:
    case IRType::XOR:
    case IRType::NOT:
    case IRType::SHL:
    case IRType::SHR:
    case IRType::SAR:
        return IRCategory::Bitwise;

    case IRType::CMP:
    case IRType::TEST:
        return IRCategory::Flag;

    case IRType::JMP:
    case IRType::CJMP:
        return IRCategory::Jump;

    case IRType::CALL:
        return IRCategory::Call;

    case IRType::RET:
        return IRCategory::Return;

    case IRType::PUSH:
    case IRType::POP:
        return IRCategory::Stack;

    case IRType::PHI:
    case IRType::NOP:
    case IRType::UNKNOWN:
        return IRCategory::Unknown;
    }
    return IRCategory::Unknown;
}

inline bool isArithmetic(const IRType type)
{
    return categoryOf(type) == IRCategory::Arithmetic;
}

inline bool isBitwise(const IRType type)
{
    return categoryOf(type) == IRCategory::Bitwise;
}

inline bool isDataMovement(const IRType type)
{
    return categoryOf(type) == IRCategory::Data;
}

inline bool isStackOperation(const IRType type)
{
    return categoryOf(type) == IRCategory::Stack;
}

inline bool isConditionalJump(const IRType type)
{
    return type == IRType::CJMP;
}

inline bool isUnconditionalJump(const IRType type)
{
    return type == IRType::JMP;
}

inline bool isJump(const IRType type)
{
    return type == IRType::JMP || type == IRType::CJMP;
}

inline bool isCall(const IRType type)
{
    return type == IRType::CALL;
}

inline bool isReturn(const IRType type)
{
    return type == IRType::RET;
}

inline bool isAssign(const IRInstruction& instruction)
{
    return instruction.type == IRType::ASSIGN;
}

inline bool isLoad(const IRInstruction& instruction)
{
    return instruction.type == IRType::LOAD;
}

inline bool isLoadConst(const IRInstruction& instruction)
{
    return instruction.type == IRType::LOAD_CONST;
}

inline bool isPhi(const IRInstruction& instruction)
{
    return instruction.type == IRType::PHI;
}

inline bool isNop(const IRInstruction& instruction)
{
    return instruction.type == IRType::NOP;
}

inline bool isTerminator(const IRType type)
{
    return isJump(type) || isReturn(type);
}

inline bool isControlFlow(const IRType type)
{
    return isJump(type) || isCall(type) || isReturn(type);
}

inline bool producesValue(const IRType type)
{
    return isDataMovement(type) || isArithmetic(type) || isBitwise(type);
}

inline bool isSimpleValueProducer(const IRType type)
{
    return type == IRType::ASSIGN || type == IRType::LOAD || type == IRType::LOAD_CONST || type == IRType::SEXT || type == IRType::LEA;
}

inline bool isPureExpression(const IRType type)
{
    return isSimpleValueProducer(type) || isArithmetic(type) || isBitwise(type);
}

inline bool isAssignmentLike(const IRType type)
{
    return type == IRType::ASSIGN || type == IRType::LOAD || type == IRType::STORE || type == IRType::SEXT;
}

inline bool isBinaryExpression(const IRType type)
{
    switch (type) {
    case IRType::ADD:
    case IRType::SUB:
    case IRType::MUL:
    case IRType::DIV:
    case IRType::MOD:
    case IRType::AND:
    case IRType::OR:
    case IRType::XOR:
    case IRType::SHL:
    case IRType::SHR:
    case IRType::SAR:
        return true;
    default:
        return false;
    }
}

inline bool createsNewValue(const IRInstruction& instruction)
{
    return hasOperandAt(instruction, 0) && isRegister(operandAt(instruction, 0)) && producesValue(instruction.type);
}

inline bool definesOperandAt(const IRInstruction& instruction, const size_t index)
{
    return index == 0 && createsNewValue(instruction);
}

inline bool usesOperandAt(const IRInstruction& instruction, const size_t index)
{
    if (!hasOperandAt(instruction, index)) {
        return false;
    }
    return !definesOperandAt(instruction, index);
}

inline bool producesFlags(const IRType type)
{
    switch (type) {
    case IRType::CMP:
    case IRType::TEST:
    case IRType::ADD:
    case IRType::SUB:
    case IRType::AND:
    case IRType::OR:
    case IRType::XOR:
    case IRType::SHL:
    case IRType::SHR:
    case IRType::SAR:
    case IRType::NEG:
        return true;
    default:
        return false;
    }
}

inline bool hasSideEffects(const IRType type)
{
    return isControlFlow(type) || isStackOperation(type) || type == IRType::STORE;
}
} // namespace Decompiler::IRProperties
