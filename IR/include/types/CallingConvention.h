#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>

namespace Decompiler {
    enum class CallingConvention {
        Unknown,
        SystemV,
        Win64
    };

    enum class StackFrameLayout {
        Generic,
        Win64HomeSlots
    };

    inline StackFrameLayout stackFrameLayoutForCallingConvention(const CallingConvention callingConvention) {
        return callingConvention == CallingConvention::Win64 ? StackFrameLayout::Win64HomeSlots : StackFrameLayout::Generic;
    }

    inline std::string normalizedRegisterBase(std::string value) {
        const auto first = value.find_first_not_of(' ');
        if (first == std::string::npos) {
            return {};
        }
        const auto last = value.find_last_not_of(' ');
        value = value.substr(first, last - first + 1);

        const auto underscore = value.find('_');
        if (underscore != std::string::npos) {
            value = value.substr(0, underscore);
        }

        std::ranges::transform(value, value.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        if (value == "al" || value == "ah" || value == "ax" || value == "eax") return "rax";
        if (value == "bl" || value == "bh" || value == "bx" || value == "ebx") return "rbx";
        if (value == "cl" || value == "ch" || value == "cx" || value == "ecx") return "rcx";
        if (value == "dl" || value == "dh" || value == "dx" || value == "edx") return "rdx";
        if (value == "sil" || value == "si" || value == "esi") return "rsi";
        if (value == "dil" || value == "di" || value == "edi") return "rdi";
        if (value == "bpl" || value == "bp" || value == "ebp") return "rbp";
        if (value == "spl" || value == "sp" || value == "esp") return "rsp";
        if (value.size() >= 3 && value[0] == 'r' && std::isdigit(static_cast<unsigned char>(value[1])) != 0) {
            if (value.ends_with('b') || value.ends_with('w') || value.ends_with('d')) {
                return value.substr(0, value.size() - 1);
            }
        }
        return value;
    }

    inline std::optional<size_t> argumentIndexForRegisterBase(const std::string& base, const CallingConvention callingConvention) {
        if (base.empty()) {
            return std::nullopt;
        }

        if (callingConvention == CallingConvention::SystemV) {
            if (base == "rdi") return 0;
            if (base == "rsi") return 1;
            if (base == "rdx") return 2;
            if (base == "rcx") return 3;
            if (base == "r8") return 4;
            if (base == "r9") return 5;
            return std::nullopt;
        }

        if (callingConvention == CallingConvention::Win64) {
            if (base == "rcx") return 0;
            if (base == "rdx") return 1;
            if (base == "r8") return 2;
            if (base == "r9") return 3;
            return std::nullopt;
        }

        if (base == "rdi" || base == "rcx") return 0;
        if (base == "rsi" || base == "rdx") return 1;
        if (base == "r8") return 2;
        if (base == "r9") return 3;
        return std::nullopt;
    }

    inline std::optional<size_t> argumentIndexForRegisterName(const std::string& registerName, const CallingConvention callingConvention) {
        return argumentIndexForRegisterBase(normalizedRegisterBase(registerName), callingConvention);
    }

    inline std::optional<size_t> argumentIndexForWin64HomeSlot(const long long rbpPositiveOffset) {
        constexpr long long firstHomeSlotOffset = 16;
        constexpr long long homeSlotSize = 8;
        if (rbpPositiveOffset < firstHomeSlotOffset) {
            return std::nullopt;
        }
        const auto relativeOffset = rbpPositiveOffset - firstHomeSlotOffset;
        if ((relativeOffset % homeSlotSize) != 0) {
            return std::nullopt;
        }
        return static_cast<size_t>(relativeOffset / homeSlotSize);
    }

    inline std::optional<size_t> parseStackArgumentSymbolIndex(const std::string& value) {
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
}
