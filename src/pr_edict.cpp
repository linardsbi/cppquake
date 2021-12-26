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
// sv_edict.c -- entity dictionary

#include <string>
#include <array>
#include <sstream>
#include "util.hpp"

dprograms_t *progs;
dfunction_t *pr_functions;
char *pr_strings;
ddef_t *pr_fielddefs;
ddef_t *pr_globaldefs;
dstatement_t *pr_statements;
globalvars_t *pr_global_struct;
float *pr_globals;            // same as pr_global_struct
int pr_edict_size;    // in bytes

std::map<unsigned, std::string> edictStrings;
std::vector<dfunction_t> edictFunctions;
std::size_t staticStringCount{};

unsigned short pr_crc;

//constexpr std::array<unsigned long, 8> type_size = {1,sizeof(string_t)/4,1,3,1,1,sizeof(func_t)/4,sizeof(void *)/4};

auto ED_FieldAtOfs(int ofs) -> ddef_t *;

auto ED_ParseEpair(void *base, ddef_t *key, std::string_view s) -> qboolean;

cvar_t nomonsters = {"nomonsters", "0"};
cvar_t gamecfg = {"gamecfg", "0"};
cvar_t scratch1 = {"scratch1", "0"};
cvar_t scratch2 = {"scratch2", "0"};
cvar_t scratch3 = {"scratch3", "0"};
cvar_t scratch4 = {"scratch4", "0"};
cvar_t savedgamecfg = {"savedgamecfg", "0", true};
cvar_t saved1 = {"saved1", "0", true};
cvar_t saved2 = {"saved2", "0", true};
cvar_t saved3 = {"saved3", "0", true};
cvar_t saved4 = {"saved4", "0", true};

#define    MAX_FIELD_LEN    64
#define GEFV_CACHESIZE    2

using gefv_cache = struct {
    ddef_t *pcache;
    char field[MAX_FIELD_LEN];
};

static gefv_cache gefvCache[GEFV_CACHESIZE] = {{nullptr, ""},
                                               {nullptr, ""}};

/*
=================
ED_ClearEdict

Sets everything to NULL
=================
*/
void ED_ClearEdict(edict_t *e) {
    memset(&e->v, 0, progs->entityfields * 4);
    e->free = false;
}

/*
=================
ED_Alloc

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
auto ED_Alloc() -> edict_t * {
    int i = 0;
    edict_t *e = nullptr;

    for (i = svs.maxclients + 1; i < sv.num_edicts; i++) {
        e = EDICT_NUM(i);
        // the first couple seconds of server time can involve a lot of
        // freeing and allocating, so relax the replacement policy
        if (e->free && (e->freetime < 2 || sv.time - e->freetime > 0.5)) {
            ED_ClearEdict(e);
            return e;
        }
    }

    if (i == MAX_EDICTS)
        Sys_Error("ED_Alloc: no free edicts");

    sv.num_edicts++;
    e = EDICT_NUM(i);
    ED_ClearEdict(e);

    return e;
}

/*
=================
ED_Free

Marks the edict as free
FIXME: walk all entities and NULL out references to this entity
=================
*/
void ED_Free(edict_t *ed) {
    SV_UnlinkEdict(ed);        // unlink from world bsp

    ed->free = true;
    ed->v.model = 0;
    ed->v.takedamage = 0;
    ed->v.modelindex = 0;
    ed->v.colormap = 0;
    ed->v.skin = 0;
    ed->v.frame = 0;
    VectorCopy (vec3_origin, ed->v.origin);
    VectorCopy (vec3_origin, ed->v.angles);
    ed->v.nextthink = -1;
    ed->v.solid = 0;

    ed->freetime = sv.time;
}

//===========================================================================

/*
============
ED_GlobalAtOfs
============
*/
auto ED_GlobalAtOfs(int ofs) -> ddef_t * {
    ddef_t *def = nullptr;
    int i = 0;

    for (i = 0; i < progs->numglobaldefs; i++) {
        def = &pr_globaldefs[i];
        if (def->ofs == ofs)
            return def;
    }
    return nullptr;
}

/*
============
ED_FieldAtOfs
============
*/
auto ED_FieldAtOfs(int ofs) -> ddef_t * {
    ddef_t *def = nullptr;
    int i = 0;

    for (i = 0; i < progs->numfielddefs; i++) {
        def = &pr_fielddefs[i];
        if (def->ofs == ofs)
            return def;
    }
    return nullptr;
}

/*
============
ED_FindField
============
*/
auto ED_FindField(std::string_view name) -> ddef_t * {
    for (auto i = 0; i < progs->numfielddefs; i++) {
        auto *def = &pr_fielddefs[i];
        if (getStringByOffset(def->s_name) == name)
            return def;
    }
    return nullptr;
}


/*
============
ED_FindGlobal
============
*/
auto ED_FindGlobal(std::string_view name) -> ddef_t * {
    for (auto i = 0; i < progs->numglobaldefs; i++) {
        auto def = &pr_globaldefs[i];
        if (getStringByOffset(def->s_name) == name)
            return def;
    }
    return nullptr;
}


auto GetEdictFieldValue(edict_t *ed, char *field) -> eval_t * {
    ddef_t *def = nullptr;
    int i = 0;
    static int rep = 0;

    for (i = 0; i < GEFV_CACHESIZE; i++) {
        if (!strcmp(field, gefvCache[i].field)) {
            def = gefvCache[i].pcache;
            goto Done;
        }
    }

    def = ED_FindField(field);

    if (strlen(field) < MAX_FIELD_LEN) {
        gefvCache[rep].pcache = def;
        strcpy(gefvCache[rep].field, field);
        rep ^= 1;
    }

    Done:
    if (!def)
        return nullptr;

    return (eval_t *) ((char *) &ed->v + def->ofs * 4);
}


/*
============
PR_ValueString

Returns a string describing *data in a type specific manner
=============
*/
auto PR_ValueString(etype_t type, eval_t *val) -> char * {
    static char line[256];
    ddef_t *def = nullptr;
    dfunction_t *f = nullptr;

    type = static_cast<etype_t>(type & ~DEF_SAVEGLOBAL);

    switch (type) {
        case ev_string:
            sprintf(line, "%s", getStringByOffset(val->string).data());
            break;
        case ev_entity:
            sprintf(line, "entity %i", NUM_FOR_EDICT(PROG_TO_EDICT(val->edict)));
            break;
        case ev_function:
            f = &edictFunctions[val->function];
            sprintf(line, "%s()", getStringByOffset(f->s_name).data());
            break;
        case ev_field:
            def = ED_FieldAtOfs(val->_int);
            sprintf(line, ".%s", getStringByOffset(def->s_name).data());
            break;
        case ev_void:
            sprintf(line, "void");
            break;
        case ev_float:
            sprintf(line, "%5.1f", val->_float);
            break;
        case ev_vector:
            sprintf(line, "'%5.1f %5.1f %5.1f'", val->vector[0], val->vector[1], val->vector[2]);
            break;
        case ev_pointer:
            sprintf(line, "pointer");
            break;
        default:
            sprintf(line, "bad type %i", type);
            break;
    }

    return line;
}

/*
============
PR_UglyValueString

Returns a string describing *data in a type specific manner
Easier to parse than PR_ValueString
=============
*/
auto PR_UglyValueString(etype_t type, eval_t *val) -> std::string {
    type = static_cast<etype_t>(type & ~DEF_SAVEGLOBAL);
    std::ostringstream line;
    switch (type) {
        case ev_string:
            line << getStringByOffset(val->string);
            break;
        case ev_entity:
            line << NUM_FOR_EDICT(PROG_TO_EDICT(val->edict));
            break;
        case ev_function:
            line << getStringByOffset(edictFunctions[val->function].s_name);
            break;
        case ev_field:
            line << getStringByOffset(ED_FieldAtOfs(val->_int)->s_name);
            break;
        case ev_void:
            line << "void";
            break;
        case ev_float:
            line << val->_float;
            break;
        case ev_vector:
            line << val->vector[0] << ' ' << val->vector[1] << ' ' << val->vector[2];
            break;
        default:
            line << "bad type " << static_cast<int>(type);
            break;
    }

    return line.str();
}

/*
============
PR_GlobalString

Returns a string with a description and the contents of a global,
padded to 20 field width
============
*/
auto PR_GlobalString(int ofs) -> char * {
    char *s = nullptr;
    int i = 0;
    ddef_t *def = nullptr;
    static char line[128];

    auto val = reinterpret_cast<eval_t *>(&pr_globals[ofs]);
    def = ED_GlobalAtOfs(ofs);
    if (!def)
        sprintf(line, "%i(?\?\?)", ofs);
    else {
        s = PR_ValueString(static_cast<etype_t>(def->type), val);
        sprintf(line, "%i(%s)%s", ofs, getStringByOffset(def->s_name).data(), s);
    }

    i = strlen(line);
    for (; i < 20; i++)
        strcat(line, " ");
    strcat(line, " ");

    return line;
}

auto PR_GlobalStringNoContents(int ofs) -> char * {
    int i = 0;
    ddef_t *def = nullptr;
    static char line[128];

    def = ED_GlobalAtOfs(ofs);
    if (!def)
        sprintf(line, "%i(?\?\?)", ofs);
    else
        sprintf(line, "%i(%s)", ofs, getStringByOffset(def->s_name).data());

    i = strlen(line);
    for (; i < 20; i++)
        strcat(line, " ");
    strcat(line, " ");

    return line;
}


/*
=============
ED_Print

For debugging
=============
*/
void ED_Print(edict_t *ed) {
    if (ed->free) {
        Con_Printf("FREE\n");
        return;
    }

    Con_Printf("\nEDICT %i:\n", NUM_FOR_EDICT(ed));
    for (auto i = 1; i < progs->numfielddefs; i++) {
        const ddef_t *d = &pr_fielddefs[i];
        std::string_view name = getStringByOffset(d->s_name);

        if (name[name.length() - 2] == '_')
            continue;    // skip _x, _y, _z vars

        const int *v = (int *) ((char *) &ed->v + d->ofs * 4);

        // if the value is still all 0, skip the field
        const int type = d->type & ~DEF_SAVEGLOBAL;

        int j = 0;

        while (j < type_size[type]) {
            if (v[j])
                break;
            j++;
        }

        if (j == type_size[type])
            continue;

        Con_Printf("%s", name);

        for (int l = name.length(); l++ < 15;)
            Con_Printf(" ");

        Con_Printf("%s\n", PR_ValueString(static_cast<etype_t>(d->type), (eval_t *) v));
    }
}

/*
=============
ED_Write

For savegames
=============
*/
void ED_Write(std::ofstream &f, edict_t *ed) {
    fmt::fprintf(f, "{\n");

    if (ed->free) {
        fmt::fprintf(f, "}\n");
        return;
    }

    for (int i = 1; i < progs->numfielddefs; i++) {
        const auto *d = &pr_fielddefs[i];
        auto name = getStringByOffset(d->s_name);
        if (name[name.length() - 2] == '_')
            continue;    // skip _x, _y, _z vars

        const auto *v = (int *) ((char *) &ed->v + d->ofs * 4);

        // if the value is still all 0, skip the field
        auto skip_field = [d, v]() {
          const auto type = d->type & ~DEF_SAVEGLOBAL;
          int j = 0;
          for (; j < type_size[type]; j++)
            if (v[j])
              break;
          return j == type_size[type];
        };

        if (skip_field()){
          continue;
        }

        fmt::fprintf(f, "\"%s\" ", name);
        fmt::fprintf(f, "\"%s\"\n", PR_UglyValueString(static_cast<etype_t>(d->type), (eval_t *) v));
    }

    fmt::fprintf(f, "}\n");
}

void ED_PrintNum(int ent) {
    ED_Print(EDICT_NUM(ent));
}

/*
=============
ED_PrintEdicts

For debugging, prints all the entities in the current server
=============
*/
void ED_PrintEdicts() {
    Con_Printf("%i entities\n", sv.num_edicts);
    for (int i = 0; i < sv.num_edicts; i++)
        ED_PrintNum(i);
}

/*
=============
ED_PrintEdict_f

For debugging, prints a single edicy
=============
*/
void ED_PrintEdict_f() {
    const auto i = Q_atoi(Cmd_Argv(1));
    if (i >= sv.num_edicts) {
        Con_Printf("Bad edict number\n");
        return;
    }
    ED_PrintNum(i);
}

/*
=============
ED_Count

For debugging
=============
*/
void ED_Count() {
    int i = 0;
    edict_t *ent = nullptr;
    int active = 0, models = 0, solid = 0, step = 0;

    active = models = solid = step = 0;
    for (i = 0; i < sv.num_edicts; i++) {
        ent = EDICT_NUM(i);
        if (ent->free)
            continue;
        active++;
        if (ent->v.solid)
            solid++;
        if (ent->v.model)
            models++;
        if (ent->v.movetype == MOVETYPE_STEP)
            step++;
    }

    Con_Printf("num_edicts:%3i\n", sv.num_edicts);
    Con_Printf("active    :%3i\n", active);
    Con_Printf("view      :%3i\n", models);
    Con_Printf("touch     :%3i\n", solid);
    Con_Printf("step      :%3i\n", step);

}

/*
==============================================================================

					ARCHIVING GLOBALS

==============================================================================
*/

/*
=============
ED_WriteGlobals
=============
*/
void ED_WriteGlobals(std::ofstream &f) {
    fmt::fprintf(f, "{\n");
    for (int i = 0; i < progs->numglobaldefs; i++) {
        const auto *def = &pr_globaldefs[i];
        if ((def->type & DEF_SAVEGLOBAL) == 0)
            continue;

        const auto type = static_cast<etype_t>(def->type & ~DEF_SAVEGLOBAL);

        if (type != ev_string
            && type != ev_float
            && type != ev_entity)
            continue;

        fmt::fprintf(f, "\"%s\" ", getStringByOffset(def->s_name));
        fmt::fprintf(f, "\"%s\"\n", PR_UglyValueString(type, reinterpret_cast<eval_t *>(&pr_globals[def->ofs])));
    }
    fmt::fprintf(f, "}\n");
}

/*
=============
ED_ParseGlobals
=============
*/
void ED_ParseGlobals(std::string_view data) {
    while (true) {
        // parse key
        data = COM_Parse(data);
        if (com_token[0] == '}')
            break;
        if (data.empty())
            Sys_Error("ED_ParseGlobals: EOF without closing brace when parsing key");

        std::string keyname{com_token}; // fixme: this should be a view into 'data' instead of a string

        // parse value
        data = COM_Parse(data);
        if (data.empty())
            Sys_Error("ED_ParseGlobals: EOF without closing brace when parsing value");

        if (com_token[0] == '}')
            Sys_Error("ED_ParseGlobals: closing brace without data");


        if (auto *key = ED_FindGlobal(keyname)) {
          if (!ED_ParseEpair((void *) pr_globals, key, com_token))
            Host_Error("ED_ParseGlobals: parse error");
        } else {
          Con_Printf("'%s' is not a global\n", keyname);
        }

    }
}

//============================================================================

/*
=============
ED_ParseEval

Can parse either fields or globals
returns false if error
=============
*/
auto ED_ParseEpair(void *base, ddef_t *key, std::string_view s) -> qboolean {
    auto *dest = (void *) ((int *) base + key->ofs);
    istringviewstream stream{s};
    switch (key->type & ~DEF_SAVEGLOBAL) {
        case ev_string: {
          // global fields that are empty (defined but not initialized), hold a value that we need to preserve
            if (s.empty()) {
              return true;
            }
            std::string str{s};
            *(string_t *) dest = static_cast<string_t>(newString(std::move(str)));
            break;
        }

        case ev_float:
            stream >> *((float *) dest);
            break;

        case ev_vector:
            for (int i = 0; i < 3; i++) {
                stream >> ((float *) dest)[i];
            }
            break;

        case ev_entity:
            *(int *) dest = EDICT_TO_PROG(EDICT_NUM(strtol(s.data(), nullptr, 10)));
            break;

        case ev_field:
        {
          const auto *def = ED_FindField(s);
          if (def == nullptr) {
            Con_Printf("Can't find field %s\n", s);
            return false;
          }
          *(int *) dest = G_INT(def->ofs);
          break;
        }
        case ev_function:
        {
          if (const auto functionDistance = getFunctionOffsetFromName(s)) {
            *(func_t *) dest = functionDistance;
            break;
          }

          Con_Printf("Can't find function %s\n", s);
          return false;
        }
        default:
            break;
    }
    return true;
}

/*
====================
ED_ParseEdict

Parses an edict out of the given string, returning the new position
ed should be a properly initialized empty edict.
Used for initial level load and for savegames.
====================
*/
auto ED_ParseEdict(std::string_view data, edict_t *ent) -> std::string_view {
    qboolean anglehack = false;
    qboolean init = false;

// clear it
    if (ent != sv.edicts)    // hack
        memset(&ent->v, 0, progs->entityfields * 4);

    auto trim_spaces = [](std::string &str) {
        auto n = str.length();
        while (n > 0 && str[n - 1] == ' ') {
            n--;
        }
        str.resize(n);
    };

// go through all the dictionary pairs
    while (true) {
        // parse key
        data = COM_Parse(data);
        if (com_token[0] == '}')
            break;
        if (data.empty())
            Sys_Error("ED_ParseEdict: EOF without closing brace");

        // anglehack is to allow QuakeEd to write single scalar angles
        // and allow them to be turned into vectors. (FIXME...)
        if (!strcmp(com_token, "angle")) {
            strcpy(com_token, "angles");
            anglehack = true;
        } else
            anglehack = false;

        // FIXME: change light to _light to get rid of this hack
        if (!strcmp(com_token, "light"))
            strcpy(com_token, "light_lev");    // hack for single light def

        std::string keyname = com_token;

        // another hack to fix heynames with trailing spaces
//        trim_spaces(keyname);

        // parse value
        data = COM_Parse(data);
        if (data.empty())
            Sys_Error("ED_ParseEdict: EOF without closing brace");

        if (com_token[0] == '}')
            Sys_Error("ED_ParseEdict: closing brace without data");

        init = true;

// keynames with a leading underscore are used for utility comments,
// and are immediately discarded by quake
//        if (keyname[0] == '_')
//            continue;

        auto key = ED_FindField(keyname);
        if (!key) {
            Con_Printf("Field '%s' does not exist\n", keyname);
            continue;
        }

        if (anglehack) {
            char temp[32]{};
            strcpy(temp, com_token);
            sprintf(com_token, "0 %s 0", temp);
        }

        if (!ED_ParseEpair((void *) &ent->v, key, com_token))
            Host_Error("ED_ParseEdict: parse error");
    }

    if (!init)
        ent->free = true;

    return data;
}


/*
================
ED_LoadFromFile

The entities are directly placed in the array, rather than allocated with
ED_Alloc, because otherwise an error loading the map would have entity
number references out of order.

Creates a server's entity / program execution context by
parsing textual entity definitions out of an ent file.

Used for both fresh maps and savegame loads.  A fresh map would also need
to call ED_CallSpawnFunctions () to let the objects initialize themselves.
================
*/
void ED_LoadFromFile(std::string_view data) {
    edict_t *ent = nullptr;
    int inhibit = 0;

    pr_global_struct->time = sv.time;

// parse ents
    while (true) {
// parse the opening brace	
        data = COM_Parse(data);
        if (data.empty())
            break;
        if (com_token[0] != '{')
            Sys_Error("ED_LoadFromFile: found %s when expecting {", com_token);

        if (!ent)
            ent = EDICT_NUM(0);
        else
            ent = ED_Alloc();

        data = ED_ParseEdict(data, ent);

// remove things from different skill levels or deathmatch
        if (deathmatch.value) {
            if (((int) ent->v.spawnflags & SPAWNFLAG_NOT_DEATHMATCH)) {
                ED_Free(ent);
                inhibit++;
                continue;
            }
        } else if ((current_skill == 0 && ((int) ent->v.spawnflags & SPAWNFLAG_NOT_EASY))
                   || (current_skill == 1 && ((int) ent->v.spawnflags & SPAWNFLAG_NOT_MEDIUM))
                   || (current_skill >= 2 && ((int) ent->v.spawnflags & SPAWNFLAG_NOT_HARD))) {
            ED_Free(ent);
            inhibit++;
            continue;
        }

//
// immediately call spawn function
//
        if (ent->v.classname == 0) {
            Con_Printf("No classname for:\n");
            ED_Print(ent);
            ED_Free(ent);
            continue;
        }

        // look for the spawn function
        auto funcOfs = getFunctionOffsetFromName(getStringByOffset(ent->v.classname)); // fixme

        if (funcOfs == 0) {
            Con_Printf("No spawn function for:\n");
            ED_Print(ent);
            ED_Free(ent);
            continue;
        }

        pr_global_struct->self = EDICT_TO_PROG(ent);
        PR_ExecuteProgram(funcOfs);
    }

    Con_DPrintf("%i entities inhibited\n", inhibit);
}


/*
===============
PR_LoadProgs
===============
*/
void PR_LoadProgs() {
    int i = 0;

// flush the non-C variable lookup cache
    for (i = 0; i < GEFV_CACHESIZE; i++)
        gefvCache[i].field[0] = 0;


    CRC_Init(&pr_crc);

    progs = (dprograms_t *) COM_LoadHunkFile("progs.dat");
    if (!progs)
        Sys_Error("PR_LoadProgs: couldn't load progs.dat");

    sysPrintf("Programs occupy %iK.\n", com_filesize / 1024);

    for (i = 0; i < com_filesize; i++)
        CRC_ProcessByte(&pr_crc, ((byte *) progs)[i]);

// byte swap the header
    for (i = 0; i < sizeof(*progs) / 4; i++)
        ((int *) progs)[i] = LittleLong(((int *) progs)[i]);

    if (progs->version != PROG_VERSION)
        Sys_Error("progs.dat has wrong version number (%i should be %i)", progs->version, PROG_VERSION);
    if (progs->crc != PROGHEADER_CRC)
        Sys_Error("progs.dat system vars have been modified, progdefs.h is out of date");

    pr_functions = (dfunction_t *) ((byte *) progs + progs->ofs_functions);
    pr_strings = (char *) progs + progs->ofs_strings;
    pr_globaldefs = (ddef_t *) ((byte *) progs + progs->ofs_globaldefs);
    pr_fielddefs = (ddef_t *) ((byte *) progs + progs->ofs_fielddefs);
    pr_statements = (dstatement_t *) ((byte *) progs + progs->ofs_statements);

    pr_global_struct = (globalvars_t * )((byte *) progs + progs->ofs_globals);
    pr_globals = (float *) pr_global_struct;

    pr_edict_size = progs->entityfields * 4 + sizeof(edict_t) - sizeof(entvars_t);

// byte swap the lumps
    for (i = 0; i < progs->numstatements; i++) {
        pr_statements[i].op = LittleShort(pr_statements[i].op);
        pr_statements[i].a = LittleShort(pr_statements[i].a);
        pr_statements[i].b = LittleShort(pr_statements[i].b);
        pr_statements[i].c = LittleShort(pr_statements[i].c);
    }
    edictFunctions.reserve(progs->numfunctions);
    for (i = 0; i < progs->numfunctions; i++) {
        edictFunctions.emplace_back(dfunction_t{
                                         .first_statement = LittleLong(pr_functions[i].first_statement),
                                         .parm_start = LittleLong(pr_functions[i].parm_start),
                                         .locals = LittleLong(pr_functions[i].locals),
                                         .profile = 0,
                                         .s_name = LittleLong(pr_functions[i].s_name),
                                         .s_file = LittleLong(pr_functions[i].s_file),
                                         .numparms = LittleLong(pr_functions[i].numparms),
                                 });
    }

    for (i = 0; i < progs->numglobaldefs; i++) {
        pr_globaldefs[i].type = LittleShort(pr_globaldefs[i].type);
        pr_globaldefs[i].ofs = LittleShort(pr_globaldefs[i].ofs);
        pr_globaldefs[i].s_name = LittleLong(pr_globaldefs[i].s_name);
    }

    for (i = 0; i < progs->numfielddefs; i++) {
        pr_fielddefs[i].type = LittleShort(pr_fielddefs[i].type);
        if (pr_fielddefs[i].type & DEF_SAVEGLOBAL)
            Sys_Error("PR_LoadProgs: pr_fielddefs[i].type & DEF_SAVEGLOBAL");
        pr_fielddefs[i].ofs = LittleShort(pr_fielddefs[i].ofs);
        pr_fielddefs[i].s_name = LittleLong(pr_fielddefs[i].s_name);
    }

    for (i = 0; i < progs->numglobals; i++)
        ((int *) pr_globals)[i] = LittleLong(((int *) pr_globals)[i]);

    std::string temp;
    for (auto j = 1; j < progs->numstrings; j++) {
        if (pr_strings[j] != '\0') {
            temp += pr_strings[j];
        } else {
            edictStrings[j - temp.length()] = std::move(temp);
            temp.clear();
        }
    }
}


/*
===============
PR_Init
===============
*/
void PR_Init() {
    Cmd_AddCommand("edict", ED_PrintEdict_f);
    Cmd_AddCommand("edicts", ED_PrintEdicts);
    Cmd_AddCommand("edictcount", ED_Count);
    Cmd_AddCommand("profile", PR_Profile_f);
    Cvar_RegisterVariable(&nomonsters);
    Cvar_RegisterVariable(&gamecfg);
    Cvar_RegisterVariable(&scratch1);
    Cvar_RegisterVariable(&scratch2);
    Cvar_RegisterVariable(&scratch3);
    Cvar_RegisterVariable(&scratch4);
    Cvar_RegisterVariable(&savedgamecfg);
    Cvar_RegisterVariable(&saved1);
    Cvar_RegisterVariable(&saved2);
    Cvar_RegisterVariable(&saved3);
    Cvar_RegisterVariable(&saved4);
}


auto EDICT_NUM(int n) -> edict_t * {
    if (n < 0 || n >= sv.max_edicts)
        Sys_Error("EDICT_NUM: bad number %i", n);
    return (edict_t *) ((byte *) sv.edicts + n * pr_edict_size);
}

auto NUM_FOR_EDICT(edict_t *e) -> int {
    auto b = (byte *) e - (byte *) sv.edicts;
    b = b / pr_edict_size;

    if (b < 0 || b >= sv.num_edicts)
        Sys_Error("NUM_FOR_EDICT: bad pointer");
    return b;
}


auto G_STRINGg(int ofs) -> const char * {
    // fixme in 64-bit this will try to access strings in function space
    return (pr_strings + *(string_t *) &pr_globals[ofs]);
}
