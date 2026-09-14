// Compatibility symbols required by imported EZ1/EZ2 units when building
// against the older Android Source engine branch.

#include "cbase.h"
#include "hl2/grenade_spit.h"

// EZ2 calls this for a large Bullsquid projectile. The Android grenade-spit
// implementation already spawns with the large model and full damage, so the
// compatibility implementation intentionally has no additional work.
void CGrenadeSpit::SetSpitSize( int nSize )
{
}

// The base branch leaves these legacy flare-round skill variables commented
// out, while the imported custom flare gun still consumes them.
ConVar sk_plr_dmg_flare_round( "sk_plr_dmg_flare_round", "30", FCVAR_REPLICATED );
ConVar sk_npc_dmg_flare_round( "sk_npc_dmg_flare_round", "10", FCVAR_REPLICATED );

// memdbgon must be the last include file in a .cpp file.
#include "tier0/memdbgon.h"
