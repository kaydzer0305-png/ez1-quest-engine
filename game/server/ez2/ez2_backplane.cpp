//=============================================================================//
//
// Purpose: Minimal mapbase backplane for the EZ2 profile (TRANSPLANT-A §4.1)
//
// Upstream, these interaction handles are defined in mapbase's rewrite of
// game/server/episodic/grenade_hopwire.cpp -- a 3283-line file whose M2-relevant
// surface is exactly these two zero-initialised ints. The hl2/ez1 profiles never
// compile that file at all, so nothing else in this tree defines them, and
// ez2/weapon_displacer_pistol.cpp needs them to link.
//
// Zero is the correct upstream value: the interaction manager assigns real
// prototypes at Init, and CBaseCombatCharacter::DispatchInteraction() already
// guards with `interactionType > 0`, so a zero handle makes
// DispatchInteraction() return false rather than crash. The visible effect in
// M2 is that the displacer's consume/release hand-off to a Xen grenade is inert
// -- the weapon still fires, holds and returns displaced entities, it just
// cannot hand off to the grenade interaction system until grenade_hopwire.cpp
// is ported. Delete this file when that happens.
//
//=============================================================================//

#include "cbase.h"

int	g_interactionXenGrenadeConsume		= 0;
int	g_interactionXenGrenadeRelease	= 0;
int	g_interactionXenGrenadeHop		= 0;

// Upstream this one lives in mapbase's hl2_player.cpp, next to the bad-cop
// order-surrender interaction setup. Same reasoning as above: zero is upstream's
// own initialiser, and DispatchInteraction() refuses a zero handle, so the
// surrender-order interaction simply does not dispatch until the surrender
// behaviour (M3) defines it.
int	g_interactionBadCopOrderSurrender = 0;

//------------------------------------------------------------------------------
// Skill convar for the EZ2 health vial. Upstream this is defined by
// hl2/item_healthkit.cpp (a 1200-line item), which is M3 roster work. Only the
// convar is needed to link, because ez2_player.cpp's UseHealthVial() early-returns
// on it. "0" is upstream's own default and the safe direction: the health-vial
// backpack behaviour stays off until the item lands.
//------------------------------------------------------------------------------
ConVar	sk_healthvial_backpack("sk_healthvial_backpack", "0");
