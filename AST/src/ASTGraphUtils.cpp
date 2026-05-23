#include "AST.h"

#include <queue>

#include "ControlGraphFlow.h"

namespace Decompiler
{
size_t find_merge_block(const Graph& cfg, size_t branch_true, size_t branch_false)
{
    if (branch_true == branch_false) {
        return branch_true;
    }

    std::set visited_a = { branch_true };
    std::set visited_b = { branch_false };
    std::queue<size_t> q_a, q_b;

    q_a.push(branch_true);
    q_b.push(branch_false);

    if (visited_a.contains(branch_false)) {
        return branch_false;
    }
    if (visited_b.contains(branch_true)) {
        return branch_true;
    }

    while (!q_a.empty() || !q_b.empty()) {
        if (!q_a.empty()) {
            const size_t curr_a = q_a.front();
            q_a.pop();
            if (curr_a >= cfg.blocks.size()) {
                continue;
            }
            for (const auto succ : cfg.blocks[curr_a].successors) {
                if (!isValidBlockId(cfg, succ)) {
                    continue;
                }
                if (visited_b.contains(succ))
                    return succ;
                if (visited_a.insert(succ).second)
                    q_a.push(succ);
            }
        }
        if (!q_b.empty()) {
            const size_t curr_b = q_b.front();
            q_b.pop();
            if (curr_b >= cfg.blocks.size()) {
                continue;
            }
            for (const auto succ : cfg.blocks[curr_b].successors) {
                if (!isValidBlockId(cfg, succ)) {
                    continue;
                }
                if (visited_a.contains(succ))
                    return succ;
                if (visited_b.insert(succ).second)
                    q_b.push(succ);
            }
        }
    }
    return static_cast<size_t>(-1);
}

bool reaches(const Graph& cfg, size_t start, size_t target, size_t exclude)
{
    if (start == target) {
        return true;
    }
    std::queue<size_t> q;
    std::set<size_t> vis;
    q.push(start);
    vis.insert(start);

    while (!q.empty()) {
        const size_t curr = q.front();
        q.pop();
        if (curr >= cfg.blocks.size()) {
            continue;
        }
        for (const auto succ : cfg.blocks[curr].successors) {
            if (!isValidBlockId(cfg, succ)) {
                continue;
            }
            if (succ == target) {
                return true;
            }
            if (succ != exclude && vis.insert(succ).second) {
                q.push(succ);
            }
        }
    }
    return false;
}

} // namespace Decompiler
