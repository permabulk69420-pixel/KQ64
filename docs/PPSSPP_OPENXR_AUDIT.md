# PPSSPP/OpenXR divergence audit

## Quest 3S hardware follow-up: immersive/cinema split

`quest-vr-diagnostic-latest.log` at branch commit
`3ec5223f063bd96250faffdcfb8b40eb386ebbc1` is a real Quest 3S run of
`32fe4ae98d4a813debfedb648aecc5038ea011cb`. It confirms that the fragile parts of the bridge are
not the current blocker: the session reached `FOCUSED`, 6,000 frames reached successful
`xrEndFrame`, the external texture kept receiving new frames, 2,240 source poses matched with only
two misses, runtime IPD was about 63 mm, and Touch input remained usable.

The same report also proves two presentation defects:

- Every normal game frame was submitted as two `LOCAL`-space quads. No user setting selected a
  native projection layer.
- The selected 2880x2160 render preset and legacy multiplier 1.0 became a 1920x1440 complete SBS
  source. Each eye therefore received only 960x1440 and was stretched back to a 4:3 screen.

This iteration leaves the proven OpenXR/EGL/SurfaceTexture/input lifecycle intact and introduces
two explicit native modes:

- **Immersive projection** submits one `XrCompositionLayerProjection` containing two projection
  views. Each view uses the pose and FOV recorded for the latched GLideN64 source frame whenever
  the exact pose-history match is available. Using that old pose as composition metadata is not a
  second game-camera rotation: it tells the compositor which view generated the texture so normal
  reprojection can move it from the source pose to display time. Current/fallback views are used
  only when a source match is unavailable.
- **World-locked cinema screen** preserves the known-working paired `LEFT`/`RIGHT`
  `XrCompositionLayerQuad` layers in `LOCAL` space. Screen size, distance, and recenter behavior
  remain cinema-only controls.

The immersive source is aspect-fitted into each near-square Quest eye image, then multiplied by a
clamped 0.5-1.0 immersive scale. Default 1.0 shows the complete 4:3 eye source without the previous
stretch/crop. Startup green and red/blue proofs remain debug-only.

### Corrected stereo-resolution semantics

The selected flat render size is now treated as desired **per-eye** content. The versioned source
multiplier defaults to 2.0 and scales both axes together:

`requested total SBS = (selected width × multiplier, selected height × multiplier / 2)`

At 2.0 this requests two full selected-width eyes and one selected-height row. Proportional safety
limits remain 2688 total pixels wide, 1440 high, and 3,000,000 pixels. Thus 2880x2160 at 2.0 safely
resolves to about 2688x1008 total, or 1344x1008 per eye, preserving 4:3. At the current width cap,
1440x1080 and larger 4:3 presets converge on that same plateau; the persistent report explicitly
labels width/height/pixel limiting and logs selected, requested total/per-eye, and effective
total/per-eye sizes.

### GLideN64 tracking-path audit

World-locked screen tracking and in-game camera tracking remain separate. The late GLideN64 path
now reports enough classifications to test the mixed Mario Kart result:

| Renderer path | Stereo behavior | Head transform behavior |
| --- | --- | --- |
| Perspective triangles/lines | Drawn once per eye | Eligible for OpenXR FOV, eye offset, and recentered head transform |
| Orthographic triangle batches | Drawn once per eye | Deliberately not transformed |
| Texture rectangles and background rectangles | Drawn once per eye | Stable screen-space path; not transformed |
| Sprite2D commands | Generally become texture rectangles | Stable screen-space path; not transformed |
| 3D billboards | Remain triangle geometry | Transform with their world pass, but still face the original game camera |
| Framebuffer copies/blits and final post-processing | Preserve left/right packed halves | Copy already-rendered pixels; cannot add missing camera motion |
| Vertices with `MODIFY_XY` | Stereo draw still occurs | Shader deliberately skips the late clip transform for those premodified positions |

The spinning Nintendo logo is therefore a strong result because it is a clean perspective mesh.
Mario Kart track, vehicles, and scenery should also count as perspective batches when they use the
normal 3D path, while menus/HUD/background rectangles should remain stable. The new counters split
perspective, orthographic, rectangles, background rectangles, Sprite2D commands, blits, missing
transform programs, total geometry vertices, and `MODIFY_XY` vertices. They will show whether
apparently flat race content is truly bypassing
the transform or simply has subtle motion. The transform still occurs after the N64 camera,
projection, and most CPU-side culling; this change does not claim to be a universal N64 camera
rewrite.

Finally, Quest hardware established that user value +1.0 produced the direction previously reached
with -1.0. The sign is now converted at the late clip-transform boundary. User-facing +1.0 follows
the headset, while negative values remain an intentional inversion.

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
| Reference spaces | Creates VIEW and stage/fake-stage choices according to mode. | Creates LOCAL for projection/cinema and VIEW for head-locked fallback/proofs. | Harmless. Cinema anchors in LOCAL; immersive projection submits source/current LOCAL poses. |
| Swapchains | Direct GLES render targets, one framebuffer path per eye/mode. | One recommended-size, single-sample GLES swapchain per eye. | Equivalent for the milestone. Formats, handles, sizes, image counts, and view recommendations are now recorded. |
| Frame pacing | PPSSPP performs `xrWaitFrame`, `xrLocateViews`, then `xrBeginFrame`. | Performs `xrWaitFrame`, `xrBeginFrame`, then `xrLocateViews`. | Harmless: locating views is not required to be inside the begun-frame interval. N64 keeps its order and logs each milestone. |
| Acquire/wait/release | Acquires, waits, renders directly, and releases before composition. | Same order, but blits an external-OES texture into the acquired image. | Core order is correct. N64 now bounds logging, validates image indices, and does not release an image after a failed wait. |
| Poses/FOV | Projection mode uses located poses and FOV; flat modes commonly use a cylinder layer. | Projection uses located poses/FOV, with conservative VIEW-space fallback values when tracking validity is absent. | Both are valid. Exact submitted poses/FOV/space are now recorded during startup samples. |
| Image rectangles | Uses valid swapchain extents, array index 0; SBS modes adjust rectangles. | Uses the full per-eye extent and array index 0. | Equivalent for separate per-eye swapchains. All fields are logged. |
| Composition layers | Builds projection/cylinder layers from images PPSSPP rendered directly. | Consumes the external texture into two eye swapchains. | N64 now explicitly selects a source-pose-matched projection layer for immersive mode or paired eye-visible LOCAL quads for cinema. VIEW-space green and red/blue proofs are available only through a debug preference. |
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

Quest test for this unverified presentation iteration:

1. Install over the existing APK so the ROM/profile remains available. In **Gallery drawer →
   Display → Quest VR prototype**, keep **Enable Quest VR**, stereo, Touch, OpenXR FOV, and periodic
   diagnostics enabled. Keep **Show startup proof layers** off and set head rotation to **+1.0**.
2. Select **Immersive projection**, immersive scale **1.0**, and source multiplier **2.0**. Choose
   the 960x720 preset first. Launch Mario Kart 64 and wear the headset for **45 seconds**: include
   the Nintendo logo/menu flag, then at least 20 seconds in a race. Confirm no green/red-blue
   startup, correct yaw/pitch direction, stereo on the logo, useful race tracking, and all Touch
   controls. Recenter once with the existing action/chord.
3. Exit normally and export the diagnostic immediately. It should report
   `presentation=IMMERSIVE_PROJECTION`, one submitted layer containing two projection views,
   `poseSource=MATCHED_SOURCE_FRAME` after startup, 1920x720 total / 960x720 per eye at the 960x720
   preset, and the new draw classifications.
4. Select **World-locked cinema screen**, relaunch for **30 seconds**, and confirm the known cinema
   behavior remains: the screen stays fixed in LOCAL space, screen scale/distance work, recenter
   places it in front, stereo remains visible, and Touch is unchanged. Export this second log if
   cinema regresses.
5. Return to immersive mode and test 1440x1080, 1920x1440, and 2880x2160 for **20 seconds each**.
   The first high preset should reach the safe plateau and every larger 4:3 preset should clearly
   log the same approximate 2688x1008 total / 1344x1008 per-eye result instead of hanging at a
   diagnostic color. Export the final high-preset log.

Do not infer success from audio or compilation. For any missing layer, check `xrBeginSession`,
`VISIBLE`/`FOCUSED`, successful `xrEndFrame`, selected presentation mode, projection/quad layer
details, unchanged EGL display/context, selected/requested/effective total and per-eye resolution,
`actualProducer`, SurfaceTexture callbacks/latches, pose matches, and `newSourceFrames` before
changing lifecycle code.
