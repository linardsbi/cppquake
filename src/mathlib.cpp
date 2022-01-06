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
// mathlib.c -- math primitives

#include <cmath>
#include <cmath>
#include "quakedef.hpp"

void Sys_Error(const char *error, ...);

int nanmask = 255 << 23;

/*-----------------------------------------------------------------*/

constexpr auto degrees_to_radians(const float deg)
{
  return ( (deg) * M_PI ) / 180.0F;
}

auto VectorNormalize(vec3 &v) -> float {
    if (v == vec3{0.F}) {
        return 0.F;
    }

    const auto length = glm::length(v);
    v = glm::normalize(v);

    return length;
}

void ProjectPointOnPlane(vec3 &dst, const vec3 p, const vec3 normal) {
    const auto inv_denom = 1.0F / glm::dot(normal, normal);
    const auto d = glm::dot(normal, p) * inv_denom;
    const vec3 n = normal * inv_denom;

    dst = p - d * n;
}

/*
** assumes "src" is normalized
*/
void PerpendicularVector(vec3 &dst, const vec3 src) {
  dst = glm::perp(dst, src);
//    int pos = 0;
//    int i = 0;
//    float minelem = 1.0F;
//    vec3 tempvec;
//
//    /*
//    ** find the smallest magnitude axially aligned vector
//    */
//    for (pos = 0, i = 0; i < 3; i++) {
//        if (fabs(src[i]) < minelem) {
//            pos = i;
//            minelem = fabs(src[i]);
//        }
//    }
//    tempvec[0] = tempvec[1] = tempvec[2] = 0.0F;
//    tempvec[pos] = 1.0F;
//
//    /*
//    ** project the point onto the plane defined by src
//    */
//    ProjectPointOnPlane(dst, tempvec, src);
//
//    /*
//    ** normalize the result
//    */
//    VectorNormalize(dst);
}


void RotatePointAroundVector(vec3 &dst, const vec3 dir, const vec3 point, float degrees) {
    vec3 vr;
    PerpendicularVector(vr, dir);

    const auto vup = glm::cross(vr, dir);

    vec3 m[3] = {vr, vup, dir};
    vec3 im[3];
    memcpy(im, m, sizeof(im));

    im[0][1] = m[1][0];
    im[0][2] = m[2][0];
    im[1][0] = m[0][1];
    im[1][2] = m[2][1];
    im[2][0] = m[0][2];
    im[2][1] = m[1][2];

    vec3 zrot[3]{};
    zrot[0][0] = zrot[1][1] = zrot[2][2] = 1.0F;

    zrot[0][0] = cos(degrees_to_radians(degrees));
    zrot[0][1] = sin(degrees_to_radians(degrees));
    zrot[1][0] = -sin(degrees_to_radians(degrees));
    zrot[1][1] = cos(degrees_to_radians(degrees));

    vec3 tmpmat[3];
    vec3 rot[3];
    R_ConcatRotations(m, zrot, tmpmat);
    R_ConcatRotations(tmpmat, im, rot);

    for (int i = 0; i < 3; i++) {
        dst[i] = rot[i][0] * point[0] + rot[i][1] * point[1] + rot[i][2] * point[2];
    }
}

/*-----------------------------------------------------------------*/


/*
==================
BOPS_Error

Split out like this for ASM to call.
==================
*/
void BOPS_Error() {
    Sys_Error("BoxOnPlaneSide:  Bad signbits");
}


#if    !id386

/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
auto BoxOnPlaneSide(vec3 emins, vec3 emaxs, mplane_t *p) -> int {
    float dist1 = NAN, dist2 = NAN;

#if 0    // this is done by the BOX_ON_PLANE_SIDE macro before calling this
    // function
// fast axial cases
if (p->type < 3)
{
    if (p->dist <= emins[p->type])
        return 1;
    if (p->dist >= emaxs[p->type])
        return 2;
    return 3;
}
#endif

// general case
    switch (p->signbits) {
        case 0:
            dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
            dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
            break;
        case 1:
            dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
            dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
            break;
        case 2:
            dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
            dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
            break;
        case 3:
            dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
            dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
            break;
        case 4:
            dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
            dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
            break;
        case 5:
            dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
            dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
            break;
        case 6:
            dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
            dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
            break;
        case 7:
            dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
            dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
            break;
        default:
            BOPS_Error();
            break;
    }

#if 0
    int		i;
    vec3	corners[2];

    for (i=0 ; i<3 ; i++)
    {
        if (plane->normal[i] < 0)
        {
            corners[0][i] = emins[i];
            corners[1][i] = emaxs[i];
        }
        else
        {
            corners[1][i] = emins[i];
            corners[0][i] = emaxs[i];
        }
    }
    dist = glm::dot (plane->normal, corners[0]) - plane->dist;
    dist2 = glm::dot (plane->normal, corners[1]) - plane->dist;
    sides = 0;
    if (dist1 >= 0)
        sides = 1;
    if (dist2 < 0)
        sides |= 2;

#endif

    int sides = 0;
    if (dist1 >= p->dist)
        sides = 1;
    if (dist2 < p->dist)
        sides |= 2;

#ifdef PARANOID
    if (sides == 0)
        Sys_Error("BoxOnPlaneSide: sides==0");
#endif

    return sides;
}

#endif


void AngleVectors(vec3 angles, vec3 &forward, vec3 &right, vec3 &up) {
    float sr = NAN, sp = NAN, sy = NAN, cr = NAN, cp = NAN, cy = NAN;

    auto angle = angles[YAW] * (M_PI * 2 / 360);
    sy = sin(angle);
    cy = cos(angle);
    angle = angles[PITCH] * (M_PI * 2 / 360);
    sp = sin(angle);
    cp = cos(angle);
    angle = angles[ROLL] * (M_PI * 2 / 360);
    sr = sin(angle);
    cr = cos(angle);

    forward[0] = cp * cy;
    forward[1] = cp * sy;
    forward[2] = -sp;
    right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
    right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
    right[2] = -1 * sr * cp;
    up[0] = (cr * sp * cy + -sr * -sy);
    up[1] = (cr * sp * sy + -sr * cy);
    up[2] = cr * cp;
}

/*
================
R_ConcatRotations
================
*/
void R_ConcatRotations(const vec3 in1[3], const vec3 in2[3], vec3 out[3]) {
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
                in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
                in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
                in1[0][2] * in2[2][2];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
                in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
                in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
                in1[1][2] * in2[2][2];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
                in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
                in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
                in1[2][2] * in2[2][2];
}


/*
================
R_ConcatTransforms
================
*/
void R_ConcatTransforms(const vec4 in1[3], const vec4 in2[3], vec4 out[3]) {
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
                in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
                in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
                in1[0][2] * in2[2][2];
    out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
                in1[0][2] * in2[2][3] + in1[0][3];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
                in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
                in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
                in1[1][2] * in2[2][2];
    out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
                in1[1][2] * in2[2][3] + in1[1][3];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
                in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
                in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
                in1[2][2] * in2[2][2];
    out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
                in1[2][2] * in2[2][3] + in1[2][3];
}


/*
===================
FloorDivMod

Returns mathematically correct (floor-based) quotient and remainder for
numer and denom, both of which should contain no fractional part. The
quotient must fit in 32 bits.
====================
*/

void FloorDivMod(double numer, double denom, int *quotient,
                 int *rem) {
    int q = 0, r = 0;
    double x = NAN;

#ifndef PARANOID
    if (denom <= 0.0)
        Sys_Error ("FloorDivMod: bad denominator %d\n", denom);

//	if ((floor(numer) != numer) || (floor(denom) != denom))
//		Sys_Error ("FloorDivMod: non-integer numer or denom %f %f\n",
//				numer, denom);
#endif

    if (numer >= 0.0) {

        x = floor(numer / denom);
        q = (int) x;
        r = (int) floor(numer - (x * denom));
    } else {
        //
        // perform operations with positive values, and fix mod to make floor-based
        //
        x = floor(-numer / denom);
        q = -(int) x;
        r = (int) floor(-numer - (x * denom));
        if (r != 0) {
            q--;
            r = (int) denom - r;
        }
    }

    *quotient = q;
    *rem = r;
}


#if    !id386

// TODO: move to nonintel.c

/*
===================
Invert24To16

Inverts an 8.24 value to a 16.16 value
====================
*/

auto Invert24To16(fixed16_t val) -> fixed16_t {
    if (val < 256)
        return (0xFFFFFFFF);

    return (fixed16_t)
            (((double) 0x10000 * (double) 0x1000000 / (double) val) + 0.5);
}

#endif
