//=============================================================================//
//
// Purpose: Pulse pistol (Bad Cop sidearm) ported from Entropy: Zero 2.
//
//=============================================================================//

#include "cbase.h"
#include "npcevent.h"
#include "basehlcombatweapon.h"
#include "basecombatcharacter.h"
#include "ai_basenpc.h"
#include "player.h"
#include "gamerules.h"
#include "in_buttons.h"
#include "soundent.h"
#include "game.h"
#include "vstdlib/random.h"
#include "gamestats.h"
#include "beam_shared.h"
#include "Sprite.h"
#include "te_effect_dispatch.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define	PULSE_PISTOL_FASTEST_REFIRE_TIME		0.4f
#define	PULSE_PISTOL_FASTEST_DRY_REFIRE_TIME	1.5f
#define	PULSE_PISTOL_ACCURACY_MAXIMUM_PENALTY_TIME	10.0f
#define	PISTOL_ACCURACY_SHOT_PENALTY_TIME		0.2f

enum PulsePistolStyle
{
	DEFAULT = 0,
	PRIMARY_CHARGE,
	LANCE_NO_CHARGE,
	SELECT_FIRE
};

ConVar sk_plr_dmg_pulse_lance( "sk_plr_dmg_pulse_lance", "15" );
ConVar sv_pulse_pistol_style( "sv_pulse_pistol_style", "0", FCVAR_NONE, "Style of pulse pistol altfire: 0) Default 1) Charge 2) Lance" );
ConVar sv_pulse_lance_style( "sv_pulse_lance_style", "0", FCVAR_NONE, "Style of pulse lance: 0) Vortigaunt 1) Stalker" );
ConVar sv_pulse_pistol_charge_per_extra_shot( "sv_pulse_pistol_charge_per_extra_shot", "5", FCVAR_NONE, "How many ammo charges equals one bullet" );
ConVar sv_pulse_pistol_slide_return_charge( "sv_pulse_pistol_slide_return_charge", "10", FCVAR_NONE, "How much charge to restore when racking the slide on the pulse pistol" );
ConVar sv_pulse_pistol_no_charge_hold( "sv_pulse_pistol_no_charge_hold", "0", FCVAR_NONE, "If 1, fully charged shots will fire automatically" );
ConVar sv_pulse_pistol_max_charge( "sv_pulse_pistol_max_charge", "40", FCVAR_NONE, "Maximum ammo to charge in one shot. For a 50 charge pulse pistol, it should be 40" );
ConVar sv_pulse_pistol_npc_charge_per_extra_shot( "sv_pulse_pistol_npc_charge_per_extra_shot", "5", FCVAR_NONE, "How many ammo charges equals one bullet on NPCs" );

#define AE_WPN_INCREMENTAMMO (LAST_SHARED_ANIMEVENT + 79)
#ifndef AE_SLIDERETURN
#define AE_SLIDERETURN (LAST_SHARED_ANIMEVENT + 80)
#endif

//-----------------------------------------------------------------------------
// CWeaponPulsePistol
//-----------------------------------------------------------------------------
class CWeaponPulsePistol : public CHLMachineGun
{
	DECLARE_DATADESC();
public:
	DECLARE_CLASS(CWeaponPulsePistol, CHLMachineGun);
	DECLARE_SERVERCLASS();

	CWeaponPulsePistol();

	void	Activate( void );
	void	Precache();
	bool	IsChargePressed( int chargeButton, CBasePlayer * pOwner );
	void	ItemPostFrame( void );
	void	PrimaryAttack( void );
	void	SecondaryAttack();
	void	ChargeAttack( void );
	void	AddViewKick( void );
	void	DryFire( void );

	void	Operator_HandleAnimEvent( animevent_t *pEvent, CBaseCombatCharacter *pOperator );

	void	FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir );
	void	FireNPCSecondaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir );

	virtual bool Reload( void ) { return false; } // The pulse pistol does not reload

	inline bool	IsDualWielding() const { return false; }
	inline bool	DualWieldOverridesSecondary() const { return false; }

	virtual int GetMaxClip2( void ) const
	{
		int iBase = BaseClass::GetMaxClip2();
		if (iBase > 0)
			return iBase;
		return sv_pulse_pistol_max_charge.GetInt();
	}

	int GetMinShotClip() const { return 10; }

	virtual const Vector& GetBulletSpread( void )
	{
		// Handle NPCs first
		static Vector npcCone = VECTOR_CONE_5DEGREES;
		static Vector npcChargeCone = VECTOR_CONE_2DEGREES;
		if (GetOwner() && GetOwner()->IsNPC())
		{
			return (GetActivity() == ACT_CHARGE_PULSE_PISTOL || GetActivity() == ACT_CHARGE_PULSE_PISTOL_LOW) ? npcChargeCone : npcCone;
		}

		static Vector cone;

		float ramp = RemapValClamped( m_flAccuracyPenalty,
			0.0f,
			PULSE_PISTOL_ACCURACY_MAXIMUM_PENALTY_TIME,
			0.0f,
			1.0f );

		VectorLerp( VECTOR_CONE_2DEGREES, VECTOR_CONE_15DEGREES, ramp, cone );
		return cone;
	}

	virtual float GetFireRate( void )
	{
		if ( GetOwner() && GetOwner()->IsPlayer() )
			return 3.0f;
		return 1.0f;
	}

	virtual void	UpdateOnRemove( void );

	bool	Deploy( void );
	bool	Holster( CBaseCombatWeapon *pSwitchingTo );
	void	DoImpactEffect( trace_t &tr, int nDamageType );
	void	MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType );
	void	DoMuzzleFlash( void );

	void	StartChargeEffects( void );
	void	SetChargeEffectBrightness( float alpha );
	void	KillChargeEffects();

	void	CreateBeamBlast( const Vector &vecOrigin );

	virtual acttable_t *GetBackupActivityList() { return BaseClass::ActivityList(); }
	virtual int			GetBackupActivityListCount() { return BaseClass::ActivityListCount(); }

	DECLARE_ACTTABLE();

protected:
	CHandle<CSprite>	m_hChargeSprite;
	CHandle<CSprite>	m_hChargeSprite2;

private:
	void			RechargeAmmo();
	int				m_nAmmoCount;
	float			m_flDrainRemainder;
	float			m_flChargeRemainder;
	float			m_flLastChargeTime;
	float			m_flLastChargeSoundTime;
	float			m_flSoonestPrimaryAttack;
	float			m_flAccuracyPenalty;
	int				m_nNumShotsFired;
};

acttable_t	CWeaponPulsePistol::m_acttable[] =
{
	{ ACT_IDLE,						ACT_IDLE_PISTOL,				true },
	{ ACT_IDLE_ANGRY,				ACT_IDLE_ANGRY_PISTOL,			true },
	{ ACT_RANGE_ATTACK1,			ACT_RANGE_ATTACK_PISTOL,		true },
	{ ACT_WALK_AIM,					ACT_WALK_AIM_PISTOL,			true },
	{ ACT_RUN_AIM,					ACT_RUN_AIM_PISTOL,				true },
	{ ACT_GESTURE_RANGE_ATTACK1,	ACT_GESTURE_RANGE_ATTACK_PISTOL,true },
	{ ACT_RANGE_ATTACK1_LOW,		ACT_RANGE_ATTACK_PISTOL_LOW,	false },
	{ ACT_COVER_LOW,				ACT_COVER_PISTOL_LOW,			false },
	{ ACT_RANGE_AIM_LOW,			ACT_RANGE_AIM_PISTOL_LOW,		false },
	{ ACT_WALK,						ACT_WALK_PISTOL,				false },
	{ ACT_RUN,						ACT_RUN_PISTOL,					false },

	{ ACT_RELOAD,						ACT_RELOAD_PULSE_PISTOL,			true },
	{ ACT_RELOAD_LOW,					ACT_RELOAD_PULSE_PISTOL_LOW,		false },
	{ ACT_GESTURE_RELOAD,				ACT_GESTURE_RELOAD_PULSE_PISTOL,	false },
	{ ACT_COMBINE_AR2_ALTFIRE,			ACT_CHARGE_PULSE_PISTOL,			false },
	{ ACT_GESTURE_COMBINE_AR2_ALTFIRE,	ACT_GESTURE_CHARGE_PULSE_PISTOL,	false },
	{ ACT_HL2MP_GESTURE_RELOAD,			ACT_GESTURE_CHARGE_PULSE_PISTOL,	false },
};

IMPLEMENT_ACTTABLE( CWeaponPulsePistol );

CWeaponPulsePistol::CWeaponPulsePistol()
{
	m_iClip2 = 0;
	m_nAmmoCount = 50;
	m_flDrainRemainder = 0.0f;
	m_flChargeRemainder = 0.0f;
	m_flLastChargeTime = 0.0f;
	m_flLastChargeSoundTime = 0.0f;
	m_flSoonestPrimaryAttack = 0.0f;
	m_flAccuracyPenalty = 0.0f;
	m_nNumShotsFired = 0;
}

void CWeaponPulsePistol::Activate( void )
{
	BaseClass::Activate();
	StartChargeEffects();
}

void CWeaponPulsePistol::Precache( void )
{
	PrecacheModel( "effects/fluttercore.vmt" );
	PrecacheModel( "sprites/laser.vmt" );
	PrecacheModel( "sprites/redglow1.vmt" );

	UTIL_PrecacheOther( "npc_vortigaunt" );
	UTIL_PrecacheOther( "npc_stalker" );

	BaseClass::Precache();
}

void CWeaponPulsePistol::UpdateOnRemove( void )
{
	KillChargeEffects();
	BaseClass::UpdateOnRemove();
}

bool CWeaponPulsePistol::IsChargePressed( int chargeButton, CBasePlayer * pOwner )
{
	if (sv_pulse_pistol_no_charge_hold.GetBool() && m_iClip2 >= GetMaxClip2())
		return false;

	if (sv_pulse_pistol_no_charge_hold.GetBool() && m_iClip2 > 0 && m_iClip1 <= GetMinShotClip())
		return false;

	return (pOwner->m_nButtons & chargeButton) > 0;
}

void CWeaponPulsePistol::ItemPostFrame( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if (!pOwner)
		return;

	bool chargePressed = false;
	switch ( sv_pulse_pistol_style.GetInt() )
	{
	case PRIMARY_CHARGE:
		chargePressed = IsChargePressed( IN_ATTACK, pOwner );
		break;
	case LANCE_NO_CHARGE:
		break;
	case SELECT_FIRE:
		DevMsg( "Select fire mode not implemented yet!\n" );
		break;
	default:
		chargePressed = IsChargePressed( IN_ATTACK2, pOwner );
		break;
	}

	if (!chargePressed && ( m_iClip2 > 0 ) && (m_flNextPrimaryAttack <= gpGlobals->curtime) )
	{
		m_flNextPrimaryAttack = gpGlobals->curtime;
		PrimaryAttack();
		SetChargeEffectBrightness( 0.0f );
		return;
	}
	else if (chargePressed || ( m_iClip2 > 0 ) )
	{
		ChargeAttack();
		return;
	}

	m_iClip2 = 0;
	RechargeAmmo();

	if ((pOwner->m_nButtons & IN_ATTACK) == false)
	{
		m_nNumShotsFired = 0;
	}

	BaseClass::ItemPostFrame();
}

void CWeaponPulsePistol::DryFire( void )
{
	WeaponSound( EMPTY );
	SendWeaponAnim( ACT_VM_DRYFIRE );

	m_flSoonestPrimaryAttack	= gpGlobals->curtime + PULSE_PISTOL_FASTEST_DRY_REFIRE_TIME;
	m_flNextPrimaryAttack		= gpGlobals->curtime + SequenceDuration();
}

void CWeaponPulsePistol::Operator_HandleAnimEvent( animevent_t * pEvent, CBaseCombatCharacter * pOperator )
{
	switch (pEvent->event)
	{
	case AE_WPN_INCREMENTAMMO:
	case AE_SLIDERETURN:
		m_iClip1 = MAX( m_iClip1, sv_pulse_pistol_slide_return_charge.GetInt() );
		WeaponSound( RELOAD, m_flNextPrimaryAttack );
		break;

	case EVENT_WEAPON_AR2_ALTFIRE:
		{
			Vector vecShootOrigin, vecShootDir;
			vecShootOrigin = pOperator->Weapon_ShootPosition();

			CAI_BaseNPC *npc = pOperator->MyNPCPointer();
			if ( npc != NULL )
			{
				vecShootDir = npc->GetActualShootTrajectory( vecShootOrigin );
				FireNPCSecondaryAttack( pOperator, vecShootOrigin, vecShootDir );
			}
		}
		break;
	}

	BaseClass::Operator_HandleAnimEvent( pEvent, pOperator );
}

void CWeaponPulsePistol::FireNPCPrimaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir )
{
	CSoundEnt::InsertSound( SOUND_COMBAT|SOUND_CONTEXT_GUNFIRE, pOperator->GetAbsOrigin(), SOUNDENT_VOLUME_PISTOL, 0.2, pOperator, SOUNDENT_CHANNEL_WEAPON, pOperator->GetEnemy() );

	WeaponSound( SINGLE_NPC );

	FireBulletsInfo_t info;
	info.m_iShots = 2;
	info.m_vecSrc = vecShootOrigin;
	info.m_vecDirShooting = vecShootDir;
	info.m_vecSpread = VECTOR_CONE_4DEGREES;
	info.m_flDistance = MAX_TRACE_LENGTH;
	info.m_iAmmoType = m_iPrimaryAmmoType;
	info.m_iTracerFreq = 1;

	pOperator->FireBullets( info );
	pOperator->DoMuzzleFlash();

	if (m_iClip1 < GetMinShotClip())
	{
		m_iClip1 = 0;
	}
	else
	{
		m_iClip1 -= GetMinShotClip();
		RechargeAmmo();
	}
}

void CWeaponPulsePistol::FireNPCSecondaryAttack( CBaseCombatCharacter *pOperator, Vector &vecShootOrigin, Vector &vecShootDir )
{
	CSoundEnt::InsertSound( SOUND_COMBAT|SOUND_CONTEXT_GUNFIRE, pOperator->GetAbsOrigin(), SOUNDENT_VOLUME_PISTOL, 0.2, pOperator, SOUNDENT_CHANNEL_WEAPON, pOperator->GetEnemy() );

	WeaponSound( DOUBLE_NPC );

	m_flLastChargeTime += SequenceDuration( GetModelPtr(), GetSequence() ) * GetCycle();
	RechargeAmmo();

	int iBulletsToFire = 0;
	int iExtraChargeBullets = m_iClip1 / sv_pulse_pistol_npc_charge_per_extra_shot.GetInt();
	iBulletsToFire += iExtraChargeBullets;

	FireBulletsInfo_t info;
	info.m_iShots = iBulletsToFire;
	info.m_vecSrc = vecShootOrigin;
	info.m_vecDirShooting = vecShootDir;
	info.m_vecSpread = VECTOR_CONE_15DEGREES;
	info.m_flDistance = MAX_TRACE_LENGTH;
	info.m_iAmmoType = m_iPrimaryAmmoType;
	info.m_iTracerFreq = 1;

	pOperator->FireBullets( info );
	pOperator->DoMuzzleFlash();
	m_iClip1 = 0;
}

bool CWeaponPulsePistol::Deploy( void )
{
	RechargeAmmo();
	return BaseClass::Deploy();
}

bool CWeaponPulsePistol::Holster( CBaseCombatWeapon *pSwitchingTo )
{
	if (m_iClip2 > 0)
	{
		WeaponSound( EMPTY );
	}

	m_iClip2 = 0;
	SetChargeEffectBrightness( 0.0f );

	return BaseClass::Holster( pSwitchingTo );
}

void CWeaponPulsePistol::RechargeAmmo( void )
{
	if (m_iClip2 >= GetMaxClip2())
		return;

	float flChargeInterval = gpGlobals->curtime - m_flLastChargeTime;
	m_flLastChargeTime = gpGlobals->curtime;

	if (m_iClip1 == GetMaxClip1())
		return;

	float flRechargeRate = 5;
	float flChargeAmount = flRechargeRate * flChargeInterval;
	if (m_flDrainRemainder != 0.0f)
	{
		if (m_flDrainRemainder >= flChargeAmount)
		{
			m_flDrainRemainder -= flChargeAmount;
			return;
		}
		else
		{
			flChargeAmount -= m_flDrainRemainder;
			m_flDrainRemainder = 0.0f;
		}
	}

	m_flChargeRemainder += flChargeAmount;
	int nAmmoToAdd = (int)m_flChargeRemainder;
	m_flChargeRemainder -= nAmmoToAdd;
	m_iClip1 += nAmmoToAdd;
	if (m_iClip1 > GetMaxClip1())
	{
		m_iClip1 = GetMaxClip1();
		m_flChargeRemainder = 0.0f;
	}
}

void CWeaponPulsePistol::MakeTracer( const Vector &vecTracerSrc, const trace_t &tr, int iTracerType )
{
	UTIL_Tracer( vecTracerSrc, tr.endpos, 1, TRACER_FLAG_USEATTACHMENT, 5000, true, "GaussTracer" );
}

void CWeaponPulsePistol::PrimaryAttack( void )
{
	if (m_iClip1 < GetMinShotClip())
	{
		DryFire();
		return;
	}

	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (!pPlayer)
		return;

	if ((UsesClipsForAmmo1() && m_iClip1 == 0) || (!UsesClipsForAmmo1() && !pPlayer->GetAmmoCount( m_iPrimaryAmmoType )))
		return;

	m_nNumShotsFired++;
	pPlayer->DoMuzzleFlash();

	int iBulletsToFire = 0;
	float fireRate = GetFireRate();

	int iExtraChargeBullets = m_iClip2 / sv_pulse_pistol_charge_per_extra_shot.GetInt();
	iBulletsToFire += iExtraChargeBullets;

	if (iExtraChargeBullets > 0)
	{
		m_flAccuracyPenalty += PISTOL_ACCURACY_SHOT_PENALTY_TIME * iExtraChargeBullets;
	}

	while (m_flNextPrimaryAttack <= gpGlobals->curtime)
	{
		m_flNextPrimaryAttack = m_flNextPrimaryAttack + fireRate;
		iBulletsToFire += 2;

		if ( iBulletsToFire >= 4 )
		{
			WeaponSound( BURST, m_flNextPrimaryAttack );
		}
		else
		{
			WeaponSound( SINGLE, m_flNextPrimaryAttack );
		}
	}

	m_iClip1 = MAX(m_iClip1 - GetMinShotClip(), 1);
	m_iClip2 = 0;

	FireBulletsInfo_t info;
	info.m_iShots = iBulletsToFire;
	info.m_vecSrc = pPlayer->Weapon_ShootPosition();
	info.m_vecDirShooting = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );
	info.m_vecSpread = pPlayer->GetAttackSpread(this);
	info.m_flDistance = MAX_TRACE_LENGTH;
	info.m_iAmmoType = m_iPrimaryAmmoType;
	info.m_iTracerFreq = 1;
	FireBullets( info );

	AddViewKick();
	DoMuzzleFlash();

	CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), SOUNDENT_VOLUME_MACHINEGUN, 0.2, pPlayer );

	if (!m_iClip1 && pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0)
	{
		pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );
	}

	SendWeaponAnim( GetPrimaryAttackActivity() );
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	pPlayer->SetMuzzleFlashTime( gpGlobals->curtime + 0.5 );
	m_flAccuracyPenalty += PISTOL_ACCURACY_SHOT_PENALTY_TIME;

	m_iPrimaryAttacks++;
	gamestats->Event_WeaponFired( pPlayer, true, GetClassname() );

	m_flSoonestPrimaryAttack = gpGlobals->curtime + PULSE_PISTOL_FASTEST_REFIRE_TIME;
	m_flNextPrimaryAttack = gpGlobals->curtime + PULSE_PISTOL_FASTEST_REFIRE_TIME;
}

void CWeaponPulsePistol::ChargeAttack( void )
{
	if ( m_iClip1 <= GetMinShotClip() && m_iClip2 <= 0) {
		if( m_flNextPrimaryAttack <= gpGlobals->curtime )
			DryFire();
		return;
	}

	int nMaxCharge = GetMaxClip2();
	if (m_iClip1 <= GetMinShotClip() || m_iClip2 == nMaxCharge)
	{
		RechargeAmmo();
		return;
	}

	if (gpGlobals->curtime - m_flLastChargeSoundTime > 4.0f && m_iClip2 == 0)
	{
		WeaponSound( SPECIAL1 );
		m_flLastChargeSoundTime = gpGlobals->curtime;
	}

	float flChargeInterval = gpGlobals->curtime - m_flLastChargeTime;
	m_flLastChargeTime = gpGlobals->curtime;

	float flRechargeRate = 10;
	float flChargeAmount = flRechargeRate * flChargeInterval;

	m_flChargeRemainder += flChargeAmount;
	int nAmmoToAdd = (int)m_flChargeRemainder;
	m_flChargeRemainder -= nAmmoToAdd;
	m_iClip2 += nAmmoToAdd;
	m_iClip1 = MAX( m_iClip1 - nAmmoToAdd, 1);
	if (m_iClip2 > nMaxCharge)
	{
		m_iClip2 = nMaxCharge;
		m_flChargeRemainder = 0.0f;
	}

	StartChargeEffects();
	SetChargeEffectBrightness( m_iClip2 );
}

void CWeaponPulsePistol::AddViewKick( void )
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	if (pPlayer == NULL)
		return;

	QAngle viewPunch;
	viewPunch.x = random->RandomFloat( -4.5f, -4.5f );
	viewPunch.y = random->RandomFloat( -.13f, .13f );
	viewPunch.z = 0.0f;
	pPlayer->ViewPunch( viewPunch );
}

void CWeaponPulsePistol::DoImpactEffect( trace_t &tr, int nDamageType )
{
	CEffectData data;
	data.m_vOrigin = tr.endpos + (tr.plane.normal * 1.0f);
	data.m_vNormal = tr.plane.normal;
	DispatchEffect( "AR2Impact", data );

	BaseClass::DoImpactEffect( tr, nDamageType );
}

void CWeaponPulsePistol::DoMuzzleFlash( void )
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if (pOwner == NULL)
		return;

	CBaseViewModel *pViewModel = pOwner->GetViewModel();
	if (pViewModel == NULL)
		return;

	CEffectData data;
	data.m_nEntIndex = pViewModel->entindex();
	data.m_nAttachmentIndex = 1;
	data.m_flScale = 0.25f;
	DispatchEffect( "ChopperMuzzleFlash", data );

	BaseClass::DoMuzzleFlash();
}

void CWeaponPulsePistol::StartChargeEffects()
{
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if (pOwner != NULL && m_hChargeSprite == NULL)
	{
		m_hChargeSprite = CSprite::SpriteCreate( "effects/fluttercore.vmt", GetAbsOrigin(), false );
		if (m_hChargeSprite != NULL)
		{
			m_hChargeSprite->SetAsTemporary();
			m_hChargeSprite->SetAttachment( pOwner->GetViewModel(), 1 );
			m_hChargeSprite->SetTransparency( kRenderTransAdd, 255, 255, 255, 255, kRenderFxNone );
			m_hChargeSprite->SetBrightness( 0, 0.1f );
			m_hChargeSprite->SetScale( 0.05f, 0.05f );
			m_hChargeSprite->TurnOn();
		}
	}
}

void CWeaponPulsePistol::SetChargeEffectBrightness( float alpha )
{
	if (m_hChargeSprite != NULL)
	{
		m_hChargeSprite->SetBrightness( (int)alpha, 0.1f );
	}
}

void CWeaponPulsePistol::KillChargeEffects()
{
	if (m_hChargeSprite != NULL)
	{
		UTIL_Remove( m_hChargeSprite );
		m_hChargeSprite = NULL;
	}
}

void CWeaponPulsePistol::SecondaryAttack()
{
	CBasePlayer *pPlayer = ToBasePlayer( GetOwner() );
	float refireModifier = sv_pulse_lance_style.GetInt() == 1 ? 0.25f : 1.0f;

	if (pPlayer == NULL)
		return;

	CSoundEnt::InsertSound( SOUND_COMBAT, GetAbsOrigin(), SOUNDENT_VOLUME_MACHINEGUN, 0.2, pPlayer );

	if (!m_iClip1 && pPlayer->GetAmmoCount( m_iPrimaryAmmoType ) <= 0)
	{
		pPlayer->SetSuitUpdate( "!HEV_AMO0", FALSE, 0 );
	}

	SendWeaponAnim( GetPrimaryAttackActivity() );
	pPlayer->SetAnimation( PLAYER_ATTACK1 );

	pPlayer->SetMuzzleFlashTime( gpGlobals->curtime + 0.5 );
	m_iSecondaryAttacks++;
	gamestats->Event_WeaponFired( pPlayer, true, GetClassname() );

	m_flNextSecondaryAttack = gpGlobals->curtime + ( PULSE_PISTOL_FASTEST_REFIRE_TIME * refireModifier );
	m_flLastChargeTime = gpGlobals->curtime;

	Vector vecSrc = pPlayer->Weapon_ShootPosition();
	Vector vecAim = pPlayer->GetAutoaimVector( AUTOAIM_SCALE_DEFAULT );
	float maxRange = 100.0f;

	trace_t tr;
	AI_TraceLine( vecSrc, vecSrc + (vecAim * maxRange), MASK_SHOT, this, COLLISION_GROUP_NONE, &tr );

	CBeam *pBeam = CBeam::BeamCreate( "sprites/laser.vmt", 2.0 );
	if (pBeam != NULL)
	{
		pBeam->PointEntInit( tr.endpos, this );
		pBeam->SetEndAttachment( 1 );
		pBeam->SetWidth( 3.0 );
		pBeam->SetEndWidth( 12.8 );
		pBeam->SetBrightness( 255 );
		pBeam->SetColor( 255, 0, 0 );
		pBeam->LiveForTime( 0.1f );
		pBeam->RelinkBeam();
	}

	UTIL_DecalTrace( &tr, "RedGlowFade" );
	UTIL_DecalTrace( &tr, "FadingScorch" );

	CBaseEntity *pEntity = tr.m_pEnt;
	if (pEntity != NULL && m_takedamage)
	{
		int dmgType = sv_pulse_lance_style.GetInt() == 1 ? DMG_BURN : DMG_SHOCK | DMG_DISSOLVE;

		CTakeDamageInfo dmgInfo( this, this, sk_plr_dmg_pulse_lance.GetFloat() * refireModifier, dmgType );
		dmgInfo.SetDamagePosition( tr.endpos );
		VectorNormalize( vecAim );
		dmgInfo.SetDamageForce( 5 * 100 * 12 * vecAim );

		pEntity->DispatchTraceAttack( dmgInfo, vecAim, &tr );
	}

	CreateBeamBlast( tr.endpos );
}

void CWeaponPulsePistol::CreateBeamBlast( const Vector &vecOrigin )
{
	CSprite *pBlastSprite = CSprite::SpriteCreate( "sprites/redglow1.vmt", GetAbsOrigin(), FALSE );
	if (pBlastSprite != NULL)
	{
		pBlastSprite->SetTransparency( kRenderTransAddFrameBlend, 255, 255, 255, 255, kRenderFxNone );
		pBlastSprite->SetBrightness( 255 );
		pBlastSprite->SetScale( random->RandomFloat( 0.5f, 0.75f ) );
		pBlastSprite->AnimateAndDie( 45.0f );
		pBlastSprite->EmitSound( "NPC_Vortigaunt.Explode" );
	}

	CPVSFilter filter( vecOrigin );
	te->GaussExplosion( filter, 0.0f, vecOrigin, Vector( 0, 0, 1 ), 0 );
}

IMPLEMENT_SERVERCLASS_ST(CWeaponPulsePistol, DT_WeaponPulsePistol)
END_SEND_TABLE()

LINK_ENTITY_TO_CLASS( weapon_pulsepistol, CWeaponPulsePistol );
PRECACHE_WEAPON_REGISTER( weapon_pulsepistol );

BEGIN_DATADESC( CWeaponPulsePistol )
	DEFINE_FIELD( m_flLastChargeTime, FIELD_TIME ),
	DEFINE_FIELD( m_flLastChargeSoundTime, FIELD_TIME ),
	DEFINE_FIELD( m_hChargeSprite, FIELD_EHANDLE ),
	DEFINE_FIELD( m_hChargeSprite2, FIELD_EHANDLE ),
	DEFINE_FIELD( m_flAccuracyPenalty, FIELD_FLOAT ),
	DEFINE_FIELD( m_nNumShotsFired, FIELD_INTEGER ),
END_DATADESC()
