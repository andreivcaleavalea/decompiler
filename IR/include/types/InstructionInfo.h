#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace Decompiler::InstructionInfo {
    enum class MnemonicKind {
        Data,
        Arithmetic,
        Bitwise,
        Flag,
        ControlFlow,
        Stack,
        Misc,
        Unknown
    };

    inline constexpr std::array<std::string_view, 8> SizeSuffixCanonicalMnemonics = {
        "mov", "add", "sub", "cmp", "test", "and", "or", "xor"
    };

    inline constexpr std::array<std::string_view, 6> ShiftMnemonics = {
        "shl", "sal", "shr", "sar", "rol", "ror"
    };

    inline constexpr std::array<std::string_view, 4> MultiplyDivideMnemonics = {
        "imul", "mul", "idiv", "div"
    };

    inline constexpr std::array<std::string_view, 6> MiscSizedMnemonics = {
        "push", "pop", "call", "ret", "lea", "nop"
    };

    inline constexpr std::array<std::string_view, 4> UnarySizedMnemonics = {
        "inc", "dec", "neg", "not"
    };

    inline bool contains(const auto& values, const std::string_view value) {
        return std::ranges::find(values, value) != values.end();
    }

    inline bool supportsSizeSuffix(const std::string_view mnemonic) {
        return contains(SizeSuffixCanonicalMnemonics, mnemonic) ||
               contains(ShiftMnemonics, mnemonic) ||
               contains(MultiplyDivideMnemonics, mnemonic) ||
               contains(MiscSizedMnemonics, mnemonic) ||
               contains(UnarySizedMnemonics, mnemonic);
    }

    inline std::string canonicalizeMnemonic(std::string mnemonic) {
        std::ranges::transform(mnemonic, mnemonic.begin(), [](const unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });

        for (const std::string_view prefix : { "lock ", "rep ", "repe ", "repne " }) {
            if (mnemonic.starts_with(prefix)) {
                mnemonic.erase(0, prefix.size());
                break;
            }
        }

        if (mnemonic == "movabs") {
            return "mov";
        }
        if (mnemonic == "retq") {
            return "ret";
        }
        if (mnemonic == "callq") {
            return "call";
        }

        if (mnemonic.size() > 1) {
            const char suffix = mnemonic.back();
            if (suffix == 'b' || suffix == 'w' || suffix == 'l' || suffix == 'q') {
                const auto base = std::string_view{ mnemonic }.substr(0, mnemonic.size() - 1);
                if (supportsSizeSuffix(base)) {
                    return std::string(base);
                }
            }
        }

        return mnemonic;
    }
}
