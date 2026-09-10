# EZ1v4.0 unique units

Imported by `scripts/import-ez1-sources.sh` from
[entropy-zero/source-sdk-2013](https://github.com/entropy-zero/source-sdk-2013)
tag `EZ1v4.0` (`8adf94f37107ce9e7a2678d75b91deb51243c8cb`).

Source only — no VPKs, maps, or Steam content.
Compiled only when `--build-games=ez1` (CI default stays `hl2`).

## Tree (extras.vpc)

### game/server/ez1
- weapon_ManhackToss.cpp
- achievements_EZ.cpp
- EZ_Achievements_Reset.cpp

### game/server/ez2
- npc_basepredator.cpp / .h
- npc_bullsquid.cpp / .h
- npc_zombigaunt.cpp / .h
- prop_command_point.cpp

### game/server/mod
- npc_base_custom.cpp / .h
- npc_lost_soul.cpp
- npc_shadow_walker.cpp / .h
- weapon_custom_flaregun.cpp
- weapon_custom_melee.h
- weapon_smg2.cpp

### Human_Error
- game/server/Human_Error/vehicle_drivable_apc.cpp / .h
- game/client/Human_Error/c_vehicle_drivable_apc.cpp

### client
- game/client/particles_ez.cpp / .h

See `game/server/server_ez1_extras.vpc` and `game/client/client_ez1_extras.vpc`.

Run locally:

```bash
git clone --branch EZ1v4.0 --depth 1 https://github.com/entropy-zero/source-sdk-2013.git
bash scripts/import-ez1-sources.sh /path/to/source-sdk-2013
```

Or dispatch workflow `import-ez1-units` on master (writes this branch).
