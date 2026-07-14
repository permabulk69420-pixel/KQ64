# Quest VR prototype notes

## Status

Branch: `quest-vr-prototype`

Primary target: Meta Quest 3, GLideN64, Mario Kart 64 (one-player Time Trial on Luigi Raceway).

The prototype currently contains:

- an optional OpenXR Android presentation module;
- a complete OpenXR instance/session/reference-space/swapchain/frame loop;
- headset pose publication into GLideN64 through a small dynamically resolved C ABI;
- side-by-side eye rendering inside GLideN64;
- a per-eye clip-space transform for world triangles, calculated as
  `N64 projection * VR view * inverse(N64 projection)`;
- monoscopic duplication of rectangle/HUD draws into both eye viewports;
- expanded GLideN64 CPU clipping while stereo is active;
- an OpenXR Touch action-set fallback merged with the normal player-one controller state;
- automatic fallback to the existing Android presentation path when no VR headset/runtime is present.

This is native stereo geometry work, not a completed-frame texture copied to both eyes. The OpenXR
presenter only splits the final external texture after GLideN64 has produced different geometry for
the two halves. It can also present a monoscopic source as an integration fallback when GLideN64 is
not selected or its VR bridge is not loaded.

No Quest 3 device validation has happened yet. Treat the APK as an instrumented prototype until the
first on-device logs and screenshots are available.

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

## OpenXR integration

Module: `quest-vr`

Dependency: `org.khronos.openxr:openxr_loader_for_android:1.1.61`

The module uses the loader's Prefab CMake target and implements:

- `xrInitializeLoaderKHR` with the activity VM/context;
- `XR_KHR_android_create_instance` and `XR_KHR_opengl_es_enable`;
- HMD system discovery and GLES requirements validation;
- a session bound to the exact EGL display/config/context used by `GameSurface`;
- a local reference space;
- one recommended-resolution GLES swapchain per primary-stereo view;
- event-driven session begin/end and instance-loss handling;
- `xrWaitFrame`, `xrBeginFrame`, `xrLocateViews`, image acquire/wait/release, and `xrEndFrame`;
- orientation and center-eye position publication using predicted display time;
- recenter propagation on OpenXR reference-space changes;
- explicit recenter from the in-game drawer or a paired keyboard's F12 key;
- Touch controller action sync, with both thumbsticks clicked together as an in-headset recenter;
- clean resource destruction before the EGL context is destroyed.

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
   headset rotation, eye offset, optional limited translation, and then reapplies the N64 projection.
5. Rectangle draws are duplicated without the world transform, keeping common HUD/2D elements at
   zero disparity for the initial build.

This shares display-list interpretation, texture decoding/uploads, lighting work already performed
on the CPU, and vertex-buffer uploads. View-dependent rasterization is duplicated. CPU X/Y clipping
is relaxed in VR mode so geometry needed by an off-axis eye is less likely to be discarded before
the GPU sees it.

The current method assumes the projection active at draw time matches the vertices in the batch.
That is generally true for conventional GLideN64 batches and is a practical first target for Mario
Kart 64, but titles that mix projection matrices inside one batch may need projection IDs or retained
untransformed vertices.

## Configuration

Developer settings are stored in Android shared preferences named `quest_vr`. Defaults are:

| Key | Default | Current use |
| --- | ---: | --- |
| `enabled` | `true` | Enables OpenXR on devices advertising VR head tracking |
| `stereo_enabled` | `true` | Enables GLideN64 side-by-side geometry |
| `ipd_meters` | `0.064` | Eye separation |
| `world_units_per_meter` | `64.0` | Maps tracked metres to N64 view units |
| `rotation_strength` | `1.0` | Scales recentered headset rotation |
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
| `touch_controller_enabled` | `true` | Merges OpenXR Touch input into N64 player one |
| `debug_logging` | `false` | Periodically logs pose and stereo draw counters |

Positional tracking is intentionally off by default for the first Quest build. Orientation and eye
separation are active. The renderer automatically captures the first valid headset pose as its
recenter origin.

## Build and CI

Local command:

```sh
./gradlew assemble
```

GitHub Actions workflow: `.github/workflows/build.yml`

Expected release APK: `app/build/outputs/apk/release/Mupen64PlusAE-release.apk`

The workflow uploads that APK as its build artifact. The application already packages `arm64-v8a`;
the OpenXR module is intentionally restricted to `arm64-v8a` for Quest.

## Changed areas

- `quest-vr/`: OpenXR loader dependency, JNI bridge, swapchains, pose source, and presentation.
- `app/src/main/AndroidManifest.xml`: optional head-tracking feature, OpenXR discovery, and immersive
  HMD category.
- `GameSurface.java`: GLES 3/OpenXR selection, VR frame pacing, fallback, and lifecycle cleanup.
- `ShaderDrawer.java`: separate external-texture latching from normal Android shader presentation.
- `mupen64plus-video-gliden64/upstream/src/QuestVr.*`: explicit VR state/configuration and C bridge.
- `mupen64plus-input-android/src/plugin.cpp`: thread-safe optional Touch overlay for player one.
- GLideN64 OpenGL drawers: two-eye viewport replay without duplicate vertex uploads.
- GLideN64 generated triangle shaders: eye/head clip transforms.
- `gSP.cpp`: stereo-aware clipping retention.

## Known limitations and risks

- No on-device validation yet; axis signs, eye order, and world scale may require immediate tuning.
- OpenXR per-eye FOV is not yet used to replace the game's projection. Both eyes retain the active
  N64 projection and differ through eye/view transforms.
- Game-side culling can still omit scenery exposed by large head turns or leaning.
- Full-screen framebuffer texture effects may sample the complete side-by-side buffer and need
  eye-aware UV cropping.
- Billboards still face the original game camera.
- Sky/background passes are not yet classified separately from world geometry.
- Rectangle HUD elements have zero disparity but are not yet submitted as an OpenXR quad layer.
- The original Android game sidebar is not yet reproduced in an immersive layer.
- OpenXR swapchain rendering is single-sample, and no foveation extension is enabled.
- A 30 FPS N64 title is latched into a higher-rate OpenXR loop; the runtime supplies head timewarp,
  but game-camera updates still occur at emulation cadence.
- Touch input uses the core Oculus Touch interaction profile, which Quest Touch Plus can expose
  through runtime compatibility. The current mapping is left stick=N64 stick, A/B=N64 A/B,
  either index trigger=Z, left/right squeeze=L/R, right stick=C buttons, right stick click=Start,
  X/Y=D-pad left/up, and both stick clicks=recenter. Paired Android controllers remain usable and
  their buttons are merged; an active Touch left stick takes analog priority.

## First device test checklist

1. Select GLideN64 and launch Mario Kart 64.
2. Confirm OpenXR runtime/session/swapchain messages in logcat.
3. Verify each eye receives the correct half (no inverted stereo).
4. Confirm the course geometry has parallax while HUD rectangles remain zero disparity.
5. Check yaw, pitch, and roll direction from a stationary kart.
6. Verify Touch A/B, analog steering, Z/R, C buttons, Start, and the two-stick-click recenter chord.
7. Tune `world_units_per_meter`, IPD, and camera Z offset before enabling positional tracking.
8. Record emulator FPS, OpenXR cadence, thermals, framebuffer-effect defects, disappearing geometry,
   and any native crash backtrace.

## Next implementation steps

1. Fix all CI compiler/linker findings and retain a downloadable APK at every checkpoint.
2. Add eye-aware sampling for framebuffer-backed texture rectangles and final post-processing passes.
3. Pass OpenXR eye FOV into GLideN64 and build asymmetric per-eye projection corrections.
4. Add a Mario Kart 64 profile to identify the main world projection and tune a close chase/driver
   camera without applying head transforms to sky/HUD passes.
5. Add a visible immersive debug overlay with pose, eye matrices, draw counts, and frame timings.
6. Validate and tune the OpenXR Touch mapping on Quest 3, then add input haptics and an immersive
   settings/recenter panel.
7. Enable comfort-limited position tracking, collect culling failures, and refine clipping/submission
   retention around the original game camera.
