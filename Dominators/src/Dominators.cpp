#include "Dominators.h"

#include <algorithm>
#include <set>

namespace Decompiler::Dominators
{

namespace
{

    std::vector<size_t> computeImmediateSetDominators(const std::vector<std::set<size_t>>& doms)
    {
        const size_t n    = doms.size();
        const size_t none = static_cast<size_t>(-1);
        std::vector<size_t> idom(n, none);
        if (n == 0) {
            return idom;
        }

        for (size_t b = 0; b < n; ++b) {
            std::vector<size_t> strict;
            strict.reserve(doms[b].size());
            for (const auto d : doms[b]) {
                if (d != b) {
                    strict.push_back(d);
                }
            }

            for (const auto candidate : strict) {
                bool isImmediate = true;
                for (const auto other : strict) {
                    if (other == candidate) {
                        continue;
                    }
                    if (doms[other].count(candidate)) {
                        isImmediate = false;
                        break;
                    }
                }
                if (isImmediate) {
                    idom[b] = candidate;
                    break;
                }
            }
        }
        return idom;
    }

} // anonymous namespace

std::vector<std::set<size_t>> computeDominators(const Graph& cfg)
{
    const size_t n = cfg.blocks.size();
    std::vector<std::set<size_t>> doms(n);
    if (n == 0) {
        return doms;
    }

    std::set<size_t> allNodes;
    for (size_t i = 0; i < n; ++i) {
        allNodes.insert(i);
    }

    doms[0] = { 0 };
    for (size_t i = 1; i < n; ++i) {
        doms[i] = allNodes;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t b = 1; b < n; ++b) {
            const auto& legacyPreds = cfg.blocks[b].predecessors;
            std::set<size_t> newDom;

            if (legacyPreds.empty()) {
                newDom = { b };
            } else {
                size_t firstPred = static_cast<size_t>(-1);
                for (const auto pred : legacyPreds) {
                    if (pred < n) {
                        firstPred = pred;
                        break;
                    }
                }
                if (firstPred == static_cast<size_t>(-1)) {
                    newDom = { b };
                } else {
                    newDom = doms[firstPred];
                    for (size_t i = 0; i < legacyPreds.size(); ++i) {
                        if (legacyPreds[i] >= n || legacyPreds[i] == firstPred) {
                            continue;
                        }
                        std::set<size_t> intersection;
                        std::set_intersection(
                              newDom.begin(),
                              newDom.end(),
                              doms[legacyPreds[i]].begin(),
                              doms[legacyPreds[i]].end(),
                              std::inserter(intersection, intersection.begin()));
                        newDom = std::move(intersection);
                    }
                }
                newDom.insert(b);
            }

            if (newDom != doms[b]) {
                doms[b] = std::move(newDom);
                changed = true;
            }
        }
    }
    return doms;
}

std::vector<std::set<size_t>> computePostDominators(const Graph& cfg)
{
    const size_t n = cfg.blocks.size();
    std::vector<std::set<size_t>> postdoms(n);
    if (n == 0) {
        return postdoms;
    }

    std::set<size_t> allNodes;
    for (size_t i = 0; i < n; ++i) {
        allNodes.insert(i);
    }

    for (size_t i = 0; i < n; ++i) {
        postdoms[i] = cfg.blocks[i].successors.empty() ? std::set<size_t>{ i } : allNodes;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t b = 0; b < n; ++b) {
            const auto& legacySuccs = cfg.blocks[b].successors;
            std::set<size_t> newPostDom;

            if (legacySuccs.empty()) {
                newPostDom = { b };
            } else {
                size_t firstSucc = static_cast<size_t>(-1);
                for (const auto succ : legacySuccs) {
                    if (succ < n) {
                        firstSucc = succ;
                        break;
                    }
                }
                if (firstSucc == static_cast<size_t>(-1)) {
                    newPostDom = { b };
                } else {
                    newPostDom = postdoms[firstSucc];
                    for (size_t i = 0; i < legacySuccs.size(); ++i) {
                        if (legacySuccs[i] >= n || legacySuccs[i] == firstSucc) {
                            continue;
                        }
                        std::set<size_t> intersection;
                        std::set_intersection(
                              newPostDom.begin(),
                              newPostDom.end(),
                              postdoms[legacySuccs[i]].begin(),
                              postdoms[legacySuccs[i]].end(),
                              std::inserter(intersection, intersection.begin()));
                        newPostDom = std::move(intersection);
                    }
                }
                newPostDom.insert(b);
            }

            if (newPostDom != postdoms[b]) {
                postdoms[b] = std::move(newPostDom);
                changed     = true;
            }
        }
    }
    return postdoms;
}

std::vector<size_t> computeImmediateDominators(const std::vector<std::set<size_t>>& doms)
{
    auto idom = computeImmediateSetDominators(doms);
    if (!idom.empty()) {
        idom[0] = static_cast<size_t>(-1);
    }
    return idom;
}

std::vector<size_t> computeImmediatePostDominators(const std::vector<std::set<size_t>>& postdoms)
{
    return computeImmediateSetDominators(postdoms);
}

std::vector<std::vector<size_t>> buildDominatorTreeChildren(const std::vector<size_t>& idom)
{
    const size_t n = idom.size();
    std::vector<std::vector<size_t>> children(n);
    for (size_t b = 0; b < n; ++b) {
        if (idom[b] != static_cast<size_t>(-1)) {
            children[idom[b]].push_back(b);
        }
    }
    return children;
}

std::vector<std::set<size_t>> computeDominanceFrontier(const Graph& cfg, const std::vector<size_t>& idom)
{
    const size_t n = cfg.blocks.size();
    std::vector<std::set<size_t>> df(n);
    for (size_t b = 0; b < n; ++b) {
        if (cfg.blocks[b].predecessors.size() < 2) {
            continue;
        }
        for (const auto pred : cfg.blocks[b].predecessors) {
            if (pred >= n) {
                continue;
            }
            size_t runner = pred;
            while (runner != static_cast<size_t>(-1) && runner != idom[b] && runner < n) {
                df[runner].insert(b);
                runner = idom[runner];
            }
        }
    }
    return df;
}

} // namespace Decompiler::Dominators
