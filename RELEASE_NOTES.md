# KQ64 v0.1.0-alpha

This is the first public KQ64 alpha: a Quest-focused N64 emulator with stereoscopic GLideN64 rendering and native OpenXR presentation.

It is ready for people to try, not ready to pretend every N64 game is perfect.

## Highlights

- stereoscopic rendering with separate per-eye geometry;
- 1920x1440 per eye by default, twice the pixels of earlier builds;
- recommended Stereo Cinema mode with a stable world-locked screen;
- Experimental Immersive mode for games and scenes that tolerate a wider VR camera;
- Quest Touch input and an in-launcher Player 1 controller mapper;
- ROM library and folder import;
- advanced Mupen64Plus-AE settings and high-resolution texture-pack support;
- recentering and bounded Quest diagnostics.

## Good first tests

Mario Kart 64, Star Fox 64, Doom 64, Donkey Kong 64 and The Legend of Zelda: Ocarina of Time have all been useful hardware test cases. Results still vary by game, region, revision and scene.

## Important before installing

- This build is an early alpha.
- It has been developed and tested primarily on Meta Quest 3.
- KQ64 uses the separate Android package ID `com.kq64.quest`, so it can coexist with M64Plus AE.
- No ROMs, game files or proprietary Nintendo assets are included.
- Use legally obtained ROMs.

## Main known issues

- Experimental Immersive renders a wider view than the field the games submit geometry for, so scenery can end at a hard edge; Stereo Cinema keeps the game's own camera and is not affected;
- Experimental Immersive framing remains rough next to Stereo Cinema;
- some menus, overlays, sprites, cutscenes and game-specific effects remain imperfect;
- compatibility is game-specific and later levels may exercise different renderer paths;
- the new launcher and controller mapper are functional but basic.

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) for the fuller list and useful bug-report details.

## Install

Download the APK attached to this release and sideload it with SideQuest, ADB or another Android sideloading tool. Open KQ64 from Quest's **Unknown Sources**, import your ROM folder and start with **Stereo Cinema**.

## Credits and licence

KQ64 is based on Mupen64Plus-AE, Mupen64Plus and GLideN64, with an OpenXR Quest presentation path. It is an independent project and is not affiliated with Nintendo, Meta or the upstream maintainers.

The source remains available under the GNU General Public License, version 3.