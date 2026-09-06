# Merging Entropy: Zero 1 game code

This tree can already **configure and link** `--build-games=ez1`. That profile
uses the HL2 VPC lists, adds `EZ=1`, `EZ1=1`, `HE_APC=1`, `GLOWS_ENABLE=1`,
and now also parses `client_ez1_extras.vpc` / `server_ez1_extras.vpc`.

The official sources live in [`entropy-zero/source-sdk-2013`](https://github.com/entropy-zero/source-sdk-2013)
(`sp/src/game/...`, tag `EZ1v4.0`). Copy **source only** — never commit VPKs,
maps, or other Steam content.

## Phase 1 — unique units (import script)

```bash
git clone --branch EZ1v4.0 --depth 1 https://github.com/entropy-zero/source-sdk-2013.git
bash scripts/import-ez1-sources.sh /path/to/source-sdk-2013
```

That copies:

- `game/server/ez1/` — `weapon_ManhackToss.cpp`, achievements
- `game/server/Human_Error/` and `game/client/Human_Error/` — drivable APC
- `game/server/ez2/` — bullsquid / zombigaunt / predator / command point
- `game/server/mod/` — custom NPCs and weapons referenced by EZ1 FGDs
- `game/client/particles_ez.{cpp,h}`

CI stays on `--build-games=hl2` until those files exist **and** the `#ifdef EZ`
hunks below compile on this nillerusr tree. Do not flip the workflow default
early: EZ1 units will not link without the hunks.

## Phase 1b — `#ifdef EZ` / `#ifdef EZ1` hunks

Do **not** replace whole files from SDK 2013 onto this Android 2017/18 tree.
Port only the EZ conditional blocks into:

- `shared/hl2/hl2_gamerules.cpp` (Combine allegiance)
- `server/hl2/hl2_player.cpp` / `.h` (kick, nightvision / flashlight-as-NV)
- `shared/hl2mp/weapon_stunstick.cpp`
- `server/ai_basenpc.cpp`
- `server/hl2/npc_metropolice.cpp`, `npc_combine.cpp`

Then keep `GAME_PROFILE` as `hl2` in the manifest until `/sdcard/srceng/ez1`
exists on the headset.

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
