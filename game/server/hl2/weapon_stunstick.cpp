//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Stun Stick- beating stick with a zappy end
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "npc_metropolice.h"
#include "weapon_stunstick.h"
#include "IEffects.h"
#include "in_buttons.h"
#include "soundent.h"
#include "rumble_shared.h"
#include "gamestats.h"
#ifdef EZ
#include "RagdollBoogie.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

ConVar    sk_plr_dmg_stunstick	( "sk_plr_dmg_stunstick","0");
ConVar    sk_npc_dmg_stunstick	( "sk_npc_dmg_stunstick","0");

extern ConVar metropolice_move_and_melee;

//-----------------------------------------------------------------------------
// CWeaponStunStick
//-----------------------------------------------------------------------------

IMPLEMENT_SERVERCLASS_ST(CWeaponStunStick, DT_WeaponStunStick)
	SendPropInt( SENDINFO( m_bActive ), 1, SPROP_UNSIGNED ),
#ifdef EZ
	SendPropInt( SENDINFO( m_bInSwing ), 1, SPROP_UNSIGNED ),
#endif
END_SEND_TABLE()

#ifndef HL2MP
LINK_ENTITY_TO_CLASS( weapon_stunstick, CWeaponStunStick );
PRECACHE_WEAPON_REGISTER( weapon_stunstick );
#endif

acttable_t CWeaponStunStick::m_acttable[] = 
{
	{ ACT_MELEE_ATTACK1,	ACT_MELEE_ATTACK_SWING,	true },
	{ ACT_IDLE_ANGRY,		ACT_IDLE_ANGRY_MELEE,	true },
};

IMPLEMENT_ACTTABLE(CWeaponStunStick);


BEGIN_DATADESC( CWeaponStunStick )

	DEFINE_FIELD( m_bActive, FIELD_BOOLEAN ),
#ifdef EZ
	DEFINE_FIELD( m_flLastChargeTime, FIELD_TIME ),
	DEFINE_FIELD( m_flChargeAmount, FIELD_FLOAT ),
#endif

END_DATADESC()



//-----------------------------------------------------------------------------
// Constructor
//-----------------------------------------------------------------------------
CWeaponStunStick::CWeaponStunStick( void )
{
	// HACK:  Don't call SetStunState because this tried to Emit a sound before
	//  any players are connected which is a bug
	m_bActive = false;
#ifdef EZ
	m_bInSwing = false;
	m_flLastChargeTime = 0.0f;
	m_flChargeAmount = 0.0f;
#endif
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CWeaponStunStick::Spawn()
{
	Precache();


	BaseClass::Spawn();
	AddSolidFlags( FSOLID_NOT_STANDABLE );
}

void CWeaponStunStick::Precache()
{
	BaseClass::Precache();

	PrecacheScriptSound( "Weapon_StunStick.Activate" );
	PrecacheScriptSound( "Weapon_StunStick.Deactivate" );

}

//-----------------------------------------------------------------------------
// Purpose: Get the damage amount for the animation we're doing
// Input  : hitActivity - currently played activity
// Output : Damage amount
//-----------------------------------------------------------------------------
float CWeaponStunStick::GetDamageForActivity( Activity hitActivity )
{
	float flDamage;
	if ( ( GetOwner() != NULL ) && ( GetOwner()->IsPlayer() ) )
		flDamage = sk_plr_dmg_stunstick.GetFloat();
	else
		flDamage = sk_npc_dmg_stunstick.GetFloat();

#ifdef EZ
	// EZ charged stunstick: damage = base * clamp(1.5 * charge^2, 1, 100).
	// A full 3 second hold is the 100x ceiling. Uncharged swings stay at 1x.
	const float flChargeMultiplier = MIN( MAX( (m_flChargeAmount * m_flChargeAmount) * 1.5f, 1.0f ), 100.0f );
	flDamage *= flChargeMultiplier;
#endif
	return flDamage;
}

#ifdef EZ
void CWeaponStunStick::PrimaryAttack( void )
{
	BaseClass::PrimaryAttack();
	m_flChargeAmount = 0.0f;
	m_flLastChargeTime = 0.0f;
}

void CWeaponStunStick::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( pOwner && m_flChargeAmount > 0.0f &&
		( !( pOwner->m_nButtons & IN_ATTACK2 ) ||
		  ( pOwner->m_nButtons & IN_ATTACK ) ||
		  m_flChargeAmount > 3.0f ) )
	{
		if ( m_flNextPrimaryAttack <= gpGlobals->curtime )
		{
			PrimaryAttack();
		}
		else
		{
			m_flChargeAmount = 0.0f;
			m_flLastChargeTime = 0.0f;
		}
	}

	BaseClass::ItemPostFrame();
	m_bInSwing = InSwing();
}

void CWeaponStunStick::SecondaryAttack( void )
{
	if ( m_flLastChargeTime > 0.0f )
	{
		m_flChargeAmount += gpGlobals->curtime - m_flLastChargeTime;
	}
	else
	{
		WeaponSound( SPECIAL1 );
	}
	m_flLastChargeTime = gpGlobals->curtime;
}

bool CWeaponStunStick::InSwing( void )
{
	return m_flChargeAmount > 0.0f;
}

void CWeaponStunStick::Hit( trace_t &traceHit, Activity nHitActivity, bool bIsSecondary )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if ( !pPlayer )
		return;

	AddViewKick();
	CSoundEnt::InsertSound( SOUND_BULLET_IMPACT, traceHit.endpos, 400, 0.2f, pPlayer );
	pPlayer->RumbleEffect( RUMBLE_AR2, 0, RUMBLE_FLAG_RESTART );

	CBaseEntity *pHitEntity = traceHit.m_pEnt;
	if ( pHitEntity != NULL )
	{
		Vector hitDirection;
		pPlayer->EyeVectors( &hitDirection, NULL, NULL );
		VectorNormalize( hitDirection );

		const float flDamage = GetDamageForActivity( nHitActivity );
		CTakeDamageInfo info( GetOwner(), GetOwner(), flDamage, DMG_CLUB );
		if ( pHitEntity->IsNPC() )
		{
			info.AdjustPlayerDamageInflictedForSkillLevel();
		}
		CalculateMeleeDamageForce( &info, hitDirection, traceHit.endpos );

		// Charged blows that would kill an NPC electrify the ragdoll. The
		// uncharged path is the same dispatch the bludgeon base uses.
		CAI_BaseNPC *pNPC = pHitEntity->MyNPCPointer();
		const float flUnscaled = sk_plr_dmg_stunstick.GetFloat();
		if ( flDamage > flUnscaled && pNPC && pNPC->CanBecomeServerRagdoll() &&
			 !pNPC->IsEFlagSet( EFL_NO_MEGAPHYSCANNON_RAGDOLL ) &&
			 pNPC->m_iHealth - info.GetDamage() <= 0.0f )
		{
			pNPC->BecomeRagdollBoogie( GetOwner(), info.GetDamageForce(), 5.0f, SF_RAGDOLL_BOOGIE_ELECTRICAL );
		}
		else
		{
			pHitEntity->DispatchTraceAttack( info, hitDirection, &traceHit );
			ApplyMultiDamage();
		}

		TraceAttackToTriggers( info, traceHit.startpos, traceHit.endpos, hitDirection );
		if ( ToBaseCombatCharacter( pHitEntity ) )
		{
			gamestats->Event_WeaponHit( pPlayer, !bIsSecondary, GetClassname(), info );
		}
	}

	ImpactEffect( traceHit );
}
#endif

//-----------------------------------------------------------------------------
// Attempt to lead the target (needed because citizens can't hit manhacks with the crowbar!)
//-----------------------------------------------------------------------------
extern ConVar sk_crowbar_lead_time;

int CWeaponStunStick::WeaponMeleeAttack1Condition( float flDot, float flDist )
{
	// Attempt to lead the target (needed because citizens can't hit manhacks with the crowbar!)
	CAI_BaseNPC *pNPC	= GetOwner()->MyNPCPointer();
	CBaseEntity *pEnemy = pNPC->GetEnemy();
	if (!pEnemy)
		return COND_NONE;

	Vector vecVelocity;
	AngularImpulse angVelocity;
	pEnemy->GetVelocity( &vecVelocity, &angVelocity );

	// Project where the enemy will be in a little while, add some randomness so he doesn't always hit
	float dt = sk_crowbar_lead_time.GetFloat();
	dt += random->RandomFloat( -0.3f, 0.2f );
	if ( dt < 0.0f )
		dt = 0.0f;

	Vector vecExtrapolatedPos;
	VectorMA( pEnemy->WorldSpaceCenter(), dt, vecVelocity, vecExtrapolatedPos );

	Vector vecDelta;
	VectorSubtract( vecExtrapolatedPos, pNPC->WorldSpaceCenter(), vecDelta );

	if ( fabs( vecDelta.z ) > 70 )
	{
		return COND_TOO_FAR_TO_ATTACK;
	}

	Vector vecForward = pNPC->BodyDirection2D( );
	vecDelta.z = 0.0f;
	float flExtrapolatedDot = DotProduct2D( vecDelta.AsVector2D(), vecForward.AsVector2D() );
	if ((flDot < 0.7) && (flExtrapolatedDot < 0.7))
	{
		return COND_NOT_FACING_ATTACK;
	}

	float flExtrapolatedDist = Vector2DNormalize( vecDelta.AsVector2D() );

	if( pEnemy->IsPlayer() )
	{
		//Vector vecDir = pEnemy->GetSmoothedVelocity();
		//float flSpeed = VectorNormalize( vecDir );

		// If player will be in front of me in one-half second, clock his arse.
		Vector vecProjectEnemy = pEnemy->GetAbsOrigin() + (pEnemy->GetAbsVelocity() * 0.35);
		Vector vecProjectMe = GetAbsOrigin();

		if( (vecProjectMe - vecProjectEnemy).Length2D() <= 48.0f )
		{
			return COND_CAN_MELEE_ATTACK1;
		}
	}
/*
	if( metropolice_move_and_melee.GetBool() )
	{
		if( pNPC->IsMoving() )
		{
			flTargetDist *= 1.5f;
		}
	}
*/
	float flTargetDist = 48.0f;
	if ((flDist > flTargetDist) && (flExtrapolatedDist > flTargetDist))
	{
		return COND_TOO_FAR_TO_ATTACK;
	}

	return COND_CAN_MELEE_ATTACK1;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CWeaponStunStick::ImpactEffect( trace_t &traceHit )
{
	//Glowing spark effect for hit
	//UTIL_DecalTrace( &m_trLineHit, "PlasmaGlowFade" );
	
	//FIXME: need new decals
	UTIL_ImpactTrace( &traceHit, DMG_CLUB );
}

void CWeaponStunStick::Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator )
{
	switch( pEvent->event )
	{
		case EVENT_WEAPON_MELEE_HIT:
		{
			// Trace up or down based on where the enemy is...
			// But only if we're basically facing that direction
			Vector vecDirection;
			AngleVectors( GetAbsAngles(), &vecDirection );

			CBaseEntity *pEnemy = pOperator->MyNPCPointer() ? pOperator->MyNPCPointer()->GetEnemy() : NULL;
			if ( pEnemy )
			{
				Vector vecDelta;
				VectorSubtract( pEnemy->WorldSpaceCenter(), pOperator->Weapon_ShootPosition(), vecDelta );
				VectorNormalize( vecDelta );
				
				Vector2D vecDelta2D = vecDelta.AsVector2D();
				Vector2DNormalize( vecDelta2D );
				if ( DotProduct2D( vecDelta2D, vecDirection.AsVector2D() ) > 0.8f )
				{
					vecDirection = vecDelta;
				}
			}

			Vector vecEnd;
			VectorMA( pOperator->Weapon_ShootPosition(), 32, vecDirection, vecEnd );
			// Stretch the swing box down to catch low level physics objects
			CBaseEntity *pHurt = pOperator->CheckTraceHullAttack( pOperator->Weapon_ShootPosition(), vecEnd, 
				Vector(-16,-16,-40), Vector(16,16,16), GetDamageForActivity( GetActivity() ), DMG_CLUB, 0.5f, false );
			
			// did I hit someone?
			if ( pHurt )
			{
				// play sound
#ifdef EZ
				// Charged NPC swings (metro cops) use the alternate hit sound.
				// Player swings play their sound from the bludgeon base.
				if ( m_flChargeAmount > 1.0f )
					WeaponSound( SPECIAL2 );
				else
#endif
				WeaponSound( MELEE_HIT );

				CBasePlayer *pPlayer = ToBasePlayer( pHurt );

				CNPC_MetroPolice *pCop = dynamic_cast<CNPC_MetroPolice *>(pOperator);
				bool bFlashed = false;

				if ( pCop != NULL && pPlayer != NULL )
				{
					// See if we need to knock out this target
					if ( pCop->ShouldKnockOutTarget( pHurt ) )
					{
						float yawKick = random->RandomFloat( -48, -24 );

						//Kick the player angles
						pPlayer->ViewPunch( QAngle( -16, yawKick, 2 ) );

						color32 white = {255,255,255,255};
						UTIL_ScreenFade( pPlayer, white, 0.2f, 1.0f, FFADE_OUT|FFADE_PURGE|FFADE_STAYOUT );
						bFlashed = true;
						
						pCop->KnockOutTarget( pHurt );

						break;
					}
					else
					{
						// Notify that we've stunned a target
						pCop->StunnedTarget( pHurt );
					}
				}
				
				// Punch angles
				if ( pPlayer != NULL && !(pPlayer->GetFlags() & FL_GODMODE) )
				{
					float yawKick = random->RandomFloat( -48, -24 );

					//Kick the player angles
					pPlayer->ViewPunch( QAngle( -16, yawKick, 2 ) );

					Vector	dir = pHurt->GetAbsOrigin() - GetAbsOrigin();

					// If the player's on my head, don't knock him up
					if ( pPlayer->GetGroundEntity() == pOperator )
					{
						dir = vecDirection;
						dir.z = 0;
					}

					VectorNormalize(dir);

					dir *= 500.0f;

					//If not on ground, then don't make them fly!
					if ( !(pPlayer->GetFlags() & FL_ONGROUND ) )
						 dir.z = 0.0f;

					//Push the target back
					pHurt->ApplyAbsVelocityImpulse( dir );

					if ( !bFlashed )
					{
						color32 red = {128,0,0,128};
						UTIL_ScreenFade( pPlayer, red, 0.5f, 0.1f, FFADE_IN );
					}
					
					// Force the player to drop anyting they were holding
					pPlayer->ForceDropOfCarriedPhysObjects();
				}
				
				// do effect?
			}
			else
			{
				WeaponSound( MELEE_MISS );
			}
		}
		break;
		default:
			BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
			break;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sets the state of the stun stick
//-----------------------------------------------------------------------------
void CWeaponStunStick::SetStunState( bool state )
{
	m_bActive = state;

	if ( m_bActive )
	{
		//FIXME: START - Move to client-side

		Vector vecAttachment;

		GetAttachment( 1, vecAttachment );
		g_pEffects->Sparks( vecAttachment );

		//FIXME: END - Move to client-side

		EmitSound( "Weapon_StunStick.Activate" );
	}
	else
	{
		EmitSound( "Weapon_StunStick.Deactivate" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponStunStick::Deploy( void )
{
	SetStunState( true );

	return BaseClass::Deploy();
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CWeaponStunStick::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	if ( BaseClass::Holster( pSwitchingTo ) == false )
		return false;

	SetStunState( false );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &vecVelocity - 
//-----------------------------------------------------------------------------
void CWeaponStunStick::Drop( const Vector &vecVelocity )
{
	SetStunState( false );

	BaseClass::Drop( vecVelocity );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CWeaponStunStick::GetStunState( void )
{
	return m_bActive;
}
