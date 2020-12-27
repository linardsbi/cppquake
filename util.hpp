//
// Created by hvssz on 27.12.20.
//

#ifndef SDLQUAKE_1_0_9_UTIL_HPP
#define SDLQUAKE_1_0_9_UTIL_HPP

#include <string_view>
#include "quakedef.hpp"

// TEMPORARY HACKY STUFF
// Why did I not just convert everything into a map?

auto getGlobalString(unsigned long offset) -> std::string_view;
auto getEdictString(unsigned long offset, edict_s* edict) -> std::string_view;
auto getGlobalStringOffsetPair(unsigned long offset) -> NameOffsetPair;

auto getStringByOffset(unsigned long offset) -> std::string_view;
auto getOffsetByString(std::string_view) -> unsigned long;

auto findFunctionByName(std::string_view name);
auto findFunctionByNameOffset(unsigned long offset);

auto findStringByOffset(unsigned long offset);
auto findStringByName(std::string_view str);

auto getFunctionByName(std::string_view name) -> dfunction_t;
auto getFunctionByNameOffset(unsigned long offset) -> dfunction_t;

auto getFunctionOffsetFromName(std::string_view name) -> unsigned long;

auto stringExistsAtOffset(unsigned long offset) -> bool;

#endif //SDLQUAKE_1_0_9_UTIL_HPP
