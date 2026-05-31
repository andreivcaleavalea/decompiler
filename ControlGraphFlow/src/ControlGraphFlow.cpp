#include "ControlGraphFlow.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace Decompiler
{
namespace
{
    std::optional<uint64_t> parseJumpAddress(const std::string& raw)
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

    void addEdge(Graph& cfg, const size_t from, const size_t to)
    {
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

    std::optional<size_t> targetIndexForAddress(const std::map<uint64_t, size_t>& addressToIndex, const uint64_t address)
    {
        const auto it = addressToIndex.lower_bound(address);
        if (it == addressToIndex.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool isPassthroughBlock(const Graph& cfg, const size_t blockId)
    {
        if (blockId == 0 || !isValidBlockId(cfg, blockId)) {
            return false;
        }

        const auto& block = cfg.blocks[blockId];
        if (block.successors.size() != 1 || block.successors[0] == blockId) {
            return false;
        }

        bool hasJump = false;
        for (const auto& instruction : block.instructions) {
            if (IRProperties::isNop(instruction)) {
                continue;
            }
            if (instruction.type == IRType::JMP && !hasJump) {
                hasJump = true;
                continue;
            }
            return false;
        }

        return !block.instructions.empty();
    }

    size_t resolvePassthroughTarget(const Graph& cfg, size_t blockId)
    {
        std::set<size_t> visited;
        while (isPassthroughBlock(cfg, blockId)) {
            if (visited.contains(blockId)) {
                return blockId;
            }
            visited.insert(blockId);
            blockId = cfg.blocks[blockId].successors[0];
        }
        return blockId;
    }

    void normalizePassthroughBlocks(Graph& cfg)
    {
        std::vector<bool> removeBlock(cfg.blocks.size(), false);
        bool hasBlocksToRemove = false;
        for (size_t i = 0; i < cfg.blocks.size(); ++i) {
            removeBlock[i]    = isPassthroughBlock(cfg, i);
            hasBlocksToRemove = hasBlocksToRemove || removeBlock[i];
        }
        if (!hasBlocksToRemove) {
            return;
        }

        std::vector<size_t> newBlockId(cfg.blocks.size(), InvalidBlockId);
        Graph normalized;
        normalized.blocks.reserve(cfg.blocks.size());

        for (size_t i = 0; i < cfg.blocks.size(); ++i) {
            if (removeBlock[i]) {
                continue;
            }

            newBlockId[i]  = normalized.blocks.size();
            InsBlock block = cfg.blocks[i];
            block.index    = normalized.blocks.size();
            block.predecessors.clear();
            block.successors.clear();
            normalized.blocks.push_back(std::move(block));
        }

        for (size_t i = 0; i < cfg.blocks.size(); ++i) {
            if (removeBlock[i]) {
                continue;
            }

            const size_t from = newBlockId[i];
            for (const auto successor : cfg.blocks[i].successors) {
                const size_t target = resolvePassthroughTarget(cfg, successor);
                if (!isValidBlockId(cfg, target) || removeBlock[target]) {
                    continue;
                }
                addEdge(normalized, from, newBlockId[target]);
            }
        }

        cfg = std::move(normalized);
    }
} // namespace

Graph buildCFG(const std::vector<IRInstruction>& instructions)
{
    if (instructions.empty())
        return {};

    std::map<uint64_t, size_t> address_to_index;
    for (size_t i = 0; i < instructions.size(); ++i) {
        address_to_index[instructions[i].address] = i;
    }

    std::set<size_t> indexes;
    indexes.insert(0);

    for (size_t i = 0; i < instructions.size(); ++i) {
        const auto& ins = instructions[i];
        if (IRProperties::isTerminator(ins.type)) {
            if (i + 1 < instructions.size()) {
                indexes.insert(i + 1);
            }
            if (!IRProperties::isReturn(ins.type)) {
                if (const auto address = parseJumpAddress(IRProperties::operandAt(ins, 0).name); address.has_value()) {
                    if (const auto targetIndex = targetIndexForAddress(address_to_index, *address); targetIndex.has_value()) {
                        indexes.insert(*targetIndex);
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
        const size_t end   = (i + 1 < vec_indexes.size()) ? vec_indexes[i + 1] - 1 : instructions.size() - 1;
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
        block.end_address   = block.instructions.back().address;

        cfg.blocks.push_back(block);
    }

    std::map<uint64_t, size_t> address_to_block_index;
    for (size_t i = 0; i < cfg.blocks.size(); ++i) {
        address_to_block_index[cfg.blocks[i].start_address] = i;
    }

    for (size_t i = 0; i < cfg.blocks.size(); ++i) {
        auto& block = cfg.blocks[i];
        if (block.instructions.empty()) {
            continue;
        }

        auto& last_ins = block.instructions.back();

        if (IRProperties::isJump(last_ins.type)) {
            if (const auto address = parseJumpAddress(IRProperties::operandAt(last_ins, 0).name); address.has_value()) {
                if (const auto targetBlock = targetIndexForAddress(address_to_block_index, *address); targetBlock.has_value()) {
                    addEdge(cfg, block.index, *targetBlock);
                }
            }
        }

        if (IRProperties::isConditionalJump(last_ins.type) || !IRProperties::isTerminator(last_ins.type)) {
            if (i + 1 < cfg.blocks.size()) {
                addEdge(cfg, block.index, i + 1);
            }
        }
    }

    normalizePassthroughBlocks(cfg);
    return cfg;
}

void log(const Graph& graph)
{
    std::ofstream out("CFG.txt");
    for (size_t i = 0; i < graph.blocks.size(); ++i) {
        out << "==== Block: " << i << "====" << std::endl;

        for (auto ins : graph.blocks[i].instructions) {
            out << ins.address << " " << ins.op << " " << IRProperties::operandAt(ins, 0).name << " " << IRProperties::operandAt(ins, 1).name << std::endl;
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

std::string EscapeGraphvizString(const std::string& input)
{
    std::string output;
    for (char c : input) {
        if (c == '"' || c == '\\') {
            output += '\\';
        }
        output += c;
    }
    return output;
}

void ExportCFGToDotWithInstructions(const std::vector<InsBlock>& all_blocks, const std::string& filename)
{
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
            const std::string first  = IRProperties::operandAt(inst, 0).name;
            const std::string second = IRProperties::operandAt(inst, 1).name;
            std::string res          = inst.op + " " + first + (second.empty() ? "" : ", " + second);
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

} // namespace Decompiler
