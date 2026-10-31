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

Pull requests compile `--build-games=ez1`. A manual dispatch of
`build-android-arm64` still defaults to `hl2`, which is the sideload that
has been exercised on a headset. Do not change that default until the
headset checklist passes.

## Phase 1b — `#ifdef EZ` / `#ifdef EZ1` hunks

Do **not** replace whole files from SDK 2013 onto this Android 2017/18 tree.
Port only the EZ conditional blocks into:

- `shared/hl2/hl2_gamerules.cpp` (Combine allegiance)
- `server/hl2/hl2_player.cpp` / `.h` (kick, nightvision / flashlight-as-NV)
- `server/hl2/weapon_stunstick.cpp` (this tree's ez1 link; official EZ1 uses `shared/hl2mp/weapon_stunstick.cpp`)
- `server/ai_basenpc.cpp`
- `server/hl2/npc_metropolice.cpp`, `npc_combine.cpp`

Keep `GAME_PROFILE` as `hl2` in the manifest until `/sdcard/srceng/ez1`
exists on the headset. The committed value is that one `android:value`.
Dispatching `build-android-arm64` with `game_profile=ez1` rewrites it in
the packaged APK only.

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

## Phase 1b status (ported onto this nillerusr tree)

Landed behind `#ifdef EZ` / `#ifdef EZ1` so `--build-games=hl2` is unchanged:

- `game/shared/hl2/hl2_gamerules.cpp` + `.h` — Combine allegiance (Bad Cop is `CLASS_PLAYER` allied with Combine / Metro / manhack / scanner), rebel/vort/hacked-mine hostility, `Manhack` ammo, `OnSkillLevelChanged`.
- `game/server/hl2/hl2_player.cpp` + `.h` — infinite aux default, flashlight-as-NV skips vehicles, stunstick is keepable, squad-marker cleanup.
- `game/server/ai_basenpc.cpp` + `.h` — SP player bleed, eye-glow sprite API (`noglow` keyfield).
- `game/server/hl2/npc_metropolice.cpp` + `.h` — `weapon_smg2` proficiency + NVG eye glow.
- `game/server/hl2/weapon_stunstick.cpp` + `.h` and `game/client/hl2/c_weapon_stunstick.cpp` — EZ charge. Attack2 builds charge, release / attack / a 3 second cap commits the swing, damage is `base * clamp(1.5 * charge², 1, 100)`, `m_bInSwing` is networked, and a charged kill electrifies the ragdoll. These are the files `--build-games=ez1` compiles (`server_hl2.vpc` / `client_hl2.vpc`). Official EZ1 instead compiles `game/shared/hl2mp/weapon_stunstick.cpp`; that HL2MP class is not on the HL2 link, so the hunks live on the HL2 weapon.
- `game/server/hl2/npc_combine.cpp` + `.h` — EZ1 ally-squad on the existing `CAI_BaseActor` soldier (`#ifdef EZ1`, so the ez2 husk build is untouched). Commandable spawnflag, `+use` follow toggle, squad add/remove inputs, command-point orders, follow-behavior deferral, `weapon_smg2` proficiency, elite alt-fire against non-allies, one-hit rebel kick.

Still not ported, and why:

- Stunstick projected texture. Official code constructs `CFlashlightEffect(0, MUZZLEFLASH)`. This tree's `flashlighteffect.h` only has a one-argument constructor. Charge is shown as a larger halo and faster sparks instead.
- Combine base-class swap to `CNPC_PlayerCompanion`, manhack deploy, and health regen. The official soldier inherits the companion and then drops assault/actbusy. This tree's companion does not own `AddToPlayerSquad` (that API is already on `CAI_BaseNPC`), and `npc_combine.cpp` is also the ez2 husk base. Regen runs inside `CAI_PlayerAlly::PrescheduleThink`, which this soldier does not inherit. The EZ1 regen bit is reserved at bit 20 so it does not alias the commandable flag. Player `weapon_ManhackToss` is already linked.
- Priority 2 NPC files in `docs/ez1-ifdef-inventory.md` (vortigaunt, fastzombie, citizen, zombine, zombie, manhack, and the rest of the 89-file scan). They are not required for the ez1 link. EZ1 maps spawn the vanilla versions of those NPCs until a later behavior pass.

Unique units from EZ1v4.0 are in-tree (`game/server/ez1`, `ez2`, `mod`, `Human_Error`, `game/client/Human_Error`) and compile with `--build-games=ez1`. Keep `GAME_PROFILE` at `hl2` until the headset checklist passes.
