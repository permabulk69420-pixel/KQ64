# Quest VR Mario Kart camera and internal-resolution audit

## Menu/HUD hardware follow-up after `b77758e`

The Quest 3S run checked in at `a994c0b` is decisive because it records the build that showed no
visible improvement. Across the complete Mario Kart session:

- fully modified screen-space triangle batches remained at zero;
- the `_renderScreenSizeBuffer` / framebuffer-blit diagnostic remained at zero;
- ordinary rectangle draws reached 54,496;
- the mono 640x580 texrect scratch path composed 61,289 rectangles in 4,242 batches; and
- the active internal framebuffer was 2688x1007, or about 1344x1007 per eye.

The previous fixes therefore targeted two paths Mario Kart's menus and racing portraits did not
use. The active presentation is `FrameBufferList::renderBuffer` followed by
`GraphicsDrawer::copyTexturedRect`, which draws a textured rectangle rather than calling the
instrumented blit path.

That active copy had a concrete double-split. Its packed texture was 2688 pixels wide, while its
source rectangle was already expressed in one-eye coordinates, normally 0..1344. The rectangle
code normalized 0..1344 by the physical width 2688, producing UV 0..0.5. The registered Quest copy
shader then selected an eye by halving that range again. The left eye sampled 0..0.25 and the right
eye sampled 0.5..0.75: only half of each intended eye image, enlarged to fill the eye. The same
incorrect physical-width comparison also selected linear filtering for what should be a same-size
per-eye copy.

The corrected contract keeps allocation width and coordinate width separate. A packed 2688-pixel
source now has a 1344-pixel coordinate width; the shader performs the sole eye selection. Mono
framebuffer textures are no longer inferred to be packed merely because they are framebuffer
textures. The final copy is also recorded by the existing bounded diagnostic, so a new log must
show a physical/logical source pair near 2688/1344 and a complete 0..1344 source rectangle.

The racing HUD also follows rectangles rather than fully modified triangles. Only the final
composition of the explicitly mono texrect scratch texture is now marked as screen-space UI and
registered for the OpenXR optical-centre transform. Ordinary rectangles, framebuffer copies, and
world geometry are not globally shifted. The next log must show nonzero screen-space batches,
draws in both eyes, a complete uniform mask, and nonzero correction counters before any claim is
made about portrait convergence.

The existing screenshot action now captures the complete external-OES producer image before eye
selection, aspect fitting, and OpenXR submission. It applies Android's actual `SurfaceTexture`
matrix and writes one bounded PNG. On character select, each 1344x1008 half should contain all
eight characters. If it does but the headset does not, the remaining fault is in OpenXR sampling;
if either half contains only four, the remaining fault is still in GLideN64.

Bounded persistent snapshots record the screen-space framebuffer/program/uniform mask, eye draw
and correction counts, actual NDC offsets, texrect scratch target (normally 640x580), final
physical/logical copy rectangles and filter, SurfaceTexture matrix/corners, per-eye UV region,
swapchain viewport, and `XrSwapchainSubImage`. Enabling **Show startup proof layers** also displays
an optional red/blue calibration pattern with a full border, centre lines, an aspect-correct square
and circle, and left/right edge marks. These are diagnostics only; the option remains off by
default.

## Hardware evidence

The checked-in `quest-vr-diagnostic-latest.log` is a Quest 3S run of the immersive-projection
build. It proves that the outer transport is healthy:

- the selected 1440x1080 per-eye preset resolved to a safe 2688x1008 complete SBS producer;
- the PixelBuffer, producer EGL surface, SurfaceTexture, and OpenXR consumer all reported that
  2688x1008 size;
- the OpenXR session remained focused, source frames continued arriving, source-pose matches were
  normally available, and projection layers reached successful `xrEndFrame` calls;
- runtime eye separation was about 63 mm and Touch input remained active.

Those facts only describe the final Android/OpenXR surfaces. They did not establish the resolution
of GLideN64's emulated N64 framebuffer or whether the game geometry received a VR view transform.
The renderer counters and configuration path expose both failures.

## Why the large source still looked like native N64 resolution

The Quest gallery intentionally selected the built-in `GlideN64-Fast` profile so the prototype
could not accidentally launch a different video plugin. That profile contains:

```ini
UseNativeResolutionFactor=1
```

In GLideN64, a nonzero native-resolution factor overrides the window-derived scale used by
`FrameBuffer::init`. Factor 1 therefore creates common emulated color buffers at roughly the N64
color-image size (often 320x240). The stereo draw mapper then divides that physical buffer into two
eye regions. In the worst common case, each eye had only about 160x240 internal samples before the
final passes stretched the pair to the correctly allocated 2688x1008 Android source. This explains
both the hardware appearance and why changing only the outer resolution preset had almost no
effect.

For an actual packed GLideN64 VR target, this iteration now:

1. overrides `UseNativeResolutionFactor` to 0 after logging the requested and effective values;
2. derives one-eye scale from `min(windowScaleX / 2, windowScaleY)`;
3. allocates framebuffer and matching depth storage at twice the one-eye physical width while
   retaining the one-eye height and independent horizontal/vertical texture ratios;
4. maps ordinary GLideN64 viewport/scissor coordinates into each full-resolution packed region;
5. leaves ordinary flat launches and their profile-selected native factor unchanged.

At the 2688x1008 safety plateau, the expected largest main buffer is therefore approximately
2688x1008 total, or 1344x1008 per eye, with `nativeFactor=0`. Periodic persistent diagnostics now
record the N64 logical size, packed physical size, per-eye physical size, render scale, viewport,
scissor, allocation count, and effective native factor. Texture artwork remains N64 source art;
higher internal resolution improves geometry edges, sub-pixel placement, and filtering but cannot
invent detail absent from the original texture.

## What immersive projection was actually doing

The OpenXR presenter submits one `XrCompositionLayerProjection` with two eye views. Each eye samples
its half of the completed GLideN64 source, and the layer uses the pose/FOV associated with that
source frame. This is correct compositor metadata, but it is not itself a game-camera transform.

If GLideN64 rendered a frame from that source pose, OpenXR can reproject it from the recorded pose
to the latest display pose. If GLideN64 did not move the scene camera, assigning the unchanged image
the current source pose makes the result look attached to the headset. The latter is exactly what
the hardware test observed during a race.

The Nintendo logo worked because it used a conventional perspective projection recognized by the
late geometry bridge. In the latest log, conventional perspective draw count reached 33,359 during
the logo and then stopped. During gameplay, the old `orthographic` count grew above 315,000 while
the conventional-perspective count remained frozen. That did not mean Mario Kart's race world was
really authored as a 2D orthographic scene.

## Mario Kart's real camera and the false orthographic classification

The Mario Kart 64 decompilation provides a source-level explanation:

- [`Camera`](https://github.com/n64decomp/mk64/blob/44c71a7978e6abae95399db732a40186a165e2ee/src/camera.h)
  stores `pos`, `lookAt`, `up`, and rotation values. The normal chase-camera update writes those
  fields before rendering.
- [`render_player_one_1p_screen`](https://github.com/n64decomp/mk64/blob/44c71a7978e6abae95399db732a40186a165e2ee/src/racing/skybox_and_splitscreen.c)
  creates a real `guPerspective` matrix and a `guLookAt` matrix from that camera.
- In a common race path, the game loads `guPerspective` into `G_MTX_PROJECTION` and then multiplies
  `guLookAt` into that same projection stack. Modelview becomes identity for course rendering.

The previous bridge inspected only the final active projection and required the canonical
perspective signature. Once the look-at matrix had been folded into it, the signature was gone, so
the bridge counted the race as orthographic and deliberately skipped the head/eye transform.

This iteration observes standard `gSPMatrix` projection loads and multiplies. It retains the
canonical perspective matrix `P` before the game's view `Vgame` is folded into it. A vertex already
transformed to `P * Vgame * model * vertex` can then be adjusted with:

```text
Popenxr * Vhead-and-eye * inverse(P)
```

`inverse(P)` recovers the existing game-camera-space value; it does not remove the chase camera.
The controller-driven camera remains the baseline, while the recentered headset rotation and eye
offset are applied on top. OpenXR composition continues to report the matched source-frame pose,
which is the normal VR contract rather than a second application of the same rotation.

Persistent counters now separate canonical perspective draws from recovered `P*Vgame` folded-view
draws. A successful Mario Kart race test should show `foldedView` growing rapidly instead of the
entire race accumulating under `orthographic`.

The Mario Kart-specific offsets now default to zero so this first camera-recovery test is not
confounded by an unvalidated 20 cm / 75 cm translation. Existing user-entered offsets remain
available.

## Limits of the experiment

This is a meaningful pre-framebuffer camera-space correction, but it is still later than Mario
Kart's game logic and display-list construction:

- Mario Kart chooses course display-list segments using the chase camera's yaw, collision section,
  and path section. The decompilation's
  [`render_course_segments`](https://github.com/n64decomp/mk64/blob/44c71a7978e6abae95399db732a40186a165e2ee/src/racing/render_courses.c)
  makes that game-level culling explicit.
- GLideN64 now avoids much of its own original-frustum X/Y clipping in stereo mode, but it cannot
  render a course segment the game never placed in the display list.
- Texture rectangles and intentional screen-space UI remain zero-disparity and head-stable.
  Perspective billboards and racers should follow the recovered world transform; framebuffer
  copies can only preserve whatever stereo was already rendered.
- Positional tracking remains disabled by default. A robust six-degree-of-freedom implementation
  needs game-camera/collision/culling policy beyond this bounded experiment.

The decompilation's fly-camera enhancement is additional evidence for the next step: it modifies
the actual `Camera` before rendering and bypasses normal course-segment selection by rendering the
credits/full-course path. A production game-specific profile would need a region-safe game hook or
an equivalent display-list/culling integration. Hard-coding one ROM's RDRAM camera addresses in
this iteration would be fragile and would not independently solve per-eye rendering.

## Quest test contract

1. Install the new APK over the prior test build. In **Quest VR prototype** settings select
   **Immersive projection**, keep stereo/OpenXR FOV/Touch enabled, set rotation to `+1.0`, position
   tracking off, Mario Kart race-camera recovery on, and both Mario Kart offsets to `0.0`.
2. Select 960x720. Run for at least **90 seconds**: include the spinning Nintendo logo, menus, and
   at least **45 seconds of a one-player race**. Recenter once. Check that yaw/pitch reveal a
   different part of the race world rather than carrying the completed picture with the headset.
3. With the head still, inspect Mario, nearby racers, item boxes, flags, and track edges. Record
   whether each object fuses, has reversed depth, or remains doubled. Do not change IPD or swap-eye
   settings during this first run.
4. Exit normally and immediately export diagnostics. Expected evidence includes
   `effectiveNativeFactor=0`, an internal packed framebuffer near 1920x720 total / 960x720 per eye,
   and a rapidly increasing `foldedView` perspective counter during the race.
5. Repeat at 1440x1080 selected resolution for **60 seconds**. The safe result should be about
   2688x1008 total / 1344x1008 per eye. This should look materially sharper than 960x720. Presets
   above this point are expected to report the same safety plateau rather than improve further.
6. Run **World-locked cinema screen** for **30 seconds** to confirm the known-working fallback and
   Touch mapping were not changed. Export that log only if cinema or input regresses.

The decisive result is not merely that the APK starts: the race log must show recovered folded-view
draws and a full-resolution packed internal framebuffer, and the headset must show whether those
draws provide usable head look and stereo convergence before a deeper game-specific camera hook is
attempted.
