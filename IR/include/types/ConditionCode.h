#pragma once

#include <string>

namespace Decompiler {
    enum class ConditionCode {
        None,
        E,
        NE,
        L,
        LE,
        G,
        GE,
        B,
        BE,
        A,
        AE
    };

    inline bool isUnsignedCondition(const ConditionCode condition) {
        return condition == ConditionCode::B || condition == ConditionCode::BE || condition == ConditionCode::A || condition == ConditionCode::AE;
    }

    inline std::string conditionToOperator(const ConditionCode condition) {
        switch (condition) {
            case ConditionCode::E: return "==";
            case ConditionCode::NE: return "!=";
            case ConditionCode::L:
            case ConditionCode::B: return "<";
            case ConditionCode::LE:
            case ConditionCode::BE: return "<=";
            case ConditionCode::G:
            case ConditionCode::A: return ">";
            case ConditionCode::GE:
            case ConditionCode::AE: return ">=";
            case ConditionCode::None: return "";
        }
        return "";
    }

    inline ConditionCode invertCondition(const ConditionCode condition) {
        switch (condition) {
            case ConditionCode::E: return ConditionCode::NE;
            case ConditionCode::NE: return ConditionCode::E;
            case ConditionCode::L: return ConditionCode::GE;
            case ConditionCode::LE: return ConditionCode::G;
            case ConditionCode::G: return ConditionCode::LE;
            case ConditionCode::GE: return ConditionCode::L;
            case ConditionCode::B: return ConditionCode::AE;
            case ConditionCode::BE: return ConditionCode::A;
            case ConditionCode::A: return ConditionCode::BE;
            case ConditionCode::AE: return ConditionCode::B;
            case ConditionCode::None: return ConditionCode::None;
        }
        return ConditionCode::None;
    }
}
