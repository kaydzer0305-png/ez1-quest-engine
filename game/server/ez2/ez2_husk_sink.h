//=============================================================================//
//
// Purpose: Minimal mapbase backplane -- the husk cast-target interface (M2)
//
// ez2_player.cpp asks "is this combine husk passive towards me?" by
// dynamic_cast-ing a target to CAI_HuskSink. Upstream that interface lives in
// ez2/npc_husk_base.h, whose CAI_BaseHusk template and the whole husk family
// are M3 roster work (npc_husk_base.h alone is ~1170 lines and pulls in
// ai_interactions, saverestore_utlvector and the shared husk header).
//
// So the interface is declared here, verbatim from upstream, and nothing
// implements it yet: every dynamic_cast fails cleanly, which is exactly the
// "missing NPCs spawn as errors, not crashes" behaviour M2 asks for.
//
// M3: delete this file and include "npc_husk_base.h" instead. The husk classes
// will then implement this same vtable, and the class definition must not exist
// twice. See TRANSPLANT-A §4.1.
//
//=============================================================================//
#ifndef EZ2_HUSK_SINK_H
#define EZ2_HUSK_SINK_H

#ifdef _WIN32
#pragma once
#endif

//-----------------------------------------------------------------------------
// Classes can cast to this and access some basic husk functions.
//-----------------------------------------------------------------------------
abstract_class CAI_HuskSink
{
public:

	// The NPC this husk is based off of
	virtual const char *GetBaseNPCClassname() = 0;

	virtual int GetHuskAggressionLevel() = 0;
	virtual int GetHuskCognitionFlags() = 0;

	virtual void MakeCalm( CBaseEntity *pActivator ) = 0;
	virtual void MakeSuspicious( CBaseEntity *pActivator ) = 0;
	virtual void MakeAngry( CBaseEntity *pActivator ) = 0;

	virtual bool IsSuspicious() = 0;
	virtual bool IsSuspiciousOrHigher() = 0;
	virtual bool IsAngry() = 0;

	virtual bool IsPassiveTarget( CBaseEntity *pTarget ) = 0;
	virtual bool IsHostileOverrideTarget( CBaseEntity *pTarget ) = 0;
};

#endif // EZ2_HUSK_SINK_H
