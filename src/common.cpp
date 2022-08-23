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
// common.c -- misc functions used in client and server

#include "quakedef.hpp"
#include <string_view>
#include <array>
#include <cstdio>
#include <sstream>
#include <ranges>

#define NUM_SAFE_ARGVS  7
;
static const char *largv[MAX_NUM_ARGVS + NUM_SAFE_ARGVS + 1] = {};
static char argvdummy[] = " ";

static constexpr const char *safeargvs[NUM_SAFE_ARGVS] =
        {"-stdvid", "-nolan", "-nosound", "-nocdaudio", "-nojoy", "-nomouse", "-dibonly"};

cvar_t registered = {"registered", "0"};
cvar_t cmdline = {"cmdline", "0", false, true};

qboolean com_modified;   // set true if using non-id files

qboolean proghack;

qboolean static_registered = true;  // only for startup check, then set

qboolean msg_suppress_1 = false;

void COM_InitFilesystem();

// if a packfile directory differs from this, it is assumed to be hacked
#define PAK0_COUNT              339
#define PAK0_CRC                32981

char com_token[1024];
int com_argc;
const char **com_argv = nullptr;

#define CMDLINE_LENGTH    256
char com_cmdline[CMDLINE_LENGTH];

qboolean standard_quake = true, rogue, hipnotic;

// this graphic needs to be in the pak file to use registered features
constexpr std::array pop =
        {
                0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x6600, 0x0000, 0x0000,
                0x0000, 0x6600, 0x0000, 0x0000, 0x0066, 0x0000, 0x0000, 0x0000, 0x0000, 0x0067, 0x0000, 0x0000, 0x6665,
                0x0000, 0x0000, 0x0000, 0x0000, 0x0065, 0x6600, 0x0063, 0x6561, 0x0000, 0x0000, 0x0000, 0x0000, 0x0061,
                0x6563, 0x0064, 0x6561, 0x0000, 0x0000, 0x0000, 0x0000, 0x0061, 0x6564, 0x0064, 0x6564, 0x0000, 0x6469,
                0x6969, 0x6400, 0x0064, 0x6564, 0x0063, 0x6568, 0x6200, 0x0064, 0x6864, 0x0000, 0x6268, 0x6563, 0x0000,
                0x6567, 0x6963, 0x0064, 0x6764, 0x0063, 0x6967, 0x6500, 0x0000, 0x6266, 0x6769, 0x6a68, 0x6768, 0x6a69,
                0x6766, 0x6200, 0x0000, 0x0062, 0x6566, 0x6666, 0x6666, 0x6666, 0x6562, 0x0000, 0x0000, 0x0000, 0x0062,
                0x6364, 0x6664, 0x6362, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0062, 0x6662, 0x0000, 0x0000, 0x0000,
                0x0000, 0x0000, 0x0000, 0x0061, 0x6661, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x6500,
                0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x6400, 0x0000, 0x0000, 0x0000
        };

/*


All of Quake's data access is through a hierchal file system, but the contents of the file system can be transparently merged from several sources.

The "base directory" is the path to the directory holding the quake.exe and all game directories.  The sys_* files pass this to host_init in quakeparms_t->basedir.  This can be overridden with the "-basedir" command line parm to allow code debugging in a different directory.  The base directory is
only used during filesystem initialization.

The "game directory" is the first tree on the search path and directory that all generated files (savegames, screenshots, demos, config files) will be saved to.  This can be overridden with the "-game" command line parameter.  The game directory can never be changed while quake is executing.  This is a precacution against having a malicious server instruct clients to write files over areas they shouldn't.

The "cache directory" is only used during development to save network bandwidth, especially over ISDN / T1 lines.  If there is a cache directory
specified, when a file is found by the normal search path, it will be mirrored
into the cache directory, then opened there.



FIXME:
The file "parms.txt" will be read out of the game directory and appended to the current command line arguments to allow different games to initialize startup parms differently.  This could be used to add a "-sspeed 22050" for the high quality sound edition.  Because they are added at the end, they will not override an explicit setting on the original command line.
	
*/

//============================================================================


// ClearLink is used for new headnodes
void ClearLink(link_t *l) {
    l->prev = l->next = l;
}

void RemoveLink(link_t *l) {
    l->next->prev = l->prev;
    l->prev->next = l->next;
}

void InsertLinkBefore(link_t *l, link_t *before) {
    l->next = before;
    l->prev = before->prev;
    l->prev->next = l;
    l->next->prev = l;
}

void InsertLinkAfter(link_t *l, link_t *after) {
    l->next = after->next;
    l->prev = after;
    l->prev->next = l;
    l->next->prev = l;
}

/*
============================================================================

					LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

void Q_memset(void *dest, int fill, int count) {
    int i;

    if ((((long) dest | count) & 3) == 0) {
        count >>= 2;
        fill = fill | (fill << 8) | (fill << 16) | (fill << 24);
        for (i = 0; i < count; i++)
            ((int *) dest)[i] = fill;
    } else
        for (i = 0; i < count; i++)
            ((byte *) dest)[i] = fill;
}

void Q_memcpy(void *dest, const void *src, int count) {
    int i;

    if ((((long) dest | (long) src | count) & 3) == 0) {
        count >>= 2;
        for (i = 0; i < count; i++)
            ((int *) dest)[i] = ((int *) src)[i];
    } else
        for (i = 0; i < count; i++)
            ((byte *) dest)[i] = ((byte *) src)[i];
}

auto Q_memcmp(void *m1, void *m2, int count) -> int {
    while (count) {
        count--;
        if (((byte *) m1)[count] != ((byte *) m2)[count])
            return -1;
    }
    return 0;
}

void Q_strcpy(char *dest, const char *src) {
    while (*src) {
        *dest++ = *src++;
    }
    *dest++ = 0;
}

void Q_strncpy(char *dest, std::string_view src, int count) {
    for (const auto ch : src | std::ranges::views::take(count)) {
        *dest++ = ch;
    }
    if (dest)
        *dest++ = 0;
}

auto Q_strlen(const char *str) -> int {
    int count = 0;
    while (str[count])
        count++;

    return count;
}

auto Q_strrchr(char *s, char c) -> char * {
    int len = Q_strlen(s);
    s += len;
    while (len--)
        if (*--s == c) return s;
    return nullptr;
}

void Q_strcat(char *dest, char *src) {
    dest += std::strlen(dest);
    Q_strcpy(dest, src);
}

auto Q_strcmp(std::string_view s1, std::string_view s2) -> bool {
    return s1.compare(s2);
}

auto Q_strncmp(std::string_view s1, std::string_view s2, const std::size_t count) -> bool {
    return s1.compare(0, count, s2, 0, count);
}

// -1 == not equal; 0 == equal
auto Q_strncasecmp(std::string_view s1, std::string_view s2, int n) -> int {
    for (auto c1 = s1.begin(), c2 = s2.begin(); n > 0; n--, c1++, c2++) {
        if (*c1 != *c2) {
            if (tolower(*c1) != tolower(*c2)) {
                return -1; // strings not equal
            }
        }
        if (c1 == s1.end() || c2 == s2.end()) {
            return 0; // strings are equal
        }
    }

    if (n == 0) {
        return 0; // strings are equal until end point
    }

    return -1;
}

auto Q_strcasecmp(std::string_view s1, std::string_view s2) -> int {
    return Q_strncasecmp(s1, s2, 99999);
}

auto Q_atoi(std::string_view str) -> int {
    return std::atoi(str.data());
//    int val;
//    int sign;
//    int c;
//
//    if (*str == '-') {
//        sign = -1;
//        str++;
//    } else
//        sign = 1;
//
//    val = 0;
//
////
//// check for hex
////
//    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
//        str += 2;
//        while (true) {
//            c = *str++;
//            if (c >= '0' && c <= '9')
//                val = (val << 4) + c - '0';
//            else if (c >= 'a' && c <= 'f')
//                val = (val << 4) + c - 'a' + 10;
//            else if (c >= 'A' && c <= 'F')
//                val = (val << 4) + c - 'A' + 10;
//            else
//                return val * sign;
//        }
//    }
//
////
//// check for character
////
//    if (str[0] == '\'') {
//        return sign * str[1];
//    }
//
////
//// assume decimal
////
//    while (true) {
//        c = *str++;
//        if (c < '0' || c > '9')
//            return val * sign;
//        val = val * 10 + c - '0';
//    }
//
//    return 0;
}


auto Q_atof(std::string_view str) -> float {
    return std::atof(str.data());
//    double val;
//    int sign;
//    int c;
//    int decimal, total;
//
//    if (*str == '-') {
//        sign = -1;
//        str++;
//    } else
//        sign = 1;
//
//    val = 0;
//
////
//// check for hex
////
//    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
//        str += 2;
//        while (true) {
//            c = *str++;
//            if (c >= '0' && c <= '9')
//                val = (val * 16) + c - '0';
//            else if (c >= 'a' && c <= 'f')
//                val = (val * 16) + c - 'a' + 10;
//            else if (c >= 'A' && c <= 'F')
//                val = (val * 16) + c - 'A' + 10;
//            else
//                return val * sign;
//        }
//    }
//
////
//// check for character
////
//    if (str[0] == '\'') {
//        return sign * str[1];
//    }
//
////
//// assume decimal
////
//    decimal = -1;
//    total = 0;
//    while (true) {
//        c = *str++;
//        if (c == '.') {
//            decimal = total;
//            continue;
//        }
//        if (c < '0' || c > '9')
//            break;
//        val = val * 10 + c - '0';
//        total++;
//    }
//
//    if (decimal == -1)
//        return val * sign;
//    while (total > decimal) {
//        val /= 10;
//        total--;
//    }
//
//    return val * sign;
}

/*
============================================================================

					BYTE ORDER FUNCTIONS

============================================================================
*/
#include <SDL.h>

qboolean bigendien;

short (*BigShort)(short l);

short (*LittleShort)(short l);

int (*BigLong)(int l);

int (*LittleLong)(int l);

float (*BigFloat)(float l);

float (*LittleFloat)(float l);

auto ShortSwap(short l) -> short {
    byte b1, b2;

    b1 = l & 255;
    b2 = (l >> 8) & 255;

    return (b1 << 8) + b2;
}

auto ShortNoSwap(short l) -> short {
    return l;
}

constexpr auto LongSwap(int l) -> int {
    byte b1, b2, b3, b4;

    b1 = l & 255;
    b2 = (l >> 8) & 255;
    b3 = (l >> 16) & 255;
    b4 = (l >> 24) & 255;

    return ((int) b1 << 24) + ((int) b2 << 16) + ((int) b3 << 8) + b4;
}

auto LongNoSwap(int l) -> int {
    return l;
}

auto FloatSwap(float f) -> float {
    union {
        float f;
        byte b[4];
    } dat1, dat2;


    dat1.f = f;
    dat2.b[0] = dat1.b[3];
    dat2.b[1] = dat1.b[2];
    dat2.b[2] = dat1.b[1];
    dat2.b[3] = dat1.b[0];
    return dat2.f;
}

auto FloatNoSwap(float f) -> float {
    return f;
}

/*
==============================================================================

			MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

//
// writing functions
//

template <std::size_t buf_size, typename ...Args>
inline void MSG_Write(byte *buf, Args... args) {
    auto write_arg = [buf, index = 0U](const auto arg) mutable {
        constexpr auto arg_size = buf_size / sizeof...(args);
        for (unsigned i = 0; i < arg_size; ++i) {
            buf[index++] = (arg >> (i * 8U)) & 0xff;
        }
    };

    (write_arg(args), ...);
}

void MSG_WriteChar(sizebuf_t *sb, int c) {

#ifdef PARANOID
    if (c < -128 || c > 127)
        Sys_Error("MSG_WriteChar: range error");
#endif

    byte *buf = SZGetSpace<decltype(buf)>(sb, 1);
    buf[0] = c;
}

void MSG_WriteByte(sizebuf_t *sb, int c) {
#ifdef PARANOID
    if (c < 0 || c > 255)
        Sys_Error("MSG_WriteByte: range error");
#endif

    byte *buf = SZGetSpace<decltype(buf)>(sb, 1);
    buf[0] = c;
}

void MSG_WriteShort(sizebuf_t *sb, short c) {
#ifdef PARANOID
    if (c < ((short) 0x8000) || c > (short) 0x7fff)
        Sys_Error("MSG_WriteShort: range error");
#endif

    byte *buf = SZGetSpace<decltype(buf)>(sb, 2);
    buf[0] = c & 0xff;
    buf[1] = c >> 8;
}

void MSG_WriteLong(sizebuf_t *sb, int c) {
    byte *buf = SZGetSpace<decltype(buf)>(sb, 4);
    MSG_Write<4>(buf, c);
}

void MSG_WriteFloat(sizebuf_t *sb, float f) {
    union {
        float f;
        int l;
    } dat{};


    dat.f = f;
    dat.l = LittleLong(dat.l);

    SZ_Write(sb, &dat.l, 4);
}

void MSG_WriteString(sizebuf_t *sb, std::string_view s) {
    SZ_Write(sb, reinterpret_cast<const void *>(s.cbegin()), s.length() + 1);
}

void MSG_WriteCoord(sizebuf_t *sb, float f) {
    MSG_WriteShort(sb, static_cast<short>((f * 8)));
}

void MSG_WriteCoords(sizebuf_t *sb, vec3 coords) {
    coords *= 8.F;
    const int x = static_cast<int>(coords[0]);
    const int y = static_cast<int>(coords[1]);
    const int z = static_cast<int>(coords[2]);

    byte *buf = SZGetSpace<decltype(buf)>(sb, 6);
    MSG_Write<6>(buf, x, y, z);
}

void MSG_WriteAngle(sizebuf_t *sb, float f) {
//    printf("angle: %d\n", ((int)f *256/360) & 255);
    MSG_WriteByte(sb, ((int) f * 256 / 360) & 255);
}

void MSG_WriteAngles(sizebuf_t *sb, vec3 angles) {
    angles = angles * 256.F / 360.F;
    const int a = static_cast<int>(angles[0]);
    const int b = static_cast<int>(angles[1]);
    const int c = static_cast<int>(angles[2]);
    byte *buf = SZGetSpace<decltype(buf)>(sb, 3);

    MSG_Write<3>(buf, a, b, c);
}

//
// reading functions
//
int msg_readcount;
qboolean msg_badread;

void MSG_BeginReading() {
    msg_readcount = 0;
    msg_badread = false;
}

// returns -1 and sets msg_badread if no more characters are available
auto MSG_ReadChar() -> int {
    if (msg_readcount + 1 > net_message.cursize) {
        msg_badread = true;
        return -1;
    }

    char c = (char) net_message.data[msg_readcount];
    msg_readcount++;
    return c;
}

auto MSG_ReadByte() -> int {
    if (msg_readcount + 1 > net_message.cursize) {
        msg_badread = true;
        return -1;
    }

    return net_message.data[msg_readcount++];
}

auto MSG_ReadShort() -> int {
    int c;

    if (msg_readcount + 2 > net_message.cursize) {
        msg_badread = true;
        return -1;
    }

    c = (short) (net_message.data[msg_readcount]
                 + (net_message.data[msg_readcount + 1] << 8));

    msg_readcount += 2;

    return c;
}

auto MSG_ReadLong() -> int {
    int c;

    if (msg_readcount + 4 > net_message.cursize) {
        msg_badread = true;
        return -1;
    }

    c = net_message.data[msg_readcount]
        + (net_message.data[msg_readcount + 1] << 8)
        + (net_message.data[msg_readcount + 2] << 16)
        + (net_message.data[msg_readcount + 3] << 24);

    msg_readcount += 4;

    return c;
}

auto MSG_ReadFloat() -> float {
    union {
        byte b[4];
        float f;
        int l;
    } dat{};

    dat.b[0] = net_message.data[msg_readcount];
    dat.b[1] = net_message.data[msg_readcount + 1];
    dat.b[2] = net_message.data[msg_readcount + 2];
    dat.b[3] = net_message.data[msg_readcount + 3];
    msg_readcount += 4;

    dat.l = LittleLong(dat.l);

    return dat.f;
}

auto MSG_ReadString() -> char * {
    static char string[2048];
    int l, c;

    l = 0;
    do {
        c = MSG_ReadChar();
        if (c == -1 || c == 0)
            break;
        string[l] = c;
        l++;
    } while (l < sizeof(string) - 1);

    string[l] = 0;

    return string;
}

auto MSG_ReadCoord() -> float {
    return static_cast<float>(MSG_ReadShort()) * (1.0f / 8.f);
}

auto MSG_ReadAngle() -> float {
    return static_cast<float>(MSG_ReadChar()) * (360.0f / 256.f);
}



//===========================================================================

void SZ_Alloc(sizebuf_t *buf, int startsize) {
    if (startsize < 256)
        startsize = 256;
    buf->data = hunkAllocName<decltype(buf->data)>(startsize, "sizebuf");
    buf->maxsize = startsize;
    buf->cursize = 0;
}


void SZ_Free(sizebuf_t *buf) {
//      Z_Free (buf->data);
//      buf->data = NULL;
//      buf->maxsize = 0;
    buf->cursize = 0;
}

void SZ_Clear(sizebuf_t *buf) {
    buf->cursize = 0;
}

/*
void *SZ_GetSpace (sizebuf_t *buf, int length)
{
	void    *data;
	
	if (buf->cursize + length > buf->maxsize)
	{
		if (!buf->allowoverflow)
			Sys_Error ("SZ_GetSpace: overflow without allowoverflow set");
		
		if (length > buf->maxsize)
			Sys_Error ("SZ_GetSpace: %i is > full buffer size", length);
			
		buf->overflowed = true;
		Con_Printf ("SZ_GetSpace: overflow");
		SZ_Clear (buf); 
	}

	data = buf->data + buf->cursize;
	buf->cursize += length;
	
	return data;
}
*/

void SZ_Write(sizebuf_t *buf, const void *data, std::size_t length) {
    std::memcpy(SZGetSpace<void *>(buf, length), data, length);
}

void SZ_Print(sizebuf_t *buf, std::string_view data) {
    const auto len = data.length() + 1;

// byte * cast to keep VC++ happy
    if (buf->data[buf->cursize - 1])
        std::memcpy((byte *) SZGetSpace<void *>(buf, len), data.data(), len); // no trailing 0
    else
        std::memcpy((byte *) SZGetSpace<void *>(buf, len - 1) - 1, data.data(), len); // write over trailing 0
}


//============================================================================


/*
============
COM_SkipPath
============
*/
[[maybe_unused]] auto COM_SkipPath(char *pathname) -> char * {
    char *last;

    last = pathname;
    while (*pathname) {
        if (*pathname == '/')
            last = pathname + 1;
        pathname++;
    }
    return last;
}

/*
============
COM_StripExtension
============
*/
void COM_StripExtension(char *in, char *out) {
    while (*in && *in != '.')
        *out++ = *in++;
    *out = 0;
}

/*
============
COM_FileExtension
============
*/
auto COM_FileExtension(const char *in) -> const char * {
    static char exten[8];
    int i;

    while (*in && *in != '.')
        in++;
    if (!*in)
        return "";
    in++;
    for (i = 0; i < 7 && *in; i++, in++)
        exten[i] = *in;
    exten[i] = 0;
    return exten;
}

/*
==================
COM_DefaultExtension
==================
*/
void COM_DefaultExtension(std::string &path, std::string_view extension) {
//
// if path doesn't have a .EXT, append extension
// (extension should include the .)
//

    for (char c : path | std::views::reverse) {
        if (c == '/') {
            break;
        }
        if (c == '.') {
            return;
        }
    }

    path.append(extension);
}


/*
==============
COM_Parse

Parse a token out of a string
==============
*/
auto COM_Parse(std::string_view str) -> std::string_view {
    com_token[0] = 0;

    if (str.empty()) {
        return {};
    }

    enum class State {
        Initial,
        Comment,
        Quote,
        Word,
        End,
    };

    State state = State::Initial;
    std::size_t token_len = 0;

    bool reconsume = false;

    for (std::size_t i = 0; i < str.size() || reconsume; i++) {

        if (reconsume) {
            --i;
            reconsume = false;
        }

        const auto ch = str.at(i);

        switch (state) {
            case State::Initial:
                if (ch == 0) {
                    return {};
                }
                // skip whitespace
                if (ch <= ' ') {
                    continue;
                }

                if (ch == '/' && str[i + 1] == '/') {
                    state = State::Comment;
                    continue;
                }

                if (ch == '"') {
                    state = State::Quote;
                    continue;
                }

                if (ch == '{' || ch == '}' || ch == ')' || ch == '(' || ch == '\'' || ch == ':') {
                    com_token[token_len] = ch;
                    token_len++;
                    com_token[token_len] = 0;
                    return str.substr(i + 1);
                }

                state = State::Word;
                reconsume = true;
                break;

                case State::Comment:
                    if (ch == 0 || ch == '\n') {
                        state = State::Initial;
                    }
                    break;
                case State::Quote:
                  if (ch == '\"' && str[i - 1] == '\"'){
                    com_token[token_len] = 0;
                    com_token[token_len + 1] = 0;
                    return str.substr(i + 1);
                  }
                  if (ch == '\"' || ch == 0) {
                      com_token[token_len] = 0;
                      return str.substr(i + 1);
                  }
                  com_token[token_len] = ch;
                  token_len++;
                  break;
                case State::Word:
                    if (ch <= ' ') {
                        state = State::End;
                        break;
                    }

                    com_token[token_len] = ch;
                    token_len++;

                    if (ch == '{' || ch == '}' || ch == ')' || ch == '(' || ch == '\'' || ch == ':')
                        state = State::End;

                    break;
                case State::End:
                    com_token[token_len] = 0;
                    return str.substr(i);
                default:
                    break;
        }
    }

    com_token[token_len] = 0;
    return {};
}


/*
================
COM_CheckParm

Returns the position (1 to argc-1) in the program's argument list
where the given parameter apears, or 0 if not present
================
*/
auto COM_CheckParm(std::string_view parm) -> int {
    for (auto i = 1; i < com_argc; i++) {
        if (!com_argv[i])
            continue;               // NEXTSTEP sometimes clears appkit vars.
        if (!Q_strcmp(parm, com_argv[i]))
            return i;
    }

    return 0;
}

/*
================
COM_CheckRegistered

Looks for the pop.txt file and verifies it.
Sets the "registered" cvar.
Immediately exits out if an alternate game was attempted to be started without
being registered.
================
*/
void COM_CheckRegistered() {
    int h;
    unsigned short check[128];
    int i;

    COM_OpenFile("gfx/pop.lmp", &h);
    static_registered = false;

    if (h == -1) {
#if WINDED
        Sys_Error ("This dedicated server requires a full registered copy of Quake");
#endif
        Con_Printf("Playing shareware version.\n");
        if (com_modified)
            Sys_Error("You must have the registered version to use modified games");
        return;
    }

    Sys_FileRead(h, check, sizeof(check));
    COM_CloseFile(h);

    for (i = 0; i < 128; i++)
        if (pop[i] != (unsigned short) BigShort(check[i]))
            Sys_Error("Corrupted data file.");

    Cvar_Set("cmdline", com_cmdline);
    Cvar_Set("registered", "1");
    static_registered = true;
    Con_Printf("Playing registered version.\n");
}


void COM_Path_f();


/*
================
COM_InitArgv
================
*/
void COM_InitArgv(int argc, char **argv) {
    qboolean safe;
    int i, j, n;

// reconstitute the command line for the cmdline externally visible cvar
    n = 0;

    for (j = 0; (j < MAX_NUM_ARGVS) && (j < argc); j++) {
        i = 0;

        while ((n < (CMDLINE_LENGTH - 1)) && argv[j][i]) {
            com_cmdline[n++] = argv[j][i++];
        }

        if (n < (CMDLINE_LENGTH - 1))
            com_cmdline[n++] = ' ';
        else
            break;
    }

    com_cmdline[n] = 0;

    safe = false;

    for (com_argc = 0; (com_argc < MAX_NUM_ARGVS) && (com_argc < argc);
         com_argc++) {
        largv[com_argc] = argv[com_argc];
        if (!Q_strcmp("-safe", argv[com_argc]))
            safe = true;
    }

    if (safe) {
        // force all the safe-mode switches. Note that we reserved extra space in
        // case we need to add these, so we don't need an overflow check
        for (i = 0; i < NUM_SAFE_ARGVS; i++) {
            largv[com_argc] = safeargvs[i];
            com_argc++;
        }
    }

    largv[com_argc] = argvdummy;
    com_argv = largv;

    if (COM_CheckParm("-rogue")) {
        rogue = true;
        standard_quake = false;
    }

    if (COM_CheckParm("-hipnotic")) {
        hipnotic = true;
        standard_quake = false;
    }

    if (COM_CheckParm("-game")) {
        standard_quake = false;
    }
}


/*
================
COM_Init
================
*/
void COM_Init() {
// set the byte swapping variables in a portable manner
#ifdef SDL
    // This is necessary because egcs 1.1.1 mis-compiles swaptest with -O2
    if constexpr (SDL_BYTEORDER == SDL_LIL_ENDIAN)
#else
        if constexpr ( __LITTLE_ENDIAN__ )
#endif
    {
        bigendien = false;
        BigShort = ShortSwap;
        LittleShort = ShortNoSwap;
        BigLong = LongSwap;
        LittleLong = LongNoSwap;
        BigFloat = FloatSwap;
        LittleFloat = FloatNoSwap;
    } else {
        bigendien = true;
        BigShort = ShortNoSwap;
        LittleShort = ShortSwap;
        BigLong = LongNoSwap;
        LittleLong = LongSwap;
        BigFloat = FloatNoSwap;
        LittleFloat = FloatSwap;
    }

    Cvar_RegisterVariable(&registered);
    Cvar_RegisterVariable(&cmdline);
    Cmd_AddCommand("path", COM_Path_f);

    COM_InitFilesystem();
    COM_CheckRegistered();
}


/// just for debugging
auto memsearch(const byte *start, int count, int search) -> int {
    int i;

    for (i = 0; i < count; i++)
        if (start[i] == search)
            return i;
    return -1;
}

/*
=============================================================================

QUAKE FILESYSTEM

=============================================================================
*/

int com_filesize;


//
// in memory
//

using packfile_t = struct {
    char name[MAX_QPATH];
    int filepos, filelen;
};

using pack_t = struct pack_s {
    char filename[MAX_OSPATH];
    int handle;
    int numfiles;
    packfile_t *files;
};

//
// on disk
//
using dpackfile_t = struct {
    char name[56];
    int filepos, filelen;
};

using dpackheader_t = struct {
    char id[4];
    int dirofs;
    int dirlen;
};

#define MAX_FILES_IN_PACK       2048

char com_cachedir[MAX_OSPATH];
char com_gamedir[MAX_OSPATH];

using searchpath_t = struct searchpath_s {
    char filename[MAX_OSPATH];
    pack_t *pack;          // only one of filename / pack will be used
    struct searchpath_s *next;
};

searchpath_t *com_searchpaths;

/*
============
COM_Path_f

============
*/
void COM_Path_f() {
    searchpath_t *s;

    Con_Printf("Current search path:\n");
    for (s = com_searchpaths; s; s = s->next) {
        if (s->pack) {
            Con_Printf("%s (%i files)\n", s->pack->filename, s->pack->numfiles);
        } else
            Con_Printf("%s\n", s->filename);
    }
}

/*
============
COM_WriteFile

The filename will be prefixed by the current game directory
============
*/
void COM_WriteFile(char *filename, void *data, int len) {
    const auto name = fmt::sprintf("%s/%s", com_gamedir, filename);

    const auto handle = Sys_FileOpenWrite(name.c_str());
    if (handle == -1) {
        sysPrintf("COM_WriteFile: failed on {}\n", name);
        return;
    }

    sysPrintf("COM_WriteFile: %s\n", name);
    Sys_FileWrite(handle, data, len);
    Sys_FileClose(handle);
}


/*
============
COM_CreatePath

Only used for CopyFile
============
*/
void COM_CreatePath(char *path) {
    char *ofs;

    for (ofs = path + 1; *ofs; ofs++) {
        if (*ofs == '/') {       // create the directory
            *ofs = 0;
            Sys_mkdir(path);
            *ofs = '/';
        }
    }
}


/*
===========
COM_CopyFile

Copies a file over from the net to the local cache, creating any directories
needed.  This is for the convenience of developers using ISDN from home.
===========
*/
void COM_CopyFile(const char *netpath, char *cachepath) {
    int in, out;

    char buf[4096];

    auto remaining = Sys_FileOpenRead(netpath, &in);
    COM_CreatePath(cachepath);     // create directories up to the cache file
    out = Sys_FileOpenWrite(cachepath);

    for (int count = 0; remaining > 0; remaining -= count) {
        if (remaining < sizeof(buf))
            count = remaining;
        else
            count = sizeof(buf);
        Sys_FileRead(in, buf, count);
        Sys_FileWrite(out, buf, count);
    }

    Sys_FileClose(in);
    Sys_FileClose(out);
}

/*
===========
COM_FindFile

Finds the file in the search path.
Sets com_filesize and one of handle or file
===========
*/
auto COM_FindFile(std::string_view filename, int *handle, FILE **file) -> int {
    sysPrintf("PackFile: %s\n", filename);
    pack_t *pak;
    int i;
    int findtime, cachetime;

    if (file && handle)
        Sys_Error("COM_FindFile: both handle and file set");
    if (!file && !handle)
        Sys_Error("COM_FindFile: neither handle or file set");

//
// search through the path, one element at a time
//
    auto search = com_searchpaths;
    if (proghack) {    // gross hack to use quake 1 progs with quake 2 maps
        if (filename == "progs.dat")
            search = search->next;
    }

    for (; search; search = search->next) {
        // is the element a pak file?
        if (search->pack) {
            // look through all the pak file elements
            pak = search->pack;
            for (i = 0; i < pak->numfiles; i++)
                if (pak->files[i].name == filename) {       // found it!
                    sysPrintf("PackFile: %s : %s\n", pak->filename, filename);
                    if (handle) {
                        *handle = pak->handle;
                        Sys_FileSeek(pak->handle, pak->files[i].filepos);
                    } else {       // open a new file on the pakfile
                        *file = fopen(pak->filename, "rb");
                        if (*file)
                            fseek(*file, pak->files[i].filepos, SEEK_SET);
                    }
                    com_filesize = pak->files[i].filelen;
                    return com_filesize;
                }
        } else {
            // check a file in the directory tree
            if (!static_registered) {       // if not a registered version, don't ever go beyond base
                if (filename.find('/') != std::string::npos
                    || filename.find('\\') != std::string::npos)
                    continue;
            }

            auto netpath = fmt::sprintf("%s/%s", search->filename, filename);

            findtime = Sys_FileTime(netpath);
            if (findtime == -1)
                continue;

            // see if the file needs to be updated in the cache
            [&]() {
                if (!com_cachedir[0]) return;

                std::stringstream temp;
                if constexpr(!_UNIX) {
                    if (netpath.length() < 2 || (netpath[1] != ':')) {
                        temp << com_cachedir << netpath;
                    } else {
                        temp << com_cachedir << netpath.substr(2);
                    }
                } else {
                    temp << com_cachedir << '/' << netpath;
                }
                cachetime = Sys_FileTime(temp.view());
                if (cachetime < findtime)
                    COM_CopyFile(netpath.data(), temp.str().data());

            }();

            sysPrintf("FindFile: %s\n", netpath);
            com_filesize = Sys_FileOpenRead(netpath.c_str(), &i);
            if (handle)
                *handle = i;
            else {
                Sys_FileClose(i);
                *file = fopen(netpath.c_str(), "rb");
            }
            return com_filesize;
        }

    }

    sysPrintf("FindFile: can't find %s\n", filename);

    if (handle)
        *handle = -1;
    else
        *file = nullptr;
    com_filesize = -1;
    return -1;
}


/*
===========
COM_OpenFile

filename never has a leading slash, but may contain directory walks
returns a handle and a length
it may actually be inside a pak file
===========
*/
auto COM_OpenFile(std::string_view filename, int *handle) -> int {
    return COM_FindFile(filename, handle, nullptr);
}

/*
===========
COM_FOpenFile

If the requested file is inside a packfile, a new FILE * will be opened
into the file.
===========
*/
auto COM_FOpenFile(std::string_view filename, FILE **file) -> int {
    return COM_FindFile(filename, nullptr, file);
}

/*
============
COM_CloseFile

If it is a pak file handle, don't really close it
============
*/
void COM_CloseFile(int h) {
    searchpath_t *s;

    for (s = com_searchpaths; s; s = s->next)
        if (s->pack && s->pack->handle == h)
            return;

    Sys_FileClose(h);
}


/*
============
COM_LoadFile

Filename are reletive to the quake directory.
Allways appends a 0 byte.
============
*/
cache_user_t *loadcache;
byte *loadbuf;
int loadsize;

auto COM_LoadFile(std::string_view path, const int usehunk) -> byte * {
    int h;
    int len;

    byte *buf = nullptr;

// look for it in the filesystem or pack files
    len = COM_OpenFile(path, &h);
    if (h == -1)
        return nullptr;

// extract the filename base name for hunk tag
    const auto base = COM_FileBase(path);

    if (usehunk == 1)
        buf = hunkAllocName<decltype(buf)>(len + 1, base);
    else if (usehunk == 2)
        buf = hunkTempAlloc<decltype(buf)>(len + 1);
    else if (usehunk == 0)
        buf = zmalloc<decltype(buf)>(len + 1);
    else if (usehunk == 3)
        buf = cacheAlloc<decltype(buf)>(loadcache, len + 1, base);
    else if (usehunk == 4) {
        if (len + 1 > loadsize)
            buf = hunkTempAlloc<decltype(buf)>(len + 1);
        else
            buf = loadbuf;
    } else
        Sys_Error("COM_LoadFile: bad usehunk");

    if (!buf)
        Sys_Error("COM_LoadFile: not enough space for %s", path);

    ((byte *) buf)[len] = 0;

    Draw_BeginDisc();
    Sys_FileRead(h, buf, len);
    COM_CloseFile(h);
    Draw_EndDisc();

    return buf;
}

auto COM_LoadHunkFile(std::string_view path) -> byte * {
    return COM_LoadFile(path, 1);
}

auto COM_LoadTempFile(char *path) -> byte * {
    return COM_LoadFile(path, 2);
}

void COM_LoadCacheFile(std::string_view path, cache_user_s *cu) {
    loadcache = cu;
    COM_LoadFile(path, 3);
}

// uses temp hunk if larger than bufsize
auto COM_LoadStackFile(std::string_view path, void *buffer, int bufsize) -> byte * {
    byte *buf;

    loadbuf = (byte *) buffer;
    loadsize = bufsize;
    buf = COM_LoadFile(path, 4);

    return buf;
}

/*
=================
COM_LoadPackFile

Takes an explicit (not game tree related) path to a pak file.

Loads the header and directory, adding the files at the beginning
of the list so they override previous pack files.
=================
*/
auto COM_LoadPackFile(const char *packfile) -> pack_t * {
    dpackheader_t header;
    int i;
    packfile_t *newfiles;
    int numpackfiles;
    pack_t *pack;
    int packhandle;
    dpackfile_t info[MAX_FILES_IN_PACK];
    unsigned short crc;

    if (Sys_FileOpenRead(packfile, &packhandle) == -1) {
        Con_Printf("Couldn't open %s\n", packfile);
        return nullptr;
    }
    Sys_FileRead(packhandle, (void *) &header, sizeof(header));
    if (header.id[0] != 'P' || header.id[1] != 'A'
        || header.id[2] != 'C' || header.id[3] != 'K')
        Sys_Error("%s is not a packfile", packfile);
    header.dirofs = LittleLong(header.dirofs);
    header.dirlen = LittleLong(header.dirlen);

    numpackfiles = header.dirlen / sizeof(dpackfile_t);

    if (numpackfiles > MAX_FILES_IN_PACK)
        Sys_Error("%s has %i files", packfile, numpackfiles);

    if (numpackfiles != PAK0_COUNT)
        com_modified = true;    // not the original file

    newfiles = hunkAllocName<decltype(newfiles)>(numpackfiles * sizeof(packfile_t), "packfile");

    Sys_FileSeek(packhandle, header.dirofs);
    Sys_FileRead(packhandle, (void *) info, header.dirlen);

// crc the directory to check for modifications
    CRC_Init(&crc);
    for (i = 0; i < header.dirlen; i++)
        CRC_ProcessByte(&crc, ((byte *) info)[i]);
    if (crc != PAK0_CRC)
        com_modified = true;

// parse the directory
    for (i = 0; i < numpackfiles; i++) {
        strcpy(newfiles[i].name, info[i].name);
        newfiles[i].filepos = LittleLong(info[i].filepos);
        newfiles[i].filelen = LittleLong(info[i].filelen);
    }

    pack = hunkAlloc<decltype(pack)>(sizeof(pack_t));
    strcpy(pack->filename, packfile);
    pack->handle = packhandle;
    pack->numfiles = numpackfiles;
    pack->files = newfiles;

    Con_Printf("Added packfile %s (%i files)\n", packfile, numpackfiles);
    return pack;
}


/*
================
COM_AddGameDirectory

Sets com_gamedir, adds the directory to the head of the path,
then loads and adds pak1.pak pak2.pak ... 
================
*/
void COM_AddGameDirectory(std::string_view dir) {
    int i;
    searchpath_t *search;
    pack_t *pak;

    strcpy(com_gamedir, dir.data());

//
// add the directory to the search path
//
    search = hunkAlloc<decltype(search)>(sizeof(searchpath_t));
    strcpy(search->filename, dir.data());
    search->next = com_searchpaths;
    com_searchpaths = search;

//
// add any pak files in the format pak0.pak pak1.pak, ...
//
    for (i = 0;; i++) {
        auto pakfile = fmt::sprintf("%s/pak%i.pak", dir, i);
        pak = COM_LoadPackFile(pakfile.c_str());
        if (!pak)
            break;
        search = hunkAlloc<decltype(search)>(sizeof(searchpath_t));
        search->pack = pak;
        search->next = com_searchpaths;
        com_searchpaths = search;
    }

//
// add the contents of the parms.txt file to the end of the command line
//

}

/*
================
COM_InitFilesystem
================
*/
void COM_InitFilesystem() {
    int i, j;
    char basedir[MAX_OSPATH];
    searchpath_t *search;

//
// -basedir <path>
// Overrides the system supplied base directory (under GAMENAME)
//
    i = COM_CheckParm("-basedir");
    if (i && i < com_argc - 1)
        strcpy(basedir, com_argv[i + 1]);
    else
        strcpy(basedir, host_parms.basedir);

    j = strlen(basedir);

    if (j > 0) {
        if ((basedir[j - 1] == '\\') || (basedir[j - 1] == '/'))
            basedir[j - 1] = 0;
    }

//
// -cachedir <path>
// Overrides the system supplied cache directory (NULL or /qcache)
// -cachedir - will disable caching.
//
    i = COM_CheckParm("-cachedir");
    if (i && i < com_argc - 1) {
        if (com_argv[i + 1][0] == '-')
            com_cachedir[0] = 0;
        else
            strcpy(com_cachedir, com_argv[i + 1]);
    } else if (host_parms.cachedir)
        strcpy(com_cachedir, host_parms.cachedir);
    else
        com_cachedir[0] = 0;

//
// start up with GAMENAME by default (id1)
//
    COM_AddGameDirectory(va("%s/" GAMENAME, basedir));

    if (COM_CheckParm("-rogue"))
        COM_AddGameDirectory(va("%s/rogue", basedir));
    if (COM_CheckParm("-hipnotic"))
        COM_AddGameDirectory(va("%s/hipnotic", basedir));

//
// -game <gamedir>
// Adds basedir/gamedir as an override game
//
    i = COM_CheckParm("-game");
    if (i && i < com_argc - 1) {
        com_modified = true;
        COM_AddGameDirectory(va("%s/%s", basedir, com_argv[i + 1]));
    }

//
// -path <dir or packfile> [<dir or packfile>] ...
// Fully specifies the exact serach path, overriding the generated one
//
    i = COM_CheckParm("-path");
    if (i) {
        com_modified = true;
        com_searchpaths = nullptr;
        while (++i < com_argc) {
            std::string_view currentArg = com_argv[i];
            if (currentArg.empty() || currentArg[0] == '+' || currentArg[0] == '-')
                break;

            search = hunkAlloc<decltype(search)>(sizeof(searchpath_t));
            if (currentArg.ends_with(".pak")) {
                search->pack = COM_LoadPackFile(com_argv[i]);
                if (!search->pack)
                    Sys_Error("Couldn't load packfile: %s", com_argv[i]);
            } else
                strncpy(search->filename, com_argv[i], currentArg.length());
            search->next = com_searchpaths;
            com_searchpaths = search;
        }
    }

    if (COM_CheckParm("-proghack"))
        proghack = true;
}


