#include "PhiNodes.h"
#include "Names.h"

#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace Decompiler {
    static IRInstruction makePhi(std::string variable, size_t inputCount) {
        std::vector<std::string> inputs;
        inputs.reserve(inputCount);
        for (size_t i = 0; i < inputCount; ++i) {
            inputs.push_back(ssaVersionName(variable, 0));
        }

        return {"phi", IRType::PHI, {{OpType::REG, variable}, {OpType::REG, joinPhiInputs(inputs)}}, 0};
    }

    static void collectDefinitionBlocks(Graph &cfg, std::unordered_map<std::string, std::set<size_t>> &definitionsByVariable) {
        for (auto &block : cfg.blocks) {
            for (auto &instruction : block.instructions) {
                if (IRProperties::createsNewValue(instruction)) {
                    definitionsByVariable[ssaBaseName(instruction.operands[0].value)].insert(block.index);
                }
            }
        }
    }

    static bool blockAlreadyHasPhi(InsBlock &block, std::string variable) {
        for (auto &instruction : block.instructions) {
            if (!IRProperties::isPhi(instruction)) {
                break;
            }
            if (ssaBaseName(instruction.operands[0].value) == variable) {
                return true;
            }
        }
        return false;
    }

    static void insertPhi(InsBlock &block, std::string variable) {
        if (blockAlreadyHasPhi(block, variable)) {
            return;
        }

        auto insertionPoint = block.instructions.begin();
        while (insertionPoint != block.instructions.end() && IRProperties::isPhi(*insertionPoint)) {
            ++insertionPoint;
        }

        block.instructions.insert(insertionPoint, makePhi(variable, block.predecessors.size()));
    }

    void insertPhiInstructions(Graph &cfg, std::vector<std::set<size_t>> &dominanceFrontier) {
        if (dominanceFrontier.size() != cfg.blocks.size()) {
            return;
        }

        std::unordered_map<std::string, std::set<size_t>> definitionsByVariable;
        collectDefinitionBlocks(cfg, definitionsByVariable);

        for (auto &[variable, definitionBlocks] : definitionsByVariable) {
            std::queue<size_t> worklist;
            std::unordered_set<size_t> queuedBlocks;
            std::unordered_set<size_t> blocksWithPhi;

            for (size_t blockId : definitionBlocks) {
                worklist.push(blockId);
                queuedBlocks.insert(blockId);
            }

            while (!worklist.empty()) {
                size_t blockId = worklist.front();
                worklist.pop();
                if (!isValidBlockId(cfg, blockId)) {
                    continue;
                }

                for (size_t frontierBlock : dominanceFrontier[blockId]) {
                    if (!isValidBlockId(cfg, frontierBlock)) {
                        continue;
                    }
                    if (!blocksWithPhi.insert(frontierBlock).second) {
                        continue;
                    }

                    insertPhi(cfg.blocks[frontierBlock], variable);
                    if (queuedBlocks.insert(frontierBlock).second) {
                        worklist.push(frontierBlock);
                    }
                }
            }
        }
    }

    static std::vector<IRInstruction> leadingPhiInstructions(InsBlock &block) {
        std::vector<IRInstruction> phis;
        for (auto &instruction : block.instructions) {
            if (!IRProperties::isPhi(instruction)) {
                break;
            }
            phis.push_back(instruction);
        }
        return phis;
    }

    static bool hasEdge(Graph &cfg, size_t fromBlock, size_t toBlock) {
        if (!isValidBlockId(cfg, fromBlock) || !isValidBlockId(cfg, toBlock)) {
            return false;
        }

        for (size_t successor : cfg.blocks[fromBlock].successors) {
            if (successor == toBlock) {
                return true;
            }
        }
        return false;
    }

    static void insertBeforeTerminator(InsBlock &block, IRInstruction copy) {
        auto insertionPoint = block.instructions.begin();
        for (auto it = block.instructions.begin(); it != block.instructions.end(); ++it) {
            if (IRProperties::isTerminator(it->type)) {
                insertionPoint = it;
                break;
            }
        }
        block.instructions.insert(insertionPoint, copy);
    }

    static void addCopiesForPhi(Graph &cfg, size_t blockId, IRInstruction phi) {
        auto &block = cfg.blocks[blockId];
        std::string variable = ssaBaseName(phi.operands[0].value);
        std::vector<std::string> inputs = splitPhiInputs(phi.operands[1].value);
        if (inputs.size() < block.predecessors.size()) {
            inputs.resize(block.predecessors.size(), ssaVersionName(variable, 0));
        }

        for (size_t predIndex = 0; predIndex < block.predecessors.size(); ++predIndex) {
            size_t predecessor = block.predecessors[predIndex];
            if (!hasEdge(cfg, predecessor, blockId)) {
                continue;
            }

            std::string incoming = inputs[predIndex].empty() ? ssaVersionName(variable, 0) : inputs[predIndex];
            IRInstruction copy = {"assign", IRType::ASSIGN, {{OpType::REG, phi.operands[0].value}, {OpType::REG, incoming}}, 0};
            insertBeforeTerminator(cfg.blocks[predecessor], copy);
        }
    }

    static void removePhiInstructions(Graph &cfg) {
        for (auto &block : cfg.blocks) {
            std::vector<IRInstruction> withoutPhi;
            withoutPhi.reserve(block.instructions.size());

            for (auto &instruction : block.instructions) {
                if (!IRProperties::isPhi(instruction)) {
                    withoutPhi.push_back(instruction);
                }
            }

            block.instructions = withoutPhi;
        }
    }

    void replacePhiInstructionsWithCopies(Graph &cfg) {
        for (size_t blockId = 0; blockId < cfg.blocks.size(); ++blockId) {
            auto phis = leadingPhiInstructions(cfg.blocks[blockId]);
            for (auto &phi : phis) {
                if (IRProperties::isRegister(phi.operands[0])) {
                    addCopiesForPhi(cfg, blockId, phi);
                }
            }
        }

        removePhiInstructions(cfg);
    }
} // namespace Decompiler
