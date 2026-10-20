# vip_sd (Plugify)

[VIP] Show Damage: prints the damage an attacker dealt to the attacker's screen centre (player_hurt), for VIPs whose 'show_damage' feature is on and when the victim is on another team. Ported from the Pisex VIP core module VIP_ShowDamage (vip_sd). Requires the vip core.

Plugify port (fork) of **VIP_ShowDamage ([VIP] Show Damage)** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-vip-modules
The code was rewritten for Plugify / s2sdk; behaviour follows the original.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugify dependencies: s2sdk, vip, hudmenu (optional)

## Installation
Download `vip_sd-1.0.1.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/vip_sd`.

`plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

