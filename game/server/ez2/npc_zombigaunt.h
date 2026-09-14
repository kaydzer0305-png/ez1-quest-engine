//=============================================================================//
//
// Purpose: Unfortunate vortiguants that have succumbed to headcrabs
// 		either in Xen or in the temporal anomaly in the Arctic.
//
//=============================================================================//

#include "npc_vortigaunt_episodic.h"

// Compatibility values normally supplied by the EZ2 AI base.
#ifndef EZ_VARIANT_RAD
#define EZ_VARIANT_RAD 2
#endif
#ifndef BLOOD_COLOR_BLUE
#define BLOOD_COLOR_BLUE BLOOD_COLOR_GREEN
#endif
#ifndef BLOOD_COLOR_ZOMBIE
#define BLOOD_COLOR_ZOMBIE BLOOD_COLOR_GREEN
#endif

//=========================================================
//	>> CNPC_Zombigaunt
//=========================================================
class CNPC_Zombigaunt : public CNPC_Vortigaunt
{
	DECLARE_CLASS( CNPC_Zombigaunt, CNPC_Vortigaunt );

public:
	virtual void	Spawn( void );
	virtual void	Precache( void );

protected:
	// EZ2 stores these in its Vortigaunt/AI base. Keep local compatibility
	// state while retaining the supported Android engine implementation.
	int					m_tEzVariant = 0;
	float				m_fGlowAge = 0.0f;

	void StartHandGlow( int beamType, int nHand ) {}
	void EndHandGlow( int beamType = VORTIGAUNT_BEAM_ALL ) {}
	void ClawAttack( float flDist, float flDamage, const QAngle &viewPunch, const Vector &velocityPunch, int bloodOrigin, int damageType )
	{
		CBaseEntity *pHurt = CheckTraceHullAttack( flDist, Vector( -16, -16, -16 ), Vector( 16, 16, 16 ), flDamage, damageType );
		if ( pHurt )
		{
			pHurt->ViewPunch( viewPunch );
			pHurt->ApplyAbsVelocityImpulse( velocityPunch );
		}
	}

	// Glowing eyes
	int					GetNumGlows() { return 0; } // No glows under headcrabs

	virtual Class_T		Classify ( void ) { return CLASS_ZOMBIE; }
	
	virtual bool	ShouldMoveAndShoot( void ) { return false; } // Zombigaunts never move and shoot, even if normal Vortigaunts would

	virtual Activity	NPC_TranslateActivity( Activity eNewActivity );

	virtual void		HandleAnimEvent( animevent_t *pEvent );

	bool IsJumpLegal( const Vector &startPos, const Vector &apex, const Vector &endPos ) const;
	bool MovementCost( int moveType, const Vector &vecStart, const Vector &vecEnd, float *pCost );

	// Since all dynamic interactions are shared with vortigaunts, zombigaunts cannot use dynamic interactions
	virtual bool CanRunAScriptedNPCInteraction( bool bForced = false ) { return false;  }

	// Zombigaunts have a much slower recharge time than vortigaunts, making them more likely to close in for the kill
	virtual float GetNextRangeAttackTime( void ) { return gpGlobals->curtime + random->RandomFloat( 5.0f, 10.0f ); }

public:
	DECLARE_DATADESC();
};
