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
#pragma once
#include <map>
#include <optional>
#include <functional>
// cmd.h -- Command buffer and command execution

//===========================================================================
template <typename AliasType>
class CMDBase {
public:
  static AliasType& get() {
    static AliasType cmd_functions;
    return cmd_functions;
  }

  inline static std::optional<typename AliasType::iterator> find(const std::string &name) {
    const auto iter = get().find(name);
    if (iter != end()) {
      return iter;
    }
    return {};
  }

  inline static typename AliasType::const_iterator end() {
    return get().end();
  }

  CMDBase() = delete;
  ~CMDBase() = delete;

  CMDBase(const CMDBase&) = delete;
  CMDBase& operator=(const CMDBase&) = delete;
  CMDBase(CMDBase&&) = delete;
  CMDBase& operator=(CMDBase&&) = delete;
};

class CMDFunctions : public CMDBase<std::map<std::string, std::function<void()>>> {};

class CMDAliases : public CMDBase<std::map<std::string, std::string>> {};

/*

Any number of commands can be added in a frame, from several different sources.
Most commands come from either keybindings or console line input, but remote
servers can also send across commands and entire text files can be execed.

The + command line options are also added to the command buffer.

The game starts with a Cbuf_AddText ("exec quake.rc\n"); Cbuf_Execute ();

*/


void Cbuf_Init();
// allocates an initial text buffer that will grow as needed

void Cbuf_AddText(std::string_view text);
// as new commands are generated from the console or keybindings,
// the text is added to the end of the command buffer.

void Cbuf_InsertText(std::string_view text);
// when a command wants to issue other commands immediately, the text is
// inserted at the beginning of the buffer, before any remaining unexecuted
// commands.

void Cbuf_Execute();
// Pulls off \n terminated lines of text from the command buffer and sends
// them through Cmd_ExecuteString.  Stops when the buffer is empty.
// Normally called once per frame, but may be explicitly invoked.
// Do not call inside a command function!

//===========================================================================

/*

Command execution takes a null terminated string, breaks it into tokens,
then searches for a command or variable that matches the first token.

Commands can come from three sources, but the handler functions may choose
to dissallow the action or forward it to a remote server if the source is
not apropriate.

*/

typedef void (*xcommand_t)();

typedef enum {
    src_client,        // came in over a net connection as a clc_stringcmd
    // host_client will be valid during this state.
    src_command        // from the command buffer
} cmd_source_t;

extern cmd_source_t cmd_source;

void Cmd_Init();

void Cmd_AddCommand(const char *cmd_name, xcommand_t function);
// called by the init functions of other parts of the program to
// register commands and functions to call for them.
// The cmd_name is referenced later, so it should not be in temp memory

qboolean Cmd_Exists(std::string_view cmd_name);
// used by the cvar code to check for cvar / command name overlap

auto Cmd_CompleteCommand(std::string_view partial) -> std::string_view;
// attempts to match a partial command for automatic command line completion
// returns NULL if nothing fits

int Cmd_Argc();

std::string_view Cmd_Argv(int arg);

std::string_view Cmd_Args();
// The functions that execute commands get their parameters with these
// functions. Cmd_Argv () will return an empty string, not a NULL
// if arg > argc, so string operations are allways safe.

[[maybe_unused]] int Cmd_CheckParm(char *parm);
// Returns the position (1 to argc-1) in the command's argument list
// where the given parameter apears, or 0 if not present

void Cmd_TokenizeString(std::string_view text);
// Takes a null terminated string.  Does not need to be /n terminated.
// breaks the string up into arg tokens.

void Cmd_ExecuteString(std::string_view text, cmd_source_t src);
// Parses a single line of text into arguments and tries to execute it.
// The text can come from the command buffer, a remote client, or stdin.

void Cmd_ForwardToServer();
// adds the current command line as a clc_stringcmd to the client message.
// things like godmode, noclip, etc, are commands directed to the server,
// so when they are typed in at the console, they will need to be forwarded.

void Cmd_Print(char *text);
// used by command functions to send output to either the graphics console or
// passed as a print message to the client

