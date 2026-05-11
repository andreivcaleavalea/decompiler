#include "Functions.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <optional>
#include <queue>
#include <set>
#include <sstream>
#include <unordered_set>

namespace Decompiler {
    std::optional<size_t> argumentIndexForRegisterBase(const std::string& base, CallingConvention cc);

    namespace {
        struct RawFunction {
            uint64_t start_address = 0;
            std::vector<IRInstruction> instructions;
        };

        struct SignatureInferenceResult {
            std::vector<CallingConvention> calling_conventions;
            std::vector<FunctionSignature> signatures;
        };

        std::vector<FunctionParameter> buildParameters(const size_t count) {
            std::vector<FunctionParameter> parameters;
            parameters.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                parameters.push_back({.name = "arg_" + std::to_string(i)});
            }
            return parameters;
        }

        std::unordered_map<uint64_t, size_t> buildAddressIndex(const std::vector<IRInstruction>& irs) {
            std::unordered_map<uint64_t, size_t> addressToIndex;
            addressToIndex.reserve(irs.size());
            for (size_t i = 0; i < irs.size(); ++i) {
                addressToIndex[irs[i].address] = i;
            }
            return addressToIndex;
        }

        std::vector<uint64_t> collectFunctionSeeds(const std::vector<IRInstruction>& irs,
                                                   const std::unordered_map<uint64_t, size_t>& addressToIndex) {
            std::set<uint64_t> seeds;
            if (!irs.empty()) {
                seeds.insert(irs.front().address);
            }

            for (size_t i = 0; i < irs.size(); ++i) {
                const auto& instruction = irs[i];
                if (IRProperties::isCall(instruction.type)) {
                    if (const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).value); target.has_value()) {
                        if (addressToIndex.contains(*target)) {
                            seeds.insert(*target);
                        }
                    }
                }

                if (i > 0 && IRProperties::isReturn(irs[i - 1].type)) {
                    seeds.insert(instruction.address);
                }
            }

            return {seeds.begin(), seeds.end()};
        }

        std::vector<size_t> discoverFunctionInstructionIndices(
            const std::vector<IRInstruction>& irs,
            const std::unordered_map<uint64_t, size_t>& addressToIndex,
            const std::unordered_set<uint64_t>& allSeeds,
            const uint64_t functionSeedAddress) {
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
                        if (const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).value); target.has_value()) {
                            if (const auto targetIt = addressToIndex.find(*target); targetIt != addressToIndex.end()) {
                                const bool isTailCallToKnownFunction =
                                    IRProperties::isUnconditionalJump(instruction.type) &&
                                    *target != functionSeedAddress &&
                                    allSeeds.contains(*target);

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
            std::ranges::sort(indices);
            return indices;
        }

        std::vector<RawFunction> discoverFunctionBodies(const std::vector<IRInstruction>& irs) {
            std::vector<RawFunction> functions;
            if (irs.empty()) {
                return functions;
            }

            const auto addressToIndex = buildAddressIndex(irs);
            const auto seeds = collectFunctionSeeds(irs, addressToIndex);
            const std::unordered_set<uint64_t> allSeeds(seeds.begin(), seeds.end());

            std::vector<int> ownerByInstruction(irs.size(), -1);
            int functionId = 0;

            for (const auto seed : seeds) {
                auto indices = discoverFunctionInstructionIndices(irs, addressToIndex, allSeeds, seed);
                std::vector<size_t> ownedIndices;
                ownedIndices.reserve(indices.size());

                for (const auto index : indices) {
                    if (ownerByInstruction[index] != -1) {
                        continue;
                    }
                    ownerByInstruction[index] = functionId;
                    ownedIndices.push_back(index);
                }

                if (ownedIndices.empty()) {
                    continue;
                }

                RawFunction function;
                function.start_address = seed;
                function.instructions.reserve(ownedIndices.size());
                for (const auto index : ownedIndices) {
                    function.instructions.push_back(irs[index]);
                }

                functions.push_back(std::move(function));
                ++functionId;
            }

            std::ranges::sort(functions, [](const RawFunction& lhs, const RawFunction& rhs) {
                return lhs.start_address < rhs.start_address;
            });
            return functions;
        }

        std::string makeFunctionName(const uint64_t address) {
            std::ostringstream oss;
            oss << "sub_" << std::hex << address;
            return oss.str();
        }

        std::string normalizedRegisterBase(std::string value) {
            const auto first = value.find_first_not_of(' ');
            if (first == std::string::npos) {
                return {};
            }
            const auto last = value.find_last_not_of(' ');
            value = value.substr(first, last - first + 1);

            const auto underscore = value.find('_');
            if (underscore != std::string::npos) {
                value = value.substr(0, underscore);
            }

            std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value;
        }

        bool isReturnRegisterBase(const std::string& base) {
            return base == "eax" || base == "rax";
        }

        bool isStackOrFrameRegisterBase(const std::string& base) {
            return base == "rsp" || base == "esp" || base == "sp" || base == "rbp" || base == "ebp" || base == "bp";
        }

        CallingConvention detectCallingConvention(const RawFunction& function) {
            bool hasSystemVLeadRegs = false;
            bool hasWinLeadRegs = false;

            const auto observeOperand = [&](const IROperand& operand) {
                if (operand.type != OpType::REG) {
                    return;
                }
                const std::string base = normalizedRegisterBase(operand.value);
                if (base == "rdi" || base == "edi" || base == "rsi" || base == "esi") {
                    hasSystemVLeadRegs = true;
                }
                if (base == "rcx" || base == "ecx") {
                    hasWinLeadRegs = true;
                }
            };

            for (const auto& instruction : function.instructions) {
                for (const auto& operand : instruction.operands) {
                    observeOperand(operand);
                }
            }

            if (hasSystemVLeadRegs) return CallingConvention::SystemV;
            if (hasWinLeadRegs) return CallingConvention::Win64;
            return CallingConvention::Unknown;
        }

        bool instructionWritesRegisterBase(const IRInstruction& instruction, const std::string& base) {
            if (base.empty()) {
                return false;
            }
            const auto& firstOperand = IRProperties::operandAt(instruction, 0);
            if (firstOperand.type != OpType::REG || normalizedRegisterBase(firstOperand.value) != base) {
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

        bool isEpilogueNoiseInstruction(const IRInstruction& instruction) {
            const std::string destBase = normalizedRegisterBase(IRProperties::operandAt(instruction, 0).value);
            const std::string srcBase = normalizedRegisterBase(IRProperties::operandAt(instruction, 1).value);
            const bool touchesFrameRegs = isStackOrFrameRegisterBase(destBase) || isStackOrFrameRegisterBase(srcBase);
            if (!touchesFrameRegs) {
                return false;
            }
            return instruction.type == IRType::ASSIGN || instruction.type == IRType::ADD || instruction.type == IRType::SUB || instruction.type == IRType::PUSH || instruction.type == IRType::POP;
        }

        size_t nextMeaningfulInstructionIndex(const std::vector<IRInstruction>& instructions, const size_t from) {
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

        bool hasConcreteReturnPattern(const RawFunction& function) {
            const auto& instructions = function.instructions;
            for (size_t i = 0; i < instructions.size(); ++i) {
                if (IRProperties::isCall(instructions[i].type)) {
                    const size_t next = nextMeaningfulInstructionIndex(instructions, i + 1);
                    if (next < instructions.size() && instructions[next].type == IRType::RET) {
                        return true;
                    }
                }
                if (!instructionWritesRegisterBase(instructions[i], "eax") &&
                    !instructionWritesRegisterBase(instructions[i], "rax")) {
                    continue;
                }
                const size_t next = nextMeaningfulInstructionIndex(instructions, i + 1);
                if (next < instructions.size() && instructions[next].type == IRType::RET) {
                    return true;
                }
            }
            return false;
        }

        size_t inferParameterCount(const RawFunction& function, const CallingConvention cc) {
            std::unordered_set<std::string> writtenRegs;
            std::optional<size_t> maxParamIndex = std::nullopt;

            const auto observeRead = [&](const IROperand& operand) {
                if (operand.type != OpType::REG) {
                    return;
                }
                const std::string base = normalizedRegisterBase(operand.value);
                const auto argIndex = argumentIndexForRegisterBase(base, cc);
                if (!argIndex.has_value()) {
                    return;
                }
                if (writtenRegs.contains(base)) {
                    return;
                }
                maxParamIndex = std::max(maxParamIndex.value_or(0), *argIndex);
            };

            for (const auto& instruction : function.instructions) {
                for (size_t operandIndex = 0; operandIndex < instruction.operands.size(); ++operandIndex) {
                    if (IRProperties::usesOperandAt(instruction, operandIndex)) {
                        observeRead(instruction.operands[operandIndex]);
                    }
                }

                if (IRProperties::definesOperandAt(instruction, 0) &&
                    instructionWritesRegisterBase(instruction, normalizedRegisterBase(IRProperties::operandAt(instruction, 0).value))) {
                    writtenRegs.insert(normalizedRegisterBase(IRProperties::operandAt(instruction, 0).value));
                }
            }

            if (!maxParamIndex.has_value()) {
                return 0;
            }
            return *maxParamIndex + 1;
        }

        SignatureInferenceResult inferFunctionSignatures(const std::vector<RawFunction>& functions) {
            SignatureInferenceResult result;
            result.calling_conventions.resize(functions.size(), CallingConvention::Unknown);
            result.signatures.resize(functions.size());
            std::unordered_map<uint64_t, size_t> functionIndexByAddress;
            functionIndexByAddress.reserve(functions.size());

            for (size_t i = 0; i < functions.size(); ++i) {
                functionIndexByAddress[functions[i].start_address] = i;
                result.calling_conventions[i] = detectCallingConvention(functions[i]);
                result.signatures[i].parameters = buildParameters(inferParameterCount(functions[i], result.calling_conventions[i]));
                result.signatures[i].returns_value = hasConcreteReturnPattern(functions[i]);
            }

            for (const auto& function : functions) {
                for (size_t i = 0; i < function.instructions.size(); ++i) {
                    const auto& instruction = function.instructions[i];
                    if (!IRProperties::isCall(instruction.type)) {
                        continue;
                    }
                    const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).value);
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
                    const bool capturesReturn =
                        (nextInstruction.type == IRType::ASSIGN || nextInstruction.type == IRType::STORE) &&
                        isReturnRegisterBase(normalizedRegisterBase(IRProperties::operandAt(nextInstruction, 1).value));
                    if (capturesReturn) {
                        result.signatures[functionIt->second].returns_value = true;
                    }
                }
            }

            return result;
        }

        std::unordered_map<uint64_t, std::string> buildFunctionNames(const std::vector<RawFunction>& functions) {
            std::unordered_map<uint64_t, size_t> functionIndexByAddress;
            functionIndexByAddress.reserve(functions.size());
            for (size_t i = 0; i < functions.size(); ++i) {
                functionIndexByAddress[functions[i].start_address] = i;
            }

            std::vector<size_t> incomingCalls(functions.size(), 0);
            for (size_t i = 0; i < functions.size(); ++i) {
                for (const auto& instruction : functions[i].instructions) {
                    if (!IRProperties::isCall(instruction.type)) {
                        continue;
                    }
                    if (const auto target = parseAddressOperand(IRProperties::operandAt(instruction, 0).value); target.has_value()) {
                        if (const auto it = functionIndexByAddress.find(*target); it != functionIndexByAddress.end()) {
                            ++incomingCalls[it->second];
                        }
                    }
                }
            }

            std::optional<size_t> mainCandidate;
            for (size_t i = 0; i < functions.size(); ++i) {
                if (incomingCalls[i] == 0) {
                    mainCandidate = i;
                }
            }

            std::unordered_map<uint64_t, std::string> names;
            names.reserve(functions.size());
            for (const auto& function : functions) {
                names[function.start_address] = makeFunctionName(function.start_address);
            }
            if (mainCandidate.has_value()) {
                names[functions[*mainCandidate].start_address] = "main";
            }
            return names;
        }
    }

    std::optional<uint64_t> parseAddressOperand(const std::string& raw) {
        const auto first = raw.find_first_not_of(' ');
        if (first == std::string::npos) {
            return std::nullopt;
        }
        const auto last = raw.find_last_not_of(' ');
        const std::string valueText = raw.substr(first, last - first + 1);

        try {
            size_t parsedChars = 0;
            const auto value = std::stoull(valueText, &parsedChars, 0);
            if (parsedChars != valueText.size()) {
                return std::nullopt;
            }
            return value;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }

    std::optional<size_t> argumentIndexForRegisterBase(const std::string& base, const CallingConvention cc) {
        if (base.empty()) {
            return std::nullopt;
        }

        if (cc == CallingConvention::SystemV) {
            if (base == "rdi" || base == "edi") return 0;
            if (base == "rsi" || base == "esi") return 1;
            if (base == "rdx" || base == "edx") return 2;
            if (base == "rcx" || base == "ecx") return 3;
            if (base == "r8" || base == "r8d") return 4;
            if (base == "r9" || base == "r9d") return 5;
            return std::nullopt;
        }
        if (cc == CallingConvention::Win64) {
            if (base == "rcx" || base == "ecx") return 0;
            if (base == "rdx" || base == "edx") return 1;
            if (base == "r8" || base == "r8d") return 2;
            if (base == "r9" || base == "r9d") return 3;
            return std::nullopt;
        }

        if (base == "rdi" || base == "edi" || base == "rcx" || base == "ecx") return 0;
        if (base == "rsi" || base == "esi" || base == "rdx" || base == "edx") return 1;
        if (base == "r8" || base == "r8d") return 2;
        if (base == "r9" || base == "r9d") return 3;
        return std::nullopt;
    }

    std::vector<DecompilerFunction> findFunctions(const std::vector<IRInstruction>& irs) {
        const auto rawFunctions = discoverFunctionBodies(irs);
        const auto signatureInfo = inferFunctionSignatures(rawFunctions);
        const auto names = buildFunctionNames(rawFunctions);

        std::vector<DecompilerFunction> functions;
        functions.reserve(rawFunctions.size());
        for (size_t i = 0; i < rawFunctions.size(); ++i) {
            functions.push_back({
                rawFunctions[i].start_address,
                names.at(rawFunctions[i].start_address),
                rawFunctions[i].instructions,
                signatureInfo.signatures[i],
                signatureInfo.calling_conventions[i]
            });
        }
        return functions;
    }

    std::unordered_map<uint64_t, std::string> functionNamesByAddress(const std::vector<DecompilerFunction>& functions) {
        std::unordered_map<uint64_t, std::string> names;
        names.reserve(functions.size());
        for (const auto& function : functions) {
            names[function.start_address] = function.name;
        }
        return names;
    }

    std::string substituteArgumentRegisters(std::string line, const FunctionSignature& signature, const CallingConvention cc) {
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

            const std::string token = line.substr(i, j - i);
            const std::string base = normalizedRegisterBase(token);
            const auto argIndex = argumentIndexForRegisterBase(base, cc);
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
}
