# Merging Entropy: Zero 1 game code

This tree can already **configure and link** `--build-games=ez1`. That profile
currently reuses the HL2 VPC lists and adds `EZ=1`, `EZ1=1`, `HE_APC=1`,
`GLOWS_ENABLE=1` plus include paths for `ez1/`, `ez2/`, `mod/`, `Human_Error/`.

The official sources live in [`entropy-zero/source-sdk-2013`](https://github.com/entropy-zero/source-sdk-2013)
(`sp/src/game/...`, tag `EZ1v4.0`). Copy **source only** — never commit VPKs,
maps, or other Steam content.

## What to copy (Phase 1)

From `sp/src/game/` into this repo's `game/`:

- `server/ez1/` — `weapon_ManhackToss.cpp`, achievements
- `server/Human_Error/` and `client/Human_Error/` — drivable APC
- `server/ez2/` — bullsquid / zombigaunt / predator / command point (EZ1 maps spawn some of these)
- `server/mod/` — custom NPCs and weapons referenced by EZ1 FGDs
- Shared `#ifdef EZ` / `#ifdef EZ1` hunks in:
  - `shared/hl2/hl2_gamerules.cpp` (Combine allegiance)
  - `server/hl2/hl2_player.cpp` / `.h` (kick, nightvision)
  - `shared/hl2mp/weapon_stunstick.cpp`
  - `server/ai_basenpc.cpp`
  - `server/hl2/npc_metropolice.cpp`, `npc_combine.cpp`

Then switch the wscript maps from `client_hl2.vpc` / `server_hl2.vpc` to
`client_ez1.vpc` / `server_ez1.vpc` once those VPC files list the new units.

## Device content (not in git)

Sideload to `/sdcard/srceng/`:

- `ez1/` — your Entropy: Zero install (`gameinfo.txt` + VPKs)
- `hl2/`, `episodic/`, `ep2/`, `platform/` — pre-20th-anniversary HL2 (`steam_legacy`)

Flip `com.ezquest.engine.GAME_PROFILE` to `ez1` in `AndroidManifest.xml` only
after that folder exists on the headset.

## Touch map (Phase 3, already in `ezquest_vr_sourceinput.cpp`)

| Control | Command |
| --- | --- |
| Left stick | move |
| Left stick click | `+speed` |
| Right stick X | snap-turn / look |
| Right stick click | `invnext` |
| Left grip + right stick click | `slot5` (manhack) |
| Triggers | `+attack` |
| Left grip + right trigger | `+alt1` (kick) |
| Grips | `+attack2` |
| A / X tap | `+jump` |
| A / X hold ~0.5s | `impulse 100` (NV) |
| B / Y | `+reload` |
| Left grip+trigger | `+use` |
| Menu | `cancelselect` |

Visor HUD: keep the 2D visor **off** in VR for now (stereo dissonance). Treat
that as a follow-up after in-headset stereo is confirmed.
