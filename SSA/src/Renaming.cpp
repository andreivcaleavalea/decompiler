#include "Renaming.h"
#include "Names.h"

#include <set>
#include <unordered_map>

namespace Decompiler {
    static void collectVariables(Graph &cfg, std::set<std::string> &variables) {
        for (auto &block : cfg.blocks) {
            for (auto &instruction : block.instructions) {
                if (IRProperties::isPhi(instruction)) {
                    if (IRProperties::hasOperandAt(instruction, 0) && IRProperties::isRegister(instruction.operands[0])) {
                        variables.insert(ssaBaseName(instruction.operands[0].value));
                    }
                    continue;
                }

                for (auto &operand : instruction.operands) {
                    if (IRProperties::isRegister(operand)) {
                        variables.insert(ssaBaseName(operand.value));
                    }
                }
            }
        }
    }

    static std::string currentVersion(std::string variable, std::unordered_map<std::string, std::vector<size_t>> &versionStack) {
        auto it = versionStack.find(variable);
        if (it == versionStack.end() || it->second.empty()) {
            return ssaVersionName(variable, 0);
        }
        return ssaVersionName(variable, it->second.back());
    }

    static std::string versionForUse(std::string variable, std::unordered_map<std::string, std::vector<size_t>> &versionStack) {
        return currentVersion(ssaBaseName(variable), versionStack);
    }

    static std::string createNewVersion(std::string variable, std::unordered_map<std::string, size_t> &versionCounters, std::unordered_map<std::string, std::vector<size_t>> &versionStack) {
        std::string base = ssaBaseName(variable);
        size_t version = ++versionCounters[base];
        versionStack[base].push_back(version);
        return ssaVersionName(base, version);
    }

    static void updatePhiInputsInSuccessors(Graph &cfg, size_t blockId, std::unordered_map<std::string, std::vector<size_t>> &versionStack) {
        auto &block = cfg.blocks[blockId];

        for (size_t successorId : block.successors) {
            if (!isValidBlockId(cfg, successorId)) {
                continue;
            }

            size_t predecessorIndex = 0;
            while (predecessorIndex < cfg.blocks[successorId].predecessors.size() && cfg.blocks[successorId].predecessors[predecessorIndex] != blockId) {
                ++predecessorIndex;
            }
            if (predecessorIndex == cfg.blocks[successorId].predecessors.size()) {
                continue;
            }

            for (auto &instruction : cfg.blocks[successorId].instructions) {
                if (!IRProperties::isPhi(instruction)) {
                    break;
                }

                std::string variable = ssaBaseName(instruction.operands[0].value);
                std::vector<std::string> inputs = splitPhiInputs(instruction.operands[1].value);
                if (inputs.size() < cfg.blocks[successorId].predecessors.size()) {
                    inputs.resize(cfg.blocks[successorId].predecessors.size(), ssaVersionName(variable, 0));
                }

                inputs[predecessorIndex] = currentVersion(variable, versionStack);
                instruction.operands[1].value = joinPhiInputs(inputs);
            }
        }
    }

    static void renameBlock(Graph &cfg, size_t blockId, std::vector<std::vector<size_t>> &dominatorTreeChildren, std::unordered_map<std::string, size_t> &versionCounters, std::unordered_map<std::string, std::vector<size_t>> &versionStack) {
        auto &block = cfg.blocks[blockId];
        std::vector<std::string> definitionsInThisBlock;

        for (auto &instruction : block.instructions) {
            if (IRProperties::isPhi(instruction)) {
                if (IRProperties::isRegister(instruction.operands[0])) {
                    std::string variable = ssaBaseName(instruction.operands[0].value);
                    instruction.operands[0].value = createNewVersion(variable, versionCounters, versionStack);
                    definitionsInThisBlock.push_back(variable);
                }
                continue;
            }

            bool createsValue = IRProperties::createsNewValue(instruction);
            for (size_t operandIndex = 0; operandIndex < instruction.operands.size(); ++operandIndex) {
                if (IRProperties::usesOperandAt(instruction, operandIndex) && IRProperties::isRegister(instruction.operands[operandIndex])) {
                    instruction.operands[operandIndex].value = versionForUse(instruction.operands[operandIndex].value, versionStack);
                }
            }

            if (createsValue && IRProperties::isRegister(instruction.operands[0])) {
                std::string variable = ssaBaseName(instruction.operands[0].value);
                instruction.operands[0].value = createNewVersion(variable, versionCounters, versionStack);
                definitionsInThisBlock.push_back(variable);
            }
        }

        updatePhiInputsInSuccessors(cfg, blockId, versionStack);

        for (size_t childBlock : dominatorTreeChildren[blockId]) {
            renameBlock(cfg, childBlock, dominatorTreeChildren, versionCounters, versionStack);
        }

        for (auto it = definitionsInThisBlock.rbegin(); it != definitionsInThisBlock.rend(); ++it) {
            auto &stack = versionStack[*it];
            if (!stack.empty()) {
                stack.pop_back();
            }
        }
    }

    void renameVariables(Graph &cfg, std::vector<size_t> &immediateDominators, std::vector<std::vector<size_t>> &dominatorTreeChildren) {
        std::set<std::string> variables;
        collectVariables(cfg, variables);

        std::unordered_map<std::string, size_t> versionCounters;
        std::unordered_map<std::string, std::vector<size_t>> versionStack;
        for (auto &variable : variables) {
            versionCounters[variable] = 0;
            versionStack[variable] = {0};
        }

        if (!cfg.blocks.empty()) {
            renameBlock(cfg, 0, dominatorTreeChildren, versionCounters, versionStack);
        }

        for (size_t blockId = 1; blockId < cfg.blocks.size(); ++blockId) {
            if (immediateDominators[blockId] == InvalidBlockId) {
                renameBlock(cfg, blockId, dominatorTreeChildren, versionCounters, versionStack);
            }
        }
    }
} // namespace Decompiler
