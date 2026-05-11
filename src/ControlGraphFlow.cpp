#include "ControlGraphFlow.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace Decompiler {
    namespace {
        std::optional<uint64_t> parseJumpAddress(const std::string& raw) {
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

        void addEdge(Graph& cfg, const size_t from, const size_t to) {
            if (!isValidBlockId(cfg, from) || !isValidBlockId(cfg, to)) {
                return;
            }

            auto& successors = cfg.blocks[from].successors;
            if (std::find(successors.begin(), successors.end(), to) == successors.end()) {
                successors.push_back(to);
            }

            auto& predecessors = cfg.blocks[to].predecessors;
            if (std::find(predecessors.begin(), predecessors.end(), from) == predecessors.end()) {
                predecessors.push_back(from);
            }
        }
    }

    Graph buildCFG(const std::vector<IRInstruction> &instructions) {
        if (instructions.empty()) return {};

        std::unordered_map<uint64_t, size_t> address_to_index;
        for (size_t i = 0; i < instructions.size(); ++i) {
            address_to_index[instructions[i].address] = i;
        }

        std::set<size_t> indexes;
        indexes.insert(0);

        for (size_t i = 0; i < instructions.size(); ++i) {
            const auto &ins = instructions[i];
            if (IRProperties::isTerminator(ins.type)) {
                if (i + 1 < instructions.size()) {
                    indexes.insert(i + 1);
                }
                if (!IRProperties::isReturn(ins.type)) {
                    if (const auto address = parseJumpAddress(IRProperties::operandAt(ins, 0).value); address.has_value()) {
                        if (address_to_index.contains(*address)) {
                            indexes.insert(address_to_index[*address]);
                        }
                    }
                }
            }
        }

        std::vector vec_indexes(indexes.begin(), indexes.end());

        Graph cfg;
        cfg.blocks.reserve(vec_indexes.size());

        for (size_t i = 0; i < vec_indexes.size(); ++i) {
            const size_t start = vec_indexes[i];
            const size_t end = (i + 1 < vec_indexes.size()) ? vec_indexes[i + 1] - 1 : instructions.size() - 1;
            if (start >= instructions.size() || start > end) {
                continue;
            }

            InsBlock block;
            block.index = cfg.blocks.size();

            for (size_t j = start; j <= end; ++j) {
                block.instructions.push_back(instructions[j]);
            }

            if (block.instructions.empty()) {
                continue;
            }

            block.start_address = block.instructions.front().address;
            block.end_address = block.instructions.back().address;

            cfg.blocks.push_back(block);
        }

        std::unordered_map<uint64_t, size_t> address_to_block_index;
        for (size_t i = 0; i < cfg.blocks.size(); ++i) {
            address_to_block_index[cfg.blocks[i].start_address] = i;
        }

        for (size_t i = 0; i < cfg.blocks.size(); ++i) {
            auto &block = cfg.blocks[i];
            if (block.instructions.empty()) {
                continue;
            }

            auto &last_ins = block.instructions.back();

            if (IRProperties::isJump(last_ins.type)) {
                if (const auto address = parseJumpAddress(IRProperties::operandAt(last_ins, 0).value); address.has_value()) {
                    if (address_to_block_index.contains(*address)) {
                        const auto targetBlock = address_to_block_index[*address];
                        addEdge(cfg, block.index, targetBlock);
                    }
                }
            }

            if (IRProperties::isConditionalJump(last_ins.type) || !IRProperties::isTerminator(last_ins.type)) {
                if (i + 1 < cfg.blocks.size()) {
                    addEdge(cfg, block.index, i + 1);
                }
            }
        }

        return cfg;
    }

    void log(const Graph &graph) {
        std::ofstream out("CFG.txt");
        for (size_t i = 0; i < graph.blocks.size(); ++i) {
            out << "==== Block: " << i << "====" << std::endl;

            for (auto ins : graph.blocks[i].instructions) {
                out << ins.address << " " << ins.op << " "
                    << IRProperties::operandAt(ins, 0).value << " "
                    << IRProperties::operandAt(ins, 1).value << std::endl;
            }

            out << "== PREDECESSORS === " << std::endl;
            for (const auto pred : graph.blocks[i].predecessors) {
                out << "Block " << pred << " ";
            }
            out << std::endl;
            out << "== SUCCESSORS === " << std::endl;
            for (const auto succ : graph.blocks[i].successors) {
                out << "Block " << succ << " ";
            }
            out << std::endl;
            out << "========" << std::endl << std::endl;
        }
    }

    std::string EscapeGraphvizString(const std::string& input) {
        std::string output;
        for (char c : input) {
            if (c == '"' || c == '\\') {
                output += '\\';
            }
            output += c;
        }
        return output;
    }

    void ExportCFGToDotWithInstructions(const std::vector<InsBlock>& all_blocks, const std::string& filename) {
        std::ofstream out(filename);
        if (!out.is_open()) {
            std::cerr << "Error opening the files" << std::endl;
            return;
        }

        out << "digraph CFG {\n";
        out << "    node [shape=box, fontname=\"Courier New\", style=filled, fillcolor=\"#f9f9f9\"];\n\n";

        for (const auto& block : all_blocks) {

            out << "    block_" << block.index << " [label=\"";
            out << "Block " << block.index << "\\n";
            out << std::hex << "0x" << block.start_address << " - 0x" << block.end_address << std::dec << "\\n";
            out << "-------------------------\\l";

            for (const auto& inst : block.instructions) {
                const std::string first = IRProperties::operandAt(inst, 0).value;
                const std::string second = IRProperties::operandAt(inst, 1).value;
                std::string res = inst.op + " " + first + (second.empty() ? "" : ", " + second);
                std::string escaped_text = EscapeGraphvizString(res);
                out << escaped_text << "\\l";
            }

            out << "\"];\n";

            for (const auto succ : block.successors) {
                if (succ >= all_blocks.size()) {
                    continue;
                }
                out << "    block_" << block.index << " -> block_" << succ << ";\n";
            }
        }

        out << "}\n";
        out.close();
    }

}
