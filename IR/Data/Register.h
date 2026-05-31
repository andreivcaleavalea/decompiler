#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace Decompiler
{

enum class PhysReg : uint8_t {
    None = 0,

    RAX,
    RBX,
    RCX,
    RDX,
    RSI,
    RDI,
    RSP,
    RBP,
    R8,
    R9,
    R10,
    R11,
    R12,
    R13,
    R14,
    R15,

    XMM0,
    XMM1,
    XMM2,
    XMM3,
    XMM4,
    XMM5,
    XMM6,
    XMM7,
    XMM8,
    XMM9,
    XMM10,
    XMM11,
    XMM12,
    XMM13,
    XMM14,
    XMM15,

    RIP,
    RFLAGS,
};

struct Register {
    PhysReg physReg     = PhysReg::None;
    uint32_t sizeBytes  = 0;
    uint32_t byteOffset = 0;
    std::string name;

    bool isValid() const
    {
        return physReg != PhysReg::None;
    }

    bool overlaps(const Register& other) const;
    bool isSubOf(const Register& other) const;
    bool isSuperOf(const Register& other) const;
};

std::string canonicalName(PhysReg physReg);
Register registerFromName(const std::string& name);

} // namespace Decompiler

namespace std
{
template <>
struct hash<Decompiler::PhysReg> {
    size_t operator()(Decompiler::PhysReg pr) const
    {
        return static_cast<size_t>(pr);
    }
};
} // namespace std
