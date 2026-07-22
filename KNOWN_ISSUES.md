# Known issues

This is the working list for the first KQ64 public alpha. It is intentionally blunt: the emulator is useful now, but the VR renderer is still being pushed through games that were never designed to expose their camera and draw state this way.

## Rendering

- **Camera culling:** games often stop drawing geometry outside the original flat camera frustum. Looking around can reveal missing walls, scenery, characters or objects.
- **2D layers and overlays:** menus, text, weapons, sprites, selection boxes and other screen-space effects can be flat, offset, doubled or clipped in individual games.
- **Game-specific effects:** framebuffer effects, post-processing, unusual microcode and uncommon rendering paths may not behave correctly in stereo.
- **Cutscenes:** scripted cameras can begin at an odd pitch or height, face the wrong way, or leave the viewer detached from the intended camera.
- **Experimental Immersive:** this mode deliberately exposes more of the game world and therefore shows more culling and camera problems. Stereo Cinema is the release default for a reason.

## Compatibility

- KQ64 is not a universal compatibility claim for the N64 library.
- A game that boots and looks good early may still fail in another level, menu, cutscene or effect.
- Different ROM revisions and regions can take different rendering paths.
- Quest 3 is the only headset with substantial hardware testing so far.

## Input and interface

- The headset launcher and Player 1 mapping panel are new and basic.
- Bluetooth controllers, specialist mappings and many advanced settings still use the legacy Mupen64Plus-AE interface.
- Some settings require leaving the game and launching it again before the emulation process reloads them.

## Reporting a useful bug

Please include:

1. the game title, region and revision if known;
2. Stereo Cinema or Experimental Immersive;
3. the exact menu, level, cutscene or object that is wrong;
4. whether the problem affects one eye, both eyes, stereo fusion or camera direction;
5. a screenshot or short headset recording where possible;
6. a fresh in-app Quest diagnostic for that game if the issue is renderer-specific.

Do not include ROMs, BIOS files, texture packs you do not have permission to redistribute, or other copyrighted game data.