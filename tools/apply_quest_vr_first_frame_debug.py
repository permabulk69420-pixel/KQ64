#!/usr/bin/env python3
"""Apply a CI-only Quest OpenXR first-frame diagnostic patch.

The debug APK skips Touch action registration and controller syncing, ignores the
emulator texture, and clears the left eye red / right eye blue. The production
quest-vr-prototype branch remains untouched.
"""

from pathlib import Path

source_path = Path("quest-vr/src/main/cpp/QuestVrBridge.cpp")
text = source_path.read_text(encoding="utf-8")

replacements = {
'''    if (!createControllerActions()) {
        LOGW("OpenXR Touch action setup failed; paired Android controllers remain available");
    }
''': '''    LOGW("FIRST_FRAME_DEBUG: skipping all OpenXR Touch action registration");
''',
'''    syncControllerInput();
''': '''    // FIRST_FRAME_DEBUG: controller syncing intentionally disabled.
''',
'''    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (g.sourceTexture == 0) {
        return true;
    }
''': '''    if (eye == 0) {
        glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
    } else {
        glClearColor(0.0f, 0.0f, 1.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);
    glFinish();
    LOGI("FIRST_FRAME_DEBUG: cleared eye %u image %u to %s", eye, imageIndex,
         eye == 0 ? "RED" : "BLUE");
    return true;
''',
'''    pollEvents();
    if (!g.sessionRunning) {
        return JNI_FALSE;
    }
''': '''    static uint32_t debugAttempt = 0;
    ++debugAttempt;
    pollEvents();
    if (debugAttempt <= 120) {
        LOGI("FIRST_FRAME_DEBUG: attempt=%u state=%d running=%d exit=%d",
             debugAttempt, static_cast<int>(g.sessionState),
             g.sessionRunning ? 1 : 0, g.exitRequested ? 1 : 0);
    }
    if (!g.sessionRunning) {
        return JNI_FALSE;
    }
''',
'''    if (!xrOk(xrWaitFrame(g.session, &waitInfo, &frameState), "xrWaitFrame")) {
''': '''    LOGI("FIRST_FRAME_DEBUG: calling xrWaitFrame");
    if (!xrOk(xrWaitFrame(g.session, &waitInfo, &frameState), "xrWaitFrame")) {
''',
'''    if (!xrOk(xrBeginFrame(g.session, &beginInfo), "xrBeginFrame")) {
''': '''    LOGI("FIRST_FRAME_DEBUG: xrWaitFrame returned shouldRender=%d", frameState.shouldRender == XR_TRUE ? 1 : 0);
    if (!xrOk(xrBeginFrame(g.session, &beginInfo), "xrBeginFrame")) {
''',
'''    const bool submitted = xrOk(xrEndFrame(g.session, &endInfo), "xrEndFrame");
''': '''    LOGI("FIRST_FRAME_DEBUG: calling xrEndFrame layerCount=%u", layerCount);
    const bool submitted = xrOk(xrEndFrame(g.session, &endInfo), "xrEndFrame");
    LOGI("FIRST_FRAME_DEBUG: xrEndFrame submitted=%d", submitted ? 1 : 0);
''',
}

for old, new in replacements.items():
    if old not in text:
        raise SystemExit(f"Expected source fragment not found:\n{old}")
    text = text.replace(old, new, 1)

source_path.write_text(text, encoding="utf-8")
print("Applied Quest first-frame red/blue diagnostic patch")
