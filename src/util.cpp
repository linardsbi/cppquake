//
// Created by hvssz on 27.12.20.
//

#include "util.hpp"
#include <limits>

// TEMPORARY HACKY STUFF


auto findStringByName(std::string_view str) {
    return std::find_if(edictStrings.begin(), edictStrings.end(), [str](const auto &eString) {
        return eString.name == str;
    });
}

auto findStringByOffset(const unsigned long offset) {
    return std::find_if(edictStrings.begin(), edictStrings.end(), [offset](const auto &eString) {
        return eString.offset == offset;
    });
}

auto stringExistsAtOffset(unsigned long offset) -> bool {
    return findStringByOffset(offset) != edictStrings.end();
}

auto getStringByOffset(const unsigned long offset) -> std::string_view {

    auto stringIt = findStringByOffset(offset);
    if (stringIt != edictStrings.end()) {
        return stringIt->name;
    }
    return {};

}

auto getOffsetByString(std::string_view name) -> unsigned long {
    auto stringIt = findStringByName(name);
    if (stringIt != edictStrings.end()) {
        return stringIt->offset;
    } else {
        return 0;
    }
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

auto getGlobalStringOffsetPair(unsigned long offset) -> NameOffsetPair {
    return *findStringByOffset(*(string_t *) &pr_globals[offset]);
}

auto findFunctionByName(std::string_view name) {
    return std::find_if(edictFunctions.begin(), edictFunctions.end(), [&name](const auto &item) {
        return getStringByOffset(item.s_name) == name;
    });
}

auto findFunctionByNameOffset(unsigned long offset) {
    auto stringIt = findStringByOffset(offset);
    return findFunctionByName(stringIt->name);
}

auto getFunctionByName(std::string_view name) -> dfunction_t {
    return *findFunctionByName(name);
}

auto getFunctionByNameOffset(unsigned long offset) -> dfunction_t {
    return *findFunctionByNameOffset(offset);
}

auto getFunctionOffsetFromName(std::string_view name) -> unsigned long {
    return std::distance(edictFunctions.begin(), findFunctionByName(name));
}

/*
=============
NewString
=============
*/

auto fixNewLines(std::string_view string) {
    std::string newstring = string.data();
    for (std::size_t i = 0, l = newstring.length(); i < l; i++) {
        if (string[i] == '\\' && i < l - 1) {
            i++;
            if (string[i] == 'n') {
                newstring[i - 1] = ' ';
                newstring[i] = '\n';
            } else {
                newstring[i] = '\\';
            }
        }
    }
    return newstring;
}

// going to have to clear all strings that were allocated after level change
auto newString(std::string_view string) -> unsigned long {
    auto lastIt = --edictStrings.end();
    auto offset = lastIt->offset + lastIt->name.length() + 1; // I might not be adding something..
    edictStrings.emplace_back(fixNewLines(string), offset);
    return offset;
}

inline auto toString(std::string_view v) -> std::string {
    return {v.data(), v.size()};
}