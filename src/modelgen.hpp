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
//
// modelgen.h: header file for model generation program
//

// *********************************************************
// * This file must be identical in the modelgen directory *
// * and in the Quake directory, because it's used to      *
// * pass data from one to the other via model files.      *
// *********************************************************

#ifdef INCLUDELIBS

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "cmdlib.hpp"
#include "scriplib.hpp"
#include "trilib.hpp"
#include "lbmlib.hpp"
#include "mathlib.hpp"

#endif

#define ALIAS_VERSION    6

#define ALIAS_ONSEAM                0x0020

// must match definition in spritegn.h
#ifndef SYNCTYPE_T
#define SYNCTYPE_T
enum synctype_t {
    ST_SYNC = 0, ST_RAND
};
#endif

enum aliasframetype_t {
    ALIAS_SINGLE = 0, ALIAS_GROUP
};

enum aliasskintype_t {
    ALIAS_SKIN_SINGLE = 0, ALIAS_SKIN_GROUP
};

struct mdl_t {
    int ident;
    int version;
    vec3 scale;
    vec3 scale_origin;
    float boundingradius;
    vec3 eyeposition;
    int numskins;
    int skinwidth;
    int skinheight;
    int numverts;
    int numtris;
    int numframes;
    synctype_t synctype;
    int flags;
    float size;
};

// TODO: could be shorts

struct stvert_t {
    int onseam;
    int s;
    int t;
};

typedef struct dtriangle_s {
    int facesfront;
    int vertindex[3];
} dtriangle_t;

#define DT_FACES_FRONT                0x0010

// This mirrors trivert_t in trilib.h, is present so Quake knows how to
// load this data

typedef struct {
    byte v[3];
    byte lightnormalindex;
} trivertx_t;

typedef struct {
    trivertx_t bboxmin;    // lightnormal isn't used
    trivertx_t bboxmax;    // lightnormal isn't used
    char name[16];    // frame name from grabbing
} daliasframe_t;

typedef struct {
    int numframes;
    trivertx_t bboxmin;    // lightnormal isn't used
    trivertx_t bboxmax;    // lightnormal isn't used
} daliasgroup_t;

typedef struct {
    int numskins;
} daliasskingroup_t;

typedef struct {
    float interval;
} daliasinterval_t;

typedef struct {
    float interval;
} daliasskininterval_t;

typedef struct {
    aliasframetype_t type;
} daliasframetype_t;

typedef struct {
    aliasskintype_t type;
} daliasskintype_t;

#define IDPOLYHEADER    (('O'<<24)+('P'<<16)+('D'<<8)+'I')
// little-endian "IDPO"

