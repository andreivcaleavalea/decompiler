#pragma once

#include <fstream>
#include <vector>

#include "IRTypes.h"

namespace Decompiler {
    using BlockId = size_t;
    inline BlockId InvalidBlockId = static_cast<BlockId>(-1);

    struct InsBlock {
        BlockId index = 0;
        uint64_t start_address = 0;
        uint64_t end_address = 0;

        std::vector<IRInstruction> instructions;

        std::vector<BlockId> predecessors;
        std::vector<BlockId> successors;
    };

    struct Graph {
        std::vector<InsBlock> blocks;
    };

    inline bool isValidBlockId(const Graph& graph, const BlockId blockId) {
        return blockId < graph.blocks.size();
    }

    Graph buildCFG(const std::vector<IRInstruction> &instructions);

    std::string EscapeGraphvizString(const std::string& input);

    void ExportCFGToDotWithInstructions(const std::vector<InsBlock>& all_blocks, const std::string& filename);

    void log(const Graph &graph);
}
