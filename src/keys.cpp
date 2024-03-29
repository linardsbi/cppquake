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
#include "quakedef.hpp"
#include <string>
#include <ranges>
/*

key up events are sent even if in console mode

*/


#define        MAXCMDLINE    256
char key_lines[32][MAXCMDLINE];
unsigned key_linepos;
int shift_down = false;
int key_lastpress;

int edit_line = 0;
int history_line = 0;

keydest_t key_dest;

int key_count;            // incremented every key event

keybind_map_t keybindings;

qboolean consolekeys[256];    // if true, can't be rebound while in console
qboolean menubound[256];    // if true, can't be rebound while in menu
int keyshift[256];        // key to map to if shift held down in console
int key_repeats[256];    // if > 1, it is autorepeating
qboolean keydown[256];

using keyname_t = std::map<std::string_view, std::uint16_t>;

static keyname_t keynames =
        {
                {"<UNKNOWN KEYNUM>",0},
                {"TAB",        K_TAB},
                {"ENTER",      K_ENTER},
                {"ESCAPE",     K_ESCAPE},
                {"SPACE",      K_SPACE},
                {"BACKSPACE",  K_BACKSPACE},
                {"UPARROW",    K_UPARROW},
                {"DOWNARROW",  K_DOWNARROW},
                {"LEFTARROW",  K_LEFTARROW},
                {"RIGHTARROW", K_RIGHTARROW},

                {"ALT",        K_ALT},
                {"CTRL",       K_CTRL},
                {"SHIFT",      K_SHIFT},

                {"F1",         K_F1},
                {"F2",         K_F2},
                {"F3",         K_F3},
                {"F4",         K_F4},
                {"F5",         K_F5},
                {"F6",         K_F6},
                {"F7",         K_F7},
                {"F8",         K_F8},
                {"F9",         K_F9},
                {"F10",        K_F10},
                {"F11",        K_F11},
                {"F12",        K_F12},

                {"INS",        K_INS},
                {"DEL",        K_DEL},
                {"PGDN",       K_PGDN},
                {"PGUP",       K_PGUP},
                {"HOME",       K_HOME},
                {"END",        K_END},

                {"MOUSE1",     K_MOUSE1},
                {"MOUSE2",     K_MOUSE2},
                {"MOUSE3",     K_MOUSE3},

                {"JOY1",       K_JOY1},
                {"JOY2",       K_JOY2},
                {"JOY3",       K_JOY3},
                {"JOY4",       K_JOY4},

                {"AUX1",       K_AUX1},
                {"AUX2",       K_AUX2},
                {"AUX3",       K_AUX3},
                {"AUX4",       K_AUX4},
                {"AUX5",       K_AUX5},
                {"AUX6",       K_AUX6},
                {"AUX7",       K_AUX7},
                {"AUX8",       K_AUX8},
                {"AUX9",       K_AUX9},
                {"AUX10",      K_AUX10},
                {"AUX11",      K_AUX11},
                {"AUX12",      K_AUX12},
                {"AUX13",      K_AUX13},
                {"AUX14",      K_AUX14},
                {"AUX15",      K_AUX15},
                {"AUX16",      K_AUX16},
                {"AUX17",      K_AUX17},
                {"AUX18",      K_AUX18},
                {"AUX19",      K_AUX19},
                {"AUX20",      K_AUX20},
                {"AUX21",      K_AUX21},
                {"AUX22",      K_AUX22},
                {"AUX23",      K_AUX23},
                {"AUX24",      K_AUX24},
                {"AUX25",      K_AUX25},
                {"AUX26",      K_AUX26},
                {"AUX27",      K_AUX27},
                {"AUX28",      K_AUX28},
                {"AUX29",      K_AUX29},
                {"AUX30",      K_AUX30},
                {"AUX31",      K_AUX31},
                {"AUX32",      K_AUX32},

                {"PAUSE",      K_PAUSE},

                {"MWHEELUP",   K_MWHEELUP},
                {"MWHEELDOWN", K_MWHEELDOWN},

                {"SEMICOLON", ';'},    // because a raw semicolon seperates commands

        };

/*
==============================================================================

			LINE TYPING INTO THE CONSOLE

==============================================================================
*/


/*
====================
Key_Console

Interactive line editing and console scrollback
====================
*/
void Key_Console(int key) {

    if (key == K_ENTER) {
        Cbuf_AddText(key_lines[edit_line] + 1);    // skip the >
        Cbuf_AddText("\n");
        Con_Printf("%s\n", key_lines[edit_line]);
        edit_line = (edit_line + 1) & 31;
        history_line = edit_line;
        key_lines[edit_line][0] = ']';
        key_linepos = 1;
        if (cls.state == ca_disconnected)
            SCR_UpdateScreen();    // force an update, because the command
        // may take some time
        return;
    }

    if (key == K_TAB) {    // command completion
        std::string_view cmd = Cmd_CompleteCommand(key_lines[edit_line] + 1);
        if (cmd.empty())
            cmd = Cvar_CompleteVariable(key_lines[edit_line] + 1);

        if (!cmd.empty()) {
            strncpy(key_lines[edit_line] + 1, cmd.data(), cmd.length());
            key_linepos = cmd.length() + 1;
            key_lines[edit_line][key_linepos] = ' ';
            key_linepos++;
            key_lines[edit_line][key_linepos] = 0;
        }

        return;
    }

    if (key == K_BACKSPACE || key == K_LEFTARROW) {
        if (key_linepos > 1)
            key_linepos--;
        return;
    }

    if (key == K_UPARROW) {
        do {
            history_line = (history_line - 1) & 31;
        } while (history_line != edit_line
                 && !key_lines[history_line][1]);
        if (history_line == edit_line)
            history_line = (edit_line + 1) & 31;
        Q_strcpy(key_lines[edit_line], key_lines[history_line]);
        key_linepos = Q_strlen(key_lines[edit_line]);
        return;
    }

    if (key == K_DOWNARROW) {
        if (history_line == edit_line) return;
        do {
            history_line = (history_line + 1) & 31;
        } while (history_line != edit_line
                 && !key_lines[history_line][1]);
        if (history_line == edit_line) {
            key_lines[edit_line][0] = ']';
            key_linepos = 1;
        } else {
            Q_strcpy(key_lines[edit_line], key_lines[history_line]);
            key_linepos = Q_strlen(key_lines[edit_line]);
        }
        return;
    }

    if (key == K_PGUP || key == K_MWHEELUP) {
        con_backscroll += 2;
        if (con_backscroll > con_totallines - (vid.height >> 3) - 1)
            con_backscroll = con_totallines - (vid.height >> 3) - 1;
        return;
    }

    if (key == K_PGDN || key == K_MWHEELDOWN) {
        con_backscroll -= 2;
        if (con_backscroll < 0)
            con_backscroll = 0;
        return;
    }

    if (key == K_HOME) {
        con_backscroll = con_totallines - (vid.height >> 3) - 1;
        return;
    }

    if (key == K_END) {
        con_backscroll = 0;
        return;
    }

    if (key < 32 || key > 127)
        return;    // non printable

    if (key_linepos < MAXCMDLINE - 1) {
        key_lines[edit_line][key_linepos] = key;
        key_linepos++;
        key_lines[edit_line][key_linepos] = 0;
    }

}

//============================================================================

char chat_buffer[32];
qboolean team_message = false;

void Key_Message(int key) {
    static int chat_bufferlen = 0;

    if (key == K_ENTER) {
        if (team_message)
            Cbuf_AddText("say_team \"");
        else
            Cbuf_AddText("say \"");
        Cbuf_AddText(chat_buffer);
        Cbuf_AddText("\"\n");

        key_dest = key_game;
        chat_bufferlen = 0;
        chat_buffer[0] = 0;
        return;
    }

    if (key == K_ESCAPE) {
        key_dest = key_game;
        chat_bufferlen = 0;
        chat_buffer[0] = 0;
        return;
    }

    if (key < 32 || key > 127)
        return;    // non printable

    if (key == K_BACKSPACE) {
        if (chat_bufferlen) {
            chat_bufferlen--;
            chat_buffer[chat_bufferlen] = 0;
        }
        return;
    }

    if (chat_bufferlen == 31)
        return; // all full

    chat_buffer[chat_bufferlen++] = key;
    chat_buffer[chat_bufferlen] = 0;
}

//============================================================================


/*
===================
Key_StringToKeynum

Returns a key number to be used to index keybindings[] by looking at
the given string.  Single ascii characters return themselves, while
the K_* names are matched up.
===================
*/
auto Key_StringToKeynum(std::string_view str) -> int {
    if (str.length() < 2)
        return str[0];

    if (keynames.contains(str)) {
        return keynames[str];
    }

    return -1;
}

/*
===================
Key_KeynumToString

Returns a string (either a single ascii char, or a K_* name) for the
given keynum.
FIXME: handle quote special (general escape sequence?)
===================
*/
auto Key_KeynumToString(int keynum) -> std::string_view {
    static char tinystr[2];

    if (keynum == -1)
        return "<KEY NOT FOUND>";
    if (keynum > ' ' && keynum < 127) {    // printable ascii
        tinystr[0] = keynum;
        tinystr[1] = 0;
        return tinystr;
    }

    const auto keynames_iter = std::find_if(keynames.begin(), keynames.end(),[keynum](const auto& pair) {
        return pair.second == keynum;
    });

    if (keynames_iter != keynames.end()) {
        return keynames_iter->first;
    }

    return "<UNKNOWN KEYNUM>";
}


/*
===================
Key_SetBinding
===================
*/

/*
===================
Key_Unbind_f
===================
*/
void Key_Unbind_f() {
    if (Cmd_Argc() != 2) {
        Con_Printf("%s", "unbind <key> : remove commands from a key\n");
        return;
    }

    const auto b = Key_StringToKeynum(Cmd_Argv(1));

    if (b == -1) {
        Con_Printf("\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }

    keybindings[b].clear();
}

void Key_Unbindall_f() {
    keybindings.clear();
}


/*
===================
Key_Bind_f
===================
*/
void Key_Bind_f() {
    const auto argc = Cmd_Argc();

    if (argc != 2 && argc != 3) {
        Con_Printf("%s", "bind <key> [command] : attach a command to a key\n");
        return;
    }

    const auto key_num = Key_StringToKeynum(Cmd_Argv(1));

    if (key_num == -1) {
        Con_Printf("\"%s\" isn't a valid key\n", Cmd_Argv(1));
        return;
    }

    if (argc == 2) {
        if (keybindings.contains(key_num))
            Con_Printf("\"%s\" = \"%s\"\n", Cmd_Argv(1), keybindings[key_num]);
        else
            Con_Printf("\"%s\" is not bound\n", Cmd_Argv(1));
        return;
    }

// copy the rest of the command line
    std::string cmd;
    for (int i = 2; i < argc; i++) {
        if (i > 2)
            cmd += ' ';
        cmd += Cmd_Argv(i);
    }

    keybindings[key_num] = std::move(cmd);
}

/*
============
Key_WriteBindings

Writes lines containing "bind key value"
============
*/
void Key_WriteBindings(FILE *f) {
    for (const auto& [i, key] : keybindings) {
        fmt::fprintf(f, "bind \"%s\" \"%s\"\n", Key_KeynumToString(i), key);
    }
}


/*
===================
Key_Init
===================
*/
void Key_Init() {
    int i = 0;

    for (i = 0; i < 32; i++) {
        key_lines[i][0] = ']';
        key_lines[i][1] = 0;
    }
    key_linepos = 1;

//
// init ascii characters in console mode
//
    for (i = 32; i < 128; i++)
        consolekeys[i] = true;
    consolekeys[K_ENTER] = true;
    consolekeys[K_TAB] = true;
    consolekeys[K_LEFTARROW] = true;
    consolekeys[K_RIGHTARROW] = true;
    consolekeys[K_UPARROW] = true;
    consolekeys[K_DOWNARROW] = true;
    consolekeys[K_BACKSPACE] = true;
    consolekeys[K_PGUP] = true;
    consolekeys[K_PGDN] = true;
    consolekeys[K_SHIFT] = true;
    consolekeys[K_MWHEELUP] = true;
    consolekeys[K_MWHEELDOWN] = true;
    consolekeys['`'] = false;
    consolekeys['~'] = false;

    for (i = 0; i < 256; i++)
        keyshift[i] = i;
    for (i = 'a'; i <= 'z'; i++)
        keyshift[i] = i - 'a' + 'A';
    keyshift['1'] = '!';
    keyshift['2'] = '@';
    keyshift['3'] = '#';
    keyshift['4'] = '$';
    keyshift['5'] = '%';
    keyshift['6'] = '^';
    keyshift['7'] = '&';
    keyshift['8'] = '*';
    keyshift['9'] = '(';
    keyshift['0'] = ')';
    keyshift['-'] = '_';
    keyshift['='] = '+';
    keyshift[','] = '<';
    keyshift['.'] = '>';
    keyshift['/'] = '?';
    keyshift[';'] = ':';
    keyshift['\''] = '"';
    keyshift['['] = '{';
    keyshift[']'] = '}';
    keyshift['`'] = '~';
    keyshift['\\'] = '|';

    menubound[K_ESCAPE] = true;
    for (i = 0; i < 12; i++)
        menubound[K_F1 + i] = true;

//
// register our functions
//
    Cmd_AddCommand("bind", Key_Bind_f);
    Cmd_AddCommand("unbind", Key_Unbind_f);
    Cmd_AddCommand("unbindall", Key_Unbindall_f);


}

/*
===================
Key_Event

Called by the system between frames for both key up and key down events
Should NOT be called during an interrupt!
===================
*/
void Key_Event(int key, qboolean down) {

    keydown[key] = down;

    if (!down)
        key_repeats[key] = 0;

    key_lastpress = key;
    key_count++;
    if (key_count <= 0) {
        return;        // just catching keys for Con_NotifyBox
    }

// update auto-repeat status
    if (down) {
        key_repeats[key]++;
        if (key != K_BACKSPACE && key != K_PAUSE && key_repeats[key] > 1) {
            return;    // ignore most autorepeats
        }

        if (key >= 200 && keybindings[key].empty())
            Con_Printf("%s is unbound, hit F4 to set.\n", Key_KeynumToString(key));
    }

    if (key == K_SHIFT)
        shift_down = down;

//
// handle escape specialy, so the user can never unbind it
//
    if (key == K_ESCAPE) {
        if (!down)
            return;
        switch (key_dest) {
            case key_message:
                Key_Message(key);
                break;
            case key_menu:
                M_Keydown(key);
                break;
            case key_game:
            case key_console:
                M_ToggleMenu_f();
                break;
            default:
                Sys_Error("Bad key_dest");
        }
        return;
    }

//
// key up events only generate commands if the game key binding is
// a button command (leading + sign).  These will occur even in console mode,
// to keep the character from continuing an action started before a console
// switch.  Button commands include the kenum as a parameter, so multiple
// downs can be matched with ups
//
    if (!down) {
        std::string_view kb = keybindings[key];
        if (kb.starts_with('+')) {
            const auto cmd = fmt::sprintf("-%s %i\n", kb.substr(1), key);
            Cbuf_AddText(cmd);
        }
        if (keyshift[key] != key) {
            kb = keybindings[keyshift[key]];
            if (kb.starts_with('+')) {
                const auto cmd = fmt::sprintf("-%s %i\n", kb.substr(1), key);
                Cbuf_AddText(cmd);
            }
        }
        return;
    }

//
// during demo playback, most keys bring up the main menu
//
    if (cls.demoplayback && down && consolekeys[key] && key_dest == key_game) {
        M_ToggleMenu_f();
        return;
    }

//
// if not a consolekey, send to the interpreter no matter what mode is
//
    if ((key_dest == key_menu && menubound[key])
        || (key_dest == key_console && !consolekeys[key])
        || (key_dest == key_game && (!con_forcedup || !consolekeys[key]))) {
        if (keybindings.contains(key)) {
            std::string_view kb = keybindings[key];
            if (kb.starts_with('+')) {    // button commands add keynum as a parm
                const auto cmd = fmt::sprintf("%s %i\n", kb, key);
                Cbuf_AddText(cmd);
            } else {
                Cbuf_AddText(kb);
                Cbuf_AddText("\n");
            }
        }
        return;
    }

    if (!down)
        return;        // other systems only care about key down events

    if (shift_down) {
        key = keyshift[key];
    }

    switch (key_dest) {
        case key_message:
            Key_Message(key);
            break;
        case key_menu:
            M_Keydown(key);
            break;

        case key_game:
        case key_console:
            Key_Console(key);
            break;
        default:
            Sys_Error("Bad key_dest");
    }
}


/*
===================
Key_ClearStates
===================
*/
void Key_ClearStates() {
    int i = 0;

    for (i = 0; i < 256; i++) {
        keydown[i] = false;
        key_repeats[i] = 0;
    }
}

