//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "basecombatweapon_shared.h"

#ifndef BASEHLCOMBATWEAPON_SHARED_H
#define BASEHLCOMBATWEAPON_SHARED_H
#ifdef _WIN32
#pragma once
#endif

#if defined( CLIENT_DLL )
#define CBaseHLCombatWeapon C_BaseHLCombatWeapon
#endif

class CBaseHLCombatWeapon : public CBaseCombatWeapon
{
#if !defined( CLIENT_DLL )
#ifndef _XBOX
	DECLARE_DATADESC();
#else
protected:
	DECLARE_DATADESC();
private:
#endif
#endif

	DECLARE_CLASS( CBaseHLCombatWeapon, CBaseCombatWeapon );
public:
	DECLARE_NETWORKCLASS();
	DECLARE_PREDICTABLE();

	virtual bool	WeaponShouldBeLowered( void );

			bool	CanLower();
	virtual bool	Ready( void );
	virtual bool	Lower( void );
	virtual bool	Deploy( void );
	virtual bool	Holster( CBaseCombatWeapon *pSwitchingTo );
	virtual void	WeaponIdle( void );

	virtual void	AddViewmodelBob( CBaseViewModel *viewmodel, Vector &origin, QAngle &angles );
	virtual	float	CalcViewmodelBob( void );

	virtual Vector	GetBulletSpread( WeaponProficiency_t proficiency );
	virtual float	GetSpreadBias( WeaponProficiency_t proficiency );

	virtual const	WeaponProficiencyInfo_t *GetProficiencyValues();
	static const	WeaponProficiencyInfo_t *GetDefaultProficiencyValues();

	virtual void	ItemHolsterFrame( void );

#ifdef EZ2
	// Dual wield accessors, ported from mapbase (which guards the whole
	// dual-wield feature with #ifdef EZ2).
	//
	// Only the ACCESSORS are ported: CBaseHLCombatWeapon::CanDualWield() is the
	// feature switch and it defaults to false, so every call site that is gated
	// on it stays dormant. The rest of mapbase's dual-wield system -- the
	// ItemPostFrame/Operator_HandleAnimEvent/ActivityOverride overrides, the
	// doubled clip math, the viewmodel and the client half -- is NOT here. Turn
	// this off by not defining EZ2 and nothing changes; turn the full system on
	// by porting it alongside these.
	virtual bool			CanDualWield() const { return false; }
	bool					IsDualWielding() const { return m_hLeftHandGun != NULL; }
	CBaseAnimating			*GetLeftHandGun() const { return m_hLeftHandGun; }
	void					SetLeftHandGun( CBaseAnimating *pGun ) { m_hLeftHandGun = pGun; }
	CBaseAnimating			*CreateLeftHandGun();
#endif

	int				m_iPrimaryAttacks;		// # of primary attacks performed with this weapon
	int				m_iSecondaryAttacks;	// # of secondary attacks performed with this weapon

protected:

#ifdef EZ2
	CHandle<CBaseAnimating>	m_hLeftHandGun;
#endif

	bool			m_bLowered;			// Whether the viewmodel is raised or lowered
	float			m_flRaiseTime;		// If lowered, the time we should raise the viewmodel
	float			m_flHolsterTime;	// When the weapon was holstered
};

#endif // BASEHLCOMBATWEAPON_SHARED_H
