#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "DecompilerTypes.h"

namespace Decompiler {
    enum class BinaryFormat {
        Unknown,
        PE,
        MachO,
        ELF
    };

    enum class BinarySubformat {
        Unknown,
        PE32,
        PE64,
        MachO32,
        MachO64,
        ELF32,
        ELF64
    };

    struct GlobalMemoryRegion {
        uint64_t start = 0;
        uint64_t size = 0;
        uint64_t fileOffset = 0;
        std::string sectionName;
    };

    struct COFFSymbolInfo {
        std::string name;
        uint32_t value = 0;
        int16_t sectionNumber = 0;
        uint16_t type = 0;
        uint8_t storageClass = 0;
    };

    struct DecompileContext {
        std::vector<SectionInfo> sections;
        std::vector<COFFSymbolInfo> coffSymbols;
        const std::vector<uint8_t>* rawData = nullptr;
        uint64_t entryPointAddress = 0;
        bool onlyEntryPointReachable = false;
        BinaryFormat format = BinaryFormat::Unknown;
        BinarySubformat subformat = BinarySubformat::Unknown;
    };

    struct ReferencedGlobal {
        uint64_t address = 0;
        size_t sizeBytes = 0;
        std::string symbol;
        bool isFloat = false;
    };
}
