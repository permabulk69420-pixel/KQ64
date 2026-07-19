# PPSSPP/OpenXR divergence audit

## Quest device follow-up: usable presentation iteration

A Quest hardware run has now confirmed that the audited startup path enters OpenXR, submits the
GLideN64 SurfaceTexture, preserves per-eye stereo, and maps Touch controllers successfully. The
next presentation iteration keeps that proven loader/session/input path and changes only how the
game image is presented:

- Normal launches no longer spend 360 visible frames on the green quad and another 360 on the
  red/blue projection proof. Those phases remain available through **Show startup proof layers**.
- The game is submitted as two coincident `XrCompositionLayerQuad` layers in `LOCAL` space, with
  `LEFT` and `RIGHT` eye visibility respectively. This preserves the existing side-by-side stereo
  source while making the screen world-locked rather than filling each projection eye.
- Default screen width is 2.4 metres at a 2.0 metre anchor distance (about 62 degrees wide). Screen
  size and distance are clamped, cross Java/JNI, and are recorded in the persistent report.
- Recenter now requests both the GLideN64 pose origin and a new LOCAL-space screen anchor in front
  of the current headset pose.
- Producer requests are resolved before core startup and are proportionally capped at 2688 pixels
  wide, 1440 pixels high, and 3,000,000 pixels. The 2688 limit deliberately stays below the
  device-observed failure at the 2880-wide preset. The exact base, requested, clamped, EGL pbuffer,
  SurfaceTexture, and OpenXR consumer sizes are logged.

The source-resolution width multiplier changes the actual GLideN64 render target and
`SurfaceTexture`, not the physical cinema-screen size. A value of 1 is the conservative default;
2 requests the flat renderer's horizontal resolution in each side-by-side eye before the safety
limits are applied.

### Head tracking scope

Screen tracking and in-game tracking are separate:

- The compositor keeps the cinema screen fixed in `LOCAL` space. Looking away therefore moves the
  screen naturally out of view; changing screen distance/scale does not rewrite game geometry.
- Each valid `xrLocateViews` result is also published to GLideN64. For perspective world draws,
  GLideN64 applies per-eye position/FOV and a recentered headset-orientation transform late in its
  vertex path. Orthographic HUD/background draws are intentionally left alone.
- This is real per-eye rotational geometry work, but not a universal N64 camera integration.
  Game-specific camera constraints and CPU-side visibility/culling can still limit useful
  look-around. Positional tracking remains opt-in and bounded. No broad camera or culling rewrite
  is part of this iteration.

## Scope and conclusion

This audit compares Mupen64Plus AE at
`ba0b775d596a0a79f0cf46201b045140229719bc` on `quest-vr-prototype` with PPSSPP at
`35a2bcf3dff34fee1c200afa015a0088221eca70`. PPSSPP was inspected read-only. The relevant PPSSPP
paths were `Common/VR/OpenXRLoader.*`, `VRBase.cpp`, `VRFramebuffer.cpp`, `VRRenderer.cpp`,
`PPSSPPVR.cpp`, and its Android/EGL lifecycle integration.

The N64 implementation already has the same essential OpenXR call sequence as PPSSPP. The audit
did not find a single obviously illegal `xrWaitFrame`/swapchain/`xrEndFrame` ordering error. It did
find four N64-specific dependencies capable of hiding every layer even when emulation continues:

1. The diagnostic clear could not run unless the external-OES shader compiled and linked, even
   though a clear needs only an FBO.
2. The consumer EGL context was moved off the Android window, but the Java config chooser did not
   request `EGL_PBUFFER_BIT`; success therefore depended on an unverified surfaceless-context path.
3. The diagnostic interval counted submitted layers before the session was `VISIBLE`, so it could
   finish while the Quest loading environment still obscured the app.
4. A transient Android `onStop` kept the OpenXR thread but paused the emulator producer, making the
   retained SurfaceTexture bridge unable to prove that it was receiving new frames.

The original audit changes isolated those dependencies with timed green and red/blue proofs. Quest
hardware subsequently confirmed the full path through the emulator texture. Those proofs are now
debug-gated, and normal launches go directly to the world-locked game screen.

## Call-by-call comparison

| Area | PPSSPP reference | N64 before audit | Assessment and audit change |
| --- | --- | --- | --- |
| VM/activity | Passes `ovrJava::Vm` and a retained Activity object to loader initialization; conditionally chains Android instance info. | Uses `GetJavaVM`, a JNI global Activity ref, loader `applicationContext`, and Android instance `applicationActivity`. | Equivalent. Now checks both JNI operations and logs addresses/thread IDs. |
| `xrInitializeLoaderKHR` | Looks up the function with the null instance and calls it when present. | Same behavior. | Harmless. Lookup and call results are now durable; absence remains compatibility-tolerant, matching PPSSPP. |
| Required extensions | GLES plus PPSSPP features such as cylinder; Android instance extension is platform-flag dependent. | Requires `XR_KHR_android_create_instance` and `XR_KHR_opengl_es_enable`. | N64 needs only these core paths. It now enumerates and verifies them before instance creation. |
| `xrCreateInstance` | API 1.0 and Android create-info when enabled. | API 1.0 with Android create-info. | Equivalent. Every returned `XrResult` is recorded. |
| `xrGetSystem` | Requests HMD form factor and later queries system data. | Requests HMD form factor. | Equivalent. N64 now records `xrGetSystemProperties`, limits, and tracking capabilities. |
| GLES requirements | Calls `xrGetOpenGLESGraphicsRequirementsKHR`. | Calls it and validates the current GLES version. | N64 is stricter. Min/max and current GLES are now recorded. |
| EGL binding | Creates the session from PPSSPP's current render context; Android config is null. That context remains on the normal GLSurface path. | Resolves the current display/context/config, presents one Android buffer, then parks the exact context on a pbuffer or surfaceless binding. | Architectural difference with failure potential. N64 now prefers a window+pbuffer config, logs all attributes, and validates the exact display/context before every startup frame. |
| Session timing | `VR_EnterVR` runs after a render context exists. | Session creation runs on `GameSurface.RenderThread` after the consumer EGL context is current and parked. | Valid if the parked binding remains current. The binding is now captured immediately before `xrCreateSession` and checked during frames. |
| Android lifecycle | PPSSPP translates app/surface lifecycle into enter/leave/focus state while its producer and VR renderer share the main graphics path. | OpenXR survives the temporary Android Surface loss on a separate consumer thread. Previously `GameActivity.onStop` still paused the core producer. | Meaningful bridge-specific divergence. Transient hand-off now retains both OpenXR consumer and emulator producer; final exit still uses normal cleanup. |
| Session events | `READY` calls `xrBeginSession`; `STOPPING` calls `xrEndSession`. | Same event-driven policy. | Equivalent. N64 now guards duplicate states, logs event session/time/state, and never calls `xrEndSession` outside `STOPPING`. |
| Reference spaces | Creates VIEW and stage/fake-stage choices according to mode. | Creates LOCAL for tracked projection and VIEW for head-locked fallback. | Harmless. VIEW is also used for the independent green proof layer. |
| Swapchains | Direct GLES render targets, one framebuffer path per eye/mode. | One recommended-size, single-sample GLES swapchain per eye. | Equivalent for the milestone. Formats, handles, sizes, image counts, and view recommendations are now recorded. |
| Frame pacing | PPSSPP performs `xrWaitFrame`, `xrLocateViews`, then `xrBeginFrame`. | Performs `xrWaitFrame`, `xrBeginFrame`, then `xrLocateViews`. | Harmless: locating views is not required to be inside the begun-frame interval. N64 keeps its order and logs each milestone. |
| Acquire/wait/release | Acquires, waits, renders directly, and releases before composition. | Same order, but blits an external-OES texture into the acquired image. | Core order is correct. N64 now bounds logging, validates image indices, and does not release an image after a failed wait. |
| Poses/FOV | Projection mode uses located poses and FOV; flat modes commonly use a cylinder layer. | Projection uses located poses/FOV, with conservative VIEW-space fallback values when tracking validity is absent. | Both are valid. Exact submitted poses/FOV/space are now recorded during startup samples. |
| Image rectangles | Uses valid swapchain extents, array index 0; SBS modes adjust rectangles. | Uses the full per-eye extent and array index 0. | Equivalent for separate per-eye swapchains. All fields are logged. |
| Composition layers | Builds projection/cylinder layers from images PPSSPP rendered directly. | Originally built projection layers only; diagnostics were coupled to the external shader and tracking path. | N64 now uses paired eye-visible LOCAL-space game quads. VIEW-space green and projection red/blue proofs are available only through an explicit debug preference. |
| `xrEndFrame` | Submits pointers to layer storage that remains alive through the call, OPAQUE blend mode. | Local layer/view storage remains alive through the call, OPAQUE hard-coded. | Lifetime is safe in both. N64 enumerates OPAQUE support and logs input layer count/pointer/result. |
| Teardown/re-entry | Ends on STOPPING and destroys session/space objects on leave. | Ends on STOPPING; retains session/context across transient focus/surface changes; destroys resources before EGL on final shutdown. | Valid architecture. Every destroy/exit result is now logged, and final surface loss no longer takes the transient-retain branch. |
| Threads | PPSSPP can register main/renderer thread IDs through optional Android extensions. | All session and frame calls occur on the consumer render thread; Android lifecycle and GLideN64 producer use other threads. | No required extension is missing for the core path. Durable logs identify every Java/native thread so a device run can confirm ownership. |

## Harmless architectural differences

- PPSSPP renders emulator content directly into OpenXR swapchains; the N64 code consumes a second
  EGL producer through `SurfaceTexture` because GLideN64 owns a separate context and native window.
- PPSSPP's flat-screen modes use a cylinder extension. The N64 milestone can use a core quad and
  core projection layer without enabling that extension.
- PPSSPP locates views before `xrBeginFrame`; the N64 implementation locates after it.
- PPSSPP sometimes passes a null EGL config in its Android graphics binding; the N64 code resolves
  the current context's exact config. Both forms are accepted by the reference runtime path.
- The N64 fallback uses VIEW-space synthetic eye poses when LOCAL tracking is not valid. That is a
  diagnostic fallback, not a replacement for tracked rendering.

## Differences that could plausibly produce a permanently invisible layer

### 1. Diagnostic rendering depended on the SurfaceTexture shader

Before this audit, failure to compile `GL_OES_EGL_image_external_essl3`, link the blit program, or
create its VAO aborted all OpenXR initialization. PPSSPP has no equivalent dependency because it
direct-renders into swapchain images. The N64 bridge now treats the FBO clear path as the minimum
OpenXR resource set. External-OES setup failure is recorded and leaves a diagnostic-only session
instead of suppressing all layers.

### 2. EGL parking depended on an accidental config capability

PPSSPP keeps an ordinary current render surface. The N64 bridge deliberately parks its consumer
context because Quest can retire the Activity window during immersive hand-off. The selected config
previously requested only RGBA/renderable attributes, not `EGL_PBUFFER_BIT`. The chooser now first
requests `EGL_WINDOW_BIT | EGL_PBUFFER_BIT`; it uses window-only plus
`EGL_KHR_surfaceless_context` only as a logged fallback. The context captured at session creation is
compared with the context used by every startup frame.

### 3. The proof interval could expire while hidden

The old 180-frame red/blue interval advanced whenever a layer was submitted. A synchronized but not
yet visible session could consume it behind Quest's loading environment. Diagnostic phases now
advance only in `XR_SESSION_STATE_VISIBLE` or `FOCUSED`: 360 visible green quad submissions, then
360 visible red/blue projection submissions. If the external texture is still not demonstrably
fresh, red/blue remains active instead of switching to black.

### 4. Consumer retention without producer retention

The special `QuestVrGameSurface` retained the parked consumer context across a transient surface
loss, but normal `GameActivity.onStop` still paused the emulator. The audit keeps the producer
running only while OpenXR owns presentation and the Activity/runtime have not requested final exit.
Flat mode and final shutdown retain the original behavior.

## SurfaceTexture bridge assessment

The bridge is not inherently incompatible with OpenXR. `SurfaceTexture` is attached to an
external-OES texture in the same consumer EGL context bound to the OpenXR session, and
`updateTexImage` is called on that context's render thread. It is, however, materially more fragile
than PPSSPP's direct-render path: producer swaps, callbacks, latching, shader extension support, and
context retention must all succeed independently.

When **Show startup proof layers** is enabled, the staged layers distinguish those failures:

- Green quad visible: loader, instance, system, session, VIEW space, swapchain, GLES FBO, frame
  pacing, and core composition are working.
- Red left / blue right visible: stereo projection construction, located/fallback poses, FOV,
  per-eye rectangles, and both swapchains are working.
- Emulator image visible: external-OES shader plus GLideN64 producer/callback/latch path is working.

A direct-rendering redesign should be considered only if green/red-blue work but logs repeatedly
show missing producer buffers, latching failures, or an unusable external-OES implementation. That
would require changing GLideN64/EGL ownership and is intentionally not part of this milestone.

## Diagnostics and test contract

The persistent log is reset at each VR game launch, mirrored to logcat, flushed line-by-line, and
contains lifecycle, surface, EGL binding/config, all initialization calls/results, session events,
the first 16 frame submissions, 300-frame status samples, swapchain operations, layer inputs, and
SurfaceTexture producer/latch counters.

Quest test for the current normal presentation:

1. Install the audit APK without uninstalling if existing ROM/profile data should be retained.
2. In **Gallery drawer → Display → Quest VR prototype**, leave **Enable Quest VR** and periodic
   diagnostics enabled, and leave **Show startup proof layers** off.
3. Launch one known-good GLideN64 game and keep the headset on for at least **30 seconds**.
4. Confirm that the game appears directly on a screen fixed in the room, without green or red/blue
   startup phases. Turn your head far enough that the screen moves toward the edge of view; then use
   **Recenter Quest VR view** (or the two-stick-click chord) and confirm that it is placed in front
   again. Also confirm stereo and Touch input remain intact.
5. Exit the game normally. Return to **Gallery drawer → Display → Quest VR prototype → Export latest
   VR diagnostic**, then share/save the `.log` file.

If no layer appears, do not infer failure from audio or compilation. The diagnostic report should
be checked for `xrBeginSession`, transition to `VISIBLE`/`FOCUSED`, a successful `xrEndFrame` with
one monoscopic or two stereo quad layers, and an unchanged EGL display/context. Check the logged
base/requested/effective producer resolution, `Producer EGL context`, `actualProducer`,
`GLideN64 SurfaceTexture callback`, `newLatchedFrames`, and native `newSourceFrames` before changing
OpenXR lifecycle code.
