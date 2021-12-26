#include <unistd.h>
#include <csignal>
#include <cstdlib>
#include <climits>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <cerrno>
#include <filesystem>

#ifndef __WIN32__

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <SDL.h>


#endif

#include "quakedef.hpp"


qboolean isDedicated;

constexpr auto basedir = ".";
constexpr auto cachedir = "/tmp";

cvar_t sys_linerefresh = {"sys_linerefresh"};// set for entity display
cvar_t sys_nostdout = {"sys_nostdout"};

// =======================================================================
// General routines
// =======================================================================

void Sys_DebugNumber(int y, int val) {
}


[[noreturn]]void Sys_Quit() {
    Host_Shutdown();
    exit(0);
}

void Sys_Init() {
#if id386
    Sys_SetFPCW();
#endif
}

#if !id386

/*
================
Sys_LowFPPrecision
================
*/
void Sys_LowFPPrecision() {
// causes weird problems on Nextstep
}


/*
================
Sys_HighFPPrecision
================
*/
void Sys_HighFPPrecision() {
// causes weird problems on Nextstep
}

#endif    // !id386


void Sys_Error(const char *error, ...) {
    va_list argptr;
    char string[1024];

    va_start (argptr, error);
    vsprintf(string, error, argptr);
    va_end (argptr);
    fprintf(stderr, "Error: %s\n", string);

    Host_Shutdown();
    exit(1);

}

void Sys_Warn(char *warning, ...) {
    va_list argptr;
    char string[1024];

    va_start (argptr, warning);
    vsprintf(string, warning, argptr);
    va_end (argptr);
    fprintf(stderr, "Warning: %s", string);
}

/*
===============================================================================

FILE IO

===============================================================================
*/

#define    MAX_HANDLES        10
FILE *sys_handles[MAX_HANDLES];

auto findhandle() -> int {
    int i = 0;

    for (i = 1; i < MAX_HANDLES; i++)
        if (!sys_handles[i])
            return i;
    Sys_Error("out of handles");
    return -1;
}

/*
================
Qfilelength
================
*/
static auto Qfilelength(FILE *f) -> int {
    int pos = 0;
    int end = 0;

    pos = ftell(f);
    fseek(f, 0, SEEK_END);
    end = ftell(f);
    fseek(f, pos, SEEK_SET);

    return end;
}

auto Sys_FileOpenRead(const char *path, int *hndl) -> int {
    FILE *f = nullptr;
    int i = 0;

    i = findhandle();

    f = fopen(path, "rb");
    if (!f) {
        *hndl = -1;
        return -1;
    }
    sys_handles[i] = f;
    *hndl = i;

    return Qfilelength(f);
}

auto Sys_FileOpenWrite(const char *path) -> int {
    FILE *f = nullptr;
    int i = 0;

    i = findhandle();

    f = fopen(path, "wb");
    if (!f)
        Sys_Error("Error opening %s: %s", path, strerror(errno));
    sys_handles[i] = f;

    return i;
}

void Sys_FileClose(int handle) {
    if (handle >= 0) {
        fclose(sys_handles[handle]);
        sys_handles[handle] = nullptr;
    }
}

void Sys_FileSeek(int handle, int position) {
    if (handle >= 0) {
        fseek(sys_handles[handle], position, SEEK_SET);
    }
}

auto Sys_FileRead(int handle, void *dst, int count) -> int {
    if (handle == -1) {
        return 0;
    }

    auto *data = static_cast<char *>(dst);
    std::size_t size = 0;

    while (count > 0) {
        const auto done = fread(data, 1, count, sys_handles[handle]);
        if (done == 0) {
            break;
        }
        std::advance(data, done);
        count -= done;
        size += done;
    }

    return size;

}

auto Sys_FileWrite(int handle, const void *src, int count) -> int {
    if (handle >= 0) {
        return fwrite(static_cast<const char *>(src), 1, count, sys_handles[handle]);
    }
    return 0;
}

auto Sys_FileTime(std::string_view path) -> int {
    return std::filesystem::exists(path) ? 1 : -1;
}

void Sys_mkdir(const char *path) {
#ifdef __WIN32__
    mkdir (path);
#else
    mkdir(path, 0777);
#endif
}

void Sys_DebugLog(char *file, char *fmt, ...) {
    va_list argptr;
    static char data[1024];
    FILE *fp = nullptr;

    va_start(argptr, fmt);
    vsprintf(data, fmt, argptr);
    va_end(argptr);
    fp = fopen(file, "a");
    fwrite(data, strlen(data), 1, fp);
    fclose(fp);
}

auto Sys_FloatTime() -> double {
#ifdef __WIN32__

    static int starttime = 0;

    if ( ! starttime )
        starttime = clock();

    return (clock()-starttime)*1.0/1024;

#else

    timeval tp{};
    struct timezone tzp{};
    static int secbase;

    gettimeofday(&tp, &tzp);

    if (!secbase) {
        secbase = tp.tv_sec;
        return tp.tv_usec / 1000000.0;
    }

    return (tp.tv_sec - secbase) + tp.tv_usec / 1000000.0;

#endif
}

// =======================================================================
// Sleeps for microseconds
// =======================================================================

static volatile int oktogo;

void alarm_handler(int x) {
    oktogo = 1;
}

auto Sys_ZoneBase(int *size) -> byte * {

    char *QUAKEOPT = getenv("QUAKEOPT");

    *size = 0xc00000;
    if (QUAKEOPT) {
        while (*QUAKEOPT)
            if (tolower(*QUAKEOPT++) == 'm') {
                *size = atof(QUAKEOPT) * 1024 * 1024;
                break;
            }
    }
    return static_cast<byte *>(malloc(*size));

}

void Sys_LineRefresh() {
}

void Sys_Sleep() {
    SDL_Delay(1);
}

void floating_point_exception_handler(int whatever) {
//	Sys_Warn("floating point exception\n");
    signal(SIGFPE, floating_point_exception_handler);
}

void moncontrol(double x) {

}

auto main(int c, char **v) -> int {
    extern int vcrFile;
    extern int recording;
    int frame{};
    constexpr int memPoolSize = sizeof(void *) * 16 * 1024 * 1024;
    constexpr float fpsInterval = 1.0;

    moncontrol(0);
    COM_InitArgv(c, v);
//	signal(SIGFPE, floating_point_exception_handler);
    signal(SIGFPE, SIG_IGN);

    quakeparms_t parms = {
            .basedir = basedir,
            .cachedir = cachedir,
            .argc = com_argc,
            .argv = com_argv,
            .membase = malloc(memPoolSize),
            .memsize = memPoolSize,
    };

    Sys_Init();

    Host_Init(&parms);

    Cvar_RegisterVariable(&sys_nostdout);

    double oldtime = Sys_FloatTime() - 0.1;
    auto fpsLastTime = SDL_GetTicks();
    const bool showFPS = COM_CheckParm("-showfps");
    while (true) {
// find time spent rendering last frame

        double newtime = Sys_FloatTime();
        double time = newtime - oldtime;

        if (cls.state == ca_dedicated) {   // play vcrfiles at max speed
            if (time < sys_ticrate.value && (vcrFile == -1 || recording)) {
                SDL_Delay(1);
                continue;       // not time to run a server only tic yet
            }
            time = sys_ticrate.value;
        }

        if (time > sys_ticrate.value * 2.0)
            oldtime = newtime;
        else
            oldtime += time;

        Host_Frame(time);

        if (showFPS) {
            ++frame;
            if (fpsLastTime < SDL_GetTicks() - fpsInterval * 1000) {
                fpsLastTime = SDL_GetTicks();
                Con_Printf("FPS: %d\n", frame); // profile only while we do each Quake frame
                frame = 0;
            }
        }
//        moncontrol(time);

// graphic debugging aids
        if (sys_linerefresh.value != 0)
            Sys_LineRefresh();
    }

}


/*
================
Sys_MakeCodeWriteable
================
*/
void Sys_MakeCodeWriteable(unsigned long startaddr, unsigned long length) {

    int r = 0;
    unsigned long addr = 0;
    int psize = getpagesize();

    fprintf(stderr, "writable code %lx-%lx\n", startaddr, startaddr + length);

    addr = startaddr & ~(psize - 1);

    r = mprotect((char *) addr, length + startaddr - addr, 7);

    if (r < 0)
        Sys_Error("Protection change failed\n");

}

