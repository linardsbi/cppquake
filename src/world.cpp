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
// world.c -- world query functions

#include <cmath>
#include <memory>
#include "quakedef.hpp"

/*

entities never clip against themselves, or their owner

line of sight checks trace->crosscontent, but bullets don't

*/


using moveclip_t = struct {
    vec3 boxmins, boxmaxs;// enclose the test object along entire move
    vec3 mins, maxs;    // size of the moving object
    vec3 mins2, maxs2;    // size when clipping against mosnters
    vec3 start, end;
    trace_t trace;
    int type;
    edict_t *passedict;
};


auto SV_HullPointContents(hull_t *hull, int num, vec3 p) -> int;

/*
===============================================================================

HULL BOXES

===============================================================================
*/


static hull_t box_hull;
static dclipnode_t box_clipnodes[6];
static mplane_t box_planes[6];

/*
===================
SV_InitBoxHull

Set up the planes and clipnodes so that the six floats of a bounding box
can just be stored out and get a proper hull_t structure.
===================
*/
void SV_InitBoxHull() {
    int i = 0;
    int side = 0;

    box_hull.clipnodes = box_clipnodes;
    box_hull.planes = box_planes;
    box_hull.firstclipnode = 0;
    box_hull.lastclipnode = 5;

    for (i = 0; i < 6; i++) {
        box_clipnodes[i].planenum = i;

        side = i & 1;

        box_clipnodes[i].children[side] = CONTENTS_EMPTY;
        if (i != 5)
            box_clipnodes[i].children[side ^ 1] = i + 1;
        else
            box_clipnodes[i].children[side ^ 1] = CONTENTS_SOLID;

        box_planes[i].type = i >> 1;
        box_planes[i].normal[i >> 1] = 1;
    }

}


/*
===================
SV_HullForBox

To keep everything totally uniform, bounding boxes are turned into small
BSP trees instead of being compared directly.
===================
*/
inline auto SV_HullForBox(vec3 mins, vec3 maxs) -> hull_t * {
    box_planes[0].dist = maxs[0];
    box_planes[1].dist = mins[0];
    box_planes[2].dist = maxs[1];
    box_planes[3].dist = mins[1];
    box_planes[4].dist = maxs[2];
    box_planes[5].dist = mins[2];

    return &box_hull;
}


/*
================
SV_HullForEntity

Returns a hull that can be used for testing or clipping an object of mins/maxs
size.
Offset is filled in to contain the adjustment that must be added to the
testing object's origin to get a point to use with the returned hull.
================
*/
auto SV_HullForEntity(edict_t *ent, vec3 mins, vec3 maxs, vec3 &offset) -> hull_t * {
    model_t *model = nullptr;
    vec3 size;
    vec3 hullmins, hullmaxs;
    hull_t *hull = nullptr;

// decide which clipping hull to use, based on the size
    if (ent->v.solid == SOLID_BSP) {    // explicit hulls in the BSP model
        if (ent->v.movetype != MOVETYPE_PUSH)
            Sys_Error("SOLID_BSP without MOVETYPE_PUSH");

        model = sv.models[(int) ent->v.modelindex];

        if (!model || model->type != mod_brush)
            Sys_Error("MOVETYPE_PUSH with a non bsp model");

        size = maxs - mins;
        if (size[0] < 3)
            hull = &model->hulls[0];
        else if (size[0] <= 32)
            hull = &model->hulls[1];
        else
            hull = &model->hulls[2];

// calculate an offset value to center the origin
        offset = hull->clip_mins - mins;
        offset = offset + ent->v.origin;
    } else {    // create a temp hull from bounding box sizes

        hullmins = ent->v.mins - maxs;
        hullmaxs = ent->v.maxs - mins;
        hull = SV_HullForBox(hullmins, hullmaxs);

        offset = ent->v.origin;
    }


    return hull;
}

/*
===============================================================================

ENTITY AREA CHECKING

===============================================================================
*/

using areanode_t = struct areanode_s {
    int axis;        // -1 = leaf node
    float dist;
    struct areanode_s *children[2];
    link_t trigger_edicts;
    link_t solid_edicts;
};

#define    AREA_DEPTH    4
#define    AREA_NODES    32

static areanode_t sv_areanodes[AREA_NODES];
static int sv_numareanodes;

/*
===============
SV_CreateAreaNode

===============
*/
auto SV_CreateAreaNode(int depth, vec3 mins, vec3 maxs) -> areanode_t * {
    areanode_t *anode = nullptr;
    vec3 size;
    vec3 mins1, maxs1, mins2, maxs2;

    anode = &sv_areanodes[sv_numareanodes];
    sv_numareanodes++;

    ClearLink(&anode->trigger_edicts);
    ClearLink(&anode->solid_edicts);

    if (depth == AREA_DEPTH) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = nullptr;
        return anode;
    }

    size = maxs - mins;
    if (size[0] > size[1])
        anode->axis = 0;
    else
        anode->axis = 1;

    anode->dist = 0.5 * (maxs[anode->axis] + mins[anode->axis]);
    mins1 = mins;
    mins2 = mins;
    maxs1 = maxs;
    maxs2 = maxs;

    maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

    anode->children[0] = SV_CreateAreaNode(depth + 1, mins2, maxs2);
    anode->children[1] = SV_CreateAreaNode(depth + 1, mins1, maxs1);

    return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld() {
    SV_InitBoxHull();

    memset(sv_areanodes, 0, sizeof(sv_areanodes));
    sv_numareanodes = 0;
    SV_CreateAreaNode(0, sv.worldmodel->mins, sv.worldmodel->maxs);
}


/*
===============
SV_UnlinkEdict

===============
*/
void SV_UnlinkEdict(edict_t *ent) {
    if (!ent->area.prev)
        return;        // not linked in anywhere
    RemoveLink(&ent->area);
    ent->area.prev = ent->area.next = nullptr;
}


/*
====================
SV_TouchLinks
====================
*/
void SV_TouchLinks(const edict_t *ent, const areanode_t *node) {
// touch linked edicts
    for (link_t *l = node->trigger_edicts.next, *next = l->next;
         l != &node->trigger_edicts;
         l = next, next = l->next) {
        auto touch = (EDICT_FROM_AREA(l));

        if (touch == ent)
            continue;
        if (!touch->v.touch || touch->v.solid != SOLID_TRIGGER)
            continue;
        if (ent->v.absmin[0] > touch->v.absmax[0]
            || ent->v.absmin[1] > touch->v.absmax[1]
            || ent->v.absmin[2] > touch->v.absmax[2]
            || ent->v.absmax[0] < touch->v.absmin[0]
            || ent->v.absmax[1] < touch->v.absmin[1]
            || ent->v.absmax[2] < touch->v.absmin[2])
            continue;

        const int old_self = pr_global_struct->self;
        const int old_other = pr_global_struct->other;

        pr_global_struct->self = EDICT_TO_PROG(touch);
        pr_global_struct->other = EDICT_TO_PROG(ent);
        pr_global_struct->time = sv.time;
        PR_ExecuteProgram(touch->v.touch);

        pr_global_struct->self = old_self;
        pr_global_struct->other = old_other;
    }

// recurse down both sides
    if (node->axis == -1)
        return;

    if (ent->v.absmax[node->axis] > node->dist)
        SV_TouchLinks(ent, node->children[0]);
    if (ent->v.absmin[node->axis] < node->dist)
        SV_TouchLinks(ent, node->children[1]);
}


/*
===============
SV_FindTouchedLeafs

===============
*/
void SV_FindTouchedLeafs(edict_t *ent, mnode_t *node) {
    mplane_t *splitplane = nullptr;
    mleaf_t *leaf = nullptr;
    int sides = 0;
    int leafnum = 0;

    if (node->contents == CONTENTS_SOLID)
        return;

// add an efrag if the node is a leaf

    if (node->contents < 0) {
        if (ent->num_leafs == MAX_ENT_LEAFS)
            return;

        leaf = (mleaf_t *) node;
        leafnum = leaf - sv.worldmodel->leafs - 1;

        ent->leafnums[ent->num_leafs] = leafnum;
        ent->num_leafs++;
        return;
    }

// NODE_MIXED

    splitplane = node->plane;
    sides = BOX_ON_PLANE_SIDE(ent->v.absmin, ent->v.absmax, splitplane);

// recurse down the contacted sides
    if (sides & 1)
        SV_FindTouchedLeafs(ent, node->children[0]);

    if (sides & 2)
        SV_FindTouchedLeafs(ent, node->children[1]);
}

/*
===============
SV_LinkEdict

===============
*/
void SV_LinkEdict(edict_t *ent, qboolean touch_triggers) {
    areanode_t *node = nullptr;

    if (ent->area.prev)
        SV_UnlinkEdict(ent);    // unlink from old position

    if (ent == sv.edicts)
        return;        // don't add the world

    if (ent->free)
        return;

// set the abs box

#ifdef QUAKE2
    if (ent->v.solid == SOLID_BSP &&
    (ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) )
    {	// expand for rotation
        float		max, v;
        int			i;

        max = 0;
        for (i=0 ; i<3 ; i++)
        {
            v =fabs( ent->v.mins[i]);
            if (v > max)
                max = v;
            v =fabs( ent->v.maxs[i]);
            if (v > max)
                max = v;
        }
        for (i=0 ; i<3 ; i++)
        {
            ent->v.absmin[i] = ent->v.origin[i] - max;
            ent->v.absmax[i] = ent->v.origin[i] + max;
        }
    }
    else
#endif
    {
        ent->v.absmin = ent->v.origin + ent->v.mins;
        ent->v.absmax = ent->v.origin + ent->v.maxs;
    }

//
// to make items easier to pick up and allow them to be grabbed off
// of shelves, the abs sizes are expanded
//
    if ((int) ent->v.flags & FL_ITEM) {
        ent->v.absmin[0] -= 15;
        ent->v.absmin[1] -= 15;
        ent->v.absmax[0] += 15;
        ent->v.absmax[1] += 15;
    } else {    // because movement is clipped an epsilon away from an actual edge,
        // we must fully check even when bounding boxes don't quite touch
        ent->v.absmin[0] -= 1;
        ent->v.absmin[1] -= 1;
        ent->v.absmin[2] -= 1;
        ent->v.absmax[0] += 1;
        ent->v.absmax[1] += 1;
        ent->v.absmax[2] += 1;
    }

// link to PVS leafs
    ent->num_leafs = 0;
    if (ent->v.modelindex)
        SV_FindTouchedLeafs(ent, sv.worldmodel->nodes);

    if (ent->v.solid == SOLID_NOT)
        return;

// find the first node that the ent's box crosses
    node = sv_areanodes;
    while (true) {
        if (node->axis == -1)
            break;
        if (ent->v.absmin[node->axis] > node->dist)
            node = node->children[0];
        else if (ent->v.absmax[node->axis] < node->dist)
            node = node->children[1];
        else
            break;        // crosses the node
    }

// link it in	

    if (ent->v.solid == SOLID_TRIGGER)
        InsertLinkBefore(&ent->area, &node->trigger_edicts);
    else
        InsertLinkBefore(&ent->area, &node->solid_edicts);

// if touch_triggers, touch all entities at this node and decend for more
    if (touch_triggers)
        SV_TouchLinks(ent, sv_areanodes);
}



/*
===============================================================================

POINT TESTING IN HULLS

===============================================================================
*/

#if    !id386

/*
==================
SV_HullPointContents

==================
*/
auto SV_HullPointContents(hull_t *hull, int num, vec3 p) -> int {
    float d = NAN;
    dclipnode_t *node = nullptr;
    mplane_t *plane = nullptr;

    while (num >= 0) {
        if (num < hull->firstclipnode || num > hull->lastclipnode)
            Sys_Error("SV_HullPointContents: bad node number");

        node = hull->clipnodes + num;
        plane = hull->planes + node->planenum;

        if (plane->type < 3)
            d = p[plane->type] - plane->dist;
        else
            d = glm::dot (plane->normal, p) - plane->dist;
        if (d < 0)
            num = node->children[1];
        else
            num = node->children[0];
    }

    return num;
}

#endif    // !id386


/*
==================
SV_PointContents

==================
*/
auto SV_PointContents(vec3 p) -> int {
    int cont = 0;

    cont = SV_HullPointContents(&sv.worldmodel->hulls[0], 0, p);
    if (cont <= CONTENTS_CURRENT_0 && cont >= CONTENTS_CURRENT_DOWN)
        cont = CONTENTS_WATER;
    return cont;
}

auto SV_TruePointContents(vec3 p) -> int {
    return SV_HullPointContents(&sv.worldmodel->hulls[0], 0, p);
}

//===========================================================================

/*
============
SV_TestEntityPosition

This could be a lot more efficient...
============
*/
auto SV_TestEntityPosition(edict_t *ent) -> edict_t * {
    trace_t trace;

    trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, ent->v.origin, 0, ent);

    if (trace.startsolid)
        return sv.edicts;

    return nullptr;
}


/*
===============================================================================

LINE TESTING IN HULLS

===============================================================================
*/

// 1/32 epsilon to keep floating point happy
#define    DIST_EPSILON    (0.03125)

/*
==================
SV_RecursiveHullCheck

==================
*/
auto
SV_RecursiveHullCheck(hull_t *hull, int num, float p1f, float p2f, vec3 p1, vec3 p2, trace_t *trace) -> qboolean {
    dclipnode_t *node = nullptr;
    mplane_t *plane = nullptr;
    float t1 = NAN, t2 = NAN;
    float frac = NAN;
    int i = 0;
    vec3 mid;
    int side = 0;
    float midf = NAN;

// check for empty
    if (num < 0) {
        if (num != CONTENTS_SOLID) {
            trace->allsolid = false;
            if (num == CONTENTS_EMPTY)
                trace->inopen = true;
            else
                trace->inwater = true;
        } else
            trace->startsolid = true;
        return true;        // empty
    }

    if (num < hull->firstclipnode || num > hull->lastclipnode)
        Sys_Error("SV_RecursiveHullCheck: bad node number");

//
// find the point distances
//
    node = hull->clipnodes + num;
    plane = hull->planes + node->planenum;

    if (plane->type < 3) {
        t1 = p1[plane->type] - plane->dist;
        t2 = p2[plane->type] - plane->dist;
    } else {
        t1 = glm::dot (plane->normal, p1) - plane->dist;
        t2 = glm::dot (plane->normal, p2) - plane->dist;
    }

#if 1
    if (t1 >= 0 && t2 >= 0)
        return SV_RecursiveHullCheck(hull, node->children[0], p1f, p2f, p1, p2, trace);
    if (t1 < 0 && t2 < 0)
        return SV_RecursiveHullCheck(hull, node->children[1], p1f, p2f, p1, p2, trace);
#else
    if ( (t1 >= DIST_EPSILON && t2 >= DIST_EPSILON) || (t2 > t1 && t1 >= 0) )
        return SV_RecursiveHullCheck (hull, node->children[0], p1f, p2f, p1, p2, trace);
    if ( (t1 <= -DIST_EPSILON && t2 <= -DIST_EPSILON) || (t2 < t1 && t1 <= 0) )
        return SV_RecursiveHullCheck (hull, node->children[1], p1f, p2f, p1, p2, trace);
#endif

// put the crosspoint DIST_EPSILON pixels on the near side
    if (t1 < 0)
        frac = (t1 + DIST_EPSILON) / (t1 - t2);
    else
        frac = (t1 - DIST_EPSILON) / (t1 - t2);
    if (frac < 0)
        frac = 0;
    if (frac > 1)
        frac = 1;

    midf = p1f + (p2f - p1f) * frac;
    mid = p1 + frac * (p2 - p1);

    side = (t1 < 0);

// move up to the node
    if (!SV_RecursiveHullCheck(hull, node->children[side], p1f, midf, p1, mid, trace))
        return false;

#ifdef PARANOID
    if (SV_HullPointContents(hull, node->children[side], mid)
        == CONTENTS_SOLID) {
//        Con_Printf("mid PointInHullSolid\n");
        return false;
    }
#endif

    if (SV_HullPointContents(hull, node->children[side ^ 1], mid)
        != CONTENTS_SOLID)
// go past the node
        return SV_RecursiveHullCheck(hull, node->children[side ^ 1], midf, p2f, mid, p2, trace);

    if (trace->allsolid)
        return false;        // never got out of the solid area

//==================
// the other side of the node is solid, this is the impact point
//==================
    if (!side) {
        trace->plane.normal = plane->normal;
        trace->plane.dist = plane->dist;
    } else {
        trace->plane.normal = vec3_origin - plane->normal;
        trace->plane.dist = -plane->dist;
    }

    while (SV_HullPointContents(hull, hull->firstclipnode, mid)
           == CONTENTS_SOLID) { // shouldn't really happen, but does occasionally
        frac -= 0.1;
        if (frac < 0) {
            trace->fraction = midf;
            trace->endpos = mid;
            Con_DPrintf("backup past 0\n");
            return false;
        }
        midf = p1f + (p2f - p1f) * frac;
        mid = p1 + frac * (p2 - p1);
    }

    trace->fraction = midf;
    trace->endpos = mid;

    return false;
}


/*
==================
SV_ClipMoveToEntity

Handles selection or creation of a clipping hull, and offseting (and
eventually rotation) of the end points
==================
*/
auto SV_ClipMoveToEntity(edict_t *ent, vec3 &start, vec3 mins, vec3 maxs, vec3 &end) -> trace_t {
    trace_t trace;
    vec3 offset;
    vec3 start_l, end_l;
    hull_t *hull = nullptr;

// fill in a default trace
    memset(&trace, 0, sizeof(trace_t));
    trace.fraction = 1;
    trace.allsolid = true;
    trace.endpos = end;

// get the clipping hull
    hull = SV_HullForEntity(ent, mins, maxs, offset);

    start_l = start - offset;
    end_l = end - offset;

#ifdef QUAKE2
    // rotate start and end into the models frame of reference
    if (ent->v.solid == SOLID_BSP &&
    (ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) )
    {
        vec3	a;
        vec3	forward, right, up;
        vec3	temp;

        AngleVectors (ent->v.angles, forward, right, up);

        temp = start_l;
        start_l[0] = glm::dot (temp, forward);
        start_l[1] = -glm::dot (temp, right);
        start_l[2] = glm::dot (temp, up);

        temp = end_l;
        end_l[0] = glm::dot (temp, forward);
        end_l[1] = -glm::dot (temp, right);
        end_l[2] = glm::dot (temp, up);
    }
#endif

// trace a line through the apropriate clipping hull
    SV_RecursiveHullCheck(hull, hull->firstclipnode, 0, 1, start_l, end_l, &trace);

#ifdef QUAKE2
    // rotate endpos back to world frame of reference
    if (ent->v.solid == SOLID_BSP &&
    (ent->v.angles[0] || ent->v.angles[1] || ent->v.angles[2]) )
    {
        vec3	a;
        vec3	forward, right, up;
        vec3	temp;

        if (trace.fraction != 1)
        {
            a = vec3_origin - ent->v.angles;
            AngleVectors (a, forward, right, up);

            temp = trace.endpos;
            trace.endpos[0] = glm::dot (temp, forward);
            trace.endpos[1] = -glm::dot (temp, right);
            trace.endpos[2] = glm::dot (temp, up);

            temp = trace.plane.normal;
            trace.plane.normal[0] = glm::dot (temp, forward);
            trace.plane.normal[1] = -glm::dot (temp, right);
            trace.plane.normal[2] = glm::dot (temp, up);
        }
    }
#endif

// fix trace up by the offset
    if (trace.fraction != 1) trace.endpos = trace.endpos + offset;

// did we clip the move?
    if (trace.fraction < 1 || trace.startsolid)
        trace.ent = ent;

    return trace;
}

//===========================================================================

/*
====================
SV_ClipToLinks

Mins and maxs enclose the entire area swept by the move
====================
*/
void SV_ClipToLinks(areanode_t *node, moveclip_t *clip) {
    link_t *l = nullptr, *next = nullptr;
    edict_t *touch = nullptr;
    trace_t trace;

// touch linked edicts
    for (l = node->solid_edicts.next; l != &node->solid_edicts; l = next) {
        next = l->next;
        touch = EDICT_FROM_AREA(l);
        if (touch->v.solid == SOLID_NOT)
            continue;
        if (touch == clip->passedict)
            continue;
        if (touch->v.solid == SOLID_TRIGGER)
            Sys_Error("Trigger in clipping list");

        if (clip->type == MOVE_NOMONSTERS && touch->v.solid != SOLID_BSP)
            continue;

        if (clip->boxmins[0] > touch->v.absmax[0]
            || clip->boxmins[1] > touch->v.absmax[1]
            || clip->boxmins[2] > touch->v.absmax[2]
            || clip->boxmaxs[0] < touch->v.absmin[0]
            || clip->boxmaxs[1] < touch->v.absmin[1]
            || clip->boxmaxs[2] < touch->v.absmin[2])
            continue;

        if (clip->passedict && clip->passedict->v.size[0] && !touch->v.size[0])
            continue;    // points never interact

        // might intersect, so do an exact clip
        if (clip->trace.allsolid)
            return;
        if (clip->passedict) {
            if (PROG_TO_EDICT(touch->v.owner) == clip->passedict)
                continue;    // don't clip against own missiles
            if (PROG_TO_EDICT(clip->passedict->v.owner) == touch)
                continue;    // don't clip against owner
        }

        if ((int) touch->v.flags & FL_MONSTER)
            trace = SV_ClipMoveToEntity(touch, clip->start, clip->mins2, clip->maxs2, clip->end);
        else
            trace = SV_ClipMoveToEntity(touch, clip->start, clip->mins, clip->maxs, clip->end);
        if (trace.allsolid || trace.startsolid ||
            trace.fraction < clip->trace.fraction) {
            trace.ent = touch;
            if (clip->trace.startsolid) {
                clip->trace = trace;
                clip->trace.startsolid = true;
            } else
                clip->trace = trace;
        } else if (trace.startsolid)
            clip->trace.startsolid = true;
    }

// recurse down both sides
    if (node->axis == -1)
        return;

    if (clip->boxmaxs[node->axis] > node->dist)
        SV_ClipToLinks(node->children[0], clip);
    if (clip->boxmins[node->axis] < node->dist)
        SV_ClipToLinks(node->children[1], clip);
}


/*
==================
SV_MoveBounds
==================
*/
inline void SV_MoveBounds(vec3 start, vec3 mins, vec3 maxs, vec3 end, vec3 &boxmins, vec3 &boxmaxs) {
#if 0
    // debug to test against everything
    boxmins[0] = boxmins[1] = boxmins[2] = -9999;
    boxmaxs[0] = boxmaxs[1] = boxmaxs[2] = 9999;
#else
    for (int i = 0; i < 3; i++) {
        if (end[i] > start[i]) {
            boxmins[i] = start[i] + mins[i] - 1;
            boxmaxs[i] = end[i] + maxs[i] + 1;
        } else {
            boxmins[i] = end[i] + mins[i] - 1;
            boxmaxs[i] = start[i] + maxs[i] + 1;
        }
    }
#endif
}

/*
==================
SV_Move
==================
*/
auto SV_Move(vec3 &start, vec3 mins, vec3 maxs, vec3 &end, int type, edict_t *passedict) -> trace_t {
    moveclip_t clip{};

// clip to world
    clip.trace = SV_ClipMoveToEntity(sv.edicts, start, mins, maxs, end);

    clip.start = start;
    clip.end = end;
    clip.mins = mins;
    clip.maxs = maxs;
    clip.type = type;
    clip.passedict = passedict;

    if (type == MOVE_MISSILE) {
        for (int i = 0; i < 3; i++) {
            clip.mins2[i] = -15;
            clip.maxs2[i] = 15;
        }
    } else {
        clip.mins2 = mins;
        clip.maxs2 = maxs;
    }

// create the bounding box of the entire move
    SV_MoveBounds(start, clip.mins2, clip.maxs2, end, clip.boxmins, clip.boxmaxs);

// clip to entities
    SV_ClipToLinks(sv_areanodes, &clip);

    return clip.trace;
}

