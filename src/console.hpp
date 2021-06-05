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
#include <fstream>
#include "client.hpp"
#include "screen.hpp"

extern qboolean con_debuglog;
extern client_static_t cls;
extern qboolean scr_disabled_for_loading;
//
// console
//
extern int con_totallines;
extern int con_backscroll;
extern qboolean con_forcedup;    // because no entities to refresh
extern qboolean con_initialized;
extern byte *con_chars;
extern int con_notifylines;        // scan lines to clear for notify lines

void Con_DrawCharacter(int cx, int line, int num);

void Con_CheckResize(void);

void Con_Init(void);

void Con_DrawConsole(int lines, qboolean drawinput);

void Con_Print(const char *txt);

template <typename ...Args>
void Con_Printf(const char* fmt, Args &&... args) {
    static qboolean inupdate;
    std::string msg = fmt::sprintf(fmt, std::forward<Args>(args)...);

// also echo to debugging console
    sysPrintf("{}", msg);    // also echo to debugging console

// log all messages to file
    if (con_debuglog) {
        auto filename = fmt::sprintf("%s/qconsole.log", com_gamedir);
        auto file = std::ofstream(filename, std::fstream::app);
        file << msg;
    }

    if (!con_initialized)
        return;

    if (cls.state == ca_dedicated)
        return;        // no graphics mode

// write it to the scrollable buffer
    Con_Print(msg.c_str());

// update the screen if the console is displayed
    if (cls.signon != SIGNONS && !scr_disabled_for_loading) {
        // protect against infinite loop if something in SCR_UpdateScreen calls
        // Con_Printd
        if (!inupdate) {
            inupdate = true;
            SCR_UpdateScreen();
            inupdate = false;
        }
    }
}

void Con_DPrintf(const char *fmt, ...);

void Con_SafePrintf(char *fmt, ...);

void Con_Clear_f(void);

void Con_DrawNotify(void);

void Con_ClearNotify(void);

void Con_ToggleConsole_f(void);

void Con_NotifyBox(char *text);    // during startup for sound / cd warnings
