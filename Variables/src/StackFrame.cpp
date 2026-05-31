#include "StackFrame.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <utility>

#include "IRProperties.h"
#include "WindowsX64.h"

namespace Decompiler
{
namespace
{
    bool isFramePointer(const std::string& base)
    {
        const std::string canonical = normalizedRegisterBase(base);
        return canonical == "rbp";
    }

    bool isStackPointer(const std::string& base)
    {
        const std::string canonical = normalizedRegisterBase(base);
        return canonical == "rsp";
    }

    bool isLocalBase(const std::string& base)
    {
        return isFramePointer(base) || isStackPointer(base);
    }

    struct FixedAccess {
        int64_t offset = 0;
        uint32_t size  = 0;
    };

    struct IndexedAccessKey {
        int64_t offset = 0;
        int32_t scale  = 1;

        bool operator<(const IndexedAccessKey& other) const
        {
            if (offset != other.offset) {
                return offset < other.offset;
            }
            return scale < other.scale;
        }
    };

    void collectAccesses(
          const Graph& cfg,
          std::vector<FixedAccess>& framePointerFixed,
          std::vector<FixedAccess>& stackPointerFixed,
          std::map<IndexedAccessKey, uint32_t>& framePointerIndexed)
    {
        for (const auto& block : cfg.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& operand : instruction.operands) {
                    if (!operand.isHeapDeref()) {
                        continue;
                    }
                    const auto& memory = operand.heapDeref;
                    if (memory.absolute.has_value() || memory.base.empty()) {
                        continue;
                    }

                    const bool framePointer = isFramePointer(memory.base);
                    const bool stackPointer = isStackPointer(memory.base);
                    if (!framePointer && !stackPointer) {
                        continue;
                    }

                    if (memory.index.empty()) {
                        FixedAccess access{ memory.displacement, memory.sizeBytes };
                        if (framePointer) {
                            framePointerFixed.push_back(access);
                        } else {
                            stackPointerFixed.push_back(access);
                        }
                        continue;
                    }

                    if (framePointer || stackPointer) {
                        IndexedAccessKey key{ memory.displacement, memory.scale };
                        const auto existing = framePointerIndexed.find(key);
                        if (existing == framePointerIndexed.end() || existing->second < memory.sizeBytes) {
                            framePointerIndexed[key] = memory.sizeBytes;
                        }
                    }
                }
            }
        }
    }

    std::string localName(const int64_t offset)
    {
        return "var_" + std::to_string(-offset);
    }

    std::string argumentName(const int64_t offset, const bool is64Bit)
    {
        if (const auto argIndex = argumentIndexForStackSlot(offset, is64Bit); argIndex.has_value()) {
            return "arg_" + std::to_string(*argIndex);
        }
        return "arg_" + std::to_string(offset);
    }

    std::string arrayName(const int64_t offset)
    {
        return "arr_" + std::to_string(offset < 0 ? -offset : offset);
    }

    StackRegion buildRegion(const int64_t offset, const std::vector<uint32_t>& sizes, const StackRegionKind defaultKind, std::string name)
    {
        std::set<uint32_t> uniqueSizes(sizes.begin(), sizes.end());
        uniqueSizes.erase(0);
        const uint32_t maxSize = uniqueSizes.empty() ? 0 : *uniqueSizes.rbegin();

        StackRegion region;
        region.offset      = offset;
        region.totalSize   = maxSize;
        region.elementSize = maxSize;
        region.count       = 1;
        region.kind        = uniqueSizes.size() > 1 ? StackRegionKind::Union : defaultKind;
        region.name        = std::move(name);
        return region;
    }

    bool stackRegionByOffset(const StackRegion& a, const StackRegion& b)
    {
        return a.offset < b.offset;
    }

    std::string resolveRegisterConstant(const std::vector<IRInstruction>& instrs, const size_t beforeIdx, const std::string& regName)
    {
        const std::string base = normalizedRegisterBase(regName);
        for (size_t i = beforeIdx; i-- > 0;) {
            const auto& inst = instrs[i];
            if (inst.operands.empty()) {
                continue;
            }
            if (normalizedRegisterBase(inst.operands[0].name) != base) {
                continue;
            }
            if ((inst.type == IRType::LOAD_CONST || inst.type == IRType::ASSIGN) && inst.operands.size() >= 2 &&
                inst.operands[1].tag == OperandTag::Immediate) {
                return inst.operands[1].name;
            }
            return {};
        }
        return {};
    }

    bool parsePackedConstant(const std::string& str, uint64_t& out)
    {
        if (str.empty()) {
            return false;
        }
        char* end = nullptr;
        out       = std::strtoull(str.c_str(), &end, 0);
        return end != str.c_str() && *end == '\0';
    }

    void collectArrayInitValues(StackFrame& frame, const Graph& cfg)
    {
        for (auto& region : frame.regions) {
            if (region.kind != StackRegionKind::Array || region.count == 0) {
                continue;
            }

            region.initValues.assign(region.count, std::string{});

            const uint32_t E    = region.elementSize;
            const uint64_t mask = E >= 8 ? ~uint64_t(0) : ((uint64_t(1) << (E * 8)) - 1);

            for (const auto& block : cfg.blocks) {
                for (size_t i = 0; i < block.instructions.size(); ++i) {
                    const auto& inst = block.instructions[i];
                    if (inst.type != IRType::STORE || inst.operands.size() < 2) {
                        continue;
                    }
                    const auto& destOp = inst.operands[0];
                    if (!destOp.isHeapDeref()) {
                        continue;
                    }
                    const auto& mem = destOp.heapDeref;
                    if (mem.absolute.has_value() || !mem.index.empty() || mem.base.empty()) {
                        continue;
                    }
                    if (!isLocalBase(mem.base)) {
                        continue;
                    }

                    const int64_t storeOff   = mem.displacement;
                    const uint32_t storeSize = mem.sizeBytes > 0 ? mem.sizeBytes : E;

                    if (storeOff < region.offset) {
                        continue;
                    }
                    if (storeOff >= region.offset + static_cast<int64_t>(region.totalSize)) {
                        continue;
                    }
                    if ((storeOff - region.offset) % static_cast<int64_t>(E) != 0) {
                        continue;
                    }

                    const auto& srcOp = inst.operands[1];
                    std::string constStr;
                    if (srcOp.tag == OperandTag::Immediate) {
                        constStr = srcOp.name;
                    } else if (srcOp.isAnyReg()) {
                        constStr = resolveRegisterConstant(block.instructions, i, srcOp.name);
                    }

                    uint64_t packedValue = 0;
                    if (!parsePackedConstant(constStr, packedValue)) {
                        continue;
                    }

                    const int64_t startK    = (storeOff - region.offset) / static_cast<int64_t>(E);
                    const uint32_t numElems = storeSize / E;

                    for (uint32_t e = 0; e < numElems; ++e) {
                        const uint32_t k = static_cast<uint32_t>(startK) + e;
                        if (k >= region.count) {
                            break;
                        }
                        if (!region.initValues[k].empty()) {
                            continue;
                        }
                        const uint64_t elemVal = (packedValue >> (e * E * 8)) & mask;
                        region.initValues[k]   = std::to_string(elemVal);
                    }

                    frame.initStoreAddrs.insert(inst.address);
                }
            }

            bool allKnown = true;
            for (const auto& v : region.initValues) {
                if (v.empty()) {
                    allKnown = false;
                    break;
                }
            }
            if (!allKnown) {
                for (const auto& block : cfg.blocks) {
                    for (const auto& inst : block.instructions) {
                        frame.initStoreAddrs.erase(inst.address);
                    }
                }
                region.initValues.clear();
            }
        }
    }

    std::map<int64_t, uint32_t> collectWriteOnlyStores(const Graph& cfg, const std::set<int64_t>& arrayBases)
    {
        std::map<int64_t, uint32_t> maxWriteSize;
        std::set<int64_t> readOffsets;

        for (const auto& block : cfg.blocks) {
            for (const auto& instruction : block.instructions) {
                for (size_t i = 0; i < instruction.operands.size(); ++i) {
                    const auto& operand = instruction.operands[i];
                    if (!operand.isHeapDeref()) {
                        continue;
                    }
                    const auto& memory = operand.heapDeref;
                    if (memory.absolute.has_value() || memory.base.empty() || !memory.index.empty()) {
                        continue;
                    }
                    if (!isLocalBase(memory.base)) {
                        continue;
                    }
                    const bool isWrite = instruction.type == IRType::STORE && i == 0;
                    if (isWrite) {
                        auto& existing = maxWriteSize[memory.displacement];
                        if (memory.sizeBytes > existing) {
                            existing = memory.sizeBytes;
                        }
                    } else {
                        readOffsets.insert(memory.displacement);
                    }
                }
            }
        }

        for (const int64_t offset : readOffsets) {
            if (!arrayBases.count(offset)) {
                maxWriteSize.erase(offset);
            }
        }
        return maxWriteSize;
    }

    int64_t computeArrayCoveredEnd(
          const std::map<int64_t, uint32_t>& writeOnlyStores, const int64_t B, const uint32_t elemSize, const std::set<int64_t>& otherArrayBases)
    {
        const auto capIt  = otherArrayBases.upper_bound(B);
        const int64_t cap = capIt != otherArrayBases.end() ? *capIt : std::numeric_limits<int64_t>::max();

        int64_t coveredEnd = B + static_cast<int64_t>(elemSize);

        const auto baseIt = writeOnlyStores.find(B);
        if (baseIt != writeOnlyStores.end() && baseIt->second > 0) {
            coveredEnd = std::max(coveredEnd, B + static_cast<int64_t>(baseIt->second));
        }
        coveredEnd = std::min(coveredEnd, cap);

        bool extended = true;
        while (extended) {
            extended = false;
            for (const auto& [offset, size] : writeOnlyStores) {
                if (offset <= B || offset >= cap || size == 0) {
                    continue;
                }
                if (offset <= coveredEnd) {
                    const int64_t newEnd = std::min(offset + static_cast<int64_t>(size), cap);
                    if (newEnd > coveredEnd) {
                        coveredEnd = newEnd;
                        extended   = true;
                    }
                }
            }
        }

        return coveredEnd;
    }

    std::set<int64_t> collectIndexSlots(const Graph& cfg)
    {
        std::set<int64_t> slots;
        for (const auto& block : cfg.blocks) {
            for (size_t i = 0; i < block.instructions.size(); ++i) {
                for (const auto& operand : block.instructions[i].operands) {
                    if (!operand.isHeapDeref() || operand.heapDeref.index.empty()) {
                        continue;
                    }
                    std::string wanted = normalizedRegisterBase(operand.heapDeref.index);
                    for (size_t j = i; j-- > 0;) {
                        const auto& prev = block.instructions[j];
                        if (prev.operands.empty() || normalizedRegisterBase(prev.operands[0].name) != wanted) {
                            continue;
                        }
                        if (prev.type == IRType::SEXT && prev.operands.size() >= 2) {
                            wanted = normalizedRegisterBase(prev.operands[1].name);
                            continue;
                        }
                        if (prev.type == IRType::LOAD && prev.operands.size() >= 2 && prev.operands[1].isHeapDeref()) {
                            const auto& mem = prev.operands[1].heapDeref;
                            if (!mem.absolute.has_value() && mem.index.empty() && isLocalBase(mem.base)) {
                                slots.insert(mem.displacement);
                            }
                        }
                        break;
                    }
                }
            }
        }
        return slots;
    }

    int64_t computeArrayInitEnd(const Graph& cfg, const int64_t base, const uint32_t elemSize, const int64_t cap, const std::set<int64_t>& indexSlots)
    {
        int64_t coveredEnd = base + static_cast<int64_t>(elemSize);
        bool started       = false;

        for (const auto& block : cfg.blocks) {
            for (const auto& instruction : block.instructions) {
                if (IRProperties::isControlFlow(instruction.type) || IRProperties::isCall(instruction.type)) {
                    if (started) {
                        return coveredEnd;
                    }
                    continue;
                }
                if (instruction.type != IRType::STORE || instruction.operands.empty() || !instruction.operands[0].isHeapDeref()) {
                    continue;
                }
                const auto& memory = instruction.operands[0].heapDeref;
                if (memory.absolute.has_value() || !memory.index.empty() || !isLocalBase(memory.base)) {
                    continue;
                }

                const int64_t off   = memory.displacement;
                const uint32_t size = memory.sizeBytes > 0 ? memory.sizeBytes : elemSize;
                if (!started) {
                    if (off == base) {
                        started    = true;
                        coveredEnd = std::min(base + static_cast<int64_t>(size), cap);
                    }
                    continue;
                }
                if (off == coveredEnd && coveredEnd < cap && indexSlots.count(off) == 0) {
                    coveredEnd = std::min(off + static_cast<int64_t>(size), cap);
                } else {
                    return coveredEnd;
                }
            }
        }
        return coveredEnd;
    }
} // namespace

const StackRegion* StackFrame::findRegion(const int64_t offset, const uint32_t sizeBytes) const
{
    const StackRegion* best = nullptr;
    for (const auto& region : regions) {
        const int64_t regionEnd = region.offset + static_cast<int64_t>(region.totalSize);
        const int64_t accessEnd = offset + static_cast<int64_t>(sizeBytes);
        if (offset >= region.offset && accessEnd <= regionEnd) {
            if (best == nullptr || region.offset > best->offset) {
                best = &region;
            }
        }
    }
    return best;
}

StackFrame StackFrame::analyze(const Graph& cfg, const bool is64Bit)
{
    std::vector<FixedAccess> framePointerFixed;
    std::vector<FixedAccess> stackPointerFixed;
    std::map<IndexedAccessKey, uint32_t> framePointerIndexed;
    collectAccesses(cfg, framePointerFixed, stackPointerFixed, framePointerIndexed);

    std::set<int64_t> indexedArrayBases;
    for (const auto& entry : framePointerIndexed) {
        indexedArrayBases.insert(entry.first.offset);
    }

    const auto writeOnlyStores = collectWriteOnlyStores(cfg, indexedArrayBases);
    const auto indexSlots      = collectIndexSlots(cfg);

    StackFrame frame;

    struct ArrayCoverage {
        int64_t start;
        int64_t end;
    };
    std::vector<ArrayCoverage> arrayCoverages;

    for (const auto& entry : framePointerIndexed) {
        const int64_t offset    = entry.first.offset;
        const int32_t scale     = entry.first.scale;
        const uint32_t elemSize = entry.second != 0 ? entry.second : static_cast<uint32_t>(scale);

        std::set<int64_t> otherBases = indexedArrayBases;
        otherBases.erase(offset);

        const auto capIt  = otherBases.upper_bound(offset);
        const int64_t cap = capIt != otherBases.end() ? *capIt : std::numeric_limits<int64_t>::max();
        const int64_t coveredEnd =
              std::max(computeArrayCoveredEnd(writeOnlyStores, offset, elemSize, otherBases), computeArrayInitEnd(cfg, offset, elemSize, cap, indexSlots));
        const uint32_t count      = static_cast<uint32_t>((coveredEnd - offset) / static_cast<int64_t>(elemSize));
        const uint32_t finalCount = count > 0 ? count : 1;

        StackRegion region;
        region.kind        = StackRegionKind::Array;
        region.offset      = offset;
        region.elementSize = elemSize;
        region.count       = finalCount;
        region.totalSize   = finalCount * elemSize;
        region.name        = arrayName(offset);
        frame.regions.push_back(region);

        arrayCoverages.push_back({ offset, offset + static_cast<int64_t>(finalCount * elemSize) });
    }

    auto inAnyArrayRange = [&](const int64_t offset) {
        for (const auto& ac : arrayCoverages) {
            if (offset >= ac.start && offset < ac.end) {
                return true;
            }
        }
        return false;
    };

    std::map<int64_t, std::vector<uint32_t>> localSizes;
    std::map<int64_t, std::vector<uint32_t>> argumentSizes;
    for (const auto& access : framePointerFixed) {
        if (inAnyArrayRange(access.offset)) {
            continue;
        }
        const uint32_t size = access.size;
        if (access.offset <= 0) {
            localSizes[access.offset].push_back(size);
        } else {
            argumentSizes[access.offset].push_back(size);
        }
    }

    for (const auto& entry : localSizes) {
        frame.regions.push_back(buildRegion(entry.first, entry.second, StackRegionKind::SingleVar, localName(entry.first)));
    }
    for (const auto& entry : argumentSizes) {
        frame.regions.push_back(buildRegion(entry.first, entry.second, StackRegionKind::Argument, argumentName(entry.first, is64Bit)));
    }

    std::map<int64_t, std::vector<uint32_t>> spLocalSizes;
    for (const auto& access : stackPointerFixed) {
        if (access.offset < 0 || inAnyArrayRange(access.offset)) {
            continue;
        }
        spLocalSizes[access.offset].push_back(access.size);
    }
    for (const auto& entry : spLocalSizes) {
        frame.regions.push_back(buildRegion(entry.first, entry.second, StackRegionKind::SingleVar, "var_" + std::to_string(entry.first)));
    }

    std::sort(frame.regions.begin(), frame.regions.end(), stackRegionByOffset);

    collectArrayInitValues(frame, cfg);

    return frame;
}
} // namespace Decompiler
