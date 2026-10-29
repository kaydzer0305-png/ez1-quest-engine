//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef GRENADE_HOPWIRE_H
#define GRENADE_HOPWIRE_H
#ifdef _WIN32
#pragma once
#endif

#include "basegrenade_shared.h"
#include "Sprite.h"

extern ConVar hopwire_trap;

#ifdef EZ2
// Minimal mapbase backplane (TRANSPLANT-A §4.1). The EZ2 displacer pistol
// derives from CDisplacerSink and builds a DisplacementInfo_t to hand to the
// interaction system; upstream declares both here next to CGravityVortexController,
// which itself derives from CDisplacerSink. The rest of mapbase's hopwire
// rewrite is M3+ work and is deliberately not ported.
class CDisplacerSink
{
public:
	CDisplacerSink() { }

	virtual void SpawnEffects( CBaseEntity *pEntity )	{};
};

// Displacement info for interactions
struct DisplacementInfo_t
{
	DisplacementInfo_t( CBaseEntity *displacer, CDisplacerSink *sink, const Vector *targetpos, const QAngle *targetang, CBaseEntity * owner = NULL )
	{
		pDisplacer = displacer;
		pSink = sink;
		vecTargetPos = targetpos;
		vecTargetAng = targetang;
		pOwner = owner;
	}

	CBaseEntity *pDisplacer;
	CDisplacerSink *pSink;
	const Vector *vecTargetPos;
	const QAngle *vecTargetAng;
	CBaseEntity *pOwner;
};
#endif

class CGravityVortexController : public CBaseEntity
{
	DECLARE_CLASS( CGravityVortexController, CBaseEntity );
	DECLARE_DATADESC();

public:
	CGravityVortexController( void ) : m_flEndTime( 0.0f ), m_flRadius( 256 ), m_flStrength( 256 ), m_flMass( 0.0f ) {}
	float	GetConsumedMass( void ) const;
	void	SetConsumedMass( float flMass ) { m_flMass = flMass; }
	void	SetConsumeRadius( float flRadius ) { m_flRadius = flRadius; }

	static CGravityVortexController *Create( const Vector &origin, float radius, float strength, float duration, CBaseEntity *pGrenade = NULL );

private:
	void	ConsumeEntity( CBaseEntity *pEnt );
	void	PullPlayersInRange( void );
	bool	KillNPCInRange( CBaseEntity *pVictim, IPhysicsObject **pPhysObj );
	void	CreateDenseBall( void );
	void	PullThink( void );
	void	StartPull( const Vector &origin, float radius, float strength, float duration );

	float	m_flMass;		// Mass consumed by the vortex
	float	m_flEndTime;	// Time when the vortex will stop functioning
	float	m_flRadius;		// Area of effect for the vortex
	float	m_flStrength;	// Pulling strength of the vortex
};

class CGrenadeHopwire : public CBaseGrenade
{
	DECLARE_CLASS( CGrenadeHopwire, CBaseGrenade );
	DECLARE_DATADESC();
	DECLARE_SERVERCLASS();

public:
	void	Spawn( void );
	void	Precache( void );
	bool	CreateVPhysics( void );
	void	SetTimer( float timer );
	void	SetVelocity( const Vector &velocity, const AngularImpulse &angVelocity );
	void	Detonate( void );
	
	void	EndThink( void );		// Last think before going away
	void	CombatThink( void );	// Makes the main explosion go off

protected:

	void	KillStriders( void );

	CHandle<CGravityVortexController>	m_hVortexController;
};

extern CBaseGrenade *HopWire_Create( const Vector &position, const QAngle &angles, const Vector &velocity, const AngularImpulse &angVelocity, CBaseEntity *pOwner, float timer );

#endif // GRENADE_HOPWIRE_H
