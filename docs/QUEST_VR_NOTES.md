# Quest VR prototype notes

The latest Mario Kart race-camera, framebuffer-resolution, and hardware-test analysis is in
[`QUEST_VR_MARIO_KART_CAMERA_AUDIT.md`](QUEST_VR_MARIO_KART_CAMERA_AUDIT.md). It supersedes the
older assumption below that Mario Kart race draws were genuinely orthographic: the game commonly
folds `guLookAt` into a real perspective projection, which the prototype previously misclassified.

## Status

Audit branch: `ppsspp-openxr-audit` (based directly on `quest-vr-prototype` at
`ba0b775d596a0a79f0cf46201b045140229719bc`)

Primary target: Meta Quest 3, GLideN64, Mario Kart 64 (one-player Time Trial on Luigi Raceway).

The prototype currently contains:

- an optional OpenXR Android presentation module;
- a complete OpenXR instance/session/reference-space/swapchain/frame loop;
- headset pose publication into GLideN64 through a small dynamically resolved C ABI;
- side-by-side eye rendering inside GLideN64;
- a per-eye clip-space transform for world triangles, calculated as
  `N64 projection * VR view * inverse(N64 projection)`;
- runtime eye positions and asymmetric OpenXR FOV terms applied to perspective world projections;
- monoscopic duplication of rectangle/HUD draws into both eye viewports;
- render-target-aware eye viewport/scissor mapping for the window and emulated framebuffer objects;
- eye-aware sampling of side-by-side framebuffer textures in rectangle and triangle passes;
- eye-aware final framebuffer copy, gamma, depth-copy, and FXAA shader paths so each OpenXR eye
  receives only its matching source half;
- source/destination-aware direct framebuffer blits, including packed per-eye partial subtexture
  copies instead of accidentally copying only the left-eye region;
- VR-aware retention of triangles that rejection microcodes would discard against the original
  camera's screen box, leaving final clipping to each GPU eye transform;
- source-frame pose association: GLideN64 records the OpenXR display-time pose consumed by the
  frame at swap (including HUD/menu-only frames), and the projection layer retains that pose for
  compositor reprojection;
- an experimental Quest/GLideN64-only source-width multiplier, capped to control memory and fill
  cost while a proper anisotropic per-eye render-target path is developed;
- expanded GLideN64 CPU clipping while stereo is active;
- an OpenXR Touch action-set fallback merged with the normal player-one controller state;
- a Mario Kart 64 profile using GLideN64's existing `hack_MK64` ROM identification, with recovery
  for race views folded into the projection stack and optional camera offsets that default to zero;
- automatic fallback to the existing Android presentation path when no VR headset/runtime is present.

This is native stereo geometry work, not a completed-frame texture copied to both eyes. The OpenXR
presenter only splits the final external texture after GLideN64 has produced different geometry for
the two halves. It can also present a monoscopic source as an integration fallback when GLideN64 is
not selected or its VR bridge is not loaded.

Quest 3 validation has confirmed that the Android emulator and Mario Kart 64 continue running, but
builds through `99e17935` remained in the Quest loading environment without a visible OpenXR layer.
The `quest-vr-first-frame-debug` build did not isolate swapchain rendering: its red/blue clear still
sat behind `shouldRender`, successful `xrLocateViews`, an exact two-view count, and both pose-valid
flags. The current build fixes that blind spot and the Android launch mismatch:

- every explicit `GameActivity` launch now carries the manifest's OpenXR action, default category,
  and `IMMERSIVE_HMD` category;
- a second `VIEW` reference space provides a head-locked projection fallback when tracked local-space
  views are not yet valid;
- the first 180 projection layers clear the left eye red and right eye blue, then automatically
  switch to the emulator texture;
- fallback layers continue with the emulator texture after that interval, so temporary tracking
  invalidity no longer forces zero-layer frames;
- session, `shouldRender`, locate-view fallback, first-layer, EGL, GLES, and OpenXR failures now leave
  concise logcat breadcrumbs.

The PPSSPP comparison and current proof sequence are documented in
[`PPSSPP_OPENXR_AUDIT.md`](PPSSPP_OPENXR_AUDIT.md). The audit build first submits a bright green
head-locked quad, then red/blue projection views, then the emulator source when new SurfaceTexture
frames are proven. Each interval counts only while the session is VISIBLE/FOCUSED, and source-shader
failure can no longer prevent the diagnostic layers.

## Presentation modes

Cinema screen is the polished mode and the one to judge the build by. Immersive projection renders
at the headset's field of view, which is wider than the frustum the games actually submit geometry
for, so the scene still ends in a hard edge where the original frustum stopped. That gap is known
and unfixed; closing it needs the expanded CPU clipping described above to cover the full eye
field, not just the packed source.

Presentation mode reaches the renderer for three things, and cinema turns all of them off, because
a panel fixed in the room is not a head-mounted camera: head rotation, head translation, and the
OpenXR field-of-view replacement. Per-eye offsets stay on, so cinema keeps its stereo depth.

## Resolution

A Quest install defaults to 1920x1440 per eye, from a 3840x1440 side-by-side producer. Both halves
of that have to move together: the render-resolution preset sets the per-eye size and
`max_stereo_source_width` clamps it, so raising either alone does nothing. Lower the width cap
first if a headset ever shows a black or missing image, since an oversized producer can fail
without reporting an error.

## Existing renderer architecture

`GameActivity` and `CoreService` both run in the manifest's `:EmulationProcess`. The service is local
to that process rather than a remote Binder service.

- `CoreService` starts emulation on `HandlerThread("ServiceStartArguments")`.
- `PixelBuffer` creates the producer `SurfaceTexture` and `Surface`.
- `CoreInterface.setNativeWindow` passes that producer surface through `ae-bridge` to the native
  video extension.
- The selected graphics plugin owns its EGL context on the core/emulation thread, renders to the
  `ANativeWindow` made from that surface, and swaps it in the video extension.
- `GameSurface.RenderThread` owns a second EGL context and Android window surface.
- `ShaderDrawer` attaches the producer `SurfaceTexture` to an external-OES texture, latches frames,
  runs optional Android shader passes, and `GameSurface` swaps the Android window.

In Quest mode, `GameSurface.RenderThread` still owns the consumer external-OES texture. It creates
the OpenXR session using that same current EGL context, latches emulator frames as they arrive, and
runs the OpenXR frame loop at runtime pacing. The normal Android shader/swap path is retained for
non-VR devices and OpenXR initialization failures.

The producer and presenter run asynchronously. OpenXR pose and view values are published as one
sequenced snapshot; GLideN64 freezes that snapshot at the first perspective draw so a frame cannot
mix head poses between batches. It records the frozen pose timestamp immediately before the
producer buffer swap. After `SurfaceTexture`
latches that buffer, the presenter matches the timestamp against a short history of located OpenXR
views. The projection layer then reports the view pose that actually produced the held emulator
image, rather than claiming that a 30 FPS image was rendered at the newest headset pose. This gives
the runtime valid metadata for rotational reprojection between N64 frames.

## OpenXR integration

Module: `quest-vr`

Dependency: `org.khronos.openxr:openxr_loader_for_android:1.1.61`

The module uses the loader's Prefab CMake target and implements:

- `xrInitializeLoaderKHR` with the activity VM/context;
- `XR_KHR_android_create_instance` and `XR_KHR_opengl_es_enable`;
- HMD system discovery and GLES requirements validation;
- a session bound to the exact EGL display/config/context used by `GameSurface`;
- a local reference space;
- a view reference space used only for head-locked bootstrap/fallback presentation;
- one recommended-resolution GLES swapchain per primary-stereo view;
- event-driven session begin/end and instance-loss handling;
- `xrWaitFrame`, `xrBeginFrame`, `xrLocateViews`, image acquire/wait/release, and `xrEndFrame`;
- orientation and center-eye position publication using predicted display time;
- per-eye tracked positions and FOV publication for projection-aligned stereo;
- recenter propagation on OpenXR reference-space changes;
- explicit recenter from the in-game drawer or a paired keyboard's F12 key;
- Touch controller action sync, with both thumbsticks clicked together as an in-headset recenter;
- runtime exit/loss propagation that stops the OpenXR frame retry loop and returns to Android
  presentation;
- clean resource destruction before the EGL context is destroyed.

The frame loop follows `frameState.shouldRender`. Once the runtime permits rendering, it always has
a valid two-view composition path: tracked views use `LOCAL`, while unavailable/invalid tracked views
use conservative identity eye poses in `VIEW`. This avoids the former behavior where a transient or
unreported tracking-validity flag silently made every `xrEndFrame` submit zero layers.

The bridge retains loader references to the resolved GLideN64 and Android input plugins until VR
shutdown, so activity or surface teardown cannot leave cleanup callbacks pointing into an unloaded
native library.

The OpenXR layer currently samples the emulator's external-OES texture into each eye swapchain. A
GLideN64 stereo source uses the left or right half. Other plugins remain monoscopic and are aspect
fitted into both views.

## GLideN64 stereo approach

GLideN64 retains separate N64 model-view and projection matrices but transforms N64 vertices to clip
space on the CPU before submitting them. Reinterpreting only the finished framebuffer cannot create
stereo.

The prototype therefore works at the final GL draw boundary:

1. GLideN64 interprets each display list and uploads textures/vertex batches once.
2. Each triangle batch is drawn once into the left half and once into the right half.
3. A generated vertex-shader uniform applies a different clip transform for each eye before the
   existing N64 viewport and screen conversion.
4. The transform reconstructs view space with the inverse active N64 projection, applies recentered
   headset rotation, runtime eye offset, optional limited translation, and then applies an eye-FOV
   projection while preserving the game's depth mapping.
5. Rectangle, screen-modified, and orthographic triangle draws are duplicated without the world
   transform, keeping common HUD/2D elements at zero disparity for the initial build.

This shares display-list interpretation, texture decoding/uploads, lighting work already performed
on the CPU, and vertex-buffer uploads. View-dependent rasterization is duplicated. CPU X/Y clipping
is relaxed in VR mode so geometry needed by an off-axis eye is less likely to be discarded before
the GPU sees it.

GLideN64's raw GL viewport is intentionally larger than the physical render target because its
generated shaders convert N64 pixel coordinates through a fixed virtual screen. The stereo mapper
therefore tracks texture/renderbuffer attachments and the current draw FBO, then maps the original
viewport and scissor from the real target width into each target half. Simply halving the raw
viewport would place the right eye outside common framebuffer sizes. Framebuffer-backed texture
coordinates are remapped to the active eye after the texture engine has produced final coordinates;
the world-transform toggle is separate so orthographic HUD passes can remain zero disparity while
still sampling the correct eye.

GLideN64's final textured-copy and post-processing programs are registered with the same per-eye
draw scope. Their source X coordinate is mapped into the active half before sampling. Text/font
programs are deliberately not registered, so ordinary atlases remain full-width and unchanged.

The current method assumes the projection active at draw time matches the vertices in the batch.
That is generally true for conventional GLideN64 batches and is a practical first target for Mario
Kart 64, but titles that mix projection matrices inside one batch may need projection IDs or retained
untransformed vertices.

## Configuration

Developer settings are stored in Android shared preferences named `quest_vr`. They are available
on-device under **Gallery drawer → Display → Quest VR prototype** and take effect on the next game
launch. Numeric fields accept signed decimal text and fall back to the documented default when
malformed. Defaults are:

| Key | Default | Current use |
| --- | ---: | --- |
| `enabled` | `true` | Attempts OpenXR; the runtime, not the PackageManager head-tracking feature, decides availability |
| `presentation_mode` | `immersive_projection` | Selects native projection views or the paired LOCAL-space cinema quads |
| `immersive_view_scale` | `1.0` | 0.5-1.0 scale applied after aspect-fitting the source into projection swapchains |
| `stereo_enabled` | `true` | Enables GLideN64 side-by-side geometry |
| `swap_eyes` | `false` | Exchanges source halves to diagnose or correct inverted stereo |
| `ipd_meters` | `0.064` | Scales the runtime eye baseline to the requested stereo separation |
| `world_units_per_meter` | `64.0` | Maps tracked metres to N64 view units |
| `rotation_strength` | `1.0` | Scales recentered headset rotation |
| `screen_scale` | `1.0` | Cinema-only physical screen-size multiplier |
| `screen_distance_meters` | `2.0` | Cinema-only LOCAL-space anchor distance |
| `position_enabled` | `false` | Enables positional head translation |
| `max_translation_meters` | `0.15` | Comfort/culling limit for translation |
| `camera_offset_x_meters` | `0.0` | Head-relative camera offset |
| `camera_offset_y_meters` | `0.0` | Head-relative camera offset |
| `camera_offset_z_meters` | `0.0` | Head-relative camera offset |
| `near_plane` | `0.1` | Reserved for the replacement projection path |
| `far_plane` | `10000.0` | Reserved for the replacement projection path |
| `hud_depth_meters` | `2.0` | Reserved for a composited HUD layer |
| `hud_scale` | `1.0` | Reserved for a composited HUD layer |
| `hud_mode` | `monoscopic_overlay` | Documents the current zero-disparity HUD policy |
| `culling_expansion` | `1.25` | Reserved for a graduated clipping policy |
| `use_openxr_fov` | `true` | Replaces perspective angular terms with each runtime eye FOV |
| `mario_kart_profile_enabled` | `true` | Enables title-scoped folded race-camera recovery |
| `mario_kart_camera_offset_y_meters` | `0.0` | Optional recovered camera-space height offset |
| `mario_kart_camera_offset_z_meters` | `0.0` | Optional recovered camera-space chase-distance offset |
| `stereo_source_width_multiplier_v2` | `2.0` | Aspect-preserving source multiplier; 2 requests the selected flat size for each eye |
| `max_stereo_source_width` | `2688` | User cap for the complete side-by-side source width |
| `touch_controller_enabled` | `true` | Merges OpenXR Touch input into N64 player one |
| `debug_logging` | `true` | Periodically logs pose, draw counters, and OpenXR frame timings |
| `startup_proof_layers` | `false` | Debug-only green and red/blue composition proofs |

Positional tracking is intentionally off by default for the first Quest build. Orientation and eye
separation are active. The renderer automatically captures the first valid headset pose as its
recenter origin.

Prototype diagnostics log average `xrWaitFrame` time, native frame-loop work time, the maximum
work-time sample, source/swapchain dimensions, geometry/rectangle/eye draw totals, the last stereo
target width, and any render-target dimension fallbacks. GLES and incomplete-framebuffer failures
are always logged even when periodic diagnostics are disabled.

The latest session also writes a line-flushed persistent report. Export it without ADB from
**Gallery drawer → Display → Quest VR prototype → Export latest VR diagnostic**.

## Build and CI

Local command:

```sh
./gradlew assemble
```

GitHub Actions workflow: `.github/workflows/build.yml`

Expected release APK: `app/build/outputs/apk/release/Mupen64PlusAE-release.apk`

The workflow verifies that the release APK contains the arm64 OpenXR loader, Quest VR bridge, and
GLideN64 plugin before uploading it as a build artifact. The application already packages
`arm64-v8a`; the OpenXR module is intentionally restricted to `arm64-v8a` for Quest.

## Changed areas

- `quest-vr/`: OpenXR loader dependency, JNI bridge, swapchains, pose source, and presentation.
- `app/src/main/AndroidManifest.xml`: optional head-tracking feature, OpenXR discovery, and immersive
  HMD category.
- `ActivityHelper.java`, `GalleryActivity.java`, and `CoreService.java`: preserve the OpenXR action
  and immersive-HMD categories on explicit game and notification launches.
- `GameSurface.java`: GLES 3/OpenXR selection, VR frame pacing, fallback, and lifecycle cleanup.
- `ShaderDrawer.java`: separate external-texture latching from normal Android shader presentation.
- `QuestVrPrefsActivity.java` and `preferences_quest_vr.xml`: on-device developer tuning controls.
- `mupen64plus-video-gliden64/upstream/src/QuestVr.*`: explicit VR state/configuration and C bridge.
- `DisplayWindow.cpp`: records the consumed OpenXR pose timestamp at producer buffer swap.
- `mupen64plus-input-android/src/plugin.cpp`: thread-safe optional Touch overlay for player one.
- GLideN64 OpenGL drawers: two-eye viewport replay without duplicate vertex uploads.
- GLideN64 generated and special shaders: eye/head clip transforms plus eye-aware framebuffer,
  final-copy, gamma, depth-copy, and FXAA sampling.
- GLideN64 context wrappers: render-target dimension and draw-FBO tracking for stereo mapping.
- `gSP.cpp`: stereo-aware clipping retention.
- `.github/workflows/build.yml`: static rejection of accidental literal escapes in generated GLSL.

## Known limitations and risks

- The emulator/audio path is validated on Quest 3, but the corrected OpenXR layer path is awaiting
  its first device test; axis signs, eye order, and world scale may require immediate tuning.
- OpenXR FOV correction only applies to matrices recognized as perspective projections. Complex
  game-specific projection tricks can still be misclassified.
- The Mario Kart camera profile is a conservative matrix-pass heuristic, not a reverse-engineered
  kart/driver transform. Its signs and distances require Quest 3 validation and may expose the kart
  interior or game-side culling on some camera modes.
- Game-side culling can still omit scenery exposed by large head turns or leaning.
- Common framebuffer-backed geometry, direct blits, and final textured-copy/post-processing
  sampling are eye-aware, but unusual framebuffer paths still need device validation.
- Billboards still face the original game camera.
- Sky/background passes are not yet classified separately from world geometry.
- Rectangle HUD elements have zero disparity but are not yet submitted as an OpenXR quad layer.
- The original Android game sidebar is not yet reproduced in an immersive layer.
- OpenXR swapchain rendering is single-sample, and no foveation extension is enabled.
- Stereo sizing now preserves the selected per-eye aspect and defaults to a full per-eye request,
  but the safe 2688-wide producer cap means 4:3 presets above roughly 1344x1008 per eye deliberately
  converge on the same 2688x1008 total plateau. Higher presets cannot add detail until the
  SurfaceTexture/EGL failure above the observed 2800-wide range is understood.
- A 30 FPS N64 title is latched into a higher-rate OpenXR loop. Source-pose metadata now permits
  runtime reprojection of held frames, but game-world animation and camera translation still update
  at emulation cadence, and the association needs on-device timing validation.
- Touch input uses the core Oculus Touch interaction profile, which Quest Touch Plus can expose
  through runtime compatibility. The current mapping is left stick=N64 stick, A/B=N64 A/B,
  either index trigger=Z, left/right squeeze=L/R, right stick=C buttons, right stick click=Start,
  X/Y=D-pad left/up, and both stick clicks=recenter. Paired Android controllers remain usable and
  their buttons are merged; an active Touch left stick takes analog priority.

## First device test checklist

1. Select GLideN64, select immersive projection, leave proof layers off, and launch Mario Kart 64.
2. Confirm the game appears directly through an `XrCompositionLayerProjection`, then compare with
   the separate world-locked cinema option.
3. Confirm OpenXR runtime/session/swapchain messages and source-frame pose matching in the exported
   persistent log.
4. Verify each eye receives the correct half (no inverted stereo); toggle **Swap source eyes** and
   relaunch the game if the order is reversed.
5. Confirm the course geometry has parallax while HUD rectangles remain zero disparity.
6. Check yaw, pitch, and roll direction from a stationary kart.
7. Verify Touch A/B, analog steering, Z/R, C buttons, Start, and the two-stick-click recenter chord.
8. Tune `world_units_per_meter`, IPD, and camera Z offset before enabling positional tracking.
9. Record emulator FPS, OpenXR cadence, thermals, framebuffer-effect defects, disappearing geometry,
   and any native crash backtrace.

## Next implementation steps

1. Fix all CI compiler/linker findings and retain a downloadable APK at every checkpoint.
2. Validate the target-aware SBS layout and framebuffer sampling on Quest 3, including menus,
   countdowns, transitions, and Mario Kart's framebuffer effects.
3. Use the new native timing logs to tune `stereo_source_width_multiplier_v2`, then add GPU timing/foveation
   if the full-resolution default misses the Quest 3 frame budget.
4. Refine the Mario Kart 64 Z-buffer/projection heuristic from device captures, then identify a
   stable kart/driver-relative transform for an optional first-person profile.
5. Add a visible immersive debug overlay with pose, eye matrices, draw counts, and frame timings.
6. Validate and tune the OpenXR Touch mapping on Quest 3, then add input haptics and an immersive
   settings/recenter panel.
7. Enable comfort-limited position tracking, collect culling failures, and refine clipping/submission
   retention around the original game camera.
