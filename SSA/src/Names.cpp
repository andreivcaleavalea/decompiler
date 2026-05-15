#include "Names.h"

namespace Decompiler {
static bool isDecimalNumber(std::string text) {
    if (text.empty()) {
        return false;
    }

    for (char ch : text) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }
    return true;
}

std::string ssaBaseName(std::string name) {
    size_t underscore = name.find_last_of('_');
    if (underscore == std::string::npos || underscore + 1 >= name.size()) {
        return name;
    }

    std::string suffix = name.substr(underscore + 1);
    if (!isDecimalNumber(suffix)) {
        return name;
    }

    return name.substr(0, underscore);
}

std::string ssaVersionName(std::string name, size_t version) {
    return name + "_" + std::to_string(version);
}

std::vector<std::string> splitPhiInputs(std::string inputs) {
    std::vector<std::string> result;
    std::string current;

    for (char ch : inputs) {
        if (ch == '|') {
            result.push_back(current);
            current.clear();
        } else {
            current += ch;
        }
    }

    result.push_back(current);
    return result;
}

std::string joinPhiInputs(std::vector<std::string> inputs) {
    std::string result;
    for (size_t i = 0; i < inputs.size(); ++i) {
        if (i != 0) {
            result += "|";
        }
        result += inputs[i];
    }
    return result;
}
} // namespace Decompiler
