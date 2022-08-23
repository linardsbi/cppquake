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
// window.c -- master for refresh, status bar, console, chat, notify, etc

#include <cmath>
#include <SDL.h>
#include "quakedef.hpp"
#include "r_local.hpp"


// only the refresh window will be updated unless these variables are flagged 
int scr_copytop;
int scr_copyeverything;

float scr_con_current;
float scr_conlines;        // lines of console to display

float oldscreensize, oldfov;

constexpr auto default_conspeed = 300.;

cvar_t scr_viewsize = {"viewsize", "100", true};
cvar_t scr_fov = {"fov", "90"};    // 10 - 170
cvar_t scr_conspeed = {"scr_conspeed", "300"}; // 300 = 1 second
cvar_t scr_centertime = {"scr_centertime", "2"};
cvar_t scr_showram = {"showram", "1"};
cvar_t scr_showturtle = {"showturtle", "0"};
cvar_t scr_showpause = {"showpause", "1"};
cvar_t scr_printspeed = {"scr_printspeed", "8"};

qboolean scr_initialized;        // ready to draw

qpic_t *scr_ram;
qpic_t *scr_net;
qpic_t *scr_turtle;

int scr_fullupdate;

int clearconsole;
int clearnotify;

extern viddef_t vid;                // global video state

vrect_t *pconupdate;
vrect_t scr_vrect;

qboolean scr_disabled_for_loading;
qboolean scr_drawloading;
float scr_disabled_time;
qboolean scr_skipupdate;

qboolean block_drawing;


void SCR_ScreenShot_f();

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char scr_centerstring[1024];
float scr_centertime_start;    // for slow victory printing
float scr_centertime_off;
int scr_center_lines;
int scr_erase_lines;
int scr_erase_center;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the window
for a few moments
==============
*/
void SCR_CenterPrint(char *str) {
    strncpy(scr_centerstring, str, sizeof(scr_centerstring) - 1);
    scr_centertime_off = scr_centertime.value;
    scr_centertime_start = cl.time;

// count the number of lines for centering
    scr_center_lines = 1;
    while (*str) {
        if (*str == '\n')
            scr_center_lines++;
        str++;
    }
}

inline void SCR_EraseCenterString() {
    int y = 0;

    if (scr_erase_center++ > vid.numpages) {
        scr_erase_lines = 0;
        return;
    }

    if (scr_center_lines <= 4)
        y = vid.height * 0.35;
    else
        y = 48;

    scr_copytop = 1;
    Draw_TileClear(0, y, vid.width, 8 * scr_erase_lines);
}

inline void SCR_DrawCenterString() {
    char *start = nullptr;
    int l = 0;
    int j = 0;
    int x = 0, y = 0;
    int remaining = 0;

// the finale prints the characters one at a time
    if (cl.intermission)
        remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
    else
        remaining = 9999;

    scr_erase_center = 0;
    start = scr_centerstring;

    if (scr_center_lines <= 4)
        y = vid.height * 0.35;
    else
        y = 48;

    do {
        // scan the width of the line
        for (l = 0; l < 40; l++)
            if (start[l] == '\n' || !start[l])
                break;
        x = (vid.width - l * 8) / 2;
        for (j = 0; j < l; j++, x += 8) {
            Draw_Character(x, y, start[j]);
            if (!remaining--)
                return;
        }

        y += 8;

        while (*start && *start != '\n')
            start++;

        if (!*start)
            break;
        start++;        // skip the \n
    } while (true);
}

inline void SCR_CheckDrawCenterString() {
    scr_copytop = 1;
    if (scr_center_lines > scr_erase_lines)
        scr_erase_lines = scr_center_lines;

    scr_centertime_off -= host_frametime;

    if (scr_centertime_off <= 0 && !cl.intermission)
        return;
    if (key_dest != key_game)
        return;

    SCR_DrawCenterString();
}

//=============================================================================

/*
====================
CalcFov
====================
*/
inline auto CalcFov(float fov_x, float width, float height) -> float {
    float a = NAN;
    float x = NAN;

    if (fov_x < 1 || fov_x > 179)
        Sys_Error("Bad fov: %f", fov_x);

    x = width / tan(fov_x / 360 * M_PI);

    a = atan(height / x);

    a = a * 360 / M_PI;

    return a;
}

/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
static inline void SCR_CalcRefdef() {
    vrect_t vrect;
    float size = NAN;

    scr_fullupdate = 0;        // force a background redraw
    vid.recalc_refdef = 0;

// force the status bar to redraw
    Sbar_Changed();

//========================================

// bound viewsize
    if (scr_viewsize.value < 30)
        Cvar_Set("viewsize", "30");
    if (scr_viewsize.value > 120)
        Cvar_Set("viewsize", "120");

// bound field of view
    if (scr_fov.value < 10)
        Cvar_Set("fov", "10");
    if (scr_fov.value > 170)
        Cvar_Set("fov", "170");

    r_refdef.fov_x = scr_fov.value;
    r_refdef.fov_y = CalcFov(r_refdef.fov_x, r_refdef.vrect.width, r_refdef.vrect.height);

// intermission is always full window
    if (cl.intermission)
        size = 120;
    else
        size = scr_viewsize.value;

    if (size >= 120)
        sb_lines = 0;        // no status bar at all
    else if (size >= 110)
        sb_lines = 24;        // no inventory
    else
        sb_lines = 24 + 16 + 8;

// these calculations mirror those in R_Init() for r_refdef, but take no
// account of water warping
    vrect.x = 0;
    vrect.y = 0;
    vrect.width = vid.width;
    vrect.height = vid.height;

    R_SetVrect(&vrect, &scr_vrect, sb_lines);

// guard against going from one mode to another that's less than half the
// vertical resolution
    if (scr_con_current > vid.height)
        scr_con_current = vid.height;

// notify the refresh of the change
    R_ViewChanged(&vrect, sb_lines, vid.aspect);
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f() {
    Cvar_SetValue("viewsize", scr_viewsize.value + 10);
    vid.recalc_refdef = 1;
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f() {
    Cvar_SetValue("viewsize", scr_viewsize.value - 10);
    vid.recalc_refdef = 1;
}

//============================================================================

/*
==================
SCR_Init
==================
*/
void SCR_Init() {
    Cvar_RegisterVariable(&scr_fov);
    Cvar_RegisterVariable(&scr_viewsize);
    Cvar_RegisterVariable(&scr_conspeed);
    Cvar_RegisterVariable(&scr_showram);
    Cvar_RegisterVariable(&scr_showturtle);
    Cvar_RegisterVariable(&scr_showpause);
    Cvar_RegisterVariable(&scr_centertime);
    Cvar_RegisterVariable(&scr_printspeed);

//
// register our commands
//
    Cmd_AddCommand("screenshot", SCR_ScreenShot_f);
    Cmd_AddCommand("sizeup", SCR_SizeUp_f);
    Cmd_AddCommand("sizedown", SCR_SizeDown_f);

    scr_ram = Draw_PicFromWad("ram");
    scr_net = Draw_PicFromWad("net");
    scr_turtle = Draw_PicFromWad("turtle");

    scr_initialized = true;
}


/*
==============
SCR_DrawRam
==============
*/
inline void SCR_DrawRam() {
    if (!scr_showram.value)
        return;

    if (!r_cache_thrash)
        return;

    Draw_Pic(scr_vrect.x + 32, scr_vrect.y, scr_ram);
}

/*
==============
SCR_DrawTurtle
==============
*/
inline void SCR_DrawTurtle() {
    static int count;

    if (!scr_showturtle.value)
        return;

    if (host_frametime < 0.1) {
        count = 0;
        return;
    }

    count++;
    if (count < 3)
        return;

    Draw_Pic(scr_vrect.x, scr_vrect.y, scr_turtle);
}

/*
==============
SCR_DrawNet
==============
*/
inline void SCR_DrawNet() {
    if (realtime - cl.last_received_message < 0.3)
        return;
    if (cls.demoplayback)
        return;

    Draw_Pic(scr_vrect.x + 64, scr_vrect.y, scr_net);
}

/*
==============
DrawPause
==============
*/
inline void SCR_DrawPause() {
    qpic_t *pic = nullptr;

    if (!scr_showpause.value)        // turn off for screenshots
        return;

    if (!cl.paused)
        return;

    pic = Draw_CachePic("gfx/pause.lmp");
    Draw_Pic((vid.width - pic->width) / 2,
             (vid.height - 48 - pic->height) / 2, pic);
}


/*
==============
SCR_DrawLoading
==============
*/
inline void SCR_DrawLoading() {
    qpic_t *pic = nullptr;

    if (!scr_drawloading)
        return;

    pic = Draw_CachePic("gfx/loading.lmp");
    Draw_Pic((vid.width - pic->width) / 2,
             (vid.height - 48 - pic->height) / 2, pic);
}



//=============================================================================
auto old_time = realtime;
bool scrollingUp = true;
/*
==================
SCR_SetUpToDrawConsole
==================
*/
void SCR_SetUpToDrawConsole() {
    Con_CheckResize();

    if (scr_drawloading)
        return;        // never a console with loading plaque

// decide on the height of the console
    con_forcedup = !cl.worldmodel || cls.signon != SIGNONS;

    if (con_forcedup) {
        scr_conlines = vid.height;        // full window
        scr_con_current = scr_conlines;
    } else if (key_dest == key_console)
        scr_conlines = vid.height / 2;    // half window
    else
        scr_conlines = 0;                // none visible

    if (scr_conlines < scr_con_current) {
        if (!scrollingUp) {
            old_time = realtime;
            scrollingUp = true;
            return;
        }
        scr_con_current -= ((realtime - old_time) / (default_conspeed / scr_conspeed.value) * scr_con_current);
        
        if (scr_conlines > scr_con_current)
            scr_con_current = scr_conlines;

    } else if (scr_conlines > scr_con_current) {
        if (scrollingUp) {
            old_time = realtime;
            scrollingUp = false;
            return;
        }
        scr_con_current = (realtime - old_time) / (default_conspeed / scr_conspeed.value) * scr_conlines;
        
        if (scr_conlines < scr_con_current)
            scr_con_current = scr_conlines;
    }

    if (clearconsole++ < vid.numpages) {
        scr_copytop = 1;
        Draw_TileClear(0, (int) scr_con_current, vid.width, vid.height - (int) scr_con_current);
        Sbar_Changed();
    } else if (clearnotify++ < vid.numpages) {
        scr_copytop = 1;
        Draw_TileClear(0, 0, vid.width, con_notifylines);
    } else
        con_notifylines = 0;
}

/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole() {
    if (scr_con_current) {
        scr_copyeverything = 1;
        Con_DrawConsole(scr_con_current, true);
        clearconsole = 0;
    } else {
        if (key_dest == key_game || key_dest == key_message)
            Con_DrawNotify();    // only draw notify in game
    }
}


/* 
============================================================================== 
 
						SCREEN SHOTS 
 
============================================================================== 
*/


using pcx_t = struct {
    char manufacturer;
    char version;
    char encoding;
    char bits_per_pixel;
    unsigned short xmin, ymin, xmax, ymax;
    unsigned short hres, vres;
    unsigned char palette[48];
    char reserved;
    char color_planes;
    unsigned short bytes_per_line;
    unsigned short palette_type;
    char filler[58];
    unsigned char data;            // unbounded
};

/* 
============== 
WritePCXfile 
============== 
*/
void WritePCXfile(char *filename, byte *data, int width, int height,
                  int rowbytes, byte *palette) {
    int i = 0, j = 0, length = 0;
    pcx_t *pcx = nullptr;
    byte *pack = nullptr;

    pcx = hunkTempAlloc<decltype(pcx)>(width * height * 2 + 1000);
    if (pcx == nullptr) {
        Con_Printf("SCR_ScreenShot_f: not enough memory\n");
        return;
    }

    pcx->manufacturer = 0x0a;    // PCX id
    pcx->version = 5;            // 256 color
    pcx->encoding = 1;        // uncompressed
    pcx->bits_per_pixel = 8;        // 256 color
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = LittleShort((short) (width - 1));
    pcx->ymax = LittleShort((short) (height - 1));
    pcx->hres = LittleShort((short) width);
    pcx->vres = LittleShort((short) height);
    memset(pcx->palette, 0, sizeof(pcx->palette));
    pcx->color_planes = 1;        // chunky image
    pcx->bytes_per_line = LittleShort((short) width);
    pcx->palette_type = LittleShort(2);        // not a grey scale
    memset(pcx->filler, 0, sizeof(pcx->filler));

// pack the image
    pack = &pcx->data;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            if ((*data & 0xc0) != 0xc0)
                *pack++ = *data++;
            else {
                *pack++ = 0xc1;
                *pack++ = *data++;
            }
        }

        data += rowbytes - width;
    }

// write the palette
    *pack++ = 0x0c;    // palette ID byte
    for (i = 0; i < 768; i++)
        *pack++ = *palette++;

// write output file 
    length = pack - (byte *) pcx;
    COM_WriteFile(filename, pcx, length);
}


/* 
================== 
SCR_ScreenShot_f
================== 
*/
void SCR_ScreenShot_f() {
    int i = 0;
    char pcxname[80];
    char checkname[MAX_OSPATH];

// 
// find a file name to save it to 
// 
    strcpy(pcxname, "quake00.pcx");

    for (i = 0; i <= 99; i++) {
        pcxname[5] = i / 10 + '0';
        pcxname[6] = i % 10 + '0';
        sprintf(checkname, "%s/%s", com_gamedir, pcxname);
        if (Sys_FileTime(checkname) == -1)
            break;    // file doesn't exist
    }
    if (i == 100) {
        Con_Printf("SCR_ScreenShot_f: Couldn't create a PCX file\n");
        return;
    }

// 
// save the pcx file 
// 
    D_EnableBackBufferAccess();    // enable direct drawing of console to back
    //  buffer

    WritePCXfile(pcxname, vid.buffer, vid.width, vid.height, vid.rowbytes,
                 host_basepal);

    D_DisableBackBufferAccess();    // for adapters that can't stay mapped in
    //  for linear writes all the time

    Con_Printf("Wrote %s\n", pcxname);
}


//=============================================================================


/*
===============
SCR_BeginLoadingPlaque

================
*/
void SCR_BeginLoadingPlaque() {
    S_StopAllSounds(true);

    if (cls.state != ca_connected)
        return;
    if (cls.signon != SIGNONS)
        return;

// redraw with no console and the loading plaque
    Con_ClearNotify();
    scr_centertime_off = 0;
    scr_con_current = 0;

    scr_drawloading = true;
    scr_fullupdate = 0;
    Sbar_Changed();
    SCR_UpdateScreen();
    scr_drawloading = false;

    scr_disabled_for_loading = true;
    scr_disabled_time = realtime;
    scr_fullupdate = 0;
}

/*
===============
SCR_EndLoadingPlaque

================
*/
void SCR_EndLoadingPlaque() {
    scr_disabled_for_loading = false;
    scr_fullupdate = 0;
    Con_ClearNotify();
}

//=============================================================================

std::string_view scr_notifystring;
qboolean scr_drawdialog;

void SCR_DrawNotifyString() {
    int l = 0;
    int j = 0;
    int x = 0, y = 0;

    auto start = scr_notifystring.begin();

    y = vid.height * 0.35;

    do {
        // scan the width of the line
        for (l = 0; l < 40; l++)
            if (start[l] == '\n' || !start[l])
                break;
        x = (vid.width - l * 8) / 2;
        for (j = 0; j < l; j++, x += 8)
            Draw_Character(x, y, start[j]);

        y += 8;

        while (*start && *start != '\n')
            start++;

        if (!*start)
            break;
        start++;        // skip the \n
    } while (true);
}

/*
==================
SCR_ModalMessage

Displays a text string in the center of the window and waits for a Y or N
keypress.  
==================
*/
auto SCR_ModalMessage(std::string_view text) -> int {
    if (cls.state == ca_dedicated)
        return true;

    scr_notifystring = text;

// draw a fresh window
    scr_fullupdate = 0;
    scr_drawdialog = true;
    SCR_UpdateScreen();
    scr_drawdialog = false;

    S_ClearBuffer();        // so dma doesn't loop current sound

    do {
        key_count = -1;        // wait for a key down and up
        Sys_SendKeyEvents();
    } while (key_lastpress != 'y' && key_lastpress != 'n' && key_lastpress != K_ESCAPE);

    scr_fullupdate = 0;
    SCR_UpdateScreen();

    return key_lastpress == 'y';
}


//=============================================================================

/*
===============
SCR_BringDownConsole

Brings the console down and fades the palettes back to normal
================
*/
void SCR_BringDownConsole() {
    int i = 0;

    scr_centertime_off = 0;

    for (i = 0; i < 20 && scr_conlines != scr_con_current; i++)
        SCR_UpdateScreen();

    cl.cshifts[0].percent = 0;        // no area contents palette on next frame
    VID_SetPalette(host_basepal);
}

void SCR_DrawPerformanceInfo() { 
    if (cl_showfps.value) {
        int fps_value = 1 / (host_frametime > 0.0 ? host_frametime : 0.001);
        const auto fps = "FPS: " + std::to_string(fps_value);
        Draw_String(vid.width - fps.length() * character_width, 0, fps);
    }
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the window.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/
void SCR_UpdateScreen() {
    static float oldscr_viewsize;
    static float oldlcd_x;

    if (scr_skipupdate || block_drawing)
        return;

    scr_copytop = 0;
    scr_copyeverything = 0;

    if (scr_disabled_for_loading) {
        if (realtime - scr_disabled_time > 60) {
            scr_disabled_for_loading = false;
            Con_Printf("load failed.\n");
        } else
            return;
    }

    if (cls.state == ca_dedicated)
        return;                // stdout only

    if (!scr_initialized || !con_initialized)
        return;                // not initialized yet

    if (scr_viewsize.value != oldscr_viewsize) {
        oldscr_viewsize = scr_viewsize.value;
        vid.recalc_refdef = 1;
    }

//
// check for vid changes
//
    if (oldfov != scr_fov.value) {
        oldfov = scr_fov.value;
        vid.recalc_refdef = true;
    }

    if (oldlcd_x != lcd_x.value) {
        oldlcd_x = lcd_x.value;
        vid.recalc_refdef = true;
    }

    if (oldscreensize != scr_viewsize.value) {
        oldscreensize = scr_viewsize.value;
        vid.recalc_refdef = true;
    }

    if (vid.recalc_refdef) {
        // something changed, so reorder the window
        SCR_CalcRefdef();
    }

//
// do 3D refresh drawing, and then update the window
//
    D_EnableBackBufferAccess();    // of all overlay stuff if drawing directly

    if (scr_fullupdate++ < vid.numpages) {    // clear the entire window
        // scr_copyeverything = 1;
        // Draw_TileClear(0, 0, vid.width, vid.height);
        Sbar_Changed();
    }

    pconupdate = nullptr;


    SCR_SetUpToDrawConsole();
    SCR_EraseCenterString();

    D_DisableBackBufferAccess();    // for adapters that can't stay mapped in
    //  for linear writes all the time

    VID_LockBuffer ();

    V_RenderView();

    VID_UnlockBuffer ();

    D_EnableBackBufferAccess();    // of all overlay stuff if drawing directly
    
    if (scr_drawdialog) {
        Sbar_Draw();
        Draw_FadeScreen();
        SCR_DrawNotifyString();
        scr_copyeverything = true;
    } else if (scr_drawloading) {
        SCR_DrawLoading();
        Sbar_Draw();
    } else if (cl.intermission == 1 && key_dest == key_game) {
        Sbar_IntermissionOverlay();
    } else if (cl.intermission == 2 && key_dest == key_game) {
        Sbar_FinaleOverlay();
        SCR_CheckDrawCenterString();
    } else if (cl.intermission == 3 && key_dest == key_game) {
        SCR_CheckDrawCenterString();
    } else {
        SCR_DrawRam();
        SCR_DrawNet();
        SCR_DrawTurtle();
        SCR_DrawPause();
        SCR_CheckDrawCenterString();
        Sbar_Draw();
        SCR_DrawConsole();
        M_Draw();
    }

    D_DisableBackBufferAccess();    // for adapters that can't stay mapped in
    //  for linear writes all the time
    if (pconupdate) {
        D_UpdateRects(pconupdate);
    }
    
    V_UpdatePalette();

    SCR_DrawPerformanceInfo();

    VID_Update({
        .x = 0,
        .y = 0,
        .width = static_cast<int>(vid.width),
        .height = static_cast<int>(vid.height),
    });
}


/*
==================
SCR_UpdateWholeScreen
==================
*/
void SCR_UpdateWholeScreen() {
    scr_fullupdate = 0;
    SCR_UpdateScreen();
}
