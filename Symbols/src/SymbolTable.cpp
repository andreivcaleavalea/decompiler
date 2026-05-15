#include "SymbolTable.h"

#include <cctype>
#include <unordered_map>
#include <string_view>

#include "Functions.h"

namespace Decompiler::Symbols {
    namespace {
        constexpr uint16_t symFunction = 0x20;

        std::string normalizeSymbolName(std::string_view name) {
            const auto separator = name.rfind(": ");
            if (separator != std::string_view::npos) {
                name = name.substr(separator + 2);
            }

            std::string normalized;
            normalized.reserve(name.size());
            for (const auto ch : name) {
                const auto c = static_cast<unsigned char>(ch);
                normalized.push_back((std::isalnum(c) != 0 || ch == '_') ? ch : '_');
            }

            while (!normalized.empty() && normalized.front() == ' ') {
                normalized.erase(normalized.begin());
            }
            while (!normalized.empty() && normalized.back() == ' ') {
                normalized.pop_back();
            }

            if (normalized.empty()) {
                return {};
            }
            if (std::isdigit(static_cast<unsigned char>(normalized.front())) != 0) {
                normalized.insert(normalized.begin(), '_');
            }
            return normalized;
        }

        bool sectionIndexForSymbol(const COFFSymbolInfo& symbol, const std::vector<SectionInfo>& sections, size_t& sectionIndex) {
            if (symbol.sectionNumber <= 0) {
                return false;
            }

            sectionIndex = static_cast<size_t>(symbol.sectionNumber - 1);
            return sectionIndex < sections.size();
        }

        bool isSectionMarkerSymbol(const COFFSymbolInfo& symbol, const SectionInfo& section) {
            return symbol.name == section.name || symbol.name.starts_with(".");
        }

        bool isLowQualityGlobalSymbolName(const std::string& name) {
            return name.starts_with("_idata") ||
                   name.starts_with("__imp_") ||
                   name.starts_with("_refptr_") ||
                   name.starts_with("refptr_");
        }

        int functionCandidateScore(const COFFSymbolInfo& symbol, const std::string& name) {
            int score = 0;
            if (symbol.type == symFunction) {
                score += 100;
            }
            if (!name.empty() && name.front() != '_') {
                score += 10;
            }
            return score;
        }

        int globalCandidateScore(const std::string& name) {
            int score = 0;
            if (!name.empty() && name.front() != '_') {
                score += 10;
            }
            if (isLowQualityGlobalSymbolName(name)) {
                score -= 100;
            }
            return score;
        }
    }

    ResolvedSymbols resolveCOFFSymbols(const std::vector<SectionInfo>& sections, const std::vector<COFFSymbolInfo>& symbols) {
        ResolvedSymbols resolved;
        std::unordered_map<uint64_t, int> functionScores;
        std::unordered_map<uint64_t, int> globalScores;

        for (const auto& symbol : symbols) {
            size_t sectionIndex = 0;
            if (!sectionIndexForSymbol(symbol, sections, sectionIndex)) {
                continue;
            }

            const std::string name = normalizeSymbolName(symbol.name);
            if (name.empty()) {
                continue;
            }

            const auto& section = sections[sectionIndex];
            if (isSectionMarkerSymbol(symbol, section)) {
                continue;
            }

            const uint64_t address = section.virtualAddress + static_cast<uint64_t>(symbol.value);

            if (section.isExecutable) {
                if (symbol.type != symFunction) {
                    continue;
                }

                const int score = functionCandidateScore(symbol, name);
                const auto existing = functionScores.find(address);
                if (existing == functionScores.end() || score > existing->second) {
                    resolved.functions[address] = name;
                    functionScores[address] = score;
                }
                continue;
            }

            const int score = globalCandidateScore(name);
            const auto existing = globalScores.find(address);
            if (existing == globalScores.end() || score > existing->second) {
                resolved.globals[address] = name;
                globalScores[address] = score;
            }
        }

        return resolved;
    }

    void applyFunctionSymbols(std::vector<DecompilerFunction>& functions, const ResolvedSymbols& symbols) {
        for (auto& function : functions) {
            if (const auto symbolIt = symbols.functions.find(function.start_address); symbolIt != symbols.functions.end()) {
                function.name = symbolIt->second;
            }
        }
    }
}
