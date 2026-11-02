# vip_sd (Plugify)

[VIP] Show Damage: prints the damage an attacker dealt to the attacker's screen centre (player_hurt), for VIPs whose 'show_damage' feature is on and when the victim is on another team. Ported from the Pisex VIP core module VIP_ShowDamage (vip_sd). Requires the vip core.

Plugify port (fork) of **VIP_ShowDamage ([VIP] Show Damage)** by Pisex (Metamod (C++)).
Original: https://github.com/Pisex/cs2-vip-modules
The code was rewritten for Plugify / s2sdk.

## Requirements
- [Plugify](https://github.com/untrustedmodders/plugify) with [s2sdk](https://github.com/untrustedmodders/plugify-plugin-s2sdk)
- Plugin version: 1.1.0
- Plugify dependencies: s2sdk (>=2.18.2), vip (>=1.0.0)
- Self-contained s2sdk integration: uses s2sdk directly, without the pisexcompat library.

## Installation
Download `vip_sd-1.1.0.zip` from Releases and extract it into `game/csgo/`.

## Building
Requires Docker. `./build.sh` builds in the Steam Runtime 3 (sniper) SDK image; the package
is written to `build/package/vip_sd`.

`plugins/vip` carry only the API headers of the plugins this one uses.

## License
GPL-3.0, see LICENSE.

