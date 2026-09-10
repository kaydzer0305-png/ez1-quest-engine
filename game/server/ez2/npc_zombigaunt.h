//=============================================================================//
#include "npc_vortigaunt_episodic.h"

class CNPC_Zombigaunt : public CNPC_Vortigaunt
{
	DECLARE_CLASS( CNPC_Zombigaunt, CNPC_Vortigaunt );

public:
	virtual void	Spawn( void );
	virtual void	Precache( void );

protected:
	int					GetNumGlows() { return 0; }
	virtual Class_T		Classify ( void ) { return CLASS_ZOMBIE; }
	virtual bool	ShouldMoveAndShoot( void ) { return false; }
	virtual Activity	NPC_TranslateActivity( Activity eNewActivity );
	virtual void		HandleAnimEvent( animevent_t *pEvent );
	bool IsJumpLegal( const Vector &startPos, const Vector &apex, const Vector &endPos ) const;
	bool MovementCost( int moveType, const Vector &vecStart, const Vector &vecEnd, float *pCost );
	virtual bool CanRunAScriptedNPCInteraction( bool bForced = false ) { return false;  }
	virtual float GetNextRangeAttackTime( void ) { return gpGlobals->curtime + random->RandomFloat( 5.0f, 10.0f ); }

public:
	DECLARE_DATADESC();
};
