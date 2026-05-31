#include "TypeInference.h"

#include <string>
#include <unordered_map>
#include <vector>

#include "ConditionCode.h"
#include "IRInstruction.h"
#include "Names.h"
#include "WindowsX64.h"

namespace Decompiler
{
namespace
{

    TypeSize sizeFromRegisterBaseName(const std::string& base)
    {
        static const std::unordered_map<std::string, TypeSize> table = {
            // 8-bit
            { "al", TypeSize::Bits8 },
            { "bl", TypeSize::Bits8 },
            { "cl", TypeSize::Bits8 },
            { "dl", TypeSize::Bits8 },
            { "ah", TypeSize::Bits8 },
            { "bh", TypeSize::Bits8 },
            { "ch", TypeSize::Bits8 },
            { "dh", TypeSize::Bits8 },
            { "sil", TypeSize::Bits8 },
            { "dil", TypeSize::Bits8 },
            { "spl", TypeSize::Bits8 },
            { "bpl", TypeSize::Bits8 },
            { "r8b", TypeSize::Bits8 },
            { "r9b", TypeSize::Bits8 },
            { "r10b", TypeSize::Bits8 },
            { "r11b", TypeSize::Bits8 },
            { "r12b", TypeSize::Bits8 },
            { "r13b", TypeSize::Bits8 },
            { "r14b", TypeSize::Bits8 },
            { "r15b", TypeSize::Bits8 },
            // 16-bit
            { "ax", TypeSize::Bits16 },
            { "bx", TypeSize::Bits16 },
            { "cx", TypeSize::Bits16 },
            { "dx", TypeSize::Bits16 },
            { "si", TypeSize::Bits16 },
            { "di", TypeSize::Bits16 },
            { "sp", TypeSize::Bits16 },
            { "bp", TypeSize::Bits16 },
            { "r8w", TypeSize::Bits16 },
            { "r9w", TypeSize::Bits16 },
            { "r10w", TypeSize::Bits16 },
            { "r11w", TypeSize::Bits16 },
            { "r12w", TypeSize::Bits16 },
            { "r13w", TypeSize::Bits16 },
            { "r14w", TypeSize::Bits16 },
            { "r15w", TypeSize::Bits16 },
            // 32-bit
            { "eax", TypeSize::Bits32 },
            { "ebx", TypeSize::Bits32 },
            { "ecx", TypeSize::Bits32 },
            { "edx", TypeSize::Bits32 },
            { "esi", TypeSize::Bits32 },
            { "edi", TypeSize::Bits32 },
            { "esp", TypeSize::Bits32 },
            { "ebp", TypeSize::Bits32 },
            { "r8d", TypeSize::Bits32 },
            { "r9d", TypeSize::Bits32 },
            { "r10d", TypeSize::Bits32 },
            { "r11d", TypeSize::Bits32 },
            { "r12d", TypeSize::Bits32 },
            { "r13d", TypeSize::Bits32 },
            { "r14d", TypeSize::Bits32 },
            { "r15d", TypeSize::Bits32 },
            // 64-bit
            { "rax", TypeSize::Bits64 },
            { "rbx", TypeSize::Bits64 },
            { "rcx", TypeSize::Bits64 },
            { "rdx", TypeSize::Bits64 },
            { "rsi", TypeSize::Bits64 },
            { "rdi", TypeSize::Bits64 },
            { "rsp", TypeSize::Bits64 },
            { "rbp", TypeSize::Bits64 },
            { "r8", TypeSize::Bits64 },
            { "r9", TypeSize::Bits64 },
            { "r10", TypeSize::Bits64 },
            { "r11", TypeSize::Bits64 },
            { "r12", TypeSize::Bits64 },
            { "r13", TypeSize::Bits64 },
            { "r14", TypeSize::Bits64 },
            { "r15", TypeSize::Bits64 },
        };

        const auto it = table.find(base);
        return it != table.end() ? it->second : TypeSize::Unknown;
    }

    TypeSize sizeFromBytes(const size_t bytes)
    {
        switch (bytes) {
        case 1:
            return TypeSize::Bits8;
        case 2:
            return TypeSize::Bits16;
        case 4:
            return TypeSize::Bits32;
        case 8:
            return TypeSize::Bits64;
        default:
            return TypeSize::Unknown;
        }
    }

    struct UnionFind {
        std::unordered_map<std::string, std::string> parent;

        std::string find(const std::string& x)
        {
            if (parent.count(x) == 0) {
                parent[x] = x;
                return x;
            }
            if (parent[x] == x) {
                return x;
            }
            parent[x] = find(parent[x]);
            return parent[x];
        }

        void unite(const std::string& a, const std::string& b)
        {
            const std::string ra = find(a);
            const std::string rb = find(b);
            if (ra != rb) {
                parent[ra] = rb;
            }
        }
    };

    struct TypeEngine {
        TypeMap types;
        UnionFind uf;

        VarType& typeOf(const std::string& var)
        {
            return types[uf.find(var)];
        }

        bool constrainCategory(const std::string& var, TypeCategory cat)
        {
            return typeOf(var).meet(cat);
        }

        bool constrainSign(const std::string& var, TypeSign sign)
        {
            return typeOf(var).meet(sign);
        }

        bool constrainSize(const std::string& var, TypeSize size)
        {
            return typeOf(var).meet(size);
        }

        bool constrainInteger(const std::string& var)
        {
            return constrainCategory(var, TypeCategory::Integer);
        }

        bool constrainSignedInt(const std::string& var)
        {
            bool changed = constrainCategory(var, TypeCategory::Integer);
            changed |= constrainSign(var, TypeSign::Signed);
            return changed;
        }

        bool constrainUnsignedInt(const std::string& var)
        {
            bool changed = constrainCategory(var, TypeCategory::Integer);
            changed |= constrainSign(var, TypeSign::Unsigned);
            return changed;
        }

        bool constrainPointer(const std::string& var)
        {
            return constrainCategory(var, TypeCategory::Pointer);
        }

        bool constrainType(const std::string& var, const VarType& type)
        {
            return typeOf(var).meet(type);
        }

        bool uniteVars(const std::string& a, const std::string& b)
        {
            const std::string ra = uf.find(a);
            const std::string rb = uf.find(b);
            if (ra == rb) {
                return false;
            }

            VarType merged     = types[ra];
            const bool changed = merged.meet(types[rb]);

            uf.unite(a, b);
            types[uf.find(a)] = merged;
            return changed;
        }
    };

    std::string regName(const IROperand& op)
    {
        if (op.isAnyReg() && !op.name.empty()) {
            return op.name;
        }
        return {};
    }

    std::string typedName(const IROperand& op)
    {
        switch (op.tag) {
        case OperandTag::Register:
        case OperandTag::SsaTemp:
        case OperandTag::StackVar:
        case OperandTag::GlobalVar:
            return op.name;
        default:
            return {};
        }
    }

    bool constrainFromOperandSize(TypeEngine& engine, const IROperand& op)
    {
        if (op.sizeBytes == 0)
            return false;
        const std::string name = typedName(op);
        if (name.empty())
            return false;
        const TypeSize sz = sizeFromBytes(op.sizeBytes);
        if (sz == TypeSize::Unknown)
            return false;
        return engine.constrainSize(name, sz);
    }

    struct KnownSignature {
        VarType returnType;
        std::vector<VarType> args;
    };

    VarType typePtr()
    {
        return { TypeCategory::Pointer, TypeSign::Unknown, TypeSize::Unknown };
    }
    VarType typeCharPtr()
    {
        return { TypeCategory::CharPointer, TypeSign::Unknown, TypeSize::Unknown };
    }
    VarType typeInt32()
    {
        return { TypeCategory::Integer, TypeSign::Signed, TypeSize::Bits32 };
    }
    VarType typeUInt64()
    {
        return { TypeCategory::Integer, TypeSign::Unsigned, TypeSize::Bits64 };
    }
    VarType typeVoid()
    {
        return {};
    }

    std::string stripDecorations(const std::string& name)
    {
        const auto at      = name.find('@');
        std::string result = at != std::string::npos ? name.substr(0, at) : name;
        size_t start       = 0;
        while (start < result.size() && result[start] == '_') {
            ++start;
        }
        if (start > 0 && start < result.size()) {
            result = result.substr(start);
        }
        if (result.size() > 6 && result.substr(0, 6) == "mingw_") {
            result = result.substr(6);
        }
        return result;
    }

    const KnownSignature* lookupSignature(const std::string& rawName)
    {
        static const std::unordered_map<std::string, KnownSignature> table = {
            { "printf", { typeInt32(), { typeCharPtr() } } },
            { "fprintf", { typeInt32(), { typePtr(), typeCharPtr() } } },
            { "sprintf", { typeInt32(), { typeCharPtr(), typeCharPtr() } } },
            { "snprintf", { typeInt32(), { typeCharPtr(), typeUInt64(), typeCharPtr() } } },
            { "puts", { typeInt32(), { typeCharPtr() } } },
            { "fputs", { typeInt32(), { typeCharPtr(), typePtr() } } },
            { "malloc", { typePtr(), { typeUInt64() } } },
            { "calloc", { typePtr(), { typeUInt64(), typeUInt64() } } },
            { "realloc", { typePtr(), { typePtr(), typeUInt64() } } },
            { "free", { typeVoid(), { typePtr() } } },
            { "memcpy", { typePtr(), { typePtr(), typePtr(), typeUInt64() } } },
            { "memmove", { typePtr(), { typePtr(), typePtr(), typeUInt64() } } },
            { "memset", { typePtr(), { typePtr(), typeInt32(), typeUInt64() } } },
            { "memcmp", { typeInt32(), { typePtr(), typePtr(), typeUInt64() } } },
            { "strlen", { typeUInt64(), { typeCharPtr() } } },
            { "strcmp", { typeInt32(), { typeCharPtr(), typeCharPtr() } } },
            { "strncmp", { typeInt32(), { typeCharPtr(), typeCharPtr(), typeUInt64() } } },
            { "strcpy", { typeCharPtr(), { typeCharPtr(), typeCharPtr() } } },
            { "strncpy", { typeCharPtr(), { typeCharPtr(), typeCharPtr(), typeUInt64() } } },
            { "fopen", { typePtr(), { typeCharPtr(), typeCharPtr() } } },
            { "fclose", { typeInt32(), { typePtr() } } },
            { "fread", { typeUInt64(), { typePtr(), typeUInt64(), typeUInt64(), typePtr() } } },
            { "fwrite", { typeUInt64(), { typePtr(), typeUInt64(), typeUInt64(), typePtr() } } },
            { "exit", { typeVoid(), { typeInt32() } } },
            { "atoi", { typeInt32(), { typeCharPtr() } } },
            { "atol", { typeInt32(), { typeCharPtr() } } },
            { "strtol", { typeInt32(), { typeCharPtr(), typePtr(), typeInt32() } } },
            { "strtoul", { typeUInt64(), { typeCharPtr(), typePtr(), typeInt32() } } },
            { "fgets", { typeCharPtr(), { typeCharPtr(), typeInt32(), typePtr() } } },
            { "scanf", { typeInt32(), { typeCharPtr() } } },
            { "sscanf", { typeInt32(), { typeCharPtr(), typeCharPtr() } } },
            { "perror", { typeVoid(), { typeCharPtr() } } },
            { "abort", { typeVoid(), {} } },
        };
        const std::string name = stripDecorations(rawName);
        const auto it          = table.find(name);
        return it != table.end() ? &it->second : nullptr;
    }

    std::string findArgVar(const InsBlock& block, const size_t beforeIdx, const std::string& regBase)
    {
        for (size_t i = beforeIdx; i-- > 0;) {
            const auto& instr = block.instructions[i];
            if (instr.operands.empty())
                continue;
            const auto& dest = instr.operands[0];
            if (dest.tag != OperandTag::Register || dest.name.empty())
                continue;
            if (normalizedRegisterBase(ssaBaseName(dest.name)) == regBase) {
                return dest.name;
            }
        }
        return {};
    }

    std::string findReturnVar(const InsBlock& block, const size_t afterIdx)
    {
        for (size_t i = afterIdx + 1; i < block.instructions.size(); ++i) {
            const auto& instr = block.instructions[i];
            if (instr.operands.size() < 2)
                continue;
            const auto& dest = instr.operands[0];
            const auto& src  = instr.operands[1];
            if (src.tag != OperandTag::Register || src.name.empty())
                continue;
            if (normalizedRegisterBase(ssaBaseName(src.name)) != "rax")
                continue;
            if (dest.tag == OperandTag::Register && !dest.name.empty()) {
                return dest.name;
            }
        }
        return {};
    }

    std::string regAt(const std::vector<IROperand>& ops, const size_t i)
    {
        return i < ops.size() ? regName(ops[i]) : std::string{};
    }

    std::string typedAt(const std::vector<IROperand>& ops, const size_t i)
    {
        return i < ops.size() ? typedName(ops[i]) : std::string{};
    }

    bool applyConstraints(TypeEngine& engine, const InsBlock& block, const size_t instrIdx)
    {
        const auto& instr = block.instructions[instrIdx];
        const auto& ops   = instr.operands;
        bool changed      = false;

        for (const auto& op : ops) {
            changed |= constrainFromOperandSize(engine, op);
        }

        switch (instr.type) {
        case IRType::PHI: {
            const std::string dest = regAt(ops, 0);
            if (dest.empty() || ops.size() < 2)
                break;
            for (const auto& input : splitPhiInputs(ops[1].name)) {
                if (!input.empty()) {
                    changed |= engine.uniteVars(dest, input);
                }
            }
            break;
        }

        case IRType::ASSIGN: {
            const std::string dest = typedAt(ops, 0);
            const std::string src  = typedAt(ops, 1);
            if (!dest.empty() && !src.empty()) {
                changed |= engine.uniteVars(dest, src);
            }
            break;
        }

        case IRType::SEXT: {
            const std::string dest = regAt(ops, 0);
            const std::string src  = regAt(ops, 1);
            if (!dest.empty())
                changed |= engine.constrainSignedInt(dest);
            if (!src.empty())
                changed |= engine.constrainSignedInt(src);
            break;
        }

        case IRType::CONVERT: {
            const std::string dest = regAt(ops, 0);
            const std::string src  = typedAt(ops, 1);
            if (!dest.empty()) {
                changed |= ssaBaseName(dest).starts_with("xmm") ? engine.constrainCategory(dest, TypeCategory::Float) : engine.constrainInteger(dest);
                const TypeSize sz = sizeFromBytes(ops[0].sizeBytes);
                if (sz != TypeSize::Unknown)
                    changed |= engine.constrainSize(dest, sz);
            }
            if (!src.empty()) {
                changed |= ssaBaseName(src).starts_with("xmm") ? engine.constrainCategory(src, TypeCategory::Float) : engine.constrainInteger(src);
                const TypeSize sz = ops.size() > 1 ? sizeFromBytes(ops[1].sizeBytes) : TypeSize::Unknown;
                if (sz != TypeSize::Unknown)
                    changed |= engine.constrainSize(src, sz);
            }
            break;
        }

        case IRType::SAR: {
            const std::string lhs = regAt(ops, 1);
            if (!lhs.empty())
                changed |= engine.constrainSignedInt(lhs);
            break;
        }

        case IRType::SHR: {
            const std::string lhs = regAt(ops, 1);
            if (!lhs.empty())
                changed |= engine.constrainUnsignedInt(lhs);
            break;
        }

        case IRType::NEG: {
            const std::string dest = regAt(ops, 0);
            const std::string src  = regAt(ops, 1);
            if (!dest.empty())
                changed |= engine.constrainSignedInt(dest);
            if (!src.empty())
                changed |= engine.constrainSignedInt(src);
            break;
        }

        case IRType::SMUL:
        case IRType::SDIV:
        case IRType::SMOD: {
            for (size_t i = 0; i < ops.size(); ++i) {
                const std::string v = regAt(ops, i);
                if (!v.empty())
                    changed |= engine.constrainSignedInt(v);
            }
            break;
        }

        case IRType::MUL:
        case IRType::DIV:
        case IRType::MOD: {
            for (size_t i = 0; i < ops.size(); ++i) {
                const std::string v = regAt(ops, i);
                if (!v.empty())
                    changed |= engine.constrainUnsignedInt(v);
            }
            break;
        }

        case IRType::LOAD: {
            const std::string dest = regAt(ops, 0);
            const std::string src  = typedAt(ops, 1);
            if (!dest.empty()) {
                const bool destIsXmm = ssaBaseName(dest).starts_with("xmm");
                if (destIsXmm) {
                    changed |= engine.constrainCategory(dest, TypeCategory::Float);
                } else {
                    changed |= engine.constrainInteger(dest);
                }
                if (ops.size() > 1 && ops[1].isHeapDeref()) {
                    const TypeSize sz = sizeFromBytes(ops[1].heapDeref.sizeBytes);
                    if (sz != TypeSize::Unknown) {
                        changed |= engine.constrainSize(dest, sz);
                    }
                } else if (!src.empty()) {
                    changed |= engine.uniteVars(dest, src);
                }
            }
            break;
        }

        case IRType::STORE: {
            const std::string dest = typedAt(ops, 0);
            const std::string src  = typedAt(ops, 1);
            if (!dest.empty() && !src.empty()) {
                changed |= engine.uniteVars(dest, src);
            } else if (!dest.empty() && ops.size() > 1 && ops[1].isImmediate()) {
                changed |= engine.constrainInteger(dest);
            }
            break;
        }

        case IRType::LEA: {
            const std::string dest = regAt(ops, 0);
            if (!dest.empty())
                changed |= engine.constrainPointer(dest);
            break;
        }

        case IRType::CJMP: {
            if (instr.condition == ConditionCode::None)
                break;
            for (size_t i = instrIdx; i-- > 0;) {
                const auto& prev = block.instructions[i];
                if (prev.type != IRType::CMP || prev.operands.size() < 3)
                    continue;
                const std::string lhs = regName(prev.operands[1]);
                const std::string rhs = regName(prev.operands[2]);
                if (isUnsignedCondition(instr.condition)) {
                    if (!lhs.empty())
                        changed |= engine.constrainUnsignedInt(lhs);
                    if (!rhs.empty())
                        changed |= engine.constrainUnsignedInt(rhs);
                } else if (
                      instr.condition == ConditionCode::L || instr.condition == ConditionCode::LE || instr.condition == ConditionCode::G ||
                      instr.condition == ConditionCode::GE) {
                    if (!lhs.empty())
                        changed |= engine.constrainSignedInt(lhs);
                    if (!rhs.empty())
                        changed |= engine.constrainSignedInt(rhs);
                }
                break;
            }
            break;
        }

        case IRType::CALL: {
            if (ops.empty())
                break;
            const KnownSignature* sig = lookupSignature(ops[0].name);
            if (sig == nullptr)
                break;

            for (size_t argIdx = 0; argIdx < sig->args.size(); ++argIdx) {
                const std::string regBase = registerBaseForArgumentIndex(argIdx);
                if (regBase.empty())
                    break;
                const std::string argVar = findArgVar(block, instrIdx, regBase);
                if (!argVar.empty()) {
                    changed |= engine.constrainType(argVar, sig->args[argIdx]);
                }
            }

            if (!sig->returnType.isUnknown()) {
                const std::string retVar = findReturnVar(block, instrIdx);
                if (!retVar.empty()) {
                    changed |= engine.constrainType(retVar, sig->returnType);
                }
            }
            break;
        }

        default:
            break;
        }

        return changed;
    }

    void initFromRegisters(TypeEngine& engine, const Graph& cfg)
    {
        for (const auto& block : cfg.blocks) {
            for (const auto& instr : block.instructions) {
                for (const auto& op : instr.operands) {
                    if (!op.isAnyReg() || op.name.empty())
                        continue;
                    const std::string base = ssaBaseName(op.name);
                    if (base.starts_with("xmm")) {
                        engine.constrainCategory(op.name, TypeCategory::Float);
                        continue;
                    }
                    if (sizeFromRegisterBaseName(base) != TypeSize::Unknown) {
                        engine.constrainInteger(op.name);
                    }
                }
            }
        }
    }

} // namespace

bool VarType::isUnknown() const
{
    return category == TypeCategory::Unknown;
}

bool VarType::meet(const TypeCategory cat)
{
    if (category == cat)
        return false;
    if (category == TypeCategory::Unknown) {
        category = cat;
        return true;
    }
    if (category == TypeCategory::Pointer && cat == TypeCategory::CharPointer) {
        category = TypeCategory::CharPointer;
        return true;
    }
    if (category == TypeCategory::Integer && (cat == TypeCategory::Pointer || cat == TypeCategory::CharPointer)) {
        category = cat;
        return true;
    }
    if (category == TypeCategory::Integer && cat == TypeCategory::Float) {
        category = TypeCategory::Float;
        return true;
    }
    return false;
}

bool VarType::meet(const TypeSign s)
{
    if (sign == s)
        return false;
    if (sign == TypeSign::Unknown) {
        sign = s;
        return true;
    }
    return false;
}

bool VarType::meet(const TypeSize sz)
{
    if (size == sz)
        return false;
    if (size == TypeSize::Unknown) {
        size = sz;
        return true;
    }
    return false;
}

bool VarType::meet(const VarType& other)
{
    bool changed = false;
    if (other.category != TypeCategory::Unknown)
        changed |= meet(other.category);
    if (other.sign != TypeSign::Unknown)
        changed |= meet(other.sign);
    if (other.size != TypeSize::Unknown)
        changed |= meet(other.size);
    return changed;
}

std::string VarType::toString() const
{
    if (category == TypeCategory::Unknown)
        return "unknown";
    if (category == TypeCategory::Pointer)
        return "void*";
    if (category == TypeCategory::CharPointer)
        return "char*";
    if (category == TypeCategory::Float) {
        if (size == TypeSize::Bits32)
            return "float";
        if (size == TypeSize::Bits64)
            return "double";
        return "float";
    }

    const std::string prefix = sign == TypeSign::Unsigned ? "uint" : "int";
    switch (size) {
    case TypeSize::Bits8:
        return prefix + "8_t";
    case TypeSize::Bits16:
        return prefix + "16_t";
    case TypeSize::Bits32:
        return prefix + "32_t";
    case TypeSize::Bits64:
        return prefix + "64_t";
    default:
        return prefix;
    }
}

TypeMap inferTypes(const Graph& cfg)
{
    TypeEngine engine;

    initFromRegisters(engine, cfg);

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t blockId = 0; blockId < cfg.blocks.size(); ++blockId) {
            const auto& block = cfg.blocks[blockId];
            for (size_t instrIdx = 0; instrIdx < block.instructions.size(); ++instrIdx) {
                changed |= applyConstraints(engine, block, instrIdx);
            }
        }
    }

    TypeMap result;
    for (const auto& [var, rep] : engine.uf.parent) {
        result[var] = engine.types[engine.uf.find(var)];
    }

    for (const auto& [var, type] : engine.types) {
        if (result.count(var) == 0) {
            result[var] = type;
        }
    }

    return result;
}

} // namespace Decompiler
