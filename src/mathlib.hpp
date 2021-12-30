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
// mathlib.h
#pragma once
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>
#include <glm/gtx/perpendicular.hpp>
#include <utility>
#include <numbers>

using fixed4_t = int;
using fixed8_t = int;
using fixed16_t = int;

using vec_t = float;

#ifndef M_PI
constexpr auto M_PI = std::numbers::pi;
#endif

using glm::vec2;
using glm::vec3;
using glm::vec4;
using vec5_t = float[5];

struct mplane_s;

static const vec3 vec3_origin(0,0,0);
extern int nanmask;

#define    IS_NAN(x) (((*(int *)&x)&nanmask)==nanmask)

auto VectorNormalize(vec3 &v) -> float;

inline void VectorMA(vec3 veca, double scale, vec3 vecb, vec3 &vecc) {
    vecc = veca + static_cast<float>(scale) * vecb;
}

void R_ConcatRotations(const vec3 in1[3], const vec3 in2[3], vec3 out[3]);
void R_ConcatTransforms(const vec4 in1[3], const vec4 in2[3], vec4 out[3]);

void FloorDivMod(double numer, double denom, int *quotient,
                 int *rem);

constexpr auto GreatestCommonDivisor(auto a, auto b) {
  while (b > 0) {
    a = std::exchange(b, a % b);
  }
  return a;
}

void AngleVectors(vec3 angles, vec3 &forward, vec3 &right, vec3 &up);
int BoxOnPlaneSide(vec3 emins, vec3 emaxs, struct mplane_s *plane);

constexpr float anglemod(float a) {
#if 0
    if (a >= 0)
        a -= 360*(int)(a/360);
    else
        a += 360*( 1 + (int)(-a/360) );
#endif
  a = (360.0 / 65536) * ((int) (a * (65536 / 360.0)) & 65535);
  return a;
}

constexpr vec4 to_vec4(const auto vec) {
  return {vec[0], vec[1], vec[2], 0.F};
}


#define BOX_ON_PLANE_SIDE(emins, emaxs, p)    \
    (((p)->type < 3)?                        \
    (                                        \
        ((p)->dist <= (emins)[(p)->type])?    \
            1                                \
        :                                    \
        (                                    \
            ((p)->dist >= (emaxs)[(p)->type])?\
                2                            \
            :                                \
                3                            \
        )                                    \
    )                                        \
    :                                        \
        BoxOnPlaneSide( (emins), (emaxs), (p)))
