/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// cvar.c -- dynamic variable tracking

#include "quakedef.hpp"

cvar_t *cvar_vars;
constexpr std::string_view cvar_null_string = "";

/*
============
Cvar_FindVar
============
*/
cvar_t *Cvar_FindVar(std::string_view var_name) {
    for (auto var = cvar_vars; var; var = var->next)
        if (var_name == var->name)
            return var;

    return nullptr;
}

/*
============
Cvar_VariableValue
============
*/
float Cvar_VariableValue(std::string_view var_name) {
    cvar_t *var = Cvar_FindVar(var_name);
    if (!var)
        return 0;
    return std::atof(var->string.c_str());
}


/*
============
Cvar_VariableString
============
*/
auto Cvar_VariableString(std::string_view var_name) -> std::string_view {
    cvar_t *var = Cvar_FindVar(var_name);
    if (!var)
        return cvar_null_string;
    return var->string;
}


/*
============
Cvar_CompleteVariable
============
*/
auto Cvar_CompleteVariable(std::string_view partial) -> std::string_view {
    if (partial.empty())
        return cvar_null_string;

// check functions
    for (auto cvar = cvar_vars; cvar; cvar = cvar->next)
        if (!Q_strncmp(partial, cvar->name, partial.length()))
            return cvar->name;

    return cvar_null_string;
}


/*
============
Cvar_Set
============
*/
// see if this will work with string_view parameters
void Cvar_Set(std::string_view var_name, std::string_view value) {
    auto var = Cvar_FindVar(var_name);
    if (!var) {    // there is an error in C code if this happens
        Con_Printf("Cvar_Set: variable %s not found\n", var_name);
        return;
    }

    auto changed = var->string != value;

    var->string = value;
    var->value = Q_atof(var->string.c_str());

    if (var->server && changed) {
        if (sv.active)
            SV_BroadcastPrintf("\"%s\" changed to \"%s\"\n", var->name.data(), var->string.data());
    }
}

/*
============
Cvar_SetValue
============
*/
void Cvar_SetValue(std::string_view var_name, float value) {
    char val[32];

    sprintf(val, "%f", value);
    Cvar_Set(var_name, val);
}


/*
============
Cvar_RegisterVariable

Adds a freestanding variable to the variable list.
============
*/
void Cvar_RegisterVariable(cvar_t *variable) {

// first check to see if it has allready been defined
    if (Cvar_FindVar(variable->name)) {
        Con_Printf("Can't register variable %s, allready defined\n", variable->name);
        return;
    }

// check for overlap with a command
    if (Cmd_Exists(variable->name)) {
        Con_Printf("Cvar_RegisterVariable: %s is a command\n", variable->name.c_str());
        return;
    }

    variable->value = Q_atof(variable->string.c_str());

// link the variable in
    variable->next = cvar_vars;
    cvar_vars = variable;
}

/*
============
Cvar_Command

Handles variable inspection and changing from the console
============
*/
qboolean Cvar_Command() {
// check variables
    auto v = Cvar_FindVar(Cmd_Argv(0));
    if (!v)
        return false;

// perform a variable print or set
    if (Cmd_Argc() == 1) {
        Con_Printf("\"%s\" is \"%s\"\n", v->name, v->string);
        return true;
    }

    Cvar_Set(v->name, Cmd_Argv(1));
    return true;
}


/*
============
Cvar_WriteVariables

Writes lines containing "set variable value" for all variables
with the archive flag set to true.
============
*/
void Cvar_WriteVariables(FILE *f) {
    for (auto var = cvar_vars; var; var = var->next)
        if (var->archive)
            fprintf(f, "%s \"%s\"\n", var->name.c_str(), var->string.c_str());
}

