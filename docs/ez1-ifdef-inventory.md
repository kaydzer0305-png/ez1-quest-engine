# EZ1 `#ifdef EZ` inventory (EZ1v4.0)

Source: [entropy-zero/source-sdk-2013](https://github.com/entropy-zero/source-sdk-2013) tag `EZ1v4.0`.
This list is the Phase 1b merge surface against the nillerusr Android tree.
**Do not copy whole files.** Port only the EZ / EZ1 conditional blocks.

CI must stay on `--build-games=hl2` until these hunks compile on this tree.

## Priority 1

| File | `#ifdef EZ*` sites |
| --- | ---: |
| `game/shared/hl2/hl2_gamerules.cpp` | 23 |
| `game/server/hl2/hl2_player.cpp` | 13 |
| `game/server/hl2/hl2_player.h` | 2 |
| `game/shared/hl2mp/weapon_stunstick.cpp` | 10 |
| `game/server/ai_basenpc.cpp` | 6 |
| `game/server/hl2/npc_metropolice.cpp` | 2 |
| `game/server/hl2/npc_combine.cpp` | 43 |

The gamerules, player, base-NPC and metropolice core hunks landed in PR #21. Remaining Priority 1 work is the stunstick surface and the compatible subset of Combine NPC hunks.

## Priority 2 — high-churn NPCs EZ1 maps spawn

| File | sites |
| --- | ---: |
| `game/server/hl2/npc_vortigaunt_episodic.cpp` | 29 |
| `game/server/hl2/npc_fastzombie.cpp` | 25 |
| `game/server/hl2/npc_citizen17.cpp` | 23 |
| `game/server/hl2/npc_zombine.cpp` | 23 |
| `game/server/hl2/npc_playercompanion.cpp` | 19 |
| `game/server/hl2/npc_combine.h` | 18 |
| `game/server/hl2/npc_zombie.cpp` | 23 |
| `game/server/hl2/npc_manhack.cpp` | 16 |

## Complete surface

The EZ1v4.0 source scan found 89 files containing `#ifdef EZ`, `#ifndef EZ`, or `#if defined(EZ`. Port only conditional hunks that are required by EZ1 maps and compatible with the nillerusr Android tree; do not wholesale-replace SDK files.
