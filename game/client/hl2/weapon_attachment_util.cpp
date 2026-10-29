//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Client-side viewmodel attachment lookup
//
// This is a verbatim backfill of the CLIENT_DLL half of
// game/shared/hl2mp/weapon_stunstick.cpp. Upstream SDK 2013 defines
// UTIL_GetWeaponAttachment() in game/client/hl2/c_weapon_stunstick.cpp, and
// game/client/client_hl2.vpc lists that file -- but this tree never imported it,
// so the symbol has never been linked into libclient. The ez1 profile does not
// need it; the EZ2 client does (c_weapon_sniperrifle.cpp, which was already
// present and already forward-declared the function, plus the M3 weapon roster).
//
// Delete this file if c_weapon_stunstick.cpp is ever imported, or the two
// definitions collide at link time.
//-----------------------------------------------------------------------------
#include "cbase.h"
#include "c_baseviewmodel.h"
#include "c_baseplayer.h"

// Defined in c_baseviewmodel.cpp but declared in no header in this tree; every
// caller forward-declares it the same way (see Human_Error/c_weapon_sniperrifle.cpp).
extern void FormatViewModelAttachment( Vector &vOrigin, bool bInverse );

//-----------------------------------------------------------------------------
// Purpose: Get the attachment point on a viewmodel that a base weapon is using
//-----------------------------------------------------------------------------
bool UTIL_GetWeaponAttachment( C_BaseCombatWeapon *pWeapon, int attachmentID, Vector &absOrigin, QAngle &absAngles )
{
	// This is already correct in third-person
	if ( pWeapon && pWeapon->ShouldDrawUsingViewModel() == false )
	{
		return pWeapon->GetAttachment( attachmentID, absOrigin, absAngles );
	}

	// Otherwise we need to translate the attachment to the viewmodel's version and reformat it
	CBasePlayer *pOwner = ToBasePlayer( pWeapon->GetOwner() );

	if ( pOwner != NULL )
	{
		int ret = pOwner->GetViewModel()->GetAttachment( attachmentID, absOrigin, absAngles );
		FormatViewModelAttachment( absOrigin, true );

		return ret;
	}

	// Wasn't found
	return false;
}
