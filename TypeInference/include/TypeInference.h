#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "ControlGraphFlow.h"

namespace Decompiler
{
enum class TypeCategory { Unknown, Integer, Float, Pointer, CharPointer };
enum class TypeSign { Unknown, Signed, Unsigned };
enum class TypeSize { Unknown, Bits8, Bits16, Bits32, Bits64 };

struct VarType {
    TypeCategory category = TypeCategory::Unknown;
    TypeSign sign         = TypeSign::Unknown;
    TypeSize size         = TypeSize::Unknown;

    bool isUnknown() const;
    std::string toString() const;

    bool meet(TypeCategory cat);
    bool meet(TypeSign s);
    bool meet(TypeSize sz);
    bool meet(const VarType& other);
};

using TypeMap = std::unordered_map<std::string, VarType>;

struct CallSiteSignature {
    VarType returnType;
    std::vector<VarType> paramTypes;
};

using CallSiteSignatureMap = std::unordered_map<std::string, CallSiteSignature>;

TypeMap inferTypes(const Graph& cfg);
} // namespace Decompiler
