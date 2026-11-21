## FHU Companion

**English** | [简体中文](README.zh-CN.md)

The current FHU Companion source is in [`companion/`](companion/). It
builds `FHUCompanion.dll` with either the English or Simplified Chinese
menu. Both editions include the optional **Leak Without Stored Fluid**
setting, which is off by default. The regular OStrands plugin can run
alongside Companion.

- [Build instructions](companion/BUILD.txt)
- [Bilingual changelog](companion/CHANGELOG.txt)
- [Nexus Mods download page](https://www.nexusmods.com/skyrimspecialedition/mods/199648?tab=files)

The section below documents the earlier integrated edition. Use the
`companion/` directory when building the current Companion.

---

## OStrands with Fill Her Up integration

**English** | [简体中文](README.zh-CN.md)

OStrands is an SKSE plugin for Skyrim SE/AE that draws procedural strands, drips and semen during OStim and SexLab scenes. This build patches it into **Fill Her Up (FHU)**, so what you see comes from FHU's own release effects and its stored cum, not only from OStrands' climax events.

> **Adult content.** This is an 18+ mod for adult Skyrim frameworks (OStim, SexLab) and is meant for adult players. The repository holds source code, generated meshes and textures only: there are no photographic, video or illustrative depictions of sexual acts.

### What this build adds

**FHU release effects.** Fill Her Up plays its releases with effect items. OStrands watches the 12 release effect forms in `sr_FillHerUp.esp` and starts a release at that opening when one is equipped, and stops it when it is unequipped. Vaginal, anal and oral are handled separately, each with its own toggle. The forms are resolved once at data load; without the ESP the plugin simply runs as stock OStrands and logs that FHU was not found.

**Leakage from FHU's stored fluid.** Outside scenes, once a second, OStrands asks FHU's quest script `sr_inflateQuest` for `GetVaginalCum`, `GetAnalCum`, `GetOralCum`, `isPlugged` and `isGagged`. It queries the player every poll and a rolling window of up to six nearby actors. The stored amount drives a reservoir that leaks slowly instead of all at once, so a character keeps dripping long after the scene. Plugs and gags stop the leakage for that opening, and `GetOralCum` is only queried when oral leakage is on.

**Clothing blocks the flow.** `ClothingBlock` stops both release and leakage while the body or the two pelvis slots are covered. Anything in the `Equipment Exceptions` list overrides the automatic check, per armor, for the openings you pick, so a revealing outfit can be marked as not blocking.

**Its own settings page.** Everything above lives on a separate **Fill Her Up Release** page in SKSE Menu Framework, next to the stock Strands / Semen / Arousal / Alignment pages: release and leakage toggles per opening, leakage rate, release amount, round droplets, randomized flow, and the clothing rules.

**Separate fluid parameters.** FHU fluid has its own section, `[FillHerUp]` in `OStrands.ini`, with its own amounts, strength, viscosity, cohesion, stretch, skin adhesion, opacity, lighting compensation, lifetime and fade time. It also runs in its own fluid domain, so FHU fluid and scene semen never share a solver or a set of parameters. `OStrands_User.ini` still holds your overrides and is never overwritten.

### Creature support (inherited)

Creature support comes from the fork of [OStrands by LeonVanWinkle](https://gitlab.com/LilRicefield/ostrands) this build is based on, and is unchanged here. Creatures stay in SexLab and OStim scenes; strands, auto-pairing, body collision and semen follow each creature's own penis bones. Rigs for BakaFactory ABC skeletons are built in, a creature whose skeleton matches no rig gets its penis bones from the erect mesh it wears, and a creature only has a penis while it wears an erect mesh. See the upstream fork for those details.

Creatures can only be the giver. Creature receivers and creature mouths aren't supported, because creature skeletons don't have the opening and mouth bones OStrands looks for.

### Requirements

- SKSE and Address Library for SKSE Plugins
- OStim Standalone or SexLab (SexLab P+ is supported)
- Fill Her Up, for the features on this page. Without it OStrands runs as stock.
- For creatures: SexLab or OStim with creature animations, creature skeletons with penis bones, and erect creature meshes (for example More Nasty Critters with Creature Framework). The built-in rigs match BakaFactory ABC.

Optional: SKSE Menu Framework (in-game settings), OCum, SexLab Aroused / SLO Aroused NG / OSL Aroused (arousal drips), PPA - Penetration Physics.

### Settings

These go in `Data/SKSE/Plugins/OStrands.ini`. Put your own changes in `OStrands_User.ini`, which updates never overwrite.

```ini
[FillHerUp]
Amount = 1.5              ; strand frequency and droplet amount during a release
Strength = 1.0            ; how fast fluid leaves the body
Viscosity = 50.0
Cohesion = 15.0
Stretch = 5.0
Stickiness = 0.4          ; skin adhesion; lower values run off the skin
Opacity = 0.9
LightingCompensation = 40.0
Lifetime = 30.0
FadeTime = 6.0

SeepRate = 1.0            ; passive leakage rate from stored FHU fluid
ReleaseVaginal = true
ReleaseAnal = true
ReleaseOral = false
SeepVaginal = true
SeepAnal = true
SeepOral = false
RoundDrops = false        ; round droplets and puddles instead of strands
ClothingBlock = true
```

Every key here can also be changed in game on the Fill Her Up Release page. `Amount` and `SeepRate` at `0` turn that effect off without needing the toggles.

### Creature settings

```ini
[Creatures]
Enabled = true

[CreatureRigs]
; name = penis bones from base to tip | forward axis (x, y or z) | tip extension | shaft radius
troll = TD 2, TD 3, TD 4, TD 5, TD 6, TD 7 | x | 0.1 | 1.1
```

- A skeleton uses the first rig whose bones it has, all of them. If none match, a creature's penis bones are read from the erect penis mesh it wears, so packs without a rig still work.
- A creature only counts as having a penis while it wears an erect mesh skinned to those bones. On its soft mesh it has none.
- Change a built-in rig by repeating its name with new values.
- Remove a built-in rig with `name = none`.
- Add a new rig with a new name.
- A shaft radius of `0` measures the shaft from the worn mesh.

The built-in rigs cover:

| | | | |
|---|---|---|---|
| bear | boar | canine | chaurus |
| deer | draugr | falmer | gargoyle |
| giant | goat | horker | horse |
| mammoth | riekling | sabrecat | skeever |
| troll | vampire lord | werewolf | |

The chaurus rig also covers spiders and reapers, and the giant rig also covers lurkers.

The Creatures toggle is also on the Strands page of the SKSE Menu Framework settings.

### Building

1. Clone CommonLibSSE-NG into `extern/`:
   ```
   git clone --depth 1 --branch v12.1.0 https://github.com/alandtse/CommonLibSSE-NG extern/CommonLibSSE-NG
   ```
2. Run `build.cmd`. It needs Visual Studio's C++ build tools and vcpkg. The plugin is written to `build/release/0_OStrands.dll`.
3. Run `package.cmd` to build the release zip in `dist/`.

### Download

The built mod is published on Nexus Mods: **[Skyrim Special Edition mod 199648](https://www.nexusmods.com/skyrimspecialedition/mods/199648?tab=files)**. Download it there and install it with your mod manager.

Release notes and source archives are also on the [releases page](https://github.com/dwhcn/OStrands-FHU/releases). Every release tag maps to this repository's source; to produce the installable zip yourself, follow [Building](#building) and then run `package.cmd`.

### Versions

Versions follow the base fork, which follows upstream OStrands with one extra number: `upstream.version.fork-revision`. `0.8.0.2` is the second creature release on top of upstream 0.8.0, and is the version this Fill Her Up build is based on.

### Credits

- OStrands by LeonVanWinkle (Saint), with contributions from crajjjj.
- Creature support by Keevan2374, with the erect-mesh check, mesh fallback and OStim creatures by crajjjj.
- The built-in creature rigs are based on the BakaFactory ABC overrides that ship with PPA - Penetration Physics.
- Fill Her Up integration by dwhcn.
- Code implemented with [codex](https://github.com/openai/codex), guided and reviewed by the maintainer.

### License

GNU General Public License v3.0; see `LICENSE`. Release zips include the license and a notice naming the exact source commit.

Source: https://github.com/dwhcn/OStrands-FHU (upstream OStrands: https://gitlab.com/Keevan2374/ostrands)

---

> This document and this repository's release were written and submitted by DeepSeek Harness, an AI coding agent, at the maintainer's direction.
