#include "SSA.h"

#include "Dominators.h"
#include "PhiNodes.h"
#include "Renaming.h"

namespace Decompiler {
    void applySSA(Graph &cfg) {
        if (cfg.blocks.empty()) {
            return;
        }

        auto dominators = Dominators::computeDominators(cfg);
        auto immediateDominators = Dominators::computeImmediateDominators(dominators);
        auto dominatorTreeChildren = Dominators::buildDominatorTreeChildren(immediateDominators);
        auto dominanceFrontier = Dominators::computeDominanceFrontier(cfg, immediateDominators);

        insertPhiInstructions(cfg, dominanceFrontier);
        renameVariables(cfg, immediateDominators, dominatorTreeChildren);
    }

    void removeSSA(Graph &cfg) {
        replacePhiInstructionsWithCopies(cfg);
    }
} // namespace Decompiler
