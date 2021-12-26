//
// Created by hvssz on 27.12.20.
//

#include "util.hpp"

auto findStringByName(std::string_view str) {
    return std::find_if(edictStrings.begin(), edictStrings.end(), [str](const auto &eString) {
        return eString.second == str;
    });
}

auto findStringByOffset(const unsigned long offset) {
    return edictStrings.find(offset);
}

auto stringExistsAtOffset(unsigned long offset) -> bool {
    return findStringByOffset(offset) != edictStrings.end();
}

auto getStringByOffset(const unsigned long offset) -> std::string_view {
    auto stringIt = findStringByOffset(offset);
    if (stringIt != edictStrings.end()) {
        return stringIt->second;
    }
    return {};
}

auto getOffsetByString(std::string_view name) -> unsigned long {
    auto stringIt = findStringByName(name);
    if (stringIt != edictStrings.end()) {
        return stringIt->first;
    }
    return 0;
}

auto getGlobalString(unsigned long offset) -> std::string_view {
    auto str = getStringByOffset(*(string_t *) &pr_globals[offset]);
    if (!str.empty()) {
        return str;
    }
    return {};
}

auto getEdictString(unsigned long offset, edict_s *edict) -> std::string_view {
    return getStringByOffset(*(string_t *) &((float *) &edict->v)[offset]);
}

auto getGlobalStringOffsetPair(unsigned long offset) -> std::pair<unsigned, std::string> {
    return *findStringByOffset(*(string_t *) &pr_globals[offset]);
}

auto findFunctionByName(std::string_view name) {
    return std::find_if(edictFunctions.begin(), edictFunctions.end(), [&name](const auto &item) {
        return getStringByOffset(item.s_name) == name;
    });
}

auto getFunctionOffsetFromName(std::string_view name) -> unsigned long {
    return static_cast<unsigned long>(std::distance(edictFunctions.begin(), findFunctionByName(name)));
}

/*
=============
NewString
=============
*/

void fixNewLines(std::string& newstring) {
    for (std::size_t i = 0, l = newstring.length(); i < l; i++) {
        if (newstring[i] == '\\' && i < l - 1) {
            i++;
            if (newstring[i] == 'n') {
                newstring[i - 1] = ' ';
                newstring[i] = '\n';
            } else {
                newstring[i] = '\\';
            }
        }
    }
}

auto newString(std::string string) -> unsigned long {
    const auto lastIt = --edictStrings.end();
    const auto offset = lastIt->first + lastIt->second.length() + 1;
    fixNewLines(string);
    edictStrings[offset] = std::move(string);
    return offset;
}