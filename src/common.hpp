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

// comndef.h  -- general definitions
#include <string_view>
#include "mathlib.hpp"
#include <fmt/printf.h>


#if !defined BYTE_DEFINED
typedef unsigned char byte;
#define BYTE_DEFINED 1
#endif

using qboolean = bool;

//============================================================================

typedef struct sizebuf_s {
    qboolean allowoverflow;    // if false, do a Sys_Error
    qboolean overflowed;        // set to true if the buffer size failed
    byte *data;
    int maxsize;
    int cursize;
} sizebuf_t;

void SZ_Alloc(sizebuf_t *buf, int startsize);

void SZ_Free(sizebuf_t *buf);

void SZ_Clear(sizebuf_t *buf);

//void *SZ_GetSpace (sizebuf_t *buf, int length);
void SZ_Write(sizebuf_t *buf, const void *data, std::size_t length);

void SZ_Print(sizebuf_t *buf, std::string_view data);    // strcats onto the sizebuf

//============================================================================

typedef struct link_s {
    struct link_s *prev, *next;
} link_t;


void ClearLink(link_t *l);

void RemoveLink(link_t *l);

void InsertLinkBefore(link_t *l, link_t *before);

void InsertLinkAfter(link_t *l, link_t *after);

// (type *)STRUCT_FROM_LINK(link_t *link, type, member)
// ent = STRUCT_FROM_LINK(link,entity_t,order)
// FIXME: remove this mess!
#define    STRUCT_FROM_LINK(l, t, m) ((t *)((byte *)l - (long)&(((t *)0)->m)))

//============================================================================

#ifndef NULL
#define NULL ((void *)0)
#endif

#define Q_MAXCHAR ((char)0x7f)
#define Q_MAXSHORT ((short)0x7fff)
#define Q_MAXINT    ((int)0x7fffffff)
#define Q_MAXLONG ((int)0x7fffffff)
#define Q_MAXFLOAT ((int)0x7fffffff)

#define Q_MINCHAR ((char)0x80)
#define Q_MINSHORT ((short)0x8000)
#define Q_MININT    ((int)0x80000000)
#define Q_MINLONG ((int)0x80000000)
#define Q_MINFLOAT ((int)0x7fffffff)

//============================================================================

extern qboolean bigendien;

extern short (*BigShort)(short l);

extern short (*LittleShort)(short l);

extern int (*BigLong)(int l);

extern int (*LittleLong)(int l);

extern float (*BigFloat)(float l);

extern float (*LittleFloat)(float l);

//============================================================================

void MSG_WriteChar(sizebuf_t *sb, int c);

void MSG_WriteByte(sizebuf_t *sb, int c);

void MSG_WriteShort(sizebuf_t *sb, short c);

void MSG_WriteLong(sizebuf_t *sb, int c);

void MSG_WriteFloat(sizebuf_t *sb, float f);

void MSG_WriteString(sizebuf_t *sb, std::string_view s);

void MSG_WriteCoord(sizebuf_t *sb, float f);
void MSG_WriteCoords(sizebuf_t *sb, vec3 coords);

void MSG_WriteAngle(sizebuf_t *sb, float f);
void MSG_WriteAngles(sizebuf_t *sb, vec3 angles);

extern int msg_readcount;
extern qboolean msg_badread;        // set if a read goes beyond end of message

void MSG_BeginReading();

int MSG_ReadChar();

int MSG_ReadByte();

int MSG_ReadShort();

int MSG_ReadLong();

float MSG_ReadFloat();

char *MSG_ReadString();

float MSG_ReadCoord();

float MSG_ReadAngle();

//============================================================================

void Q_memset(void *dest, int fill, int count);

void Q_memcpy(void *dest, const void *src, int count);

int Q_memcmp(void *m1, void *m2, int count);

void Q_strcpy(char *dest, const char *src);

void Q_strncpy(char *dest, std::string_view src, int count);

int Q_strlen(const char *str);

char *Q_strrchr(char *s, char c);

void Q_strcat(char *dest, char *src);

bool Q_strcmp(std::string_view s1, std::string_view s2);

//int Q_strcmp (char *s1, char *s2);
bool Q_strncmp(std::string_view s1, std::string_view s2, std::size_t count);

//int Q_strncmp (char *s1, char *s2, int count);
int Q_strcasecmp(std::string_view s1, std::string_view s2);

int Q_strncasecmp(std::string_view s1, std::string_view s2, int n);

int Q_atoi(std::string_view str);

float Q_atof(std::string_view str);

//============================================================================

extern char com_token[1024];
extern qboolean com_eof;

std::string_view COM_Parse(std::string_view data);


extern int com_argc;
extern const char **com_argv;

int COM_CheckParm(std::string_view parm);

void COM_Init();

void COM_InitArgv(int argc, char **argv);

[[maybe_unused]] char *COM_SkipPath(char *pathname);

[[maybe_unused]] void COM_StripExtension(char *in, char *out);

constexpr std::string_view COM_FileBase(std::string_view in) {
    auto ext_period = in.find_last_of('.');
    auto filename_start = in.find_last_of('/');
    filename_start = filename_start == std::string::npos ? 0UL : filename_start + 1; // + 1 to skip /

    if (ext_period - filename_start < 2)
        return "?model?";

    return in.substr(filename_start, ext_period - filename_start);
}

void COM_DefaultExtension(std::string &path, std::string_view extension);

/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
============
*/
template<typename S, typename... Args>
std::string va(const S &fmt, Args &&... args) {
    return fmt::sprintf(fmt, std::forward<Args>(args)...);
}
// does a varargs printf into a temp buffer


//============================================================================

extern int com_filesize;
struct cache_user_s;

extern char com_gamedir[MAX_OSPATH];

void COM_WriteFile(char *filename, void *data, int len);

int COM_OpenFile(std::string_view filename, int *handle);

int COM_FOpenFile(std::string_view filename, FILE **file);

void COM_CloseFile(int h);

byte *COM_LoadStackFile(std::string_view path, void *buffer, int bufsize);

byte *COM_LoadTempFile(char *path);

byte *COM_LoadHunkFile(std::string_view path);

void COM_LoadCacheFile(std::string_view path, cache_user_s *cu);


extern struct cvar_s registered;

extern qboolean standard_quake, rogue, hipnotic;

#include "sys.hpp"
#include "console.hpp"

template<typename MemType>
MemType SZGetSpace(sizebuf_t *buf, int length) {
    if (buf->cursize + length > buf->maxsize) {
        if (!buf->allowoverflow)
            Sys_Error("SZ_GetSpace: overflow without allowoverflow set");

        if (length > buf->maxsize)
            Sys_Error("SZ_GetSpace: %i is > full buffer size", length);

        buf->overflowed = true;
        Con_Printf("SZ_GetSpace: overflow");
        SZ_Clear(buf);
    }

    MemType data = buf->data + buf->cursize;
    buf->cursize += length;

    return data;
}

