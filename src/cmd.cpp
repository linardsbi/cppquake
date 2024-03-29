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
// cmd.c -- Quake script command processing module

#include "quakedef.hpp"


void Cmd_ForwardToServer();

int trashtest;
int *trashspot;

qboolean cmd_wait;

//=============================================================================

/*
============
Cmd_Wait_f

Causes execution of the remainder of the command buffer to be delayed until
next frame.  This allows commands like:
bind g "impulse 5 ; +attack ; wait ; -attack ; impulse 2"
============
*/
void Cmd_Wait_f() {
    cmd_wait = true;
}

/*
=============================================================================

						COMMAND BUFFER

=============================================================================
*/

sizebuf_t cmd_text;

/*
============
Cbuf_Init
============
*/
void Cbuf_Init() {
    SZ_Alloc(&cmd_text, 32768);        // space for commands and script files
}


/*
============
Cbuf_AddText

Adds command text at the end of the buffer
============
*/
void Cbuf_AddText(std::string_view text) {
    if (cmd_text.cursize + text.length() >= cmd_text.maxsize) {
        Con_Printf("Cbuf_AddText: overflow\n");
        return;
    }

    SZ_Write(&cmd_text, text.data(), text.length());
}


/*
============
Cbuf_InsertText

Adds command text immediately after the current command
Adds a \n to the text
FIXME: actually change the command buffer to do less copying
============
*/
void Cbuf_InsertText(std::string_view text) {
    unsigned char *temp = nullptr;

// copy off any commands still remaining in the exec buffer
    const auto templen = cmd_text.cursize;
    if (templen > 0) {
        temp = zmalloc<decltype(temp)>(templen);
        memcpy(temp, cmd_text.data, templen);
        SZ_Clear(&cmd_text);
    } else
        temp = nullptr;    // shut up compiler

// add the entire text of the file
    Cbuf_AddText(text);

// add the copied off data
    if (templen > 0) {
        SZ_Write(&cmd_text, temp, templen);
        Z_Free(temp);
    }
}

/*
============
Cbuf_Execute
============
*/
void Cbuf_Execute() {
    int i = 0;
    char *text = nullptr;
    char line[1024];
    int quotes = 0;

    while (cmd_text.cursize) {
// find a \n or ; line break
        text = (char *) cmd_text.data;

        quotes = 0;
        for (i = 0; i < cmd_text.cursize; i++) {
            if (text[i] == '"')
                quotes++;
            if (!(quotes & 1) && text[i] == ';')
                break;    // don't break if inside a quoted string
            if (text[i] == '\n')
                break;
        }


        memcpy(line, text, i);
        line[i] = 0;

// delete the text from the command buffer and move remaining commands down
// this is necessary because commands (exec, alias) can insert data at the
// beginning of the text buffer

        if (i == cmd_text.cursize)
            cmd_text.cursize = 0;
        else {
            i++;
            cmd_text.cursize -= i;
            Q_memcpy(text, text + i, cmd_text.cursize);
        }

// execute the command line
        Cmd_ExecuteString(line, src_command);

        if (cmd_wait) {    // skip out while text still remains in buffer, leaving it
            // for next frame
            cmd_wait = false;
            break;
        }
    }
}

/*
==============================================================================

						SCRIPT COMMANDS

==============================================================================
*/

/*
===============
Cmd_StuffCmds_f

Adds command line parameters as script statements
Commands lead with a +, and continue until a - or another +
quake +prog jctest.qp +cmd amlev1
quake -nosound +cmd amlev1
===============
*/
void Cmd_StuffCmds_f() {
    char *text = nullptr, *build = nullptr, c = 0;

    if (Cmd_Argc() != 1) {
        Con_Printf("stuffcmds : execute command line parameters\n");
        return;
    }

// build the combined string to parse from
    int size = 0;
    for (auto i = 1; i < com_argc; i++) {
        if (!com_argv[i])
            continue;        // NEXTSTEP nulls out -NXHost
        size += Q_strlen(com_argv[i]) + 1;
    }
    if (!size)
        return;

    text = zmalloc<decltype(text)>(size + 1);
    text[0] = 0;
    for (auto i = 1; i < com_argc; i++) {
        if (!com_argv[i])
            continue;        // NEXTSTEP nulls out -NXHost
        strcat(text, com_argv[i]);
        if (i != com_argc - 1)
            strcat(text, " ");
    }

// pull out the commands
    build = zmalloc<decltype(build)>(size + 1);
    build[0] = 0;

    for (auto i = 0; i < size - 1; i++) {
        if (text[i] == '+') {
            i++;

            for (auto j = i; (text[j] != '+') && (text[j] != '-') && (text[j] != 0); j++) {
                c = text[j];
                text[j] = 0;

                strcat(build, text + i);
                strcat(build, "\n");
                text[j] = c;
                i = j - 1;
            }


        }
    }

    if (build[0])
        Cbuf_InsertText(build);

    Z_Free(text);
    Z_Free(build);
}


/*
===============
Cmd_Exec_f
===============
*/
void Cmd_Exec_f() {
    char *f = nullptr;
    int mark = 0;

    if (Cmd_Argc() != 2) {
        Con_Printf("exec <filename> : execute a script file\n");
        return;
    }

    mark = Hunk_LowMark();
    f = (char *) COM_LoadHunkFile(Cmd_Argv(1));
    if (!f) {
        Con_Printf("couldn't exec %s\n", Cmd_Argv(1));
        return;
    }
    Con_Printf("execing %s\n", Cmd_Argv(1));

    Cbuf_InsertText(f);
    Hunk_FreeToLowMark(mark);
}


/*
===============
Cmd_Echo_f

Just prints the rest of the line to the console
===============
*/
void Cmd_Echo_f() {
    int i = 0;

    for (i = 1; i < Cmd_Argc(); i++)
        Con_Printf("%s ", Cmd_Argv(i));
    Con_Printf("\n");
}

/*
===============
Cmd_Alias_f

Creates a new command that executes a command string (possibly ; seperated)
===============
*/

auto CopyString(char *in) -> char * {
    char *out = nullptr;

    out = zmalloc<decltype(out)>(strlen(in) + 1);
    strcpy(out, in);
    return out;
}

void Cmd_Alias_f() {
    if (Cmd_Argc() == 1) {
        Con_Printf("Current alias commands:\n");
        for (const auto &[name, value] : CMDAliases::get()) {
          Con_Printf("%s : %s\n", name, value);
        }
        return;
    }

    auto make_cmd_string = []() {
      std::string command;
      for (int i = 2, c = Cmd_Argc(); i < c; i++) {
        auto argval = Cmd_Argv(i);
        command += argval;
        if (i != c)
          command += ' ';
      }
      command += '\n';
      return command;
    };

    constexpr auto MAX_ALIAS_NAME = 512;
    std::string new_alias_name{Cmd_Argv(1)};

    if (new_alias_name.length() >= MAX_ALIAS_NAME) {
        Con_Printf("Alias name is too long\n");
        return;
    }

    auto &aliases = CMDAliases::get();

    aliases.insert_or_assign(new_alias_name, make_cmd_string());
}

/*
=============================================================================

					COMMAND EXECUTION

=============================================================================
*/

using cmd_function_t = struct cmd_function_s {
    struct cmd_function_s *next;
    const char *name;
    xcommand_t function;
};


#define    MAX_ARGS        80

static int cmd_argc;
static char *cmd_argv[MAX_ARGS];
static std::string_view cmd_args;

cmd_source_t cmd_source;

/*
============
Cmd_Init
============
*/
void Cmd_Init() {
//
// register our commands
//
    Cmd_AddCommand("stuffcmds", Cmd_StuffCmds_f);
    Cmd_AddCommand("exec", Cmd_Exec_f);
    Cmd_AddCommand("echo", Cmd_Echo_f);
    Cmd_AddCommand("alias", Cmd_Alias_f);
    Cmd_AddCommand("cmd", Cmd_ForwardToServer);
    Cmd_AddCommand("wait", Cmd_Wait_f);
}

/*
============
Cmd_Argc
============
*/
auto Cmd_Argc() -> int {
    return cmd_argc;
}

/*
============
Cmd_Argv
============
*/
auto Cmd_Argv(int arg) -> std::string_view {
    if ((unsigned) arg >= cmd_argc)
        return {};
    return cmd_argv[arg];
}

/*
============
Cmd_Args
============
*/
auto Cmd_Args() -> std::string_view {
    return cmd_args;
}


/*
============
Cmd_TokenizeString

Parses the given string into command line tokens.
============
*/
void Cmd_TokenizeString(std::string_view text) {
    int i = 0;

// clear the args from the last string
    for (i = 0; i < cmd_argc; i++)
        Z_Free(cmd_argv[i]);

    cmd_argc = 0;

    while (!text.empty()) {
        if (cmd_argc == 1) {
            cmd_args = text;
        }

        text = COM_Parse(text);

        if (*com_token == 0)
          return;

        if (cmd_argc < MAX_ARGS) {
            cmd_argv[cmd_argc] = zmalloc<char *>(strlen(com_token) + 1);
            std::strcpy(cmd_argv[cmd_argc], com_token);
            cmd_argc++;
        }
    }

}


/*
============
Cmd_AddCommand
============
*/
void Cmd_AddCommand(const char *cmd_name, xcommand_t function) {
    if (host_initialized)    // because hunk allocation would get stomped
        Sys_Error("Cmd_AddCommand after host_initialized");

// fail if the command is a variable name
    if (!Cvar_VariableString(cmd_name).empty()) {
        Con_Printf("Cmd_AddCommand: %s already defined as a var\n", cmd_name);
        return;
    }

    const auto [iter, inserted] = CMDFunctions::get().insert({cmd_name, function});

    if (!inserted) {
      Con_Printf("Cmd_AddCommand: %s already defined\n", cmd_name);
      return;
    }
}

/*
============
Cmd_Exists
============
*/
auto Cmd_Exists(std::string_view cmd_name) -> qboolean {
  return CMDFunctions::get().contains(std::string{cmd_name});
}


/*
============
Cmd_CompleteCommand
============
*/
auto Cmd_CompleteCommand(std::string_view partial) -> std::string_view {
    if (partial.empty())
        return {};

    auto starts_with = [partial](const auto &element) {
      return element.first.starts_with(partial);
    };

    const auto function = std::ranges::find_if(CMDFunctions::get(), starts_with);
    if (function != CMDFunctions::end()) {
      return function->first;
    }

    return {};
}

/*
============
Cmd_ExecuteString

A complete command line has been parsed, so try to execute it
FIXME: lookupnoadd the token to speed search?
============
*/
void Cmd_ExecuteString(std::string_view text, cmd_source_t src) {
    cmd_source = src;

    Cmd_TokenizeString(text);

// execute the command line
    if (Cmd_Argc() == 0)
        return;        // no tokens

// check functions
    if (auto function_it = CMDFunctions::find(cmd_argv[0])) {
      function_it.value()->second();
      return;
    }

// check alias
    // fixme: constructing a new string just to find an element is not good
    if (const auto alias = CMDAliases::find(cmd_argv[0])) {
      Cbuf_InsertText(alias.value()->second);
      return;
    }


// check cvars
    if (!Cvar_Command())
        Con_Printf("Unknown command \"%s\"\n", Cmd_Argv(0));

}


/*
===================
Cmd_ForwardToServer

Sends the entire command line over to the server
===================
*/
void Cmd_ForwardToServer() {
    if (cls.state != ca_connected) {
        Con_Printf("Can't \"%s\", not connected\n", Cmd_Argv(0));
        return;
    }

    if (cls.demoplayback)
        return;        // not really connected

    MSG_WriteByte(&cls.message, clc_stringcmd);
    if (Q_strcasecmp(Cmd_Argv(0), "cmd") != 0) {
        SZ_Print(&cls.message, Cmd_Argv(0));
        SZ_Print(&cls.message, " ");
    }
    if (Cmd_Argc() > 1)
        SZ_Print(&cls.message, Cmd_Args());
    else
        SZ_Print(&cls.message, "\n");
}


/*
================
Cmd_CheckParm

Returns the position (1 to argc-1) in the command's argument list
where the given parameter apears, or 0 if not present
================
*/

[[maybe_unused]] auto Cmd_CheckParm(char *parm) -> int {
    int i = 0;

    if (!parm)
        Sys_Error("Cmd_CheckParm: NULL");

    for (i = 1; i < Cmd_Argc(); i++)
        if (!Q_strcasecmp(parm, Cmd_Argv(i)))
            return i;

    return 0;
}
