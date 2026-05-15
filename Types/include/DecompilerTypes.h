#pragma once

#include <cstdint>
#include <string>

namespace Decompiler {
    struct AssemblyInstruction {
        uint64_t address = 0;
        uint16_t size = 0;
        std::string mnemonic;
        std::string operands;
        std::string bytes;
        std::string comment;
    };

    struct SectionInfo {
        std::string name;
        uint64_t virtualAddress = 0;
        uint64_t size = 0;
        uint64_t offset = 0;
        bool isExecutable = false;
    };
}
