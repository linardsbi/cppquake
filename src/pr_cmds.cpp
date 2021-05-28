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

#include <cmath>
#include <string>
#include "quakedef.hpp"
#include "util.hpp"

#define	RETURN_EDICT(e) (((int *)pr_globals)[OFS_RETURN] = EDICT_TO_PROG(e))

/*
===============================================================================

						BUILT-IN FUNCTIONS

===============================================================================
*/

auto PF_VarString (int first) -> std::string
{
	std::string out;

	for (int i=first ; i<pr_argc ; i++)
	{
	    out += getGlobalString(OFS_PARM0+i*3);
	}
	//out += " boi";

	return out;
}


/*
=================
PF_errror

This is a TERMINAL error, which will kill off the entire server.
Dumps self.

error(value)
=================
*/
void PF_error ()
{
	Con_Printf ("======SERVER ERROR in %s:\n%s\n", getStringByOffset(pr_xfunction->s_name).data(), PF_VarString(0).c_str());
    edict_t	*ed = PROG_TO_EDICT(pr_global_struct->self);
	ED_Print (ed);

	Host_Error ("Program error");
}

/*
=================
PF_objerror

Dumps out self, then an error message.  The program is aborted and self is
removed, but the level can continue.

objerror(value)
=================
*/
void PF_objerror ()
{
	Con_Printf ("======OBJECT ERROR in %s:\n%s\n", getStringByOffset(pr_xfunction->s_name).data(), PF_VarString(0).c_str());
    edict_t	*ed = PROG_TO_EDICT(pr_global_struct->self);
	ED_Print (ed);
	ED_Free (ed);
	
	Host_Error ("Program error");
}



/*
==============
PF_makevectors

Writes newv values for v_forward, v_up, and v_right based on angles
makevectors(vector)
==============
*/
void PF_makevectors ()
{
	AngleVectors (G_VECTOR(OFS_PARM0), pr_global_struct->v_forward, pr_global_struct->v_right, pr_global_struct->v_up);
}

/*
=================
PF_setorigin

This is the only valid way to move an object without using the physics of the world (setting velocity and waiting).  Directly changing origin will not set internal links correctly, so clipping would be messed up.  This should be called when an object is spawned, and then only if it is teleported.

setorigin (entity, origin)
=================
*/
void PF_setorigin ()
{
	edict_t	*e = nullptr;
	float	*org = nullptr;
	
	e = G_EDICT(OFS_PARM0);
	org = G_VECTOR(OFS_PARM1);
	VectorCopy (org, e->v.origin);
	SV_LinkEdict (e, false);
}


void SetMinMaxSize (edict_t *e, float *min, float *max, qboolean rotate)
{
	float	*angles = nullptr;
	vec3_t	rmin, rmax;
	float	bounds[2][3];
	float	xvector[2], yvector[2];
	float	a = NAN;
	vec3_t	base, transformed;
	int		i = 0, j = 0, k = 0, l = 0;
	
	for (i=0 ; i<3 ; i++)
		if (min[i] > max[i])
			PR_RunError ("backwards mins/maxs");

	rotate = false;		// FIXME: implement rotation properly again

	if (!rotate)
	{
		VectorCopy (min, rmin);
		VectorCopy (max, rmax);
	}
	else
	{
	// find min / max for rotations
		angles = e->v.angles;
		
		a = angles[1]/180 * M_PI;
		
		xvector[0] = cos(a);
		xvector[1] = sin(a);
		yvector[0] = -sin(a);
		yvector[1] = cos(a);
		
		VectorCopy (min, bounds[0]);
		VectorCopy (max, bounds[1]);
		
		rmin[0] = rmin[1] = rmin[2] = 9999;
		rmax[0] = rmax[1] = rmax[2] = -9999;
		
		for (i=0 ; i<= 1 ; i++)
		{
			base[0] = bounds[i][0];
			for (j=0 ; j<= 1 ; j++)
			{
				base[1] = bounds[j][1];
				for (k=0 ; k<= 1 ; k++)
				{
					base[2] = bounds[k][2];
					
				// transform the point
					transformed[0] = xvector[0]*base[0] + yvector[0]*base[1];
					transformed[1] = xvector[1]*base[0] + yvector[1]*base[1];
					transformed[2] = base[2];
					
					for (l=0 ; l<3 ; l++)
					{
						if (transformed[l] < rmin[l])
							rmin[l] = transformed[l];
						if (transformed[l] > rmax[l])
							rmax[l] = transformed[l];
					}
				}
			}
		}
	}
	
// set derived values
	VectorCopy (rmin, e->v.mins);
	VectorCopy (rmax, e->v.maxs);
	VectorSubtract (max, min, e->v.size);
	
	SV_LinkEdict (e, false);
}

/*
=================
PF_setsize

the size box is rotated by the current angle

setsize (entity, minvector, maxvector)
=================
*/
void PF_setsize ()
{
	edict_t	*e = nullptr;
	float	*min = nullptr, *max = nullptr;
	
	e = G_EDICT(OFS_PARM0);
	min = G_VECTOR(OFS_PARM1);
	max = G_VECTOR(OFS_PARM2);
	SetMinMaxSize (e, min, max, false);
}


/*
=================
PF_setmodel

setmodel(entity, model)
=================
*/
void PF_setmodel ()
{
	char **check = nullptr;
	model_t	*mod = nullptr;
	int		i = 0;

    edict_t	*e = G_EDICT(OFS_PARM0);
    auto m = getGlobalStringOffsetPair(OFS_PARM1);

// check to see if model was properly precached
	for (i=0, check = sv.model_precache ; *check ; i++, check++)
		if (!Q_strcmp(*check, m.name))
			break;
			
	if (!*check)
		PR_RunError ("no precache: %s\n", m.name.c_str());

	e->v.model = m.offset;
	e->v.modelindex = i; //SV_ModelIndex (m);

	mod = sv.models[ (int)e->v.modelindex];  // Mod_ForName (m, true);
	
	if (mod)
		SetMinMaxSize (e, mod->mins, mod->maxs, true);
	else
		SetMinMaxSize (e, vec3_origin, vec3_origin, true);
}

/*
=================
PF_bprint

broadcast print to everyone on server

bprint(value)
=================
*/
void PF_bprint ()
{

	SV_BroadcastPrintf ("%s", PF_VarString(0).c_str());
}

/*
=================
PF_sprint

single print to a specific client

sprint(clientent, value)
=================
*/
void PF_sprint ()
{
	int entnum = G_EDICTNUM(OFS_PARM0);

	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to sprint to a non-client\n");
		return;
	}
		
	client_t* client = &svs.clients[entnum-1];
		
	MSG_WriteChar (&client->message,svc_print);
	MSG_WriteString (&client->message, PF_VarString(1) );
}


/*
=================
PF_centerprint

single print to a specific client

centerprint(clientent, value)
=================
*/
void PF_centerprint ()
{
	int entnum = G_EDICTNUM(OFS_PARM0);
	
	if (entnum < 1 || entnum > svs.maxclients)
	{
		Con_Printf ("tried to sprint to a non-client\n");
		return;
	}

    client_t *client = &svs.clients[entnum-1];
		
	MSG_WriteChar (&client->message,svc_centerprint);
	MSG_WriteString (&client->message, PF_VarString(1) );
}


/*
=================
PF_normalize

vector normalize(vector)
=================
*/
void PF_normalize ()
{
	float	*value1 = nullptr;
	vec3_t	newvvalue;
	float	newv = NAN;
	
	value1 = G_VECTOR(OFS_PARM0);

	newv = value1[0] * value1[0] + value1[1] * value1[1] + value1[2]*value1[2];
	newv = sqrt(newv);
	
	if (newv == 0)
		newvvalue[0] = newvvalue[1] = newvvalue[2] = 0;
	else
	{
		newv = 1/newv;
		newvvalue[0] = value1[0] * newv;
		newvvalue[1] = value1[1] * newv;
		newvvalue[2] = value1[2] * newv;
	}
	
	VectorCopy (newvvalue, G_VECTOR(OFS_RETURN));	
}

/*
=================
PF_vlen

scalar vlen(vector)
=================
*/
void PF_vlen ()
{
	float	*value1 = nullptr;
	float	newv = NAN;
	
	value1 = G_VECTOR(OFS_PARM0);

	newv = value1[0] * value1[0] + value1[1] * value1[1] + value1[2]*value1[2];
	newv = sqrt(newv);
	
	G_FLOAT(OFS_RETURN) = newv;
}

/*
=================
PF_vectoyaw

float vectoyaw(vector)
=================
*/
void PF_vectoyaw ()
{
	float	*value1 = nullptr;
	float	yaw = NAN;
	
	value1 = G_VECTOR(OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
		yaw = 0;
	else
	{
		yaw = (int) (atan2(value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;
	}

	G_FLOAT(OFS_RETURN) = yaw;
}


/*
=================
PF_vectoangles

vector vectoangles(vector)
=================
*/
void PF_vectoangles ()
{
	float	*value1 = nullptr;
	float	forward = NAN;
	float	yaw = NAN, pitch = NAN;
	
	value1 = G_VECTOR(OFS_PARM0);

	if (value1[1] == 0 && value1[0] == 0)
	{
		yaw = 0;
		if (value1[2] > 0)
			pitch = 90;
		else
			pitch = 270;
	}
	else
	{
		yaw = (int) (atan2(value1[1], value1[0]) * 180 / M_PI);
		if (yaw < 0)
			yaw += 360;

		forward = sqrt (value1[0]*value1[0] + value1[1]*value1[1]);
		pitch = (int) (atan2(value1[2], forward) * 180 / M_PI);
		if (pitch < 0)
			pitch += 360;
	}

	G_FLOAT(OFS_RETURN+0) = pitch;
	G_FLOAT(OFS_RETURN+1) = yaw;
	G_FLOAT(OFS_RETURN+2) = 0;
}

/*
=================
PF_Random

Returns a number from 0<= num < 1

random()
=================
*/
void PF_random ()
{
	float		num = NAN;
		
	num = (rand ()&0x7fff) / ((float)0x7fff);
	
	G_FLOAT(OFS_RETURN) = num;
}

/*
=================
PF_particle

particle(origin, color, count)
=================
*/
void PF_particle ()
{
	float		*org = nullptr, *dir = nullptr;
	float		color = NAN;
	float		count = NAN;
			
	org = G_VECTOR(OFS_PARM0);
	dir = G_VECTOR(OFS_PARM1);
	color = G_FLOAT(OFS_PARM2);
	count = G_FLOAT(OFS_PARM3);
	SV_StartParticle (org, dir, color, count);
}


/*
=================
PF_ambientsound

=================
*/
void PF_ambientsound ()
{
	char		**check = nullptr;
	float		*pos = nullptr;
	float 		vol = NAN, attenuation = NAN;
	int			i = 0, soundnum = 0;

	pos = G_VECTOR (OFS_PARM0);			
	std::string_view samp = getGlobalString(OFS_PARM1);
	vol = G_FLOAT(OFS_PARM2);
	attenuation = G_FLOAT(OFS_PARM3);
	
// check to see if samp was properly precached
	for (soundnum=0, check = sv.sound_precache ; *check ; check++, soundnum++)
		if (!Q_strcmp(*check,samp))
			break;
			
	if (!*check)
	{
		Con_Printf ("no precache: %s\n", samp.data());
		return;
	}

// add an svc_spawnambient command to the level signon packet

	MSG_WriteByte (&sv.signon,svc_spawnstaticsound);
	for (i=0 ; i<3 ; i++)
		MSG_WriteCoord(&sv.signon, pos[i]);

	MSG_WriteByte (&sv.signon, soundnum);

	MSG_WriteByte (&sv.signon, vol*255);
	MSG_WriteByte (&sv.signon, attenuation*64);

}

/*
=================
PF_sound

Each entity can have eight independant sound sources, like voice,
weapon, feet, etc.

Channel 0 is an auto-allocate channel, the others override anything
allready running on that entity/channel pair.

An attenuation of 0 will play full volume everywhere in the level.
Larger attenuations will drop off.

=================
*/
void PF_sound ()
{
	int			channel = 0;
	edict_t		*entity = nullptr;
	int 		volume = 0;
	float attenuation = NAN;
		
	entity = G_EDICT(OFS_PARM0);
	channel = G_FLOAT(OFS_PARM1);
	std::string_view sample = getGlobalString(OFS_PARM2);
	volume = G_FLOAT(OFS_PARM3) * 255;
	attenuation = G_FLOAT(OFS_PARM4);
	
	if (volume < 0 || volume > 255)
		Sys_Error ("SV_StartSound: volume = %i", volume);

	if (attenuation < 0 || attenuation > 4)
		Sys_Error ("SV_StartSound: attenuation = %f", attenuation);

	if (channel < 0 || channel > 7)
		Sys_Error ("SV_StartSound: channel = %i", channel);

	SV_StartSound (entity, channel, sample.data(), volume, attenuation);
}

/*
=================
PF_break

break()
=================
*/
void PF_break ()
{
Con_Printf ("break statement\n");
*(int *)-4 = 0;	// dump to debugger
//	PR_RunError ("break statement");
}

/*
=================
PF_traceline

Used for use tracing and shot targeting
Traces are blocked by bbox and exact bsp entityes, and also slide box entities
if the tryents flag is set.

traceline (vector1, vector2, tryents)
=================
*/
void PF_traceline ()
{
	float	*v1 = nullptr, *v2 = nullptr;
	trace_t	trace;
	int		nomonsters = 0;
	edict_t	*ent = nullptr;

	v1 = G_VECTOR(OFS_PARM0);
	v2 = G_VECTOR(OFS_PARM1);
	nomonsters = G_FLOAT(OFS_PARM2);
	ent = G_EDICT(OFS_PARM3);

	trace = SV_Move (v1, vec3_origin, vec3_origin, v2, nomonsters, ent);

	pr_global_struct->trace_allsolid = trace.allsolid;
	pr_global_struct->trace_startsolid = trace.startsolid;
	pr_global_struct->trace_fraction = trace.fraction;
	pr_global_struct->trace_inwater = trace.inwater;
	pr_global_struct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
	VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
	pr_global_struct->trace_plane_dist =  trace.plane.dist;	
	if (trace.ent)
		pr_global_struct->trace_ent = EDICT_TO_PROG(trace.ent);
	else
		pr_global_struct->trace_ent = EDICT_TO_PROG(sv.edicts);
}


#ifdef QUAKE2
extern trace_t SV_Trace_Toss (edict_t *ent, edict_t *ignore);

void PF_TraceToss (void)
{
	trace_t	trace;
	edict_t	*ent;
	edict_t	*ignore;

	ent = G_EDICT(OFS_PARM0);
	ignore = G_EDICT(OFS_PARM1);

	trace = SV_Trace_Toss (ent, ignore);

	pr_global_struct->trace_allsolid = trace.allsolid;
	pr_global_struct->trace_startsolid = trace.startsolid;
	pr_global_struct->trace_fraction = trace.fraction;
	pr_global_struct->trace_inwater = trace.inwater;
	pr_global_struct->trace_inopen = trace.inopen;
	VectorCopy (trace.endpos, pr_global_struct->trace_endpos);
	VectorCopy (trace.plane.normal, pr_global_struct->trace_plane_normal);
	pr_global_struct->trace_plane_dist =  trace.plane.dist;	
	if (trace.ent)
		pr_global_struct->trace_ent = EDICT_TO_PROG(trace.ent);
	else
		pr_global_struct->trace_ent = EDICT_TO_PROG(sv.edicts);
}
#endif


/*
=================
PF_checkpos

Returns true if the given entity can move to the given position from it's
current position by walking or rolling.
FIXME: make work...
scalar checkpos (entity, vector)
=================
*/
void PF_checkpos ()
{
}

//============================================================================

byte	checkpvs[MAX_MAP_LEAFS/8];

auto PF_newcheckclient (int check) -> int
{
	int		i = 0;
	byte	*pvs = nullptr;
	edict_t	*ent = nullptr;
	mleaf_t	*leaf = nullptr;
	vec3_t	org;

// cycle to the next one

	if (check < 1)
		check = 1;
	if (check > svs.maxclients)
		check = svs.maxclients;

	if (check == svs.maxclients)
		i = 1;
	else
		i = check + 1;

	for ( ;  ; i++)
	{
		if (i == svs.maxclients+1)
			i = 1;

		ent = EDICT_NUM(i);

		if (i == check)
			break;	// didn't find anything else

		if (ent->free)
			continue;
		if (ent->v.health <= 0)
			continue;
		if ((int)ent->v.flags & FL_NOTARGET)
			continue;

	// anything that is a client, or has a client as an enemy
		break;
	}

// get the PVS for the entity
	VectorAdd (ent->v.origin, ent->v.view_ofs, org);
	leaf = Mod_PointInLeaf (org, sv.worldmodel);
	pvs = Mod_LeafPVS (leaf, sv.worldmodel);
	memcpy (checkpvs, pvs, (sv.worldmodel->numleafs+7)>>3 );

	return i;
}

/*
=================
PF_checkclient

Returns a client (or object that has a client enemy) that would be a
valid target.

If there are more than one valid options, they are cycled each frame

If (self.origin + self.viewofs) is not in the PVS of the current target,
it is not returned at all.

name checkclient ()
=================
*/
#define	MAX_CHECK	16
int c_invis, c_notvis;
void PF_checkclient ()
{
	edict_t	*ent = nullptr, *self = nullptr;
	mleaf_t	*leaf = nullptr;
	int		l = 0;
	vec3_t	view;
	
// find a newv check if on a newv frame
	if (sv.time - sv.lastchecktime >= 0.1)
	{
		sv.lastcheck = PF_newcheckclient (sv.lastcheck);
		sv.lastchecktime = sv.time;
	}

// return check if it might be visible	
	ent = EDICT_NUM(sv.lastcheck);
	if (ent->free || ent->v.health <= 0)
	{
		RETURN_EDICT(sv.edicts);
		return;
	}

// if current entity can't possibly see the check entity, return 0
	self = PROG_TO_EDICT(pr_global_struct->self);
	VectorAdd (self->v.origin, self->v.view_ofs, view);
	leaf = Mod_PointInLeaf (view, sv.worldmodel);
	l = (leaf - sv.worldmodel->leafs) - 1;
	if ( (l<0) || !(checkpvs[l>>3] & (1<<(l&7)) ) )
	{
c_notvis++;
		RETURN_EDICT(sv.edicts);
		return;
	}

// might be able to see it
c_invis++;
	RETURN_EDICT(ent);
}

//============================================================================


/*
=================
PF_stuffcmd

Sends text over to the client's execution buffer

stuffcmd (clientent, value)
=================
*/
void PF_stuffcmd ()
{
	int		entnum = 0;
	char	*str = nullptr;
	client_t	*old = nullptr;
	
	entnum = G_EDICTNUM(OFS_PARM0);
	if (entnum < 1 || entnum > svs.maxclients)
		PR_RunError ("Parm 0 not a client");

	old = host_client;
	host_client = &svs.clients[entnum-1];
	Host_ClientCommands ("%s", getGlobalString(OFS_PARM1).data());
	host_client = old;
}

/*
=================
PF_localcmd

Sends text over to the client's execution buffer

localcmd (string)
=================
*/
void PF_localcmd ()
{
	Cbuf_AddText (getGlobalString(OFS_PARM0));
}

/*
=================
PF_cvar

float cvar (string)
=================
*/
void PF_cvar ()
{
	G_FLOAT(OFS_RETURN) = Cvar_VariableValue (const_cast<char*>(getGlobalString(OFS_PARM0).data()));
}

/*
=================
PF_cvar_set

float cvar (string)
=================
*/
void PF_cvar_set ()
{
	Cvar_Set (getGlobalString(OFS_PARM0).data(), getGlobalString(OFS_PARM1).data());
}

/*
=================
PF_findradius

Returns a chain of entities that have origins within a spherical area

findradius (origin, radius)
=================
*/
void PF_findradius ()
{
	edict_t	*ent = nullptr, *chain = nullptr;
	float	rad = NAN;
	float	*org = nullptr;
	vec3_t	eorg;
	int		i = 0, j = 0;

	chain = (edict_t *)sv.edicts;
	
	org = G_VECTOR(OFS_PARM0);
	rad = G_FLOAT(OFS_PARM1);

	ent = NEXT_EDICT(sv.edicts);
	for (i=1 ; i<sv.num_edicts ; i++, ent = NEXT_EDICT(ent))
	{
		if (ent->free)
			continue;
		if (ent->v.solid == SOLID_NOT)
			continue;
		for (j=0 ; j<3 ; j++)
			eorg[j] = org[j] - (ent->v.origin[j] + (ent->v.mins[j] + ent->v.maxs[j])*0.5);			
		if (Length(eorg) > rad)
			continue;
			
		ent->v.chain = EDICT_TO_PROG(chain);
		chain = ent;
	}

	RETURN_EDICT(chain);
}


/*
=========
PF_dprint
=========
*/
void PF_dprint ()
{
	Con_DPrintf ("%s", PF_VarString(0).c_str());
}

char	pr_string_temp[128];

void PF_ftos ()
{
	float	v = NAN;
	v = G_FLOAT(OFS_PARM0);
	
	if (v == (int)v)
		sprintf (pr_string_temp, "%d",(int)v);
	else
		sprintf (pr_string_temp, "%5.1f",v);
	G_INT(OFS_RETURN) = getOffsetByString(pr_string_temp);
//	G_INT(OFS_RETURN) = pr_string_temp - pr_strings;
}

void PF_fabs ()
{
	float	v = NAN;
	v = G_FLOAT(OFS_PARM0);
	G_FLOAT(OFS_RETURN) = std::fabs(v);
}

void PF_vtos ()
{
	sprintf (pr_string_temp, "'%5.1f %5.1f %5.1f'", G_VECTOR(OFS_PARM0)[0], G_VECTOR(OFS_PARM0)[1], G_VECTOR(OFS_PARM0)[2]);
	G_INT(OFS_RETURN) = getOffsetByString(pr_string_temp);
//	G_INT(OFS_RETURN) = pr_string_temp - pr_strings;
}

#ifdef QUAKE2
void PF_etos (void)
{
	sprintf (pr_string_temp, "entity %i", G_EDICTNUM(OFS_PARM0));
	G_INT(OFS_RETURN) = pr_string_temp - pr_strings;
}
#endif

void PF_Spawn ()
{
	edict_t	*ed = nullptr;
	ed = ED_Alloc();
	RETURN_EDICT(ed);
}

void PF_Remove ()
{
	edict_t	*ed = nullptr;
	
	ed = G_EDICT(OFS_PARM0);
	ED_Free (ed);
}


// entity (entity start, .string field, string match) find = #5;
void PF_Find ()
#ifdef QUAKE2
{
	int		e;	
	int		f;
	char	*s, *t;
	edict_t	*ed;
	edict_t	*first;
	edict_t	*second;
	edict_t	*last;

	first = second = last = (edict_t *)sv.edicts;
	e = G_EDICTNUM(OFS_PARM0);
	f = G_INT(OFS_PARM1);
	s = G_STRING(OFS_PARM2);
	if (!s)
		PR_RunError ("PF_Find: bad search string");
		
	for (e++ ; e < sv.num_edicts ; e++)
	{
		ed = EDICT_NUM(e);
		if (ed->free)
			continue;
		t = E_STRING(ed,f);
		if (!t)
			continue;
		if (!strcmp(t,s))
		{
			if (first == (edict_t *)sv.edicts)
				first = ed;
			else if (second == (edict_t *)sv.edicts)
				second = ed;
			ed->v.chain = EDICT_TO_PROG(last);
			last = ed;
		}
	}

	if (first != last)
	{
		if (last != second)
			first->v.chain = last->v.chain;
		else
			first->v.chain = EDICT_TO_PROG(last);
		last->v.chain = EDICT_TO_PROG((edict_t *)sv.edicts);
		if (second && second != last)
			second->v.chain = EDICT_TO_PROG(last);
	}
	RETURN_EDICT(first);
}
#else
{
	int e = G_EDICTNUM(OFS_PARM0);
	std::string_view s = getGlobalString(OFS_PARM2);

	if (s.empty())
		PR_RunError ("PF_Find: bad search string");
		
	for (e++ ; e < sv.num_edicts ; e++)
	{
        edict_t	*ed = EDICT_NUM(e);
		if (ed->free)
			continue;

		auto t = getEdictString(G_INT(OFS_PARM1), ed);

		if (t.empty())
			continue;
		if (!Q_strcmp(t,s))
		{
			RETURN_EDICT(ed);
			return;
		}
	}

	RETURN_EDICT(sv.edicts);
}
#endif

void PR_CheckEmptyString (const char *s)
{
	if (s[0] <= ' ')
		PR_RunError ("Bad string");
}

void PF_precache_file ()
{	// precache_file is only used to copy files with qcc, it does nothing
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);
}

void PF_precache_sound ()
{
	if (sv.state != ss_loading)
		PR_RunError ("PF_Precache_*: Precache can only be done in spawn functions");
		
	std::string_view s = getGlobalString(OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);

	if (s.empty()) {
        PR_RunError ("Bad string");
	}
	
	for (auto & i : sv.sound_precache)
	{
		if (!i)
		{
            i = const_cast<char*>(s.data());
			return;
		}
		if (!Q_strcmp(i, s))
			return;
	}
	PR_RunError ("PF_precache_sound: overflow");
}

void PF_precache_model ()
{
	if (sv.state != ss_loading)
		PR_RunError ("PF_Precache_*: Precache can only be done in spawn functions");


	std::string_view s = getGlobalString(OFS_PARM0);
	G_INT(OFS_RETURN) = G_INT(OFS_PARM0);

    if (s.empty()) {
        PR_RunError ("Bad string");
    }

	for (std::size_t i=0 ; i<MAX_MODELS ; i++)
	{
		if (!sv.model_precache[i])
		{
			sv.model_precache[i] = const_cast<char*>(s.data());

			sv.models[i] = Mod_ForName (s.data(), true);
			return;
		}
		if (!Q_strcmp(sv.model_precache[i], s))
			return;
	}
	PR_RunError ("PF_precache_model: overflow");
}


void PF_coredump ()
{
	ED_PrintEdicts ();
}

void PF_traceon ()
{
	pr_trace = true;
}

void PF_traceoff ()
{
	pr_trace = false;
}

void PF_eprint ()
{
	ED_PrintNum (G_EDICTNUM(OFS_PARM0));
}

/*
===============
PF_walkmove

float(float yaw, float dist) walkmove
===============
*/
void PF_walkmove ()
{
	edict_t	*ent = nullptr;
	float	yaw = NAN, dist = NAN;
	vec3_t	move;
	dfunction_t	*oldf = nullptr;
	int 	oldself = 0;
	
	ent = PROG_TO_EDICT(pr_global_struct->self);
	yaw = G_FLOAT(OFS_PARM0);
	dist = G_FLOAT(OFS_PARM1);
	
	if ( !( (int)ent->v.flags & (FL_ONGROUND|FL_FLY|FL_SWIM) ) )
	{
		G_FLOAT(OFS_RETURN) = 0;
		return;
	}

	yaw = yaw*M_PI*2 / 360;
	
	move[0] = cos(yaw)*dist;
	move[1] = sin(yaw)*dist;
	move[2] = 0;

// save program state, because SV_movestep may call other progs
	oldf = pr_xfunction;
	oldself = pr_global_struct->self;
	
	G_FLOAT(OFS_RETURN) = SV_movestep(ent, move, true);
	
	
// restore program state
	pr_xfunction = oldf;
	pr_global_struct->self = oldself;
}

/*
===============
PF_droptofloor

void() droptofloor
===============
*/
void PF_droptofloor ()
{
	edict_t		*ent = nullptr;
	vec3_t		end;
	trace_t		trace;
	
	ent = PROG_TO_EDICT(pr_global_struct->self);

	VectorCopy (ent->v.origin, end);
	end[2] -= 256;
	
	trace = SV_Move (ent->v.origin, ent->v.mins, ent->v.maxs, end, false, ent);

	if (trace.fraction == 1 || trace.allsolid)
		G_FLOAT(OFS_RETURN) = 0;
	else
	{
		VectorCopy (trace.endpos, ent->v.origin);
		SV_LinkEdict (ent, false);
		ent->v.flags = (int)ent->v.flags | FL_ONGROUND;
		ent->v.groundentity = EDICT_TO_PROG(trace.ent);
		G_FLOAT(OFS_RETURN) = 1;
	}
}

/*
===============
PF_lightstyle

void(float style, string value) lightstyle
===============
*/
void PF_lightstyle ()
{
	int style = G_FLOAT(OFS_PARM0);

// change the string in sv
	sv.lightstyles[style] = const_cast<char*>(getGlobalString(OFS_PARM1).data());
	
// send message to all clients on this server
	if (sv.state != ss_active)
		return;

    auto *client = svs.clients;
	for (auto j=0 ; j<svs.maxclients ; j++, client++) {
        if (client->active || client->spawned)
        {
            MSG_WriteChar (&client->message, svc_lightstyle);
            MSG_WriteChar (&client->message,style);
            MSG_WriteString (&client->message, getGlobalString(OFS_PARM1));
        }
	}
}

void PF_rint ()
{
	float	f = NAN;
	f = G_FLOAT(OFS_PARM0);
	if (f > 0)
		G_FLOAT(OFS_RETURN) = (int)(f + 0.5);
	else
		G_FLOAT(OFS_RETURN) = (int)(f - 0.5);
}
void PF_floor ()
{
	G_FLOAT(OFS_RETURN) = floor(G_FLOAT(OFS_PARM0));
}
void PF_ceil ()
{
	G_FLOAT(OFS_RETURN) = ceil(G_FLOAT(OFS_PARM0));
}


/*
=============
PF_checkbottom
=============
*/
void PF_checkbottom ()
{
	edict_t	*ent = nullptr;
	
	ent = G_EDICT(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = SV_CheckBottom (ent);
}

/*
=============
PF_pointcontents
=============
*/
void PF_pointcontents ()
{
	float	*v = nullptr;
	
	v = G_VECTOR(OFS_PARM0);

	G_FLOAT(OFS_RETURN) = SV_PointContents (v);	
}

/*
=============
PF_nextent

entity nextent(entity)
=============
*/
void PF_nextent ()
{
	int		i = 0;
	edict_t	*ent = nullptr;
	
	i = G_EDICTNUM(OFS_PARM0);
	while (true)
	{
		i++;
		if (i == sv.num_edicts)
		{
			RETURN_EDICT(sv.edicts);
			return;
		}
		ent = EDICT_NUM(i);
		if (!ent->free)
		{
			RETURN_EDICT(ent);
			return;
		}
	}
}

/*
=============
PF_aim

Pick a vector for the player to shoot along
vector aim(entity, missilespeed)
=============
*/
cvar_t	sv_aim = {"sv_aim", "0.93"};
void PF_aim ()
{
	edict_t	*ent = nullptr, *check = nullptr, *bestent = nullptr;
	vec3_t	start, dir, end, bestdir;
	int		i = 0, j = 0;
	trace_t	tr;
	float	dist = NAN, bestdist = NAN;
	float	speed = NAN;
	
	ent = G_EDICT(OFS_PARM0);
	speed = G_FLOAT(OFS_PARM1);

	VectorCopy (ent->v.origin, start);
	start[2] += 20;

// try sending a trace straight
	VectorCopy (pr_global_struct->v_forward, dir);
	VectorMA (start, 2048, dir, end);
	tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);
	if (tr.ent && tr.ent->v.takedamage == DAMAGE_AIM
	&& (!teamplay.value || ent->v.team <=0 || ent->v.team != tr.ent->v.team) )
	{
		VectorCopy (pr_global_struct->v_forward, G_VECTOR(OFS_RETURN));
		return;
	}


// try all possible entities
	VectorCopy (dir, bestdir);
	bestdist = sv_aim.value;
	bestent = nullptr;
	
	check = NEXT_EDICT(sv.edicts);
	for (i=1 ; i<sv.num_edicts ; i++, check = NEXT_EDICT(check) )
	{
		if (check->v.takedamage != DAMAGE_AIM)
			continue;
		if (check == ent)
			continue;
		if (teamplay.value && ent->v.team > 0 && ent->v.team == check->v.team)
			continue;	// don't aim at teammate
		for (j=0 ; j<3 ; j++)
			end[j] = check->v.origin[j]
			+ 0.5*(check->v.mins[j] + check->v.maxs[j]);
		VectorSubtract (end, start, dir);
		VectorNormalize (dir);
		dist = DotProduct (dir, pr_global_struct->v_forward);
		if (dist < bestdist)
			continue;	// to far to turn
		tr = SV_Move (start, vec3_origin, vec3_origin, end, false, ent);
		if (tr.ent == check)
		{	// can shoot at this one
			bestdist = dist;
			bestent = check;
		}
	}
	
	if (bestent)
	{
		VectorSubtract (bestent->v.origin, ent->v.origin, dir);
		dist = DotProduct (dir, pr_global_struct->v_forward);
		VectorScale (pr_global_struct->v_forward, dist, end);
		end[2] = dir[2];
		VectorNormalize (end);
		VectorCopy (end, G_VECTOR(OFS_RETURN));	
	}
	else
	{
		VectorCopy (bestdir, G_VECTOR(OFS_RETURN));
	}
}

/*
==============
PF_changeyaw

This was a major timewaster in progs, so it was converted to C
==============
*/
void PF_changeyaw ()
{
	edict_t		*ent = nullptr;
	float		ideal = NAN, current = NAN, move = NAN, speed = NAN;
	
	ent = PROG_TO_EDICT(pr_global_struct->self);
	current = anglemod( ent->v.angles[1] );
	ideal = ent->v.ideal_yaw;
	speed = ent->v.yaw_speed;
	
	if (current == ideal)
		return;
	move = ideal - current;
	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
			move = move + 360;
	}
	if (move > 0)
	{
		if (move > speed)
			move = speed;
	}
	else
	{
		if (move < -speed)
			move = -speed;
	}
	
	ent->v.angles[1] = anglemod (current + move);
}

#ifdef QUAKE2
/*
==============
PF_changepitch
==============
*/
void PF_changepitch (void)
{
	edict_t		*ent;
	float		ideal, current, move, speed;
	
	ent = G_EDICT(OFS_PARM0);
	current = anglemod( ent->v.angles[0] );
	ideal = ent->v.idealpitch;
	speed = ent->v.pitch_speed;
	
	if (current == ideal)
		return;
	move = ideal - current;
	if (ideal > current)
	{
		if (move >= 180)
			move = move - 360;
	}
	else
	{
		if (move <= -180)
			move = move + 360;
	}
	if (move > 0)
	{
		if (move > speed)
			move = speed;
	}
	else
	{
		if (move < -speed)
			move = -speed;
	}
	
	ent->v.angles[0] = anglemod (current + move);
}
#endif

/*
===============================================================================

MESSAGE WRITING

===============================================================================
*/

#define	MSG_BROADCAST	0		// unreliable to all
#define	MSG_ONE			1		// reliable to one (msg_entity)
#define	MSG_ALL			2		// reliable to all
#define	MSG_INIT		3		// write to the init string

auto WriteDest () -> sizebuf_t *
{
	int		entnum = 0;
	int		dest = 0;
	edict_t	*ent = nullptr;

	dest = G_FLOAT(OFS_PARM0);
	switch (dest)
	{
	case MSG_BROADCAST:
		return &sv.datagram;
	
	case MSG_ONE:
		ent = PROG_TO_EDICT(pr_global_struct->msg_entity);
		entnum = NUM_FOR_EDICT(ent);
		if (entnum < 1 || entnum > svs.maxclients)
			PR_RunError ("WriteDest: not a client");
		return &svs.clients[entnum-1].message;
		
	case MSG_ALL:
		return &sv.reliable_datagram;
	
	case MSG_INIT:
		return &sv.signon;

	default:
		PR_RunError ("WriteDest: bad destination");
		break;
	}
	
	return nullptr;
}

void PF_WriteByte ()
{
	MSG_WriteByte (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteChar ()
{
	MSG_WriteChar (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteShort ()
{
	MSG_WriteShort (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteLong ()
{
	MSG_WriteLong (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteAngle ()
{
	MSG_WriteAngle (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteCoord ()
{
	MSG_WriteCoord (WriteDest(), G_FLOAT(OFS_PARM1));
}

void PF_WriteString ()
{
	MSG_WriteString (WriteDest(), getGlobalString(OFS_PARM1));
}


void PF_WriteEntity ()
{
	MSG_WriteShort (WriteDest(), G_EDICTNUM(OFS_PARM1));
}

//=============================================================================



void PF_makestatic ()
{
	edict_t	*ent = G_EDICT(OFS_PARM0);

	MSG_WriteByte (&sv.signon,svc_spawnstatic);

	MSG_WriteByte (&sv.signon, ent->v.modelindex);

	MSG_WriteByte (&sv.signon, ent->v.frame);
	MSG_WriteByte (&sv.signon, ent->v.colormap);
	MSG_WriteByte (&sv.signon, ent->v.skin);

	for (std::size_t i=0 ; i<3 ; i++)
	{
		MSG_WriteCoord(&sv.signon, ent->v.origin[i]);
		MSG_WriteAngle(&sv.signon, ent->v.angles[i]);
	}

// throw the entity away now
	ED_Free (ent);
}

//=============================================================================

/*
==============
PF_setspawnparms
==============
*/
void PF_setspawnparms ()
{
	edict_t	*ent = nullptr;
	int		i = 0;
	client_t	*client = nullptr;

	ent = G_EDICT(OFS_PARM0);
	i = NUM_FOR_EDICT(ent);
	if (i < 1 || i > svs.maxclients)
		PR_RunError ("Entity is not a client");

	// copy spawn parms out of the client_t
	client = svs.clients + (i-1);

	for (i=0 ; i< NUM_SPAWN_PARMS ; i++)
		(&pr_global_struct->parm1)[i] = client->spawn_parms[i];
}

/*
==============
PF_changelevel
==============
*/
void PF_changelevel ()
{
#ifdef QUAKE2
	char	*s1, *s2;

	if (svs.changelevel_issued)
		return;
	svs.changelevel_issued = true;

	s1 = G_STRING(OFS_PARM0);
	s2 = G_STRING(OFS_PARM1);

	if ((int)pr_global_struct->serverflags & (SFL_new_UNIT | SFL_new_EPISODE))
		Cbuf_AddText (va("changelevel %s %s\n",s1, s2));
	else
		Cbuf_AddText (va("changelevel2 %s %s\n",s1, s2));
#else

// make sure we don't issue two changelevels
	if (svs.changelevel_issued)
		return;
	svs.changelevel_issued = true;

	Cbuf_AddText (va("changelevel %s\n", getGlobalString(OFS_PARM0)));
#endif
}


#ifdef QUAKE2

#define	CONTENT_WATER	-3
#define CONTENT_SLIME	-4
#define CONTENT_LAVA	-5

#define FL_IMMUNE_WATER	131072
#define	FL_IMMUNE_SLIME	262144
#define FL_IMMUNE_LAVA	524288

#define	CHAN_VOICE	2
#define	CHAN_BODY	4

#define	ATTN_NORM	1

void PF_WaterMove (void)
{
	edict_t		*self;
	int			flags;
	int			waterlevel;
	int			watertype;
	float		drownlevel;
	float		damage = 0.0;

	self = PROG_TO_EDICT(pr_global_struct->self);

	if (self->v.movetype == MOVETYPE_NOCLIP)
	{
		self->v.air_finished = sv.time + 12;
		G_FLOAT(OFS_RETURN) = damage;
		return;
	}

	if (self->v.health < 0)
	{
		G_FLOAT(OFS_RETURN) = damage;
		return;
	}

	if (self->v.deadflag == DEAD_NO)
		drownlevel = 3;
	else
		drownlevel = 1;

	flags = (int)self->v.flags;
	waterlevel = (int)self->v.waterlevel;
	watertype = (int)self->v.watertype;

	if (!(flags & (FL_IMMUNE_WATER + FL_GODMODE)))
		if (((flags & FL_SWIM) && (waterlevel < drownlevel)) || (waterlevel >= drownlevel))
		{
			if (self->v.air_finished < sv.time)
				if (self->v.pain_finished < sv.time)
				{
					self->v.dmg = self->v.dmg + 2;
					if (self->v.dmg > 15)
						self->v.dmg = 10;
//					T_Damage (self, world, world, self.dmg, 0, FALSE);
					damage = self->v.dmg;
					self->v.pain_finished = sv.time + 1.0;
				}
		}
		else
		{
			if (self->v.air_finished < sv.time)
//				sound (self, CHAN_VOICE, "player/gasp2.wav", 1, ATTN_NORM);
				SV_StartSound (self, CHAN_VOICE, "player/gasp2.wav", 255, ATTN_NORM);
			else if (self->v.air_finished < sv.time + 9)
//				sound (self, CHAN_VOICE, "player/gasp1.wav", 1, ATTN_NORM);
				SV_StartSound (self, CHAN_VOICE, "player/gasp1.wav", 255, ATTN_NORM);
			self->v.air_finished = sv.time + 12.0;
			self->v.dmg = 2;
		}
	
	if (!waterlevel)
	{
		if (flags & FL_INWATER)
		{	
			// play leave water sound
//			sound (self, CHAN_BODY, "misc/outwater.wav", 1, ATTN_NORM);
			SV_StartSound (self, CHAN_BODY, "misc/outwater.wav", 255, ATTN_NORM);
			self->v.flags = (float)(flags &~FL_INWATER);
		}
		self->v.air_finished = sv.time + 12.0;
		G_FLOAT(OFS_RETURN) = damage;
		return;
	}

	if (watertype == CONTENT_LAVA)
	{	// do damage
		if (!(flags & (FL_IMMUNE_LAVA + FL_GODMODE)))
			if (self->v.dmgtime < sv.time)
			{
				if (self->v.radsuit_finished < sv.time)
					self->v.dmgtime = sv.time + 0.2;
				else
					self->v.dmgtime = sv.time + 1.0;
//				T_Damage (self, world, world, 10*self.waterlevel, 0, TRUE);
				damage = (float)(10*waterlevel);
			}
	}
	else if (watertype == CONTENT_SLIME)
	{	// do damage
		if (!(flags & (FL_IMMUNE_SLIME + FL_GODMODE)))
			if (self->v.dmgtime < sv.time && self->v.radsuit_finished < sv.time)
			{
				self->v.dmgtime = sv.time + 1.0;
//				T_Damage (self, world, world, 4*self.waterlevel, 0, TRUE);
				damage = (float)(4*waterlevel);
			}
	}
	
	if ( !(flags & FL_INWATER) )
	{	

// player enter water sound
		if (watertype == CONTENT_LAVA)
//			sound (self, CHAN_BODY, "player/inlava.wav", 1, ATTN_NORM);
			SV_StartSound (self, CHAN_BODY, "player/inlava.wav", 255, ATTN_NORM);
		if (watertype == CONTENT_WATER)
//			sound (self, CHAN_BODY, "player/inh2o.wav", 1, ATTN_NORM);
			SV_StartSound (self, CHAN_BODY, "player/inh2o.wav", 255, ATTN_NORM);
		if (watertype == CONTENT_SLIME)
//			sound (self, CHAN_BODY, "player/slimbrn2.wav", 1, ATTN_NORM);
			SV_StartSound (self, CHAN_BODY, "player/slimbrn2.wav", 255, ATTN_NORM);

		self->v.flags = (float)(flags | FL_INWATER);
		self->v.dmgtime = 0;
	}
	
	if (! (flags & FL_WATERJUMP) )
	{
//		self.velocity = self.velocity - 0.8*self.waterlevel*frametime*self.velocity;
		VectorMA (self->v.velocity, -0.8 * self->v.waterlevel * host_frametime, self->v.velocity, self->v.velocity);
	}

	G_FLOAT(OFS_RETURN) = damage;
}


void PF_sin (void)
{
	G_FLOAT(OFS_RETURN) = sin(G_FLOAT(OFS_PARM0));
}

void PF_cos (void)
{
	G_FLOAT(OFS_RETURN) = cos(G_FLOAT(OFS_PARM0));
}

void PF_sqrt (void)
{
	G_FLOAT(OFS_RETURN) = sqrt(G_FLOAT(OFS_PARM0));
}
#endif

void PF_Fixme ()
{
	PR_RunError ("unimplemented bulitin");
}

#include <array>
std::array pr_builtin =
{
PF_Fixme,
PF_makevectors,	// void(entity e)	makevectors 		= #1;
PF_setorigin,	// void(entity e, vector o) setorigin	= #2;
PF_setmodel,	// void(entity e, string m) setmodel	= #3;
PF_setsize,	// void(entity e, vector min, vector max) setsize = #4;
PF_Fixme,	// void(entity e, vector min, vector max) setabssize = #5;
PF_break,	// void() break						= #6;
PF_random,	// float() random						= #7;
PF_sound,	// void(entity e, float chan, string samp) sound = #8;
PF_normalize,	// vector(vector v) normalize			= #9;
PF_error,	// void(string e) error				= #10;
PF_objerror,	// void(string e) objerror				= #11;
PF_vlen,	// float(vector v) vlen				= #12;
PF_vectoyaw,	// float(vector v) vectoyaw		= #13;
PF_Spawn,	// entity() spawn						= #14;
PF_Remove,	// void(entity e) remove				= #15;
PF_traceline,	// float(vector v1, vector v2, float tryents) traceline = #16;
PF_checkclient,	// entity() clientlist					= #17;
PF_Find,	// entity(entity start, .string fld, string match) find = #18;
PF_precache_sound,	// void(string s) precache_sound		= #19;
PF_precache_model,	// void(string s) precache_model		= #20;
PF_stuffcmd,	// void(entity client, string s)stuffcmd = #21;
PF_findradius,	// entity(vector org, float rad) findradius = #22;
PF_bprint,	// void(string s) bprint				= #23;
PF_sprint,	// void(entity client, string s) sprint = #24;
PF_dprint,	// void(string s) dprint				= #25;
PF_ftos,	// void(string s) ftos				= #26;
PF_vtos,	// void(string s) vtos				= #27;
PF_coredump,
PF_traceon,
PF_traceoff,
PF_eprint,	// void(entity e) debug print an entire entity
PF_walkmove, // float(float yaw, float dist) walkmove
PF_Fixme, // float(float yaw, float dist) walkmove
PF_droptofloor,
PF_lightstyle,
PF_rint,
PF_floor,
PF_ceil,
PF_Fixme,
PF_checkbottom,
PF_pointcontents,
PF_Fixme,
PF_fabs,
PF_aim,
PF_cvar,
PF_localcmd,
PF_nextent,
PF_particle,
PF_changeyaw,
PF_Fixme,
PF_vectoangles,

PF_WriteByte,
PF_WriteChar,
PF_WriteShort,
PF_WriteLong,
PF_WriteCoord,
PF_WriteAngle,
PF_WriteString,
PF_WriteEntity,

#ifdef QUAKE2
PF_sin,
PF_cos,
PF_sqrt,
PF_changepitch,
PF_TraceToss,
PF_etos,
PF_WaterMove,
#else
PF_Fixme,
PF_Fixme,
PF_Fixme,
PF_Fixme,
PF_Fixme,
PF_Fixme,
PF_Fixme,
#endif

SV_MoveToGoal,
PF_precache_file,
PF_makestatic,

PF_changelevel,
PF_Fixme,

PF_cvar_set,
PF_centerprint,

PF_ambientsound,

PF_precache_model,
PF_precache_sound,		// precache_sound2 is different only for qcc
PF_precache_file,

PF_setspawnparms
};

builtin_t *pr_builtins = pr_builtin.data();
constexpr int pr_numbuiltins = sizeof(pr_builtin)/sizeof(pr_builtin[0]);

