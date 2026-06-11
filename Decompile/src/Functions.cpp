#include "Functions.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_set>

#include "WindowsX64.h"

namespace Decompiler
{
namespace
{
    struct RawFunction {
        uint64_t start_address = 0;
        std::vector<IRInstruction> instructions;
    };

    std::vector<FunctionParameter> buildParameters(const size_t count)
    {
        std::vector<FunctionParameter> parameters;
        parameters.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            parameters.push_back({ .name = "arg_" + std::to_string(i) });
        }
        return parameters;
    }

    std::unordered_map<uint64_t, size_t> buildAddressIndex(const std::vector<IRInstruction>& irs)
    {
        std::unordered_map<uint64_t, size_t> addressToIndex;
        addressToIndex.reserve(irs.size());
        for (size_t i = 0; i < irs.size(); ++i) {
            addressToIndex[irs[i].address] = i;
        }
        return addressToIndex;
    }

    std::vector<uint64_t> collectFunctionSeeds(const std::vector<IRInstruction>& irs, const std::unordered_map<uint64_t, size_t>& addressToIndex)
    {
        std::set<uint64_t> seeds;
        if (!irs.empty()) {
            seeds.insert(irs.front().address);
        }

        for (size_t i = 0; i < irs.size(); ++i) {
            const auto& instruction = irs[i];
            if (IRProperties::isCall(instruction.type)) {
                if (const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).name); target.has_value()) {
                    if (addressToIndex.contains(*target)) {
                        seeds.insert(*target);
                    }
                }
            }

            if (i > 0 && IRProperties::isReturn(irs[i - 1].type)) {
                seeds.insert(instruction.address);
            }
        }

        return { seeds.begin(), seeds.end() };
    }

    std::vector<size_t> discoverFunctionInstructionIndices(
          const std::vector<IRInstruction>& irs,
          const std::unordered_map<uint64_t, size_t>& addressToIndex,
          const std::unordered_set<uint64_t>& allSeeds,
          const uint64_t functionSeedAddress)
    {
        const auto startIt = addressToIndex.find(functionSeedAddress);
        if (startIt == addressToIndex.end()) {
            return {};
        }

        std::queue<size_t> worklist;
        std::unordered_set<size_t> queued;
        std::unordered_set<size_t> visited;

        worklist.push(startIt->second);
        queued.insert(startIt->second);

        while (!worklist.empty()) {
            size_t index = worklist.front();
            worklist.pop();

            for (; index < irs.size(); ++index) {
                if (!visited.insert(index).second) {
                    break;
                }

                const auto& instruction = irs[index];
                if (IRProperties::isReturn(instruction.type)) {
                    break;
                }

                if (IRProperties::isJump(instruction.type)) {
                    if (const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).name); target.has_value()) {
                        if (const auto targetIt = addressToIndex.find(*target); targetIt != addressToIndex.end()) {
                            const bool isTailCallToKnownFunction =
                                  IRProperties::isUnconditionalJump(instruction.type) && *target != functionSeedAddress && allSeeds.contains(*target);

                            if (!isTailCallToKnownFunction && queued.insert(targetIt->second).second) {
                                worklist.push(targetIt->second);
                            }
                        }
                    }

                    if (IRProperties::isConditionalJump(instruction.type) && index + 1 < irs.size()) {
                        if (queued.insert(index + 1).second) {
                            worklist.push(index + 1);
                        }
                    }
                    break;
                }
            }
        }

        std::vector<size_t> indices(visited.begin(), visited.end());
        std::sort(indices.begin(), indices.end());
        return indices;
    }

    bool rawFunctionByAddress(const RawFunction& lhs, const RawFunction& rhs)
    {
        return lhs.start_address < rhs.start_address;
    }

    void claimFunctionFromSeed(
          const std::vector<IRInstruction>& irs,
          const std::unordered_map<uint64_t, size_t>& addressToIndex,
          std::unordered_set<uint64_t>& allSeeds,
          std::vector<int>& ownerByInstruction,
          std::vector<RawFunction>& functions,
          int& functionId,
          const uint64_t seed)
    {
        auto indices = discoverFunctionInstructionIndices(irs, addressToIndex, allSeeds, seed);
        std::vector<size_t> ownedIndices;
        ownedIndices.reserve(indices.size());

        for (const auto index : indices) {
            if (ownerByInstruction[index] != -1)
                continue;
            ownerByInstruction[index] = functionId;
            ownedIndices.push_back(index);
        }

        if (ownedIndices.empty())
            return;

        RawFunction function;
        function.start_address = seed;
        function.instructions.reserve(ownedIndices.size());
        for (const auto index : ownedIndices) {
            function.instructions.push_back(irs[index]);
        }
        functions.push_back(std::move(function));
        ++functionId;
    }

    std::vector<RawFunction> discoverFunctionBodies(const std::vector<IRInstruction>& irs)
    {
        std::vector<RawFunction> functions;
        if (irs.empty()) {
            return functions;
        }

        const auto addressToIndex = buildAddressIndex(irs);
        const auto initialSeeds   = collectFunctionSeeds(irs, addressToIndex);
        std::unordered_set<uint64_t> allSeeds(initialSeeds.begin(), initialSeeds.end());

        std::vector<int> ownerByInstruction(irs.size(), -1);
        int functionId = 0;

        for (const auto seed : initialSeeds) {
            claimFunctionFromSeed(irs, addressToIndex, allSeeds, ownerByInstruction, functions, functionId, seed);
        }

        for (size_t i = 0; i < irs.size(); ++i) {
            if (ownerByInstruction[i] != -1)
                continue;
            const uint64_t newSeed = irs[i].address;
            allSeeds.insert(newSeed);
            claimFunctionFromSeed(irs, addressToIndex, allSeeds, ownerByInstruction, functions, functionId, newSeed);
        }

        std::sort(functions.begin(), functions.end(), rawFunctionByAddress);
        return functions;
    }

    std::string makeFunctionName(const uint64_t address)
    {
        std::ostringstream oss;
        oss << "sub_" << std::hex << address;
        return oss.str();
    }

    bool instructionWritesToPhysReg(const IRInstruction& instruction, const PhysReg physReg)
    {
        if (physReg == PhysReg::None) {
            return false;
        }
        const auto& firstOperand = IRProperties::operandAt(instruction, 0);
        if (firstOperand.tag != OperandTag::Register || firstOperand.reg.physReg != physReg) {
            return false;
        }
        if (IRProperties::isControlFlow(instruction.type)) {
            return false;
        }
        if (instruction.type == IRType::CMP || instruction.type == IRType::PUSH || instruction.type == IRType::STORE) {
            return false;
        }
        return true;
    }

    bool isEpilogueNoiseInstruction(const IRInstruction& instruction)
    {
        const PhysReg dest          = IRProperties::operandAt(instruction, 0).reg.physReg;
        const PhysReg src           = IRProperties::operandAt(instruction, 1).reg.physReg;
        const bool touchesFrameRegs = dest == PhysReg::RSP || dest == PhysReg::RBP || src == PhysReg::RSP || src == PhysReg::RBP;
        if (!touchesFrameRegs) {
            return false;
        }
        return instruction.type == IRType::ASSIGN || instruction.type == IRType::ADD || instruction.type == IRType::SUB || instruction.type == IRType::PUSH ||
               instruction.type == IRType::POP;
    }

    size_t nextMeaningfulInstructionIndex(const std::vector<IRInstruction>& instructions, const size_t from)
    {
        size_t index = from;
        while (index < instructions.size()) {
            const auto& instruction = instructions[index];
            if (IRProperties::isTerminator(instruction.type)) {
                return index;
            }
            if (!isEpilogueNoiseInstruction(instruction)) {
                return index;
            }
            ++index;
        }
        return instructions.size();
    }

    bool hasConcreteReturnPattern(const RawFunction& function)
    {
        const auto& instructions = function.instructions;
        for (size_t i = 0; i < instructions.size(); ++i) {
            const bool writesReturnReg =
                  instructionWritesToPhysReg(instructions[i], PhysReg::RAX) || instructionWritesToPhysReg(instructions[i], PhysReg::XMM0);
            if (!writesReturnReg) {
                continue;
            }
            const size_t next = nextMeaningfulInstructionIndex(instructions, i + 1);
            if (next < instructions.size() && instructions[next].type == IRType::RET) {
                return true;
            }
        }
        return false;
    }

    size_t inferParameterCount(const RawFunction& function, const bool is64Bit)
    {
        std::unordered_set<PhysReg> writtenRegs;
        std::optional<size_t> maxParamIndex = std::nullopt;

        for (const auto& instruction : function.instructions) {
            for (size_t operandIndex = 0; operandIndex < instruction.operands.size(); ++operandIndex) {
                if (!IRProperties::usesOperandAt(instruction, operandIndex)) {
                    continue;
                }
                const auto& operand = instruction.operands[operandIndex];

                if (operand.isHeapDeref() && operand.heapDeref.index.empty() && isFramePointerRegisterName(operand.heapDeref.base)) {
                    if (const auto slotIndex = argumentIndexForStackSlot(operand.heapDeref.displacement, is64Bit);
                        slotIndex.has_value() && (!is64Bit || *slotIndex >= 4)) {
                        maxParamIndex = std::max(maxParamIndex.value_or(0), *slotIndex);
                    }
                    continue;
                }

                if (!is64Bit || operand.tag != OperandTag::Register || operand.reg.physReg == PhysReg::None) {
                    continue;
                }
                const auto argIndex = argumentIndexForPhysReg(operand.reg.physReg);
                if (!argIndex.has_value() || writtenRegs.contains(operand.reg.physReg)) {
                    continue;
                }
                maxParamIndex = std::max(maxParamIndex.value_or(0), *argIndex);
            }

            const PhysReg dest = IRProperties::operandAt(instruction, 0).reg.physReg;
            if (instructionWritesToPhysReg(instruction, dest)) {
                writtenRegs.insert(dest);
            }

            if (IRProperties::isCall(instruction.type)) {
                writtenRegs.insert(PhysReg::RAX);
                writtenRegs.insert(PhysReg::XMM0);
            }
        }

        if (!maxParamIndex.has_value()) {
            return 0;
        }
        return *maxParamIndex + 1;
    }

    std::vector<FunctionSignature> inferFunctionSignatures(const std::vector<RawFunction>& functions, const bool is64Bit)
    {
        std::vector<FunctionSignature> signatures;
        signatures.resize(functions.size());
        std::unordered_map<uint64_t, size_t> functionIndexByAddress;
        functionIndexByAddress.reserve(functions.size());

        for (size_t i = 0; i < functions.size(); ++i) {
            functionIndexByAddress[functions[i].start_address] = i;
            signatures[i].parameters                           = buildParameters(inferParameterCount(functions[i], is64Bit));
            signatures[i].returns_value                        = hasConcreteReturnPattern(functions[i]);
        }

        for (const auto& function : functions) {
            for (size_t i = 0; i < function.instructions.size(); ++i) {
                const auto& instruction = function.instructions[i];
                if (!IRProperties::isCall(instruction.type)) {
                    continue;
                }
                const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).name);
                if (!target.has_value()) {
                    continue;
                }
                const auto functionIt = functionIndexByAddress.find(*target);
                if (functionIt == functionIndexByAddress.end()) {
                    continue;
                }

                const size_t next = nextMeaningfulInstructionIndex(function.instructions, i + 1);
                if (next >= function.instructions.size()) {
                    continue;
                }

                const auto& nextInstruction = function.instructions[next];
                const bool capturesReturn   = (nextInstruction.type == IRType::ASSIGN || nextInstruction.type == IRType::STORE) &&
                                            IRProperties::operandAt(nextInstruction, 1).reg.physReg == PhysReg::RAX;
                if (capturesReturn) {
                    signatures[functionIt->second].returns_value = true;
                }
            }
        }

        return signatures;
    }

} // namespace

std::optional<uint64_t> parseAddressOperand(const std::string& raw)
{
    const auto first = raw.find_first_not_of(' ');
    if (first == std::string::npos) {
        return std::nullopt;
    }
    const auto last             = raw.find_last_not_of(' ');
    const std::string valueText = raw.substr(first, last - first + 1);

    try {
        size_t parsedChars = 0;
        const auto value   = std::stoull(valueText, &parsedChars, 0);
        if (parsedChars != valueText.size()) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::vector<DecompilerFunction> findFunctions(const std::vector<IRInstruction>& irs, const bool is64Bit)
{
    const auto rawFunctions = discoverFunctionBodies(irs);
    const auto signatures   = inferFunctionSignatures(rawFunctions, is64Bit);

    std::vector<DecompilerFunction> functions;
    functions.reserve(rawFunctions.size());
    for (size_t i = 0; i < rawFunctions.size(); ++i) {
        functions.push_back(
              { rawFunctions[i].start_address, makeFunctionName(rawFunctions[i].start_address), false, rawFunctions[i].instructions, signatures[i] });
    }
    return functions;
}

void resolveThunks(std::vector<DecompilerFunction>& functions, const std::unordered_map<uint64_t, std::string>& globalSymbols)
{
    std::unordered_map<uint64_t, std::string> nameByAddr;
    nameByAddr.reserve(functions.size());
    for (const auto& f : functions) {
        nameByAddr[f.start_address] = f.name;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& f : functions) {
            const IRInstruction* jmpInstr = nullptr;
            for (const auto& instr : f.instructions) {
                if (IRProperties::isNop(instr) || IRProperties::isPhi(instr)) {
                    continue;
                }
                if (IRProperties::isUnconditionalJump(instr.type)) {
                    jmpInstr = &instr;
                }
                break;
            }
            if (jmpInstr == nullptr) {
                continue;
            }

            std::string resolved;
            const auto& target = IRProperties::operandAt(*jmpInstr, 0);

            if (const auto addr = parseAddressOperand(target.name); addr.has_value()) {
                const auto it = nameByAddr.find(*addr);
                if (it != nameByAddr.end()) {
                    resolved = it->second;
                }
            }

            if (resolved.empty() && target.isHeapDeref() && target.heapDeref.absolute.has_value()) {
                const auto it = globalSymbols.find(*target.heapDeref.absolute);
                if (it != globalSymbols.end()) {
                    std::string name = it->second;
                    if (name.starts_with("__imp_")) {
                        name = name.substr(6);
                    }
                    if (!name.empty()) {
                        resolved = name;
                    }
                }
            }

            if (!resolved.empty() && resolved != f.name) {
                f.name                      = resolved;
                f.isThunk                   = true;
                nameByAddr[f.start_address] = resolved;
                changed                     = true;
            }
        }
    }
}

std::unordered_map<uint64_t, std::string> functionNamesByAddress(const std::vector<DecompilerFunction>& functions)
{
    std::unordered_map<uint64_t, std::string> names;
    names.reserve(functions.size());
    for (const auto& function : functions) {
        names[function.start_address] = function.name;
    }
    return names;
}

std::string substituteArgumentRegisters(std::string line, const FunctionSignature& signature)
{
    if (signature.parameters.empty()) {
        return line;
    }

    std::string output;
    output.reserve(line.size());
    for (size_t i = 0; i < line.size();) {
        const unsigned char ch = static_cast<unsigned char>(line[i]);
        if (!(std::isalnum(ch) || line[i] == '_')) {
            output.push_back(line[i]);
            ++i;
            continue;
        }

        size_t j = i + 1;
        while (j < line.size()) {
            const unsigned char nextCh = static_cast<unsigned char>(line[j]);
            if (!(std::isalnum(nextCh) || line[j] == '_')) {
                break;
            }
            ++j;
        }

        const std::string token    = line.substr(i, j - i);
        const std::string base     = normalizedRegisterBase(token);
        const auto argIndex        = argumentIndexForRegisterBase(base);
        const bool isRegisterToken = !base.empty() && (token == base || token.starts_with(base + "_"));

        if (argIndex.has_value() && *argIndex < signature.parameters.size() && isRegisterToken) {
            output += signature.parameters[*argIndex].name;
        } else {
            output += token;
        }
        i = j;
    }
    return output;
}
} // namespace Decompiler
