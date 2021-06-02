// vid_sdl.h -- sdl video driver 

#include "SDL.h"
#include "quakedef.hpp"
#include "d_local.hpp"


viddef_t vid;                // global video state
unsigned short d_8to16table[256];

// The original defaults
//#define    BASEWIDTH    320
//#define    BASEHEIGHT   200
// Much better for high resolution displays
#define    BASEWIDTH    (320*2)
#define    BASEHEIGHT   (200*2)

int _VGA_width, _VGA_height, _VGA_rowbytes, _VGA_bufferrowbytes = 0;
byte *_VGA_pagebase;

static SDL_Surface *screen = nullptr;

static qboolean mouse_avail;
static float mouse_x, mouse_y;
static int mouse_oldbuttonstate = 0;

// No support for option menus
void (*vid_menudrawfn)() = nullptr;

void (*vid_menukeyfn)(int key) = nullptr;

void VID_SetPalette(unsigned char *palette) {
    int i = 0;
    SDL_Color colors[256];

    for (i = 0; i < 256; ++i) {
        colors[i].r = *palette++;
        colors[i].g = *palette++;
        colors[i].b = *palette++;
    }
    SDL_SetColors(screen, colors, 0, 256);
}

void VID_ShiftPalette(unsigned char *palette) {
    VID_SetPalette(palette);
}

void VID_Init(unsigned char *palette) {
    int pnum = 0, chunk = 0;
    byte *cache = nullptr;
    int cachesize = 0;
    Uint8 video_bpp = 0;
    Uint16 video_w = 0, video_h = 0;
    Uint32 flags = 0;

    // Load the SDL library
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_CDROM) < 0)
        Sys_Error("VID: Couldn't load SDL: %s", SDL_GetError());

    // Set up display mode (width and height)
    vid.width = BASEWIDTH;
    vid.height = BASEHEIGHT;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    if ((pnum = COM_CheckParm("-winsize"))) {
        if (pnum >= com_argc - 2)
            Sys_Error("VID: -winsize <width> <height>\n");
        vid.width = static_cast<unsigned>(strtol(com_argv[pnum + 1], nullptr, 10));
        vid.height = static_cast<unsigned>(strtol(com_argv[pnum + 2], nullptr, 10));
        if (!vid.width || !vid.height)
            Sys_Error("VID: Bad window width/height\n");
    }

    // Set video width, height and flags
    flags = (SDL_SWSURFACE | SDL_HWPALETTE);
    if (COM_CheckParm("-fullscreen"))
        flags |= SDL_FULLSCREEN;

    // Initialize display 
    if (!(screen = SDL_SetVideoMode(vid.width, vid.height, 8, flags)))
        Sys_Error("VID: Couldn't set video mode: %s\n", SDL_GetError());
    VID_SetPalette(palette);
    SDL_WM_SetCaption("sdlquake", "sdlquake");
    // now know everything we need to know about the buffer
    _VGA_width = vid.conwidth = vid.width;
    _VGA_height = vid.conheight = vid.height;
    vid.aspect = ((float) vid.height / (float) vid.width) * (320.0 / 240.0);
    vid.numpages = 1;
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *) vid.colormap + 2048));
    _VGA_pagebase = vid.buffer = static_cast<pixel_t *>(screen->pixels);
    _VGA_rowbytes = vid.rowbytes = screen->pitch;
    vid.conbuffer = vid.buffer;
    vid.conrowbytes = vid.rowbytes;
    vid.direct = nullptr;

    // allocate z buffer and surface cache
    chunk = vid.width * vid.height * sizeof(*d_pzbuffer);
    cachesize = D_SurfaceCacheForRes(vid.width, vid.height);
    chunk += cachesize;
    d_pzbuffer = hunkHighAllocName<decltype(d_pzbuffer)>(chunk, "video");
    if (d_pzbuffer == nullptr)
        Sys_Error("Not enough memory for video mode\n");

    // initialize the cache memory 
    cache = (byte *) d_pzbuffer
            + vid.width * vid.height * sizeof(*d_pzbuffer);

    D_InitCaches(cache, cachesize);

    // initialize the mouse
    SDL_ShowCursor(0);
}

void VID_Shutdown() {
    SDL_Quit();
}

void VID_Update(vrect_t *rects) {
    SDL_Rect *sdlrects = nullptr;
    int n = 0, i = 0;
    vrect_t *rect = nullptr;

    // Two-pass system, since Quake doesn't do it the SDL way...

    // First, count the number of rectangles
    n = 0;
    for (rect = rects; rect; rect = rect->pnext)
        ++n;

    // Second, copy them to SDL rectangles and update
    if (!(sdlrects = (SDL_Rect *) alloca(n * sizeof(*sdlrects))))
        Sys_Error("Out of memory");
    i = 0;
    for (rect = rects; rect; rect = rect->pnext) {
        sdlrects[i].x = rect->x;
        sdlrects[i].y = rect->y;
        sdlrects[i].w = rect->width;
        sdlrects[i].h = rect->height;
        ++i;
    }
    SDL_UpdateRects(screen, n, sdlrects);
}

/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height) {
    Uint8 *offset = nullptr;


    if (!screen) return;
    if (x < 0) x = screen->w + x - 1;
    offset = (Uint8 *) screen->pixels + y * screen->pitch + x;
    while (height--) {
        memcpy(offset, pbitmap, width);
        offset += screen->pitch;
        pbitmap += width;
    }
}


/*
================
D_EndDirectRect
================
*/
void D_EndDirectRect(int x, int y, int width, int height) {
    if (!screen) return;
    if (x < 0) x = screen->w + x - 1;
    SDL_UpdateRect(screen, x, y, width, height);
}


/*
================
Sys_SendKeyEvents
================
*/

void Sys_SendKeyEvents() {
    SDL_Event event;
    int sym = 0, state = 0;
    int modstate = 0;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {

            case SDL_KEYDOWN:
            case SDL_KEYUP:
                sym = event.key.keysym.sym;
                state = event.key.state;
                modstate = SDL_GetModState();
                switch (sym) {
                    case SDLK_DELETE:
                        sym = K_DEL;
                        break;
                    case SDLK_BACKSPACE:
                        sym = K_BACKSPACE;
                        break;
                    case SDLK_F1:
                        sym = K_F1;
                        break;
                    case SDLK_F2:
                        sym = K_F2;
                        break;
                    case SDLK_F3:
                        sym = K_F3;
                        break;
                    case SDLK_F4:
                        sym = K_F4;
                        break;
                    case SDLK_F5:
                        sym = K_F5;
                        break;
                    case SDLK_F6:
                        sym = K_F6;
                        break;
                    case SDLK_F7:
                        sym = K_F7;
                        break;
                    case SDLK_F8:
                        sym = K_F8;
                        break;
                    case SDLK_F9:
                        sym = K_F9;
                        break;
                    case SDLK_F10:
                        sym = K_F10;
                        break;
                    case SDLK_F11:
                        sym = K_F11;
                        break;
                    case SDLK_F12:
                        sym = K_F12;
                        break;
                    case SDLK_BREAK:
                    case SDLK_PAUSE:
                        sym = K_PAUSE;
                        break;
                    case SDLK_UP:
                        sym = K_UPARROW;
                        break;
                    case SDLK_DOWN:
                        sym = K_DOWNARROW;
                        break;
                    case SDLK_RIGHT:
                        sym = K_RIGHTARROW;
                        break;
                    case SDLK_LEFT:
                        sym = K_LEFTARROW;
                        break;
                    case SDLK_INSERT:
                        sym = K_INS;
                        break;
                    case SDLK_HOME:
                        sym = K_HOME;
                        break;
                    case SDLK_END:
                        sym = K_END;
                        break;
                    case SDLK_PAGEUP:
                        sym = K_PGUP;
                        break;
                    case SDLK_PAGEDOWN:
                        sym = K_PGDN;
                        break;
                    case SDLK_RSHIFT:
                    case SDLK_LSHIFT:
                        sym = K_SHIFT;
                        break;
                    case SDLK_RCTRL:
                    case SDLK_LCTRL:
                        sym = K_CTRL;
                        break;
                    case SDLK_RALT:
                    case SDLK_LALT:
                        sym = K_ALT;
                        break;
                    case SDLK_KP0:
                        if (modstate & KMOD_NUM) sym = K_INS;
                        else sym = SDLK_0;
                        break;
                    case SDLK_KP1:
                        if (modstate & KMOD_NUM) sym = K_END;
                        else sym = SDLK_1;
                        break;
                    case SDLK_KP2:
                        if (modstate & KMOD_NUM) sym = K_DOWNARROW;
                        else sym = SDLK_2;
                        break;
                    case SDLK_KP3:
                        if (modstate & KMOD_NUM) sym = K_PGDN;
                        else sym = SDLK_3;
                        break;
                    case SDLK_KP4:
                        if (modstate & KMOD_NUM) sym = K_LEFTARROW;
                        else sym = SDLK_4;
                        break;
                    case SDLK_KP5:
                        sym = SDLK_5;
                        break;
                    case SDLK_KP6:
                        if (modstate & KMOD_NUM) sym = K_RIGHTARROW;
                        else sym = SDLK_6;
                        break;
                    case SDLK_KP7:
                        if (modstate & KMOD_NUM) sym = K_HOME;
                        else sym = SDLK_7;
                        break;
                    case SDLK_KP8:
                        if (modstate & KMOD_NUM) sym = K_UPARROW;
                        else sym = SDLK_8;
                        break;
                    case SDLK_KP9:
                        if (modstate & KMOD_NUM) sym = K_PGUP;
                        else sym = SDLK_9;
                        break;
                    case SDLK_KP_PERIOD:
                        if (modstate & KMOD_NUM) sym = K_DEL;
                        else sym = SDLK_PERIOD;
                        break;
                    case SDLK_KP_DIVIDE:
                        sym = SDLK_SLASH;
                        break;
                    case SDLK_KP_MULTIPLY:
                        sym = SDLK_ASTERISK;
                        break;
                    case SDLK_KP_MINUS:
                        sym = SDLK_MINUS;
                        break;
                    case SDLK_KP_PLUS:
                        sym = SDLK_PLUS;
                        break;
                    case SDLK_KP_ENTER:
                        sym = SDLK_RETURN;
                        break;
                    case SDLK_KP_EQUALS:
                        sym = SDLK_EQUALS;
                        break;
                }
                // If we're not directly handled and still above 255
                // just force it to 0
                if (sym > 255) sym = 0;
                Key_Event(sym, state);
                break;

            case SDL_MOUSEMOTION:
                if ((event.motion.x != (vid.width / 2)) ||
                    (event.motion.y != (vid.height / 2))) {
                    mouse_x = event.motion.xrel * 10;
                    mouse_y = event.motion.yrel * 10;
                    if ((event.motion.x < ((vid.width / 2) - (vid.width / 4))) ||
                        (event.motion.x > ((vid.width / 2) + (vid.width / 4))) ||
                        (event.motion.y < ((vid.height / 2) - (vid.height / 4))) ||
                        (event.motion.y > ((vid.height / 2) + (vid.height / 4)))) {
                        SDL_WarpMouse(vid.width / 2, vid.height / 2);
                    }
                }
                break;

            case SDL_QUIT:
                CL_Disconnect();
                Host_ShutdownServer(false);
                Sys_Quit();
                break;
            default:
                break;
        }
    }
}

void IN_Init() {
    if (COM_CheckParm("-nomouse"))
        return;
    mouse_x = mouse_y = 0.0;
    mouse_avail = true;
}

void IN_Shutdown() {
    mouse_avail = false;
}

void IN_Commands() {
    int i = 0;
    int mouse_buttonstate = 0;

    if (!mouse_avail) return;

    i = SDL_GetMouseState(nullptr, nullptr);
    /* Quake swaps the second and third buttons */
    mouse_buttonstate = (i & ~0x06) | ((i & 0x02) << 1) | ((i & 0x04) >> 1);
    for (i = 0; i < 3; i++) {
        if ((mouse_buttonstate & (1 << i)) && !(mouse_oldbuttonstate & (1 << i)))
            Key_Event(K_MOUSE1 + i, true);

        if (!(mouse_buttonstate & (1 << i)) && (mouse_oldbuttonstate & (1 << i)))
            Key_Event(K_MOUSE1 + i, false);
    }
    mouse_oldbuttonstate = mouse_buttonstate;
}

void IN_Move(usercmd_t *cmd) {
    if (!mouse_avail)
        return;

    mouse_x *= sensitivity.value;
    mouse_y *= sensitivity.value;

    if ((in_strafe.state & 1) || (lookstrafe.value && (in_mlook.state & 1)))
        cmd->sidemove += m_side.value * mouse_x;
    else
        cl.viewangles[YAW] -= m_yaw.value * mouse_x;
    if (in_mlook.state & 1)
        V_StopPitchDrift();

    if ((in_mlook.state & 1) && !(in_strafe.state & 1)) {
        cl.viewangles[PITCH] += m_pitch.value * mouse_y;
        if (cl.viewangles[PITCH] > 80)
            cl.viewangles[PITCH] = 80;
        if (cl.viewangles[PITCH] < -70)
            cl.viewangles[PITCH] = -70;
    } else {
        if ((in_strafe.state & 1) && noclip_anglehack)
            cmd->upmove -= m_forward.value * mouse_y;
        else
            cmd->forwardmove -= m_forward.value * mouse_y;
    }
    mouse_x = mouse_y = 0.0;
}

/*
================
Sys_ConsoleInput
================
*/
auto Sys_ConsoleInput() -> char * {
    return nullptr;
}