#include "Register.h"

#include <unordered_map>

namespace Decompiler
{

bool Register::overlaps(const Register& other) const
{
    if (physReg == PhysReg::None || other.physReg == PhysReg::None)
        return false;
    if (physReg != other.physReg)
        return false;
    const uint32_t endA = byteOffset + sizeBytes;
    const uint32_t endB = other.byteOffset + other.sizeBytes;
    return byteOffset < endB && other.byteOffset < endA;
}

bool Register::isSubOf(const Register& other) const
{
    if (physReg == PhysReg::None || other.physReg == PhysReg::None)
        return false;
    if (physReg != other.physReg)
        return false;
    return byteOffset >= other.byteOffset && (byteOffset + sizeBytes) <= (other.byteOffset + other.sizeBytes);
}

bool Register::isSuperOf(const Register& other) const
{
    return other.isSubOf(*this);
}

std::string canonicalName(const PhysReg pr)
{
    switch (pr) {
    case PhysReg::RAX:
        return "rax";
    case PhysReg::RBX:
        return "rbx";
    case PhysReg::RCX:
        return "rcx";
    case PhysReg::RDX:
        return "rdx";
    case PhysReg::RSI:
        return "rsi";
    case PhysReg::RDI:
        return "rdi";
    case PhysReg::RSP:
        return "rsp";
    case PhysReg::RBP:
        return "rbp";
    case PhysReg::R8:
        return "r8";
    case PhysReg::R9:
        return "r9";
    case PhysReg::R10:
        return "r10";
    case PhysReg::R11:
        return "r11";
    case PhysReg::R12:
        return "r12";
    case PhysReg::R13:
        return "r13";
    case PhysReg::R14:
        return "r14";
    case PhysReg::R15:
        return "r15";
    case PhysReg::XMM0:
        return "xmm0";
    case PhysReg::XMM1:
        return "xmm1";
    case PhysReg::XMM2:
        return "xmm2";
    case PhysReg::XMM3:
        return "xmm3";
    case PhysReg::XMM4:
        return "xmm4";
    case PhysReg::XMM5:
        return "xmm5";
    case PhysReg::XMM6:
        return "xmm6";
    case PhysReg::XMM7:
        return "xmm7";
    case PhysReg::XMM8:
        return "xmm8";
    case PhysReg::XMM9:
        return "xmm9";
    case PhysReg::XMM10:
        return "xmm10";
    case PhysReg::XMM11:
        return "xmm11";
    case PhysReg::XMM12:
        return "xmm12";
    case PhysReg::XMM13:
        return "xmm13";
    case PhysReg::XMM14:
        return "xmm14";
    case PhysReg::XMM15:
        return "xmm15";
    case PhysReg::RIP:
        return "rip";
    case PhysReg::RFLAGS:
        return "rflags";
    case PhysReg::None:
        return "";
    }
    return "";
}

static Register makeReg(PhysReg pr, uint32_t size, uint32_t off, const char* n)
{
    return { pr, size, off, n };
}

static const std::unordered_map<std::string, Register>& lookupTable()
{
    static const std::unordered_map<std::string, Register> table = {
        { "rax", makeReg(PhysReg::RAX, 8, 0, "rax") },
        { "eax", makeReg(PhysReg::RAX, 4, 0, "eax") },
        { "ax", makeReg(PhysReg::RAX, 2, 0, "ax") },
        { "al", makeReg(PhysReg::RAX, 1, 0, "al") },
        { "ah", makeReg(PhysReg::RAX, 1, 1, "ah") },

        { "rbx", makeReg(PhysReg::RBX, 8, 0, "rbx") },
        { "ebx", makeReg(PhysReg::RBX, 4, 0, "ebx") },
        { "bx", makeReg(PhysReg::RBX, 2, 0, "bx") },
        { "bl", makeReg(PhysReg::RBX, 1, 0, "bl") },
        { "bh", makeReg(PhysReg::RBX, 1, 1, "bh") },

        { "rcx", makeReg(PhysReg::RCX, 8, 0, "rcx") },
        { "ecx", makeReg(PhysReg::RCX, 4, 0, "ecx") },
        { "cx", makeReg(PhysReg::RCX, 2, 0, "cx") },
        { "cl", makeReg(PhysReg::RCX, 1, 0, "cl") },
        { "ch", makeReg(PhysReg::RCX, 1, 1, "ch") },

        { "rdx", makeReg(PhysReg::RDX, 8, 0, "rdx") },
        { "edx", makeReg(PhysReg::RDX, 4, 0, "edx") },
        { "dx", makeReg(PhysReg::RDX, 2, 0, "dx") },
        { "dl", makeReg(PhysReg::RDX, 1, 0, "dl") },
        { "dh", makeReg(PhysReg::RDX, 1, 1, "dh") },

        { "rsi", makeReg(PhysReg::RSI, 8, 0, "rsi") },
        { "esi", makeReg(PhysReg::RSI, 4, 0, "esi") },
        { "si", makeReg(PhysReg::RSI, 2, 0, "si") },
        { "sil", makeReg(PhysReg::RSI, 1, 0, "sil") },

        { "rdi", makeReg(PhysReg::RDI, 8, 0, "rdi") },
        { "edi", makeReg(PhysReg::RDI, 4, 0, "edi") },
        { "di", makeReg(PhysReg::RDI, 2, 0, "di") },
        { "dil", makeReg(PhysReg::RDI, 1, 0, "dil") },

        { "rsp", makeReg(PhysReg::RSP, 8, 0, "rsp") },
        { "esp", makeReg(PhysReg::RSP, 4, 0, "esp") },
        { "sp", makeReg(PhysReg::RSP, 2, 0, "sp") },
        { "spl", makeReg(PhysReg::RSP, 1, 0, "spl") },

        { "rbp", makeReg(PhysReg::RBP, 8, 0, "rbp") },
        { "ebp", makeReg(PhysReg::RBP, 4, 0, "ebp") },
        { "bp", makeReg(PhysReg::RBP, 2, 0, "bp") },
        { "bpl", makeReg(PhysReg::RBP, 1, 0, "bpl") },

        { "r8", makeReg(PhysReg::R8, 8, 0, "r8") },
        { "r8d", makeReg(PhysReg::R8, 4, 0, "r8d") },
        { "r8w", makeReg(PhysReg::R8, 2, 0, "r8w") },
        { "r8b", makeReg(PhysReg::R8, 1, 0, "r8b") },

        { "r9", makeReg(PhysReg::R9, 8, 0, "r9") },
        { "r9d", makeReg(PhysReg::R9, 4, 0, "r9d") },
        { "r9w", makeReg(PhysReg::R9, 2, 0, "r9w") },
        { "r9b", makeReg(PhysReg::R9, 1, 0, "r9b") },

        { "r10", makeReg(PhysReg::R10, 8, 0, "r10") },
        { "r10d", makeReg(PhysReg::R10, 4, 0, "r10d") },
        { "r10w", makeReg(PhysReg::R10, 2, 0, "r10w") },
        { "r10b", makeReg(PhysReg::R10, 1, 0, "r10b") },

        { "r11", makeReg(PhysReg::R11, 8, 0, "r11") },
        { "r11d", makeReg(PhysReg::R11, 4, 0, "r11d") },
        { "r11w", makeReg(PhysReg::R11, 2, 0, "r11w") },
        { "r11b", makeReg(PhysReg::R11, 1, 0, "r11b") },

        { "r12", makeReg(PhysReg::R12, 8, 0, "r12") },
        { "r12d", makeReg(PhysReg::R12, 4, 0, "r12d") },
        { "r12w", makeReg(PhysReg::R12, 2, 0, "r12w") },
        { "r12b", makeReg(PhysReg::R12, 1, 0, "r12b") },

        { "r13", makeReg(PhysReg::R13, 8, 0, "r13") },
        { "r13d", makeReg(PhysReg::R13, 4, 0, "r13d") },
        { "r13w", makeReg(PhysReg::R13, 2, 0, "r13w") },
        { "r13b", makeReg(PhysReg::R13, 1, 0, "r13b") },

        { "r14", makeReg(PhysReg::R14, 8, 0, "r14") },
        { "r14d", makeReg(PhysReg::R14, 4, 0, "r14d") },
        { "r14w", makeReg(PhysReg::R14, 2, 0, "r14w") },
        { "r14b", makeReg(PhysReg::R14, 1, 0, "r14b") },

        { "r15", makeReg(PhysReg::R15, 8, 0, "r15") },
        { "r15d", makeReg(PhysReg::R15, 4, 0, "r15d") },
        { "r15w", makeReg(PhysReg::R15, 2, 0, "r15w") },
        { "r15b", makeReg(PhysReg::R15, 1, 0, "r15b") },

        { "xmm0", makeReg(PhysReg::XMM0, 16, 0, "xmm0") },
        { "xmm1", makeReg(PhysReg::XMM1, 16, 0, "xmm1") },
        { "xmm2", makeReg(PhysReg::XMM2, 16, 0, "xmm2") },
        { "xmm3", makeReg(PhysReg::XMM3, 16, 0, "xmm3") },
        { "xmm4", makeReg(PhysReg::XMM4, 16, 0, "xmm4") },
        { "xmm5", makeReg(PhysReg::XMM5, 16, 0, "xmm5") },
        { "xmm6", makeReg(PhysReg::XMM6, 16, 0, "xmm6") },
        { "xmm7", makeReg(PhysReg::XMM7, 16, 0, "xmm7") },
        { "xmm8", makeReg(PhysReg::XMM8, 16, 0, "xmm8") },
        { "xmm9", makeReg(PhysReg::XMM9, 16, 0, "xmm9") },
        { "xmm10", makeReg(PhysReg::XMM10, 16, 0, "xmm10") },
        { "xmm11", makeReg(PhysReg::XMM11, 16, 0, "xmm11") },
        { "xmm12", makeReg(PhysReg::XMM12, 16, 0, "xmm12") },
        { "xmm13", makeReg(PhysReg::XMM13, 16, 0, "xmm13") },
        { "xmm14", makeReg(PhysReg::XMM14, 16, 0, "xmm14") },
        { "xmm15", makeReg(PhysReg::XMM15, 16, 0, "xmm15") },

        { "rip", makeReg(PhysReg::RIP, 8, 0, "rip") },
        { "rflags", makeReg(PhysReg::RFLAGS, 8, 0, "rflags") },
        { "eflags", makeReg(PhysReg::RFLAGS, 4, 0, "eflags") },
    };

    return table;
}

Register registerFromName(const std::string& name)
{
    const auto& table = lookupTable();
    const auto it     = table.find(name);
    if (it != table.end()) {
        return it->second;
    }
    return { PhysReg::None, 0, 0, name };
}

} // namespace Decompiler
