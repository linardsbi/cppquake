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
// sv_phys.c

#include <cmath>
#include "quakedef.hpp"
#include "util.hpp"

/*


pushmove objects do not obey gravity, and do not interact with each other or trigger fields, but block normal movement and push normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects 

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

cvar_t sv_friction = {"sv_friction", "4", false, true};
cvar_t sv_stopspeed = {"sv_stopspeed", "100"};
cvar_t sv_gravity = {"sv_gravity", "800", false, true};
cvar_t sv_maxvelocity = {"sv_maxvelocity", "2000"};
cvar_t sv_nostep = {"sv_nostep", "0"};

#ifdef QUAKE2
static	vec3	vec_origin = {0.0, 0.0, 0.0};
#endif

#define    MOVE_EPSILON    0.01

void SV_Physics_Toss(edict_t *ent);

/*
================
SV_CheckAllEnts
================
*/
void SV_CheckAllEnts() {
    int e = 0;
    edict_t *check = nullptr;

// see if any solid entities are inside the final position
    check = NEXT_EDICT(sv.edicts);
    for (e = 1; e < sv.num_edicts; e++, check = NEXT_EDICT(check)) {
        if (check->free)
            continue;
        if (check->v.movetype == MOVETYPE_PUSH
            || check->v.movetype == MOVETYPE_NONE
            #ifdef QUAKE2
            || check->v.movetype == MOVETYPE_FOLLOW
            #endif
            || check->v.movetype == MOVETYPE_NOCLIP)
            continue;

        if (SV_TestEntityPosition(check))
            Con_Printf("entity in invalid position\n");
    }
}

/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity(edict_t *ent) {
//
// bound velocity
//
    for (int i = 0; i < 3; i++) {
        if (std::isnan(ent->v.velocity[i])) {
            Con_Printf("Got a NaN velocity on %s\n", getStringByOffset(ent->v.classname));
            ent->v.velocity[i] = 0;
        }
        if (std::isnan(ent->v.origin[i])) {
            Con_Printf("Got a NaN origin on %s\n", getStringByOffset(ent->v.classname));
            ent->v.origin[i] = 0;
        }
        if (ent->v.velocity[i] > sv_maxvelocity.value)
            ent->v.velocity[i] = sv_maxvelocity.value;
        else if (ent->v.velocity[i] < -sv_maxvelocity.value)
            ent->v.velocity[i] = -sv_maxvelocity.value;
    }
}

/*
=============
SV_RunThink

Runs thinking code if time.  There is some play in the exact time the think
function will be called, because it is called before any movement is done
in a frame.  Not used for pushmove objects, because they must be exact.
Returns false if the entity removed itself.
=============
*/
auto SV_RunThink(edict_t *ent) -> qboolean {
    float thinktime = NAN;

    thinktime = ent->v.nextthink;
    if (thinktime <= 0 || thinktime > sv.time + host_frametime)
        return true;

    if (thinktime < sv.time)
        thinktime = sv.time;    // don't let things stay in the past.
    // it is possible to start that way
    // by a trigger with a local time.
    ent->v.nextthink = 0;
    pr_global_struct->time = thinktime;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
    PR_ExecuteProgram(ent->v.think);
    return !ent->free;
}

/*
==================
SV_Impact

Two entities have touched, so run their touch functions
==================
*/
void SV_Impact(edict_t *e1, edict_t *e2) {
    int old_self = 0, old_other = 0;

    old_self = pr_global_struct->self;
    old_other = pr_global_struct->other;

    pr_global_struct->time = sv.time;
    if (e1->v.touch && e1->v.solid != SOLID_NOT) {
        pr_global_struct->self = EDICT_TO_PROG(e1);
        pr_global_struct->other = EDICT_TO_PROG(e2);
        PR_ExecuteProgram(e1->v.touch);
    }

    if (e2->v.touch && e2->v.solid != SOLID_NOT) {
        pr_global_struct->self = EDICT_TO_PROG(e2);
        pr_global_struct->other = EDICT_TO_PROG(e1);
        PR_ExecuteProgram(e2->v.touch);
    }

    pr_global_struct->self = old_self;
    pr_global_struct->other = old_other;
}


/*
==================
ClipVelocity

Slide off of the impacting object
returns the blocked flags (1 = floor, 2 = step / wall)
==================
*/
#define    STOP_EPSILON    0.1

auto ClipVelocity(vec3 in, vec3 normal, vec3 &out, float overbounce) -> int {
    float backoff = NAN;
    float change = NAN;
    int i = 0, blocked = 0;

    blocked = 0;
    if (normal[2] > 0)
        blocked |= 1;        // floor
    if (!normal[2])
        blocked |= 2;        // step

    backoff = glm::dot (in, normal) * overbounce;

    for (i = 0; i < 3; i++) {
        change = normal[i] * backoff;
        out[i] = in[i] - change;
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
            out[i] = 0;
    }

    return blocked;
}


/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
Returns the clipflags if the velocity was modified (hit something solid)
1 = floor
2 = wall / step
4 = dead stop
If steptrace is not NULL, the trace of any vertical wall hit will be stored
============
*/
#define    MAX_CLIP_PLANES    5

auto SV_FlyMove(edict_t *ent, double time, trace_t *steptrace) -> int {
    vec3 dir;
    vec3 planes[MAX_CLIP_PLANES];
    vec3 primal_velocity, original_velocity, new_velocity;
    trace_t trace;
    vec3 end;

    const auto numbumps = 4;

    auto blocked = 0;
    original_velocity = ent->v.velocity;
    primal_velocity = ent->v.velocity;
    auto numplanes = 0;

    auto time_left = time;

    for (int bumpcount = 0; bumpcount < numbumps; bumpcount++) {
        if (!ent->v.velocity[0] && !ent->v.velocity[1] && !ent->v.velocity[2])
            break;

        end = ent->v.origin + static_cast<float>(time_left) * ent->v.velocity;

        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, 0, ent);

        if (trace.allsolid) {    // entity is trapped in another solid
            ent->v.velocity = vec3_origin;
            return 3;
        }

        if (trace.fraction > 0) {    // actually covered some distance
            ent->v.origin = trace.endpos;
            original_velocity = ent->v.velocity;
            numplanes = 0;
        }

        if (trace.fraction == 1)
            break;        // moved the entire distance

        if (!trace.ent)
            Sys_Error("SV_FlyMove: !trace.ent");

        if (trace.plane.normal[2] > 0.7) {
            blocked |= 1;        // floor
            if (trace.ent->v.solid == SOLID_BSP) {
                ent->v.flags = (int) ent->v.flags | FL_ONGROUND;
                ent->v.groundentity = EDICT_TO_PROG(trace.ent);
            }
        }
        if (!trace.plane.normal[2]) {
            blocked |= 2;        // step
            if (steptrace)
                *steptrace = trace;    // save for player extrafriction
        }

//
// run the impact function
//
        SV_Impact(ent, trace.ent);
        if (ent->free)
            break;        // removed by the impact function


        time_left -= time_left * trace.fraction;

        // cliped to another plane
        if (numplanes >= MAX_CLIP_PLANES) {    // this shouldn't really happen
            ent->v.velocity = vec3_origin;
            return 3;
        }

        planes[numplanes] = trace.plane.normal;
        numplanes++;

//
// modify original_velocity so it parallels all of the clip planes
//
        int i = 0;
        for (; i < numplanes; i++) {
            ClipVelocity(original_velocity, planes[i], new_velocity, 1);
            int j = 0;
            for (; j < numplanes; j++)
                if (j != i) {
                    if (glm::dot (new_velocity, planes[j]) < 0)
                        break;    // not ok
                }
            if (j == numplanes)
                break;
        }

        if (i != numplanes) {    // go along this plane
            ent->v.velocity = new_velocity;
        } else {    // go along the crease
            if (numplanes != 2) {
//				Con_Printf ("clip velocity, numplanes == %i\n",numplanes);
                ent->v.velocity = vec3_origin;
                return 7;
            }
            dir = glm::cross(planes[0], planes[1]);
            const auto d = glm::dot (dir, ent->v.velocity);
            ent->v.velocity = dir * d;
        }

//
// if original velocity is against the original velocity, stop dead
// to avoid tiny occilations in sloping corners
//
        if (glm::dot (ent->v.velocity, primal_velocity) <= 0) {
            ent->v.velocity = vec3_origin;
            return blocked;
        }
    }

    return blocked;
}


/*
============
SV_AddGravity

============
*/
void SV_AddGravity(edict_t *ent) {
    float ent_gravity = NAN;

#ifdef QUAKE2
    if (ent->v.gravity)
        ent_gravity = ent->v.gravity;
    else
        ent_gravity = 1.0;
#else
    eval_t *val = nullptr;

    val = GetEdictFieldValue(ent, "gravity");
    if (val && val->_float)
        ent_gravity = val->_float;
    else
        ent_gravity = 1.0F;
#endif
    ent->v.velocity[2] -= ent_gravity * sv_gravity.value * host_frametime;
}


/*
===============================================================================

PUSHMOVE

===============================================================================
*/

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
auto SV_PushEntity(edict_t *ent, vec3 push) -> trace_t {
    trace_t trace;

    auto end = ent->v.origin + push;

    if (ent->v.movetype == MOVETYPE_FLYMISSILE)
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_MISSILE, ent);
    else if (ent->v.solid == SOLID_TRIGGER || ent->v.solid == SOLID_NOT)
        // only clip against bmodels
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NOMONSTERS, ent);
    else
        trace = SV_Move(ent->v.origin, ent->v.mins, ent->v.maxs, end, MOVE_NORMAL, ent);

    ent->v.origin = trace.endpos;
    SV_LinkEdict(ent, true);

    if (trace.ent)
        SV_Impact(ent, trace.ent);

    return trace;
}


/*
============
SV_PushMove

============
*/
void SV_PushMove(edict_t *pusher, float movetime) {
    int i = 0, e = 0;
    edict_t *check = nullptr, *block = nullptr;
    vec3 mins, maxs, move;
    vec3 entorig, pushorig;
    int num_moved = 0;
    edict_t *moved_edict[MAX_EDICTS];
    vec3 moved_from[MAX_EDICTS];

    if (!pusher->v.velocity[0] && !pusher->v.velocity[1] && !pusher->v.velocity[2]) {
        pusher->v.ltime += movetime;
        return;
    }

    for (i = 0; i < 3; i++) {
        move[i] = pusher->v.velocity[i] * movetime;
        mins[i] = pusher->v.absmin[i] + move[i];
        maxs[i] = pusher->v.absmax[i] + move[i];
    }

    pushorig = pusher->v.origin;

// move the pusher to it's final position

    pusher->v.origin = pusher->v.origin + move;
    pusher->v.ltime += movetime;
    SV_LinkEdict(pusher, false);


// see if any solid entities are inside the final position
    num_moved = 0;
    check = NEXT_EDICT(sv.edicts);
    for (e = 1; e < sv.num_edicts; e++, check = NEXT_EDICT(check)) {
        if (check->free)
            continue;
        if (check->v.movetype == MOVETYPE_PUSH
            || check->v.movetype == MOVETYPE_NONE
            #ifdef QUAKE2
            || check->v.movetype == MOVETYPE_FOLLOW
            #endif
            || check->v.movetype == MOVETYPE_NOCLIP)
            continue;

        // if the entity is standing on the pusher, it will definately be moved
        if (!(((int) check->v.flags & FL_ONGROUND)
              && PROG_TO_EDICT(check->v.groundentity) == pusher)) {
            if (check->v.absmin[0] >= maxs[0]
                || check->v.absmin[1] >= maxs[1]
                || check->v.absmin[2] >= maxs[2]
                || check->v.absmax[0] <= mins[0]
                || check->v.absmax[1] <= mins[1]
                || check->v.absmax[2] <= mins[2])
                continue;

            // see if the ent's bbox is inside the pusher's final position
            if (!SV_TestEntityPosition(check))
                continue;
        }

        // remove the onground flag for non-players
        if (check->v.movetype != MOVETYPE_WALK)
            check->v.flags = (int) check->v.flags & ~FL_ONGROUND;

        entorig = check->v.origin;
        moved_from[num_moved] = check->v.origin;
        moved_edict[num_moved] = check;
        num_moved++;

        // try moving the contacted entity
        pusher->v.solid = SOLID_NOT;
        SV_PushEntity(check, move);
        pusher->v.solid = SOLID_BSP;

        // if it is still inside the pusher, block
        block = SV_TestEntityPosition(check);
        if (block) {    // fail the move
            if (check->v.mins[0] == check->v.maxs[0])
                continue;
            if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER) {    // corpse
                check->v.mins[0] = check->v.mins[1] = 0;
                check->v.maxs = check->v.mins;
                continue;
            }

            check->v.origin = entorig;
            SV_LinkEdict(check, true);

            pusher->v.origin = pushorig;
            SV_LinkEdict(pusher, false);
            pusher->v.ltime -= movetime;

            // if the pusher has a "blocked" function, call it
            // otherwise, just stay in place until the obstacle is gone
            if (pusher->v.blocked) {
                pr_global_struct->self = EDICT_TO_PROG(pusher);
                pr_global_struct->other = EDICT_TO_PROG(check);
                PR_ExecuteProgram(pusher->v.blocked);
            }

            // move back any entities we already moved
            for (i = 0; i < num_moved; i++) {
                moved_edict[i]->v.origin = moved_from[i];
                SV_LinkEdict(moved_edict[i], false);
            }
            return;
        }
    }


}

#ifdef QUAKE2
/*
============
SV_PushRotate

============
*/
void SV_PushRotate (edict_t *pusher, float movetime)
{
    int			i, e;
    edict_t		*check, *block;
    vec3		move, a, amove;
    vec3		entorig, pushorig;
    int			num_moved;
    edict_t		*moved_edict[MAX_EDICTS];
    vec3		moved_from[MAX_EDICTS];
    vec3		org, org2;
    vec3		forward, right, up;

    if (!pusher->v.avelocity[0] && !pusher->v.avelocity[1] && !pusher->v.avelocity[2])
    {
        pusher->v.ltime += movetime;
        return;
    }

    for (i=0 ; i<3 ; i++)
        amove[i] = pusher->v.avelocity[i] * movetime;

    a = vec3_origin - amove;
    AngleVectors (a, forward, right, up);

    pushorig = pusher->v.angles;

// move the pusher to it's final position

    pusher->v.angles = pusher->v.angles + amove;
    pusher->v.ltime += movetime;
    SV_LinkEdict (pusher, false);


// see if any solid entities are inside the final position
    num_moved = 0;
    check = NEXT_EDICT(sv.edicts);
    for (e=1 ; e<sv.num_edicts ; e++, check = NEXT_EDICT(check))
    {
        if (check->free)
            continue;
        if (check->v.movetype == MOVETYPE_PUSH
        || check->v.movetype == MOVETYPE_NONE
        || check->v.movetype == MOVETYPE_FOLLOW
        || check->v.movetype == MOVETYPE_NOCLIP)
            continue;

    // if the entity is standing on the pusher, it will definately be moved
        if ( ! ( ((int)check->v.flags & FL_ONGROUND)
        && PROG_TO_EDICT(check->v.groundentity) == pusher) )
        {
            if ( check->v.absmin[0] >= pusher->v.absmax[0]
            || check->v.absmin[1] >= pusher->v.absmax[1]
            || check->v.absmin[2] >= pusher->v.absmax[2]
            || check->v.absmax[0] <= pusher->v.absmin[0]
            || check->v.absmax[1] <= pusher->v.absmin[1]
            || check->v.absmax[2] <= pusher->v.absmin[2] )
                continue;

        // see if the ent's bbox is inside the pusher's final position
            if (!SV_TestEntityPosition (check))
                continue;
        }

    // remove the onground flag for non-players
        if (check->v.movetype != MOVETYPE_WALK)
            check->v.flags = (int)check->v.flags & ~FL_ONGROUND;

        entorig = check->v.origin;
        moved_from[num_moved] = check->v.origin;
        moved_edict[num_moved] = check;
        num_moved++;

        // calculate destination position
        org = check->v.origin - pusher->v.origin;
        org2[0] = glm::dot (org, forward);
        org2[1] = -glm::dot (org, right);
        org2[2] = glm::dot (org, up);
        move = org2 - org;

        // try moving the contacted entity
        pusher->v.solid = SOLID_NOT;
        SV_PushEntity (check, move);
        pusher->v.solid = SOLID_BSP;

    // if it is still inside the pusher, block
        block = SV_TestEntityPosition (check);
        if (block)
        {	// fail the move
            if (check->v.mins[0] == check->v.maxs[0])
                continue;
            if (check->v.solid == SOLID_NOT || check->v.solid == SOLID_TRIGGER)
            {	// corpse
                check->v.mins[0] = check->v.mins[1] = 0;
                check->v.maxs = check->v.mins;
                continue;
            }

            check->v.origin = entorig;
            SV_LinkEdict (check, true);

            pusher->v.angles = pushorig;
            SV_LinkEdict (pusher, false);
            pusher->v.ltime -= movetime;

            // if the pusher has a "blocked" function, call it
            // otherwise, just stay in place until the obstacle is gone
            if (pusher->v.blocked)
            {
                pr_global_struct->self = EDICT_TO_PROG(pusher);
                pr_global_struct->other = EDICT_TO_PROG(check);
                PR_ExecuteProgram (pusher->v.blocked);
            }

        // move back any entities we already moved
            for (i=0 ; i<num_moved ; i++)
            {
                moved_edict[i]->v.origin = moved_from[i];
                moved_edict[i]->v.angles = moved_edict[i]->v.angles - amove;
                SV_LinkEdict (moved_edict[i], false);
            }
            return;
        }
        else
        {
            check->v.angles = check->v.angles + amove;
        }
    }


}
#endif

/*
================
SV_Physics_Pusher

================
*/
void SV_Physics_Pusher(edict_t *ent) {
    float thinktime = NAN;
    float oldltime = NAN;
    float movetime = NAN;

    oldltime = ent->v.ltime;

    thinktime = ent->v.nextthink;
    if (thinktime < ent->v.ltime + host_frametime) {
        movetime = thinktime - ent->v.ltime;
        if (movetime < 0)
            movetime = 0;
    } else
        movetime = host_frametime;

    if (movetime) {
#ifdef QUAKE2
        if (ent->v.avelocity[0] || ent->v.avelocity[1] || ent->v.avelocity[2])
            SV_PushRotate (ent, movetime);
        else
#endif
        SV_PushMove(ent, movetime);    // advances ent->v.ltime if not blocked
    }

    if (thinktime > oldltime && thinktime <= ent->v.ltime) {
        ent->v.nextthink = 0;
        pr_global_struct->time = sv.time;
        pr_global_struct->self = EDICT_TO_PROG(ent);
        pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
        PR_ExecuteProgram(ent->v.think);
        if (ent->free)
            return;
    }

}


/*
===============================================================================

CLIENT MOVEMENT

===============================================================================
*/

/*
=============
SV_CheckStuck

This is a big hack to try and fix the rare case of getting stuck in the world
clipping hull.
=============
*/
void SV_CheckStuck(edict_t *ent) {
    if (!SV_TestEntityPosition(ent)) {
        ent->v.oldorigin = ent->v.origin;
        return;
    }

    const auto org = ent->v.origin;
    ent->v.origin = ent->v.oldorigin;
    if (!SV_TestEntityPosition(ent)) {
        Con_DPrintf("Unstuck.\n");
        SV_LinkEdict(ent, true);
        return;
    }

    for (int z = 0; z < 18; z++)
        for (int i = -1; i <= 1; i++)
            for (int j = -1; j <= 1; j++) {
                ent->v.origin[0] = org[0] + i;
                ent->v.origin[1] = org[1] + j;
                ent->v.origin[2] = org[2] + z;
                if (!SV_TestEntityPosition(ent)) {
                    Con_DPrintf("Unstuck.\n");
                    SV_LinkEdict(ent, true);
                    return;
                }
            }

    ent->v.origin = org;
    Con_DPrintf("player is stuck.\n");
}


/*
=============
SV_CheckWater
=============
*/
auto SV_CheckWater(edict_t *ent) -> qboolean {
    vec3 point;
    int cont = 0;
#ifdef QUAKE2
    int		truecont;
#endif

    point[0] = ent->v.origin[0];
    point[1] = ent->v.origin[1];
    point[2] = ent->v.origin[2] + ent->v.mins[2] + 1;

    ent->v.waterlevel = 0;
    ent->v.watertype = CONTENTS_EMPTY;
    cont = SV_PointContents(point);
    if (cont <= CONTENTS_WATER) {
#ifdef QUAKE2
        truecont = SV_TruePointContents (point);
#endif
        ent->v.watertype = cont;
        ent->v.waterlevel = 1;
        point[2] = ent->v.origin[2] + (ent->v.mins[2] + ent->v.maxs[2]) * 0.5;
        cont = SV_PointContents(point);
        if (cont <= CONTENTS_WATER) {
            ent->v.waterlevel = 2;
            point[2] = ent->v.origin[2] + ent->v.view_ofs[2];
            cont = SV_PointContents(point);
            if (cont <= CONTENTS_WATER)
                ent->v.waterlevel = 3;
        }
#ifdef QUAKE2
        if (truecont <= CONTENTS_CURRENT_0 && truecont >= CONTENTS_CURRENT_DOWN)
        {
            static vec3 current_table[] =
            {
                {1, 0, 0},
                {0, 1, 0},
                {-1, 0, 0},
                {0, -1, 0},
                {0, 0, 1},
                {0, 0, -1}
            };

            VectorMA (ent->v.basevelocity, 150.0*ent->v.waterlevel/3.0, current_table[CONTENTS_CURRENT_0 - truecont], ent->v.basevelocity);
        }
#endif
    }

    return ent->v.waterlevel > 1;
}

/*
============
SV_WallFriction

============
*/
void SV_WallFriction(edict_t *ent, trace_t *trace) {
    vec3 forward, right, up;
    float d = NAN, i = NAN;
    vec3 into, side;

    AngleVectors(ent->v.v_angle, forward, right, up);
    d = glm::dot (trace->plane.normal, forward);

    d += 0.5;
    if (d >= 0)
        return;

// cut the tangential velocity
    i = glm::dot (trace->plane.normal, ent->v.velocity);
    into = trace->plane.normal * i;
    side = ent->v.velocity - into;

    ent->v.velocity[0] = side[0] * (1 + d);
    ent->v.velocity[1] = side[1] * (1 + d);
}

/*
=====================
SV_TryUnstick

Player has come to a dead stop, possibly due to the problem with limited
float precision at some angle joins in the BSP hull.

Try fixing by pushing one pixel in each direction.

This is a hack, but in the interest of good gameplay...
======================
*/
auto SV_TryUnstick(edict_t *ent, vec3 oldvel) -> int {
    int i = 0;
    vec3 oldorg;
    vec3 dir;
    int clip = 0;
    trace_t steptrace;

    oldorg = ent->v.origin;
    dir = vec3_origin;

    for (i = 0; i < 8; i++) {
// try pushing a little in an axial direction
        switch (i) {
            case 0:
                dir[0] = 2;
                dir[1] = 0;
                break;
            case 1:
                dir[0] = 0;
                dir[1] = 2;
                break;
            case 2:
                dir[0] = -2;
                dir[1] = 0;
                break;
            case 3:
                dir[0] = 0;
                dir[1] = -2;
                break;
            case 4:
                dir[0] = 2;
                dir[1] = 2;
                break;
            case 5:
                dir[0] = -2;
                dir[1] = 2;
                break;
            case 6:
                dir[0] = 2;
                dir[1] = -2;
                break;
            case 7:
                dir[0] = -2;
                dir[1] = -2;
                break;
        }

        SV_PushEntity(ent, dir);

// retry the original move
        ent->v.velocity[0] = oldvel[0];
        ent->v.velocity[1] = oldvel[1];
        ent->v.velocity[2] = 0;
        clip = SV_FlyMove(ent, 0.1, &steptrace);

        if (fabs(oldorg[1] - ent->v.origin[1]) > 4
            || fabs(oldorg[0] - ent->v.origin[0]) > 4) {
//Con_DPrintf ("unstuck!\n");
            return clip;
        }

// go back to the original pos and try again
        ent->v.origin = oldorg;
    }

    ent->v.velocity = vec3_origin;
    return 7;        // still not moving
}

/*
=====================
SV_WalkMove

Only used by players
======================
*/
#define    STEPSIZE    18

void SV_WalkMove(edict_t *ent) {
    vec3 upmove, downmove;
    vec3 oldorg, oldvel;
    vec3 nosteporg, nostepvel;
    int clip = 0;
    int oldonground = 0;
    trace_t steptrace, downtrace;

//
// do a regular slide move unless it looks like you ran into a step
//
    oldonground = (int) ent->v.flags & FL_ONGROUND;
    ent->v.flags = (int) ent->v.flags & ~FL_ONGROUND;

    oldorg = ent->v.origin;
    oldvel = ent->v.velocity;

    clip = SV_FlyMove(ent, host_frametime, &steptrace);

    if (!(clip & 2))
        return;        // move didn't block on a step

    if (!oldonground && ent->v.waterlevel == 0)
        return;        // don't stair up while jumping

    if (ent->v.movetype != MOVETYPE_WALK)
        return;        // gibbed by a trigger

    if (sv_nostep.value)
        return;

    if ((int) sv_player->v.flags & FL_WATERJUMP)
        return;

    nosteporg = ent->v.origin;
    nostepvel = ent->v.velocity;

//
// try moving up and forward to go up a step
//
    ent->v.origin = oldorg;    // back to start pos

    upmove = vec3_origin;
    downmove = vec3_origin;
    upmove[2] = STEPSIZE;
    downmove[2] = -STEPSIZE + oldvel[2] * host_frametime;

// move up
    SV_PushEntity(ent, upmove);    // FIXME: don't link?

// move forward
    ent->v.velocity[0] = oldvel[0];
    ent->v.velocity[1] = oldvel[1];
    ent->v.velocity[2] = 0;
    clip = SV_FlyMove(ent, host_frametime, &steptrace);

// check for stuckness, possibly due to the limited precision of floats
// in the clipping hulls
    if (clip) {
        if (fabs(oldorg[1] - ent->v.origin[1]) < 0.03125
            && fabs(oldorg[0] - ent->v.origin[0]) < 0.03125) {    // stepping up didn't make any progress
            clip = SV_TryUnstick(ent, oldvel);
        }
    }

// extra friction based on view angle
    if (clip & 2)
        SV_WallFriction(ent, &steptrace);

// move down
    downtrace = SV_PushEntity(ent, downmove);    // FIXME: don't link?

    if (downtrace.plane.normal[2] > 0.7) {
        if (ent->v.solid == SOLID_BSP) {
            ent->v.flags = (int) ent->v.flags | FL_ONGROUND;
            ent->v.groundentity = EDICT_TO_PROG(downtrace.ent);
        }
    } else {
// if the push down didn't end up on good ground, use the move without
// the step up.  This happens near wall / slope combinations, and can
// cause the player to hop up higher on a slope too steep to climb	
        ent->v.origin = nosteporg;
        ent->v.velocity = nostepvel;
    }
}


/*
================
SV_Physics_Client

Player character actions
================
*/
void SV_Physics_Client(edict_t *ent, int num) {
    if (!svs.clients[num - 1].active)
        return;        // unconnected slot

//
// call standard client pre-think
//	
    pr_global_struct->time = static_cast<float>(sv.time);
    pr_global_struct->self = EDICT_TO_PROG(ent);
    PR_ExecuteProgram(pr_global_struct->PlayerPreThink);

//
// do a move
//
    SV_CheckVelocity(ent);

//
// decide which move function to call
//
    switch ((int) ent->v.movetype) {
        case MOVETYPE_NONE:
            if (!SV_RunThink(ent))
                return;
            break;

        case MOVETYPE_WALK:
            if (!SV_RunThink(ent))
                return;
            if (!SV_CheckWater(ent) && !((int) ent->v.flags & FL_WATERJUMP))
                SV_AddGravity(ent);
            SV_CheckStuck(ent);
#ifdef QUAKE2
            ent->v.velocity = ent->v.velocity + ent->v.basevelocity;
#endif
            SV_WalkMove(ent);

#ifdef QUAKE2
            ent->v.velocity = ent->v.velocity - ent->v.basevelocity;
#endif
            break;

        case MOVETYPE_TOSS:
        case MOVETYPE_BOUNCE:
            SV_Physics_Toss(ent);
            break;

        case MOVETYPE_FLY:
            if (!SV_RunThink(ent))
                return;
            SV_FlyMove(ent, host_frametime, nullptr);
            break;

        case MOVETYPE_NOCLIP:
            if (!SV_RunThink(ent))
                return;
            VectorMA(ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);
            break;

        default:
            Sys_Error("SV_Physics_client: bad movetype %i", (int) ent->v.movetype);
    }

//
// call standard player post-think
//		
    SV_LinkEdict(ent, true);

    pr_global_struct->time = static_cast<float>(sv.time);
    pr_global_struct->self = EDICT_TO_PROG(ent);
    PR_ExecuteProgram(pr_global_struct->PlayerPostThink);
}

//============================================================================

/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
void SV_Physics_None(edict_t *ent) {
// regular thinking
    SV_RunThink(ent);
}

#ifdef QUAKE2
/*
=============
SV_Physics_Follow

Entities that are "stuck" to another entity
=============
*/
void SV_Physics_Follow (edict_t *ent)
{
// regular thinking
    SV_RunThink (ent);
    ent->v.origin = PROG_TO_EDICT(ent->v.aiment)->v.origin + ent->v.v_angle;
    SV_LinkEdict (ent, true);
}
#endif

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
void SV_Physics_Noclip(edict_t *ent) {
// regular thinking
    if (!SV_RunThink(ent))
        return;

    VectorMA(ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);
    VectorMA(ent->v.origin, host_frametime, ent->v.velocity, ent->v.origin);

    SV_LinkEdict(ent, false);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_CheckWaterTransition

=============
*/
void SV_CheckWaterTransition(edict_t *ent) {
    int cont = 0;
#ifdef QUAKE2
    vec3	point;

    point[0] = ent->v.origin[0];
    point[1] = ent->v.origin[1];
    point[2] = ent->v.origin[2] + ent->v.mins[2] + 1;
    cont = SV_PointContents (point);
#else
    cont = SV_PointContents(ent->v.origin);
#endif
    if (!ent->v.watertype) {    // just spawned here
        ent->v.watertype = cont;
        ent->v.waterlevel = 1;
        return;
    }

    if (cont <= CONTENTS_WATER) {
        if (ent->v.watertype == CONTENTS_EMPTY) {    // just crossed into water
            SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
        }
        ent->v.watertype = cont;
        ent->v.waterlevel = 1;
    } else {
        if (ent->v.watertype != CONTENTS_EMPTY) {    // just crossed into water
            SV_StartSound(ent, 0, "misc/h2ohit1.wav", 255, 1);
        }
        ent->v.watertype = CONTENTS_EMPTY;
        ent->v.waterlevel = cont;
    }
}

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
void SV_Physics_Toss(edict_t *ent) {
    trace_t trace;
    vec3 move;
    float backoff = NAN;
#ifdef QUAKE2
    edict_t	*groundentity;

    groundentity = PROG_TO_EDICT(ent->v.groundentity);
    if ((int)groundentity->v.flags & FL_CONVEYOR)
        ent->v.basevelocity = groundentity->v.movedir * groundentity->v.speed;
    else
        ent->v.basevelocity = vec_origin;
    SV_CheckWater (ent);
#endif
    // regular thinking
    if (!SV_RunThink(ent))
        return;

#ifdef QUAKE2
    if (ent->v.velocity[2] > 0)
        ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

    if ( ((int)ent->v.flags & FL_ONGROUND) )
//@@
        if (VectorCompare(ent->v.basevelocity, vec_origin))
            return;

    SV_CheckVelocity (ent);

// add gravity
    if (! ((int)ent->v.flags & FL_ONGROUND)
        && ent->v.movetype != MOVETYPE_FLY
        && ent->v.movetype != MOVETYPE_BOUNCEMISSILE
        && ent->v.movetype != MOVETYPE_FLYMISSILE)
            SV_AddGravity (ent);

#else
// if onground, return without moving
    if (((int) ent->v.flags & FL_ONGROUND))
        return;

    SV_CheckVelocity(ent);

// add gravity
    if (ent->v.movetype != MOVETYPE_FLY
        && ent->v.movetype != MOVETYPE_FLYMISSILE)
        SV_AddGravity(ent);
#endif

// move angles
    VectorMA(ent->v.angles, host_frametime, ent->v.avelocity, ent->v.angles);

// move origin
#ifdef QUAKE2
    ent->v.velocity = ent->v.velocity + ent->v.basevelocity;
#endif
    move = ent->v.velocity * static_cast<float>(host_frametime);
    trace = SV_PushEntity(ent, move);
#ifdef QUAKE2
    ent->v.velocity = ent->v.velocity - ent->v.basevelocity;
#endif
    if (trace.fraction == 1)
        return;
    if (ent->free)
        return;

    if (ent->v.movetype == MOVETYPE_BOUNCE)
        backoff = 1.5;
#ifdef QUAKE2
        else if (ent->v.movetype == MOVETYPE_BOUNCEMISSILE)
            backoff = 2.0;
#endif
    else
        backoff = 1;

    ClipVelocity(ent->v.velocity, trace.plane.normal, ent->v.velocity, backoff);

// stop if on ground
    if (trace.plane.normal[2] > 0.7) {
#ifdef QUAKE2
        if (ent->v.velocity[2] < 60 || (ent->v.movetype != MOVETYPE_BOUNCE && ent->v.movetype != MOVETYPE_BOUNCEMISSILE))
#else
        if (ent->v.velocity[2] < 60 || ent->v.movetype != MOVETYPE_BOUNCE)
#endif
        {
            ent->v.flags = (int) ent->v.flags | FL_ONGROUND;
            ent->v.groundentity = EDICT_TO_PROG(trace.ent);
            ent->v.velocity = vec3_origin;
            ent->v.avelocity = vec3_origin;
        }
    }

// check for in water
    SV_CheckWaterTransition(ent);
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
=============
*/
#ifdef QUAKE2
void SV_Physics_Step (edict_t *ent)
{
    qboolean	wasonground;
    qboolean	inwater;
    qboolean	hitsound = false;
    float		*vel;
    float		speed, newspeed, control;
    float		friction;
    edict_t		*groundentity;

    groundentity = PROG_TO_EDICT(ent->v.groundentity);
    if ((int)groundentity->v.flags & FL_CONVEYOR)
        ent->v.basevelocity = groundentity->v.movedir * groundentity->v.speed;
    else
        ent->v.basevelocity = vec_origin;
//@@
    pr_global_struct->time = sv.time;
    pr_global_struct->self = EDICT_TO_PROG(ent);
    PF_WaterMove();

    SV_CheckVelocity (ent);

    wasonground = (int)ent->v.flags & FL_ONGROUND;
//	ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;

    // add gravity except:
    //   flying monsters
    //   swimming monsters who are in the water
    inwater = SV_CheckWater(ent);
    if (! wasonground)
        if (!((int)ent->v.flags & FL_FLY))
            if (!(((int)ent->v.flags & FL_SWIM) && (ent->v.waterlevel > 0)))
            {
                if (ent->v.velocity[2] < sv_gravity.value*-0.1)
                    hitsound = true;
                if (!inwater)
                    SV_AddGravity (ent);
            }

    if (!VectorCompare(ent->v.velocity, vec_origin) || !VectorCompare(ent->v.basevelocity, vec_origin))
    {
        ent->v.flags = (int)ent->v.flags & ~FL_ONGROUND;
        // apply friction
        // let dead monsters who aren't completely onground slide
        if (wasonground)
            if (!(ent->v.health <= 0.0 && !SV_CheckBottom(ent)))
            {
                vel = ent->v.velocity;
                speed = sqrt(vel[0]*vel[0] +vel[1]*vel[1]);
                if (speed)
                {
                    friction = sv_friction.value;

                    control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
                    newspeed = speed - host_frametime*control*friction;

                    if (newspeed < 0)
                        newspeed = 0;
                    newspeed /= speed;

                    vel[0] = vel[0] * newspeed;
                    vel[1] = vel[1] * newspeed;
                }
            }

        ent->v.velocity = ent->v.velocity + ent->v.basevelocity;
        SV_FlyMove (ent, host_frametime, NULL);
        ent->v.velocity = ent->v.velocity - ent->v.basevelocity;

        // determine if it's on solid ground at all
        {
            vec3	mins, maxs, point;
            int		x, y;

            mins = ent->v.origin + ent->v.mins;
            maxs = ent->v.origin + ent->v.maxs;

            point[2] = mins[2] - 1;
            for	(x=0 ; x<=1 ; x++)
                for	(y=0 ; y<=1 ; y++)
                {
                    point[0] = x ? maxs[0] : mins[0];
                    point[1] = y ? maxs[1] : mins[1];
                    if (SV_PointContents (point) == CONTENTS_SOLID)
                    {
                        ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
                        break;
                    }
                }

        }

        SV_LinkEdict (ent, true);

        if ((int)ent->v.flags & FL_ONGROUND)
            if (!wasonground)
                if (hitsound)
                    SV_StartSound (ent, 0, "demon/dland2.wav", 255, 1);
    }

// regular thinking
    SV_RunThink (ent);
    SV_CheckWaterTransition (ent);
}
#else

void SV_Physics_Step(edict_t *ent) {
    qboolean hitsound = 0;

// freefall if not onground
    if (!((int) ent->v.flags & (FL_ONGROUND | FL_FLY | FL_SWIM))) {
        if (ent->v.velocity[2] < sv_gravity.value * -0.1F)
            hitsound = true;
        else
            hitsound = false;

        SV_AddGravity(ent);
        SV_CheckVelocity(ent);
        SV_FlyMove(ent, host_frametime, nullptr);
        SV_LinkEdict(ent, true);

        if ((int) ent->v.flags & FL_ONGROUND)    // just hit ground
        {
            if (hitsound)
                SV_StartSound(ent, 0, "demon/dland2.wav", 255, 1);
        }
    }

// regular thinking
    SV_RunThink(ent);

    SV_CheckWaterTransition(ent);
}

#endif

//============================================================================

/*
================
SV_Physics

================
*/
void SV_Physics() {
    int i = 0;
    edict_t *ent = nullptr;

// let the progs know that a new frame has started
    pr_global_struct->self = EDICT_TO_PROG(sv.edicts);
    pr_global_struct->other = EDICT_TO_PROG(sv.edicts);
    pr_global_struct->time = static_cast<float>(sv.time);
    PR_ExecuteProgram(pr_global_struct->StartFrame);

//SV_CheckAllEnts ();

//
// treat each object in turn
//
    ent = sv.edicts;
    for (i = 0; i < sv.num_edicts; i++, ent = NEXT_EDICT(ent)) {
        if (ent->free)
            continue;

        if (pr_global_struct->force_retouch) {
            SV_LinkEdict(ent, true);    // force retouch even for stationary
        }

        if (i > 0 && i <= svs.maxclients)
            SV_Physics_Client(ent, i);
        else if (ent->v.movetype == MOVETYPE_PUSH)
            SV_Physics_Pusher(ent);
        else if (ent->v.movetype == MOVETYPE_NONE)
            SV_Physics_None(ent);
#ifdef QUAKE2
            else if (ent->v.movetype == MOVETYPE_FOLLOW)
                SV_Physics_Follow (ent);
#endif
        else if (ent->v.movetype == MOVETYPE_NOCLIP)
            SV_Physics_Noclip(ent);
        else if (ent->v.movetype == MOVETYPE_STEP)
            SV_Physics_Step(ent);
        else if (ent->v.movetype == MOVETYPE_TOSS
                 || ent->v.movetype == MOVETYPE_BOUNCE
                 #ifdef QUAKE2
                 || ent->v.movetype == MOVETYPE_BOUNCEMISSILE
                 #endif
                 || ent->v.movetype == MOVETYPE_FLY
                 || ent->v.movetype == MOVETYPE_FLYMISSILE)
            SV_Physics_Toss(ent);
        else
            Sys_Error("SV_Physics: bad movetype %i", (int) ent->v.movetype);
    }

    if (pr_global_struct->force_retouch)
        pr_global_struct->force_retouch--;

    sv.time += host_frametime;
}


#ifdef QUAKE2
trace_t SV_Trace_Toss (edict_t *ent, edict_t *ignore)
{
    edict_t	tempent, *tent;
    trace_t	trace;
    vec3	move;
    vec3	end;
    double	save_frametime;
//	extern particle_t	*active_particles, *free_particles;
//	particle_t	*p;


    save_frametime = host_frametime;
    host_frametime = 0.05;

    memcpy(&tempent, ent, sizeof(edict_t));
    tent = &tempent;

    while (1)
    {
        SV_CheckVelocity (tent);
        SV_AddGravity (tent);
        VectorMA (tent->v.angles, host_frametime, tent->v.avelocity, tent->v.angles);
        VectorScale (tent->v.velocity, host_frametime, move);
        end = tent->v.origin + move;
        trace = SV_Move (tent->v.origin, tent->v.mins, tent->v.maxs, end, MOVE_NORMAL, tent);
        tent->v.origin = trace.endpos;

//		p = free_particles;
//		if (p)
//		{
//			free_particles = p->next;
//			p->next = active_particles;
//			active_particles = p;
//
//			p->die = 256;
//			p->color = 15;
//			p->type = pt_static;
//			p->vel = vec3_origin;
//			p->org = tent->v.origin;
//		}

        if (trace.ent)
            if (trace.ent != ignore)
                break;
    }
//	p->color = 224;
    host_frametime = save_frametime;
    return trace;
}
#endif
