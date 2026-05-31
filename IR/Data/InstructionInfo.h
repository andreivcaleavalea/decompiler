#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace Decompiler::InstructionInfo
{
enum class MnemonicKind { Data, Arithmetic, Bitwise, Flag, ControlFlow, Stack, Misc, Unknown };

inline constexpr std::array<std::string_view, 8> SizeSuffixCanonicalMnemonics = { "mov", "add", "sub", "cmp", "test", "and", "or", "xor" };

inline constexpr std::array<std::string_view, 6> ShiftMnemonics = { "shl", "sal", "shr", "sar", "rol", "ror" };

inline constexpr std::array<std::string_view, 4> MultiplyDivideMnemonics = { "imul", "mul", "idiv", "div" };

inline constexpr std::array<std::string_view, 6> MiscSizedMnemonics = { "push", "pop", "call", "ret", "lea", "nop" };

inline constexpr std::array<std::string_view, 4> UnarySizedMnemonics = { "inc", "dec", "neg", "not" };

template <typename Container>
inline bool contains(const Container& values, const std::string_view value)
{
    for (const auto& v : values) {
        if (v == value) {
            return true;
        }
    }
    return false;
}

inline bool supportsSizeSuffix(const std::string_view mnemonic)
{
    return contains(SizeSuffixCanonicalMnemonics, mnemonic) || contains(ShiftMnemonics, mnemonic) || contains(MultiplyDivideMnemonics, mnemonic) ||
           contains(MiscSizedMnemonics, mnemonic) || contains(UnarySizedMnemonics, mnemonic);
}

inline std::string canonicalizeMnemonic(std::string mnemonic)
{
    for (size_t i = 0; i < mnemonic.size(); ++i) {
        mnemonic[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(mnemonic[i])));
    }

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
} // namespace Decompiler::InstructionInfo
