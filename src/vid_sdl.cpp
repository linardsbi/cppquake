// vid_sdl.h -- sdl video driver 


#include <SDL2/SDL.h>
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

static SDL_Window *window = nullptr;
static SDL_Renderer *renderer = nullptr;
static SDL_Texture *sdltexture = nullptr;
static SDL_Surface *screen = nullptr;

static qboolean mouse_avail;
static float mouse_x, mouse_y;
static int mouse_oldbuttonstate = 0;

// No support for option menus
void (*vid_menudrawfn)() = nullptr;

void (*vid_menukeyfn)(int key) = nullptr;

void VID_SetPalette(unsigned char *palette) {
    constexpr auto color_count = 256;
    std::array<SDL_Color, color_count> colors{};

    for (std::size_t i = 0; i < color_count; ++i) {
        colors.at(i).r = *palette++;
        colors.at(i).g = *palette++;
        colors.at(i).b = *palette++;
    }
    auto ret = SDL_SetPaletteColors(screen->format->palette, colors.cbegin(), 0, color_count);

    if (ret == -1) {
        Con_Printf("Vid: couldn't set palette colors\n");
    }
}

void VID_ShiftPalette(unsigned char *palette) {
    VID_SetPalette(palette);
}

void VID_Init(unsigned char *palette) {
    int chunk = 0;
    byte *cache = nullptr;
    int cachesize = 0;
    Uint32 flags = 0;

    // Load the SDL library
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        Sys_Error("VID: Couldn't load SDL: %s", SDL_GetError());
    }

    // Set up display mode (width and height)
    vid.width = BASEWIDTH;
    vid.height = BASEHEIGHT;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;

    if (const auto width_offset = COM_CheckParm("-w"); width_offset > 0) {
        vid.width = static_cast<unsigned>(std::atoi(com_argv[width_offset + 1]));
    }
    if (const auto height_offset = COM_CheckParm("-h"); height_offset > 0) {
        vid.height = static_cast<unsigned>(std::atoi(com_argv[height_offset + 1]));
    }

    if (COM_CheckParm("-vsync") != 0) {
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    }

    if (!vid.width || !vid.height)
        Sys_Error("VID: Bad window width/height\n");

    // Set video width, height and flags
    if (COM_CheckParm("-fullscreen") != 0) {
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    window = SDL_CreateWindow("CPPQuake",
                              SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED,
                              vid.width, vid.height,
                              flags | SDL_WINDOW_RESIZABLE);
    // Initialize display
    if (window == nullptr) {
        Sys_Error("VID: Couldn't set video mode: %s\n", SDL_GetError());
    }

    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_TARGETTEXTURE);

    if (renderer == nullptr) {
        Sys_Error("VID: Couldn't create renderer: %s\n", SDL_GetError());
    }

    constexpr Uint8 video_bpp = 8;
    screen = SDL_CreateRGBSurface(0, vid.width, vid.height, video_bpp, 0, 0, 0, 0);
    

    if (screen == nullptr) {
        Sys_Error("VID: Couldn't create screen: %s\n", SDL_GetError());
    }

    VID_SetPalette(palette);

    sdltexture = SDL_CreateTexture(renderer,
                                   SDL_PIXELFORMAT_RGBA8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   vid.width,
                                   vid.height);

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    SDL_RenderSetLogicalSize(renderer,
                             vid.width, vid.height);

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

    SDL_SetRelativeMouseMode(SDL_TRUE);
}

void VID_Shutdown() {
    SDL_Quit();
}

void VID_Update(const vrect_t &vrect) {
    void *pixels{};
    int pitch{};
    auto *window_surface = SDL_GetWindowSurface(window);
    SDL_Rect rect = {
        .x = vrect.x,
        .y = vrect.y,
        .w = vrect.width,
        .h = vrect.height,
    };
    /*
     * Blit 8-bit palette surface onto the window surface that's
     * closer to the texture's format
     */
    SDL_BlitSurface(screen, &rect, window_surface, &rect);

    /* Modify the texture's pixels */
    SDL_LockTexture(sdltexture, &rect, &pixels, &pitch);
    SDL_ConvertPixels(rect.w, rect.h,
                      window_surface->format->format,
                      window_surface->pixels, window_surface->pitch,
                      SDL_PIXELFORMAT_RGBA8888,
                      pixels, pitch);
    SDL_UnlockTexture(sdltexture);

    SDL_RenderCopy(renderer, sdltexture, &rect, &rect);
    SDL_RenderPresent(renderer);
    SDL_RenderClear(renderer);
}

/*
================
D_BeginDirectRect
================
*/
void D_BeginDirectRect(int x, int y, byte *pbitmap, int width, int height) {
    if (!window) return;

    Uint8 *offset = nullptr;

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
void D_EndDirectRect(int x, int y, int width, int height) {}


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
//                    case SDLK_BREAK:
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
                    case SDLK_KP_0:
                        if (modstate & KMOD_NUM) sym = K_INS;
                        else sym = SDLK_0;
                        break;
                    case SDLK_KP_1:
                        if (modstate & KMOD_NUM) sym = K_END;
                        else sym = SDLK_1;
                        break;
                    case SDLK_KP_2:
                        if (modstate & KMOD_NUM) sym = K_DOWNARROW;
                        else sym = SDLK_2;
                        break;
                    case SDLK_KP_3:
                        if (modstate & KMOD_NUM) sym = K_PGDN;
                        else sym = SDLK_3;
                        break;
                    case SDLK_KP_4:
                        if (modstate & KMOD_NUM) sym = K_LEFTARROW;
                        else sym = SDLK_4;
                        break;
                    case SDLK_KP_5:
                        sym = SDLK_5;
                        break;
                    case SDLK_KP_6:
                        if (modstate & KMOD_NUM) sym = K_RIGHTARROW;
                        else sym = SDLK_6;
                        break;
                    case SDLK_KP_7:
                        if (modstate & KMOD_NUM) sym = K_HOME;
                        else sym = SDLK_7;
                        break;
                    case SDLK_KP_8:
                        if (modstate & KMOD_NUM) sym = K_UPARROW;
                        else sym = SDLK_8;
                        break;
                    case SDLK_KP_9:
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
                mouse_x = event.motion.xrel * 10;
                mouse_y = event.motion.yrel * 10;          
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
        
    if (mouselook.value != 0.f || in_mlook.state & 1)
        V_StopPitchDrift();

    if (mouselook.value != 0.f || ((in_mlook.state & 1) && !(in_strafe.state & 1))) {
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
