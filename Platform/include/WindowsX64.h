#pragma once

#include <cctype>
#include <optional>
#include <string>
#include <string_view>

#include "Register.h"

namespace Decompiler
{

inline std::string normalizedRegisterBase(std::string value)
{
    const auto first = value.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(' ');
    value           = value.substr(first, last - first + 1);

    const auto underscore = value.find('_');
    if (underscore != std::string::npos) {
        value = value.substr(0, underscore);
    }

    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    if (value == "al" || value == "ah" || value == "ax" || value == "eax")
        return "rax";
    if (value == "bl" || value == "bh" || value == "bx" || value == "ebx")
        return "rbx";
    if (value == "cl" || value == "ch" || value == "cx" || value == "ecx")
        return "rcx";
    if (value == "dl" || value == "dh" || value == "dx" || value == "edx")
        return "rdx";
    if (value == "sil" || value == "si" || value == "esi")
        return "rsi";
    if (value == "dil" || value == "di" || value == "edi")
        return "rdi";
    if (value == "bpl" || value == "bp" || value == "ebp")
        return "rbp";
    if (value == "spl" || value == "sp" || value == "esp")
        return "rsp";
    if (value.size() >= 3 && value[0] == 'r' && std::isdigit(static_cast<unsigned char>(value[1])) != 0) {
        if (value.back() == 'b' || value.back() == 'w' || value.back() == 'd') {
            return value.substr(0, value.size() - 1);
        }
    }
    return value;
}

inline bool isReturnRegisterName(const std::string& name)
{
    const std::string base = normalizedRegisterBase(name);
    return base == "rax" || base == "xmm0";
}

inline bool isStackPointerRegisterName(const std::string& name)
{
    return normalizedRegisterBase(name) == "rsp";
}

inline bool isFramePointerRegisterName(const std::string& name)
{
    return normalizedRegisterBase(name) == "rbp";
}

inline bool isStackOrFrameRegisterName(const std::string& name)
{
    return isStackPointerRegisterName(name) || isFramePointerRegisterName(name);
}

inline std::optional<size_t> argumentIndexForPhysReg(const PhysReg physReg)
{
    switch (physReg) {
    case PhysReg::RCX:
    case PhysReg::XMM0:
        return 0;
    case PhysReg::RDX:
    case PhysReg::XMM1:
        return 1;
    case PhysReg::R8:
    case PhysReg::XMM2:
        return 2;
    case PhysReg::R9:
    case PhysReg::XMM3:
        return 3;
    default:
        return std::nullopt;
    }
}

inline std::optional<size_t> argumentIndexForRegisterBase(const std::string& base)
{
    if (base.empty()) {
        return std::nullopt;
    }
    if (base == "rcx" || base == "xmm0")
        return 0;
    if (base == "rdx" || base == "xmm1")
        return 1;
    if (base == "r8" || base == "xmm2")
        return 2;
    if (base == "r9" || base == "xmm3")
        return 3;
    return std::nullopt;
}

inline std::optional<size_t> argumentIndexForRegisterName(const std::string& name)
{
    return argumentIndexForRegisterBase(normalizedRegisterBase(name));
}

inline std::string registerBaseForArgumentIndex(const size_t index)
{
    static const std::string regs[] = { "rcx", "rdx", "r8", "r9" };
    return index < 4 ? regs[index] : std::string{};
}

inline std::optional<size_t> argumentIndexForWin64HomeSlot(const long long rbpPositiveOffset)
{
    constexpr long long firstHomeSlotOffset = 16;
    constexpr long long homeSlotSize        = 8;
    if (rbpPositiveOffset < firstHomeSlotOffset) {
        return std::nullopt;
    }
    const auto relativeOffset = rbpPositiveOffset - firstHomeSlotOffset;
    if ((relativeOffset % homeSlotSize) != 0) {
        return std::nullopt;
    }
    return static_cast<size_t>(relativeOffset / homeSlotSize);
}

inline std::optional<size_t> argumentIndexForStackSlot(const long long framePointerOffset, const bool is64Bit)
{
    if (is64Bit) {
        return argumentIndexForWin64HomeSlot(framePointerOffset);
    }
    constexpr long long firstArgumentOffset = 8;
    constexpr long long slotSize            = 4;
    if (framePointerOffset < firstArgumentOffset) {
        return std::nullopt;
    }
    const auto relativeOffset = framePointerOffset - firstArgumentOffset;
    if ((relativeOffset % slotSize) != 0) {
        return std::nullopt;
    }
    return static_cast<size_t>(relativeOffset / slotSize);
}

inline std::optional<size_t> parseStackArgumentSymbolIndex(const std::string& value)
{
    if (!value.starts_with("arg_")) {
        return std::nullopt;
    }
    const auto digits = std::string_view(value).substr(4);
    if (digits.empty()) {
        return std::nullopt;
    }
    size_t index = 0;
    for (const unsigned char ch : digits) {
        if (std::isdigit(ch) == 0) {
            return std::nullopt;
        }
        index = index * 10 + static_cast<size_t>(ch - '0');
    }
    return index;
}

} // namespace Decompiler
