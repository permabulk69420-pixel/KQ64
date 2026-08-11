#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "QuestVrDiagnostics.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace {

constexpr const char* TAG = "M64P-QuestVR";
// Count only layers submitted while the runtime says the app is VISIBLE or FOCUSED. This keeps the
// diagnostic phases from expiring behind Quest's loading environment.
constexpr uint32_t STARTUP_QUAD_VISIBLE_LAYER_COUNT = 360;
constexpr uint32_t STARTUP_PROJECTION_VISIBLE_LAYER_COUNT = 360;
constexpr uint32_t STARTUP_DETAILED_FRAME_COUNT = 16;
constexpr uint32_t PERIODIC_DETAILED_FRAME_INTERVAL = 300;

#define LOGI(...) QuestVrDiagnostics::log(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) QuestVrDiagnostics::log(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) QuestVrDiagnostics::log(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

enum class RenderContent {
    SourceTexture,
    StereoDiagnostic,
    HeadLockedDiagnostic,
};

enum class PresentationMode : int {
    ImmersiveProjection = 0,
    CinemaScreen = 1,
};

const char* presentationModeName(PresentationMode mode) {
    switch (mode) {
        case PresentationMode::ImmersiveProjection: return "IMMERSIVE_PROJECTION";
        case PresentationMode::CinemaScreen: return "CINEMA_SCREEN";
        default: return "UNKNOWN";
    }
}

const char* renderContentName(RenderContent content) {
    switch (content) {
        case RenderContent::SourceTexture: return "SOURCE_TEXTURE";
        case RenderContent::StereoDiagnostic: return "STEREO_DIAGNOSTIC";
        case RenderContent::HeadLockedDiagnostic: return "HEAD_LOCKED_DIAGNOSTIC";
        default: return "UNKNOWN";
    }
}

struct Swapchain {
    XrSwapchain handle{XR_NULL_HANDLE};
    int32_t width{0};
    int32_t height{0};
    std::vector<XrSwapchainImageOpenGLESKHR> images;
};

using SetVrEnabledFn = void (*)(int enabled);
using SetVrPoseFn = void (*)(float qx, float qy, float qz, float qw,
                             float px, float py, float pz, int64_t timestamp);
using SetVrViewsFn = void (*)(float leftPx, float leftPy, float leftPz,
                              float leftAngleLeft, float leftAngleRight,
                              float leftAngleUp, float leftAngleDown,
                              float rightPx, float rightPy, float rightPz,
                              float rightAngleLeft, float rightAngleRight,
                              float rightAngleUp, float rightAngleDown);
using ConfigureVrFn = void (*)(int stereoEnabled, float ipdMeters, float worldUnitsPerMeter,
                               float rotationStrength, int positionEnabled, float maxTranslationMeters,
                               float cameraOffsetX, float cameraOffsetY, float cameraOffsetZ,
                               int useOpenXrFov, int marioKartProfileEnabled,
                               float marioKartCameraOffsetY, float marioKartCameraOffsetZ);
using RecenterVrFn = void (*)();
using RendererDiagnosticSinkFn = void (*)(const char* message);
using SetVrDiagnosticSinkFn = void (*)(RendererDiagnosticSinkFn sink);
using GetVrStatsFn = void (*)(uint32_t* geometryDraws, uint32_t* rectangleDraws,
                              uint32_t* eyeDraws, uint32_t* poseGeneration,
                              uint32_t* targetWidthFallbacks, uint32_t* lastTargetWidth,
                              uint32_t* perspectiveDraws, uint32_t* orthographicDraws,
                              uint32_t* missingTransformProgramDraws,
                              uint32_t* geometryVertices,
                              uint32_t* modifiedPositionVertices,
                              uint32_t* framebufferBlits,
                              uint32_t* backgroundRectangles,
                              uint32_t* sprite2DCommands);
using GetVrPipelineStatsFn = void (*)(uint32_t* canonicalPerspectiveDraws,
                                      uint32_t* foldedPerspectiveDraws,
                                      uint32_t* projectionLoads,
                                      uint32_t* foldedProjectionMultiplies,
                                      uint32_t* framebufferAllocations,
                                      uint32_t* framebufferN64Width,
                                      uint32_t* framebufferN64Height,
                                      uint32_t* framebufferPhysicalWidth,
                                      uint32_t* framebufferPhysicalHeight,
                                      uint32_t* framebufferScaleMilli,
                                      uint32_t* nativeResolutionFactor,
                                      uint32_t* viewportWidth,
                                      uint32_t* viewportHeight,
                                      uint32_t* scissorWidth,
                                      uint32_t* scissorHeight);
using GetVrUiStatsFn = void (*)(
    uint32_t* screenSpaceBatches, uint32_t* screenSpaceVertices,
    uint32_t* leftEyeDraws, uint32_t* rightEyeDraws,
    uint32_t* leftCorrections, uint32_t* rightCorrections,
    uint32_t* missingUniformDraws, uint32_t* lastFramebuffer,
    uint32_t* lastProgram, uint32_t* lastUniformMask,
    int32_t* leftOffsetXMicro, int32_t* leftOffsetYMicro,
    int32_t* rightOffsetXMicro, int32_t* rightOffsetYMicro,
    uint32_t* finalBlits, uint32_t* finalReadFramebuffer,
    uint32_t* finalTexture, uint32_t* finalPhysicalSourceWidth,
    uint32_t* finalLogicalSourceWidth, uint32_t* finalSourceHeight,
    uint32_t* finalPhysicalDestinationWidth,
    uint32_t* finalLogicalDestinationWidth,
    uint32_t* finalDestinationHeight, int32_t* finalSourceX0,
    int32_t* finalSourceX1, int32_t* finalDestinationX0,
    int32_t* finalDestinationX1, uint32_t* finalFilter,
    uint32_t* finalClassification,
    uint32_t* scratchDraws, uint32_t* scratchRectangles,
    uint32_t* scratchFramebuffer, uint32_t* scratchTexture,
    uint32_t* scratchWidth, uint32_t* scratchHeight);
using GetPresentedPoseTimestampFn = int64_t (*)();
using SetVrInputFn = void (*)(int enabled, uint32_t buttonMask, float analogX, float analogY);

struct PoseHistoryEntry {
    XrTime displayTime{0};
    std::array<XrView, 2> views{};
    bool valid{false};
};

struct ControllerActions {
    XrActionSet actionSet{XR_NULL_HANDLE};
    XrAction leftStick{XR_NULL_HANDLE};
    XrAction rightStick{XR_NULL_HANDLE};
    XrAction buttonA{XR_NULL_HANDLE};
    XrAction buttonB{XR_NULL_HANDLE};
    XrAction buttonX{XR_NULL_HANDLE};
    XrAction buttonY{XR_NULL_HANDLE};
    XrAction leftTrigger{XR_NULL_HANDLE};
    XrAction rightTrigger{XR_NULL_HANDLE};
    XrAction leftSqueeze{XR_NULL_HANDLE};
    XrAction rightSqueeze{XR_NULL_HANDLE};
    XrAction leftStickClick{XR_NULL_HANDLE};
    XrAction rightStickClick{XR_NULL_HANDLE};
};

struct State {
    JavaVM* vm{nullptr};
    jobject activity{nullptr};

    XrInstance instance{XR_NULL_HANDLE};
    XrSystemId systemId{XR_NULL_SYSTEM_ID};
    XrSession session{XR_NULL_HANDLE};
    XrSpace localSpace{XR_NULL_HANDLE};
    XrSpace viewSpace{XR_NULL_HANDLE};
    XrSessionState sessionState{XR_SESSION_STATE_UNKNOWN};
    bool sessionRunning{false};
    bool initialized{false};
    bool exitRequested{false};

    std::vector<XrViewConfigurationView> configViews;
    std::vector<XrView> views;
    std::vector<Swapchain> swapchains;
    int64_t colorFormat{GL_RGBA8};
    XrEnvironmentBlendMode environmentBlendMode{XR_ENVIRONMENT_BLEND_MODE_OPAQUE};

    EGLDisplay graphicsDisplay{EGL_NO_DISPLAY};
    EGLConfig graphicsConfig{nullptr};
    EGLContext graphicsContext{EGL_NO_CONTEXT};

    GLuint framebuffer{0};
    GLuint program{0};
    GLuint vertexArray{0};
    GLint sourceUniform{-1};
    GLint uvTransformUniform{-1};
    GLint quadScaleUniform{-1};
    GLint surfaceTransformUniform{-1};
    GLint calibrationModeUniform{-1};
    GLint calibrationEyeUniform{-1};
    GLint targetAspectUniform{-1};
    bool sourceBlitReady{false};

    GLuint sourceTexture{0};
    int sourceWidth{0};
    int sourceHeight{0};
    float sourceContentAspect{4.0f / 3.0f};
    bool stereoRequested{false};
    bool stereoSourceActive{false};
    bool immersiveScaleLogged{false};
    std::array<float, 16> sourceTransform{{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
    uint32_t sourceTransformUpdates{0};
    uint32_t sourceTransformChanges{0};

    SetVrEnabledFn setVrEnabled{nullptr};
    SetVrPoseFn setVrPose{nullptr};
    SetVrViewsFn setVrViews{nullptr};
    ConfigureVrFn configureVr{nullptr};
    RecenterVrFn recenterVr{nullptr};
    SetVrDiagnosticSinkFn setVrDiagnosticSink{nullptr};
    GetVrStatsFn getVrStats{nullptr};
    GetVrPipelineStatsFn getVrPipelineStats{nullptr};
    GetVrUiStatsFn getVrUiStats{nullptr};
    GetPresentedPoseTimestampFn getPresentedPoseTimestamp{nullptr};
    SetVrInputFn setVrInput{nullptr};
    void* rendererLibraryHandle{nullptr};
    void* inputLibraryHandle{nullptr};
    bool rendererBridgeLogged{false};
    bool inputBridgeLogged{false};

    ControllerActions controllerActions;
    bool recenterChordDown{false};
    uint32_t menuInputMask{0};

    bool configurationStereoEnabled{true};
    bool configurationSwapEyes{false};
    float configurationIpdMeters{0.064f};
    PresentationMode configurationPresentationMode{PresentationMode::ImmersiveProjection};
    float configurationImmersiveViewScale{1.0f};
    float configurationScreenScale{1.0f};
    float configurationScreenDistanceMeters{2.0f};
    bool configurationStartupProofLayers{false};
    float configurationWorldUnitsPerMeter{64.0f};
    float configurationRotationStrength{1.0f};
    bool configurationPositionEnabled{false};
    float configurationMaxTranslationMeters{0.15f};
    float configurationCameraOffsetX{0.0f};
    float configurationCameraOffsetY{0.0f};
    float configurationCameraOffsetZ{0.0f};
    bool configurationUseOpenXrFov{true};
    bool configurationMarioKartProfileEnabled{true};
    float configurationMarioKartCameraOffsetY{0.0f};
    float configurationMarioKartCameraOffsetZ{0.0f};
    bool touchControllerEnabled{true};
    bool debugLogging{false};
    XrPosef screenPose{};
    bool screenPoseValid{false};
    bool screenRecenterPending{true};
    uint32_t screenAnchorCount{0};
    std::array<PoseHistoryEntry, 256> poseHistory{};
    size_t poseHistoryWriteIndex{0};
    std::array<XrView, 2> sourceViews{};
    bool sourceViewsValid{false};
    XrTime sourceViewDisplayTime{0};
    int64_t sourceTextureTimestamp{0};
    uint32_t sourcePoseMatches{0};
    uint32_t sourcePoseMisses{0};
    uint32_t posePublicationCount{0};
    uint32_t submittedFrameCount{0};
    uint32_t submittedLayerCount{0};
    uint32_t visibleLayerSubmissionCount{0};
    uint32_t frameLoopCallCount{0};
    uint32_t begunFrameCount{0};
    uint32_t sourceFrameLatchCalls{0};
    uint32_t newSourceFrameCount{0};
    uint32_t sessionWaitPollCount{0};
    uint32_t locateFallbackCount{0};
    uint32_t shouldRenderFalseCount{0};
    uint32_t timingSampleCount{0};
    uint32_t renderedLayerFrameCount{0};
    double waitFrameMilliseconds{0.0};
    double nativeFrameMilliseconds{0.0};
    double workMilliseconds{0.0};
    double maximumWorkMilliseconds{0.0};
};

State g;

// The cinema screen is a panel fixed in the room, so the head must not also drive the game
// camera behind it. Doing both pans the world while the panel stays put, and turning to look
// at the edge of the screen swings the scene away at the same time. Immersive projection is
// the mode where the head is the camera. Per-eye offsets are untouched, so stereo depth on
// the cinema screen still works.
float effectiveRotationStrength() {
    return g.configurationPresentationMode == PresentationMode::CinemaScreen
        ? 0.0f : g.configurationRotationStrength;
}

bool effectivePositionEnabled() {
    return g.configurationPresentationMode == PresentationMode::CinemaScreen
        ? false : g.configurationPositionEnabled;
}

// Replacing the game's angular terms with the headset FOV only makes sense when the headset is
// the window onto the world. On the cinema screen the result is painted on a flat panel, so the
// wider FOV asks for geometry outside the frustum the game submitted: the scene is cut off with
// a hard vertical edge exactly where the original frustum ended, and world draws no longer agree
// with screen-space elements that never went through the replacement. Let the game keep its own
// projection there.
bool effectiveUseOpenXrFov() {
    return g.configurationPresentationMode == PresentationMode::CinemaScreen
        ? false : g.configurationUseOpenXrFov;
}

const char* xrResultName(XrResult result) {
    static thread_local char buffer[XR_MAX_RESULT_STRING_SIZE];
    if (g.instance != XR_NULL_HANDLE && XR_SUCCEEDED(xrResultToString(g.instance, result, buffer))) {
        return buffer;
    }
    std::snprintf(buffer, sizeof(buffer), "XrResult(%d)", result);
    return buffer;
}

bool xrOk(XrResult result, const char* operation) {
    if (XR_FAILED(result)) {
        LOGE("%s -> %d (%s)", operation, result, xrResultName(result));
        return false;
    }
    LOGI("%s -> %d (%s)", operation, result, xrResultName(result));
    return true;
}

const char* sessionStateName(XrSessionState state) {
    switch (state) {
        case XR_SESSION_STATE_UNKNOWN: return "UNKNOWN";
        case XR_SESSION_STATE_IDLE: return "IDLE";
        case XR_SESSION_STATE_READY: return "READY";
        case XR_SESSION_STATE_SYNCHRONIZED: return "SYNCHRONIZED";
        case XR_SESSION_STATE_VISIBLE: return "VISIBLE";
        case XR_SESSION_STATE_FOCUSED: return "FOCUSED";
        case XR_SESSION_STATE_STOPPING: return "STOPPING";
        case XR_SESSION_STATE_LOSS_PENDING: return "LOSS_PENDING";
        case XR_SESSION_STATE_EXITING: return "EXITING";
        default: return "INVALID";
    }
}

bool isSessionVisible() {
    return g.sessionState == XR_SESSION_STATE_VISIBLE ||
           g.sessionState == XR_SESSION_STATE_FOCUSED;
}

bool shouldLogDetailedFrame() {
    return g.begunFrameCount <= STARTUP_DETAILED_FRAME_COUNT ||
           (g.begunFrameCount != 0 &&
            g.begunFrameCount % PERIODIC_DETAILED_FRAME_INTERVAL == 0);
}

bool xrFrameOk(XrResult result, const char* operation) {
    if (XR_FAILED(result)) {
        LOGE("frame=%u %s -> %d (%s)", g.begunFrameCount, operation, result,
             xrResultName(result));
        return false;
    }
    if (shouldLogDetailedFrame()) {
        LOGI("frame=%u %s -> %d (%s)", g.begunFrameCount, operation, result,
             xrResultName(result));
    }
    return true;
}

void logCurrentEglState(const char* label) {
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    const EGLSurface drawSurface = eglGetCurrentSurface(EGL_DRAW);
    const EGLSurface readSurface = eglGetCurrentSurface(EGL_READ);
    EGLint configId = 0;
    EGLint clientVersion = 0;
    EGLint surfaceType = 0;
    EGLint renderableType = 0;
    EGLint red = 0;
    EGLint green = 0;
    EGLint blue = 0;
    EGLint alpha = 0;
    EGLConfig config = nullptr;

    if (display != EGL_NO_DISPLAY && context != EGL_NO_CONTEXT) {
        eglQueryContext(display, context, EGL_CONFIG_ID, &configId);
        eglQueryContext(display, context, EGL_CONTEXT_CLIENT_VERSION, &clientVersion);
        const EGLint attributes[] = {EGL_CONFIG_ID, configId, EGL_NONE};
        EGLint count = 0;
        if (eglChooseConfig(display, attributes, &config, 1, &count) == EGL_TRUE && count == 1) {
            eglGetConfigAttrib(display, config, EGL_SURFACE_TYPE, &surfaceType);
            eglGetConfigAttrib(display, config, EGL_RENDERABLE_TYPE, &renderableType);
            eglGetConfigAttrib(display, config, EGL_RED_SIZE, &red);
            eglGetConfigAttrib(display, config, EGL_GREEN_SIZE, &green);
            eglGetConfigAttrib(display, config, EGL_BLUE_SIZE, &blue);
            eglGetConfigAttrib(display, config, EGL_ALPHA_SIZE, &alpha);
        }
    }

    const char* glVersion = context == EGL_NO_CONTEXT
            ? nullptr : reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const char* glRenderer = context == EGL_NO_CONTEXT
            ? nullptr : reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    LOGI("EGL[%s] tid=%ld display=%p context=%p draw=%p read=%p config=%p id=%d "
         "client=%d surfaceType=0x%x renderable=0x%x rgba=%d/%d/%d/%d "
         "GL_VERSION=%s GL_RENDERER=%s",
         label, QuestVrDiagnostics::currentThreadId(), display, context, drawSurface, readSurface,
         config, configId, clientVersion, surfaceType, renderableType, red, green, blue, alpha,
         glVersion == nullptr ? "(none)" : glVersion,
         glRenderer == nullptr ? "(none)" : glRenderer);
}

bool validateFrameEglBinding() {
    const EGLDisplay display = eglGetCurrentDisplay();
    const EGLContext context = eglGetCurrentContext();
    if (display != g.graphicsDisplay || context != g.graphicsContext ||
            display == EGL_NO_DISPLAY || context == EGL_NO_CONTEXT) {
        LOGE("OpenXR graphics binding changed: expected display=%p context=%p, current "
             "display=%p context=%p", g.graphicsDisplay, g.graphicsContext, display, context);
        logCurrentEglState("binding-mismatch");
        return false;
    }
    if (shouldLogDetailedFrame()) {
        logCurrentEglState("frame-current");
    }
    return true;
}

bool createAction(XrActionSet actionSet, XrActionType type, const char* name,
                  const char* localizedName, XrAction& action) {
    XrActionCreateInfo info{XR_TYPE_ACTION_CREATE_INFO};
    info.actionType = type;
    std::strncpy(info.actionName, name, XR_MAX_ACTION_NAME_SIZE - 1);
    std::strncpy(info.localizedActionName, localizedName, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1);
    return xrOk(xrCreateAction(actionSet, &info, &action), name);
}

bool createControllerActions() {
    ControllerActions& actions = g.controllerActions;
    XrActionSetCreateInfo setInfo{XR_TYPE_ACTION_SET_CREATE_INFO};
    std::strncpy(setInfo.actionSetName, "n64_controller", XR_MAX_ACTION_SET_NAME_SIZE - 1);
    std::strncpy(setInfo.localizedActionSetName, "N64 Controller",
                 XR_MAX_LOCALIZED_ACTION_SET_NAME_SIZE - 1);
    if (!xrOk(xrCreateActionSet(g.instance, &setInfo, &actions.actionSet),
              "xrCreateActionSet(N64 controller)")) {
        return false;
    }

    const bool created =
        createAction(actions.actionSet, XR_ACTION_TYPE_VECTOR2F_INPUT, "left_stick",
                     "N64 Control Stick", actions.leftStick) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_VECTOR2F_INPUT, "right_stick",
                     "N64 C Buttons", actions.rightStick) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_a",
                     "N64 A Button", actions.buttonA) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_b",
                     "N64 B Button", actions.buttonB) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_x",
                     "N64 D-Pad Left", actions.buttonX) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "button_y",
                     "N64 D-Pad Up", actions.buttonY) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_FLOAT_INPUT, "left_trigger",
                     "N64 Z Trigger Left", actions.leftTrigger) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_FLOAT_INPUT, "right_trigger",
                     "N64 Z Trigger Right", actions.rightTrigger) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_FLOAT_INPUT, "left_squeeze",
                     "N64 L Trigger", actions.leftSqueeze) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_FLOAT_INPUT, "right_squeeze",
                     "N64 R Trigger", actions.rightSqueeze) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "left_stick_click",
                     "VR Recenter Chord", actions.leftStickClick) &&
        createAction(actions.actionSet, XR_ACTION_TYPE_BOOLEAN_INPUT, "right_stick_click",
                     "N64 Start Button", actions.rightStickClick);
    if (!created) {
        xrDestroyActionSet(actions.actionSet);
        actions = {};
        return false;
    }

    struct BindingPath {
        XrAction action;
        const char* path;
    };
    const std::array<BindingPath, 12> bindingPaths{{
        {actions.leftStick, "/user/hand/left/input/thumbstick"},
        {actions.rightStick, "/user/hand/right/input/thumbstick"},
        {actions.buttonA, "/user/hand/right/input/a/click"},
        {actions.buttonB, "/user/hand/right/input/b/click"},
        {actions.buttonX, "/user/hand/left/input/x/click"},
        {actions.buttonY, "/user/hand/left/input/y/click"},
        {actions.leftTrigger, "/user/hand/left/input/trigger/value"},
        {actions.rightTrigger, "/user/hand/right/input/trigger/value"},
        {actions.leftSqueeze, "/user/hand/left/input/squeeze/value"},
        {actions.rightSqueeze, "/user/hand/right/input/squeeze/value"},
        {actions.leftStickClick, "/user/hand/left/input/thumbstick/click"},
        {actions.rightStickClick, "/user/hand/right/input/thumbstick/click"},
    }};
    std::array<XrActionSuggestedBinding, 12> bindings{};
    for (size_t i = 0; i < bindingPaths.size(); ++i) {
        bindings[i].action = bindingPaths[i].action;
        if (!xrOk(xrStringToPath(g.instance, bindingPaths[i].path, &bindings[i].binding),
                  bindingPaths[i].path)) {
            xrDestroyActionSet(actions.actionSet);
            actions = {};
            return false;
        }
    }

    XrPath interactionProfile = XR_NULL_PATH;
    if (!xrOk(xrStringToPath(g.instance, "/interaction_profiles/oculus/touch_controller",
                             &interactionProfile), "Oculus Touch interaction profile")) {
        xrDestroyActionSet(actions.actionSet);
        actions = {};
        return false;
    }
    XrInteractionProfileSuggestedBinding suggested{XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING};
    suggested.interactionProfile = interactionProfile;
    suggested.countSuggestedBindings = static_cast<uint32_t>(bindings.size());
    suggested.suggestedBindings = bindings.data();
    if (!xrOk(xrSuggestInteractionProfileBindings(g.instance, &suggested),
              "xrSuggestInteractionProfileBindings(Oculus Touch)")) {
        xrDestroyActionSet(actions.actionSet);
        actions = {};
        return false;
    }

    XrSessionActionSetsAttachInfo attach{XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO};
    attach.countActionSets = 1;
    attach.actionSets = &actions.actionSet;
    if (!xrOk(xrAttachSessionActionSets(g.session, &attach), "xrAttachSessionActionSets")) {
        xrDestroyActionSet(actions.actionSet);
        actions = {};
        return false;
    }
    LOGI("OpenXR Touch actions attached for N64 controller fallback");
    return true;
}

void setIdentity(XrPosef& pose) {
    pose = {};
    pose.orientation.w = 1.0f;
}

XrQuaternionf normalizeQuaternion(XrQuaternionf quaternion) {
    const float lengthSquared = quaternion.x * quaternion.x +
        quaternion.y * quaternion.y + quaternion.z * quaternion.z +
        quaternion.w * quaternion.w;
    if (!std::isfinite(lengthSquared) || lengthSquared < 1.0e-12f) {
        return {0.0f, 0.0f, 0.0f, 1.0f};
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    quaternion.x *= inverseLength;
    quaternion.y *= inverseLength;
    quaternion.z *= inverseLength;
    quaternion.w *= inverseLength;
    return quaternion;
}

// Rotation about the up axis only. A cinema screen hangs in the room, so it should be upright
// and level however the player's head happened to be tilted when it was anchored: keeping pitch
// leans the panel and plants it above or below eye height, and keeping roll tips the picture in
// the frame. OpenXR is Y-up.
XrQuaternionf yawOnlyQuaternion(const XrQuaternionf& rawQuaternion) {
    const XrQuaternionf q = normalizeQuaternion(rawQuaternion);
    const float yaw = std::atan2(2.0f * (q.w * q.y + q.x * q.z),
        1.0f - 2.0f * (q.y * q.y + q.z * q.z));
    const float half = yaw * 0.5f;
    return {0.0f, std::sin(half), 0.0f, std::cos(half)};
}

XrVector3f rotateVector(const XrQuaternionf& rawQuaternion, const XrVector3f& vector) {
    const XrQuaternionf quaternion = normalizeQuaternion(rawQuaternion);
    const XrVector3f uv{
        quaternion.y * vector.z - quaternion.z * vector.y,
        quaternion.z * vector.x - quaternion.x * vector.z,
        quaternion.x * vector.y - quaternion.y * vector.x,
    };
    const XrVector3f uuv{
        quaternion.y * uv.z - quaternion.z * uv.y,
        quaternion.z * uv.x - quaternion.x * uv.z,
        quaternion.x * uv.y - quaternion.y * uv.x,
    };
    return {
        vector.x + 2.0f * (quaternion.w * uv.x + uuv.x),
        vector.y + 2.0f * (quaternion.w * uv.y + uuv.y),
        vector.z + 2.0f * (quaternion.w * uv.z + uuv.z),
    };
}

void anchorScreenToCurrentView(uint32_t viewCount) {
    if (viewCount == 0 || g.views.empty()) {
        return;
    }
    const XrPosef& leftPose = g.views[0].pose;
    XrVector3f center = leftPose.position;
    if (viewCount > 1 && g.views.size() > 1) {
        center.x = (leftPose.position.x + g.views[1].pose.position.x) * 0.5f;
        center.y = (leftPose.position.y + g.views[1].pose.position.y) * 0.5f;
        center.z = (leftPose.position.z + g.views[1].pose.position.z) * 0.5f;
    }
    // Yaw only, so the panel hangs upright and level and the offset that places it stays
    // horizontal: the screen lands directly ahead at the height of the head rather than
    // wherever the head happened to be pointing.
    const XrQuaternionf orientation = yawOnlyQuaternion(leftPose.orientation);
    const XrVector3f offset = rotateVector(orientation,
        {0.0f, 0.0f, -g.configurationScreenDistanceMeters});
    g.screenPose.orientation = orientation;
    g.screenPose.position = {
        center.x + offset.x,
        center.y + offset.y,
        center.z + offset.z,
    };
    g.screenPoseValid = true;
    g.screenRecenterPending = false;
    ++g.screenAnchorCount;
    LOGI("World-locked screen anchored count=%u distance=%.3fm pose=[q %.4f %.4f "
         "%.4f %.4f, p %.4f %.4f %.4f] space=LOCAL",
         g.screenAnchorCount, g.configurationScreenDistanceMeters,
         g.screenPose.orientation.x, g.screenPose.orientation.y,
         g.screenPose.orientation.z, g.screenPose.orientation.w,
         g.screenPose.position.x, g.screenPose.position.y, g.screenPose.position.z);
}

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        std::array<char, 4096> log{};
        GLsizei length = 0;
        glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &length, log.data());
        LOGE("OpenXR blit shader compilation failed: %.*s", length, log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool createGlResources() {
    while (glGetError() != GL_NO_ERROR) {
        // Start the diagnostic-resource check from a clean GL error state.
    }
    glGenFramebuffers(1, &g.framebuffer);
    const GLenum framebufferObjectError = glGetError();
    if (g.framebuffer == 0 || framebufferObjectError != GL_NO_ERROR) {
        LOGE("Unable to create the OpenXR diagnostic framebuffer object: id=%u error=0x%x",
             g.framebuffer, framebufferObjectError);
        return false;
    }
    LOGI("Created OpenXR diagnostic framebuffer object %u", g.framebuffer);

    static constexpr const char* vertexSource = R"glsl(#version 300 es
precision highp float;
out vec2 vUv;
out vec2 vPatternUv;
uniform vec4 uUvTransform;
uniform vec2 uQuadScale;
uniform mat4 uSurfaceTransform;
const vec2 positions[6] = vec2[6](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0,  1.0),
    vec2(-1.0,  1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0));
const vec2 texcoords[6] = vec2[6](
    vec2(0.0, 0.0), vec2(1.0, 0.0), vec2(0.0, 1.0),
    vec2(0.0, 1.0), vec2(1.0, 0.0), vec2(1.0, 1.0));
void main() {
    gl_Position = vec4(positions[gl_VertexID] * uQuadScale, 0.0, 1.0);
    vec2 sourceUv = texcoords[gl_VertexID] * uUvTransform.xy + uUvTransform.zw;
    vPatternUv = texcoords[gl_VertexID];
    vUv = (uSurfaceTransform * vec4(sourceUv, 0.0, 1.0)).xy;
}
)glsl";

    static constexpr const char* fragmentSource = R"glsl(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
in vec2 vUv;
in vec2 vPatternUv;
layout(location = 0) out vec4 outColor;
uniform samplerExternalOES uSource;
uniform lowp int uCalibrationMode;
uniform lowp int uCalibrationEye;
uniform highp float uTargetAspect;
float segment(vec2 point, vec2 start, vec2 end, float width) {
    vec2 line = end - start;
    float along = clamp(dot(point - start, line) / dot(line, line), 0.0, 1.0);
    float distanceToLine = length(point - (start + line * along));
    return 1.0 - smoothstep(width, width * 1.6, distanceToLine);
}
float boxDistance(vec2 point, vec2 halfSize) {
    vec2 delta = abs(point) - halfSize;
    return length(max(delta, 0.0)) + min(max(delta.x, delta.y), 0.0);
}
void main() {
    if (uCalibrationMode != 0) {
        vec2 uv = vPatternUv;
        vec2 point = vec2((uv.x - 0.5) * uTargetAspect, uv.y - 0.5);
        vec3 color = uCalibrationEye == 0
            ? vec3(0.18, 0.015, 0.015) : vec3(0.015, 0.025, 0.18);
        float border = max(max(1.0 - smoothstep(0.008, 0.014, uv.x),
                               1.0 - smoothstep(0.008, 0.014, 1.0 - uv.x)),
                           max(1.0 - smoothstep(0.008, 0.014, uv.y),
                               1.0 - smoothstep(0.008, 0.014, 1.0 - uv.y)));
        float centers = max(1.0 - smoothstep(0.003, 0.007, abs(uv.x - 0.5)),
                            1.0 - smoothstep(0.003, 0.007, abs(uv.y - 0.5)));
        float square = 1.0 - smoothstep(0.006, 0.012,
            abs(boxDistance(point - vec2(-0.22, 0.0), vec2(0.12))));
        float circle = 1.0 - smoothstep(0.006, 0.012,
            abs(length(point - vec2(0.22, 0.0)) - 0.12));
        float leftLabel = max(segment(uv, vec2(0.035, 0.56), vec2(0.035, 0.44), 0.006),
                              segment(uv, vec2(0.035, 0.44), vec2(0.085, 0.44), 0.006));
        float rightLabel = segment(uv, vec2(0.915, 0.44), vec2(0.915, 0.56), 0.006);
        rightLabel = max(rightLabel,
            segment(uv, vec2(0.915, 0.56), vec2(0.965, 0.56), 0.006));
        rightLabel = max(rightLabel,
            segment(uv, vec2(0.965, 0.56), vec2(0.965, 0.50), 0.006));
        rightLabel = max(rightLabel,
            segment(uv, vec2(0.915, 0.50), vec2(0.965, 0.50), 0.006));
        rightLabel = max(rightLabel,
            segment(uv, vec2(0.94, 0.50), vec2(0.97, 0.44), 0.006));
        color = mix(color, vec3(1.0), max(max(border, centers), max(square, circle)));
        color = mix(color, vec3(1.0, 0.15, 0.08), leftLabel);
        color = mix(color, vec3(0.10, 0.65, 1.0), rightLabel);
        outColor = vec4(color, 1.0);
        return;
    }
    outColor = texture(uSource, vUv);
}
)glsl";

    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        LOGW("External-OES source blit is unavailable; keeping diagnostic-only OpenXR presentation");
        g.sourceBlitReady = false;
        return true;
    }

    g.program = glCreateProgram();
    glAttachShader(g.program, vertexShader);
    glAttachShader(g.program, fragmentShader);
    glLinkProgram(g.program);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    GLint linked = GL_FALSE;
    glGetProgramiv(g.program, GL_LINK_STATUS, &linked);
    if (linked == GL_FALSE) {
        std::array<char, 4096> log{};
        GLsizei length = 0;
        glGetProgramInfoLog(g.program, static_cast<GLsizei>(log.size()), &length, log.data());
        LOGE("OpenXR blit program link failed: %.*s", length, log.data());
        glDeleteProgram(g.program);
        g.program = 0;
        g.sourceBlitReady = false;
        LOGW("OpenXR session will continue with diagnostic layers so shader failure remains visible");
        return true;
    }

    g.sourceUniform = glGetUniformLocation(g.program, "uSource");
    g.uvTransformUniform = glGetUniformLocation(g.program, "uUvTransform");
    g.quadScaleUniform = glGetUniformLocation(g.program, "uQuadScale");
    g.surfaceTransformUniform = glGetUniformLocation(g.program, "uSurfaceTransform");
    g.calibrationModeUniform = glGetUniformLocation(g.program, "uCalibrationMode");
    g.calibrationEyeUniform = glGetUniformLocation(g.program, "uCalibrationEye");
    g.targetAspectUniform = glGetUniformLocation(g.program, "uTargetAspect");
    glGenVertexArrays(1, &g.vertexArray);
    const GLenum resourceError = glGetError();
    if (g.sourceUniform < 0 || g.uvTransformUniform < 0 || g.quadScaleUniform < 0 ||
            g.surfaceTransformUniform < 0 || g.calibrationModeUniform < 0 ||
            g.calibrationEyeUniform < 0 || g.targetAspectUniform < 0 ||
            g.vertexArray == 0 || resourceError != GL_NO_ERROR) {
        LOGE("External-OES blit resources invalid: source=%d uv=%d scale=%d "
             "surfaceMatrix=%d calibration=[%d,%d,%d] vao=%u error=0x%x", g.sourceUniform,
             g.uvTransformUniform, g.quadScaleUniform, g.surfaceTransformUniform,
             g.calibrationModeUniform, g.calibrationEyeUniform, g.targetAspectUniform,
             g.vertexArray, resourceError);
        if (g.vertexArray != 0) glDeleteVertexArrays(1, &g.vertexArray);
        if (g.program != 0) glDeleteProgram(g.program);
        g.vertexArray = 0;
        g.program = 0;
        g.sourceBlitReady = false;
        return true;
    }
    g.sourceBlitReady = true;
    LOGI("External-OES source blit initialized: program=%u vao=%u", g.program, g.vertexArray);
    return true;
}

void persistRendererDiagnostic(const char* message) {
    if (message != nullptr && message[0] != '\0') {
        LOGI("%s", message);
    }
}

void resolveRendererBridge() {
    if (g.setVrEnabled != nullptr && g.setVrPose != nullptr) {
        return;
    }

    void* videoPlugin = dlopen("libmupen64plus-video-GLideN64.so", RTLD_NOW | RTLD_NOLOAD);
    void* symbolScope = videoPlugin != nullptr ? videoPlugin : RTLD_DEFAULT;
    g.setVrEnabled = reinterpret_cast<SetVrEnabledFn>(dlsym(symbolScope, "M64PQuestVrSetEnabled"));
    g.setVrPose = reinterpret_cast<SetVrPoseFn>(dlsym(symbolScope, "M64PQuestVrSetPose"));
    g.setVrViews = reinterpret_cast<SetVrViewsFn>(dlsym(symbolScope, "M64PQuestVrSetViews"));
    g.configureVr = reinterpret_cast<ConfigureVrFn>(dlsym(symbolScope, "M64PQuestVrConfigure"));
    g.recenterVr = reinterpret_cast<RecenterVrFn>(dlsym(symbolScope, "M64PQuestVrRecenter"));
    g.setVrDiagnosticSink = reinterpret_cast<SetVrDiagnosticSinkFn>(
        dlsym(symbolScope, "M64PQuestVrSetDiagnosticSink"));
    g.getVrStats = reinterpret_cast<GetVrStatsFn>(dlsym(symbolScope, "M64PQuestVrGetStats"));
    g.getVrPipelineStats = reinterpret_cast<GetVrPipelineStatsFn>(
        dlsym(symbolScope, "M64PQuestVrGetPipelineStats"));
    g.getVrUiStats = reinterpret_cast<GetVrUiStatsFn>(
        dlsym(symbolScope, "M64PQuestVrGetUiStats"));
    g.getPresentedPoseTimestamp = reinterpret_cast<GetPresentedPoseTimestampFn>(
        dlsym(symbolScope, "M64PQuestVrGetPresentedPoseTimestamp"));

    const bool found = g.setVrEnabled != nullptr && g.setVrPose != nullptr;
    if (found) {
        // Keep the plugin mapped until destroyState has disabled VR. The core may unload its
        // reference first during activity/surface teardown, which would otherwise leave these
        // function pointers dangling.
        g.rendererLibraryHandle = videoPlugin;
        videoPlugin = nullptr;
        if (g.setVrDiagnosticSink != nullptr) {
            g.setVrDiagnosticSink(&persistRendererDiagnostic);
        }
        if (g.configureVr != nullptr) {
            g.configureVr(g.configurationStereoEnabled ? 1 : 0, g.configurationIpdMeters,
                          g.configurationWorldUnitsPerMeter, effectiveRotationStrength(),
                          effectivePositionEnabled() ? 1 : 0, g.configurationMaxTranslationMeters,
                          g.configurationCameraOffsetX, g.configurationCameraOffsetY,
                          g.configurationCameraOffsetZ, effectiveUseOpenXrFov() ? 1 : 0,
                          g.configurationMarioKartProfileEnabled ? 1 : 0,
                          g.configurationMarioKartCameraOffsetY,
                          g.configurationMarioKartCameraOffsetZ);
        }
        g.setVrEnabled(1);
        g.stereoSourceActive = g.stereoRequested && g.configurationStereoEnabled;
        LOGI("Connected OpenXR pose source to GLideN64 VR renderer bridge");
    } else if (!g.rendererBridgeLogged) {
        LOGI("GLideN64 VR bridge is not loaded yet; presenting monoscopic source until it appears");
        g.rendererBridgeLogged = true;
    }

    if (videoPlugin != nullptr) {
        dlclose(videoPlugin);
    }
}

struct RendererUiStats {
    uint32_t screenSpaceBatches{0};
    uint32_t screenSpaceVertices{0};
    uint32_t leftEyeDraws{0};
    uint32_t rightEyeDraws{0};
    uint32_t leftCorrections{0};
    uint32_t rightCorrections{0};
    uint32_t missingUniformDraws{0};
    uint32_t lastFramebuffer{0};
    uint32_t lastProgram{0};
    uint32_t lastUniformMask{0};
    int32_t leftOffsetXMicro{0};
    int32_t leftOffsetYMicro{0};
    int32_t rightOffsetXMicro{0};
    int32_t rightOffsetYMicro{0};
    uint32_t finalBlits{0};
    uint32_t finalReadFramebuffer{0};
    uint32_t finalTexture{0};
    uint32_t finalPhysicalSourceWidth{0};
    uint32_t finalLogicalSourceWidth{0};
    uint32_t finalSourceHeight{0};
    uint32_t finalPhysicalDestinationWidth{0};
    uint32_t finalLogicalDestinationWidth{0};
    uint32_t finalDestinationHeight{0};
    int32_t finalSourceX0{0};
    int32_t finalSourceX1{0};
    int32_t finalDestinationX0{0};
    int32_t finalDestinationX1{0};
    uint32_t finalFilter{0};
    uint32_t finalClassification{0};
    uint32_t scratchDraws{0};
    uint32_t scratchRectangles{0};
    uint32_t scratchFramebuffer{0};
    uint32_t scratchTexture{0};
    uint32_t scratchWidth{0};
    uint32_t scratchHeight{0};
};

bool queryRendererUiStats(RendererUiStats& stats) {
    resolveRendererBridge();
    if (g.getVrUiStats == nullptr)
        return false;
    g.getVrUiStats(
        &stats.screenSpaceBatches, &stats.screenSpaceVertices,
        &stats.leftEyeDraws, &stats.rightEyeDraws,
        &stats.leftCorrections, &stats.rightCorrections,
        &stats.missingUniformDraws, &stats.lastFramebuffer,
        &stats.lastProgram, &stats.lastUniformMask,
        &stats.leftOffsetXMicro, &stats.leftOffsetYMicro,
        &stats.rightOffsetXMicro, &stats.rightOffsetYMicro,
        &stats.finalBlits, &stats.finalReadFramebuffer,
        &stats.finalTexture, &stats.finalPhysicalSourceWidth,
        &stats.finalLogicalSourceWidth, &stats.finalSourceHeight,
        &stats.finalPhysicalDestinationWidth,
        &stats.finalLogicalDestinationWidth,
        &stats.finalDestinationHeight, &stats.finalSourceX0,
        &stats.finalSourceX1, &stats.finalDestinationX0,
        &stats.finalDestinationX1, &stats.finalFilter,
        &stats.finalClassification,
        &stats.scratchDraws, &stats.scratchRectangles,
        &stats.scratchFramebuffer, &stats.scratchTexture,
        &stats.scratchWidth, &stats.scratchHeight);
    return true;
}

void logRendererUiStats(const char* reason) {
    RendererUiStats stats;
    if (!queryRendererUiStats(stats)) {
        LOGW("UI pipeline snapshot reason=%s unavailable: renderer symbol missing", reason);
        return;
    }
    constexpr uint32_t enabledBit = 1U << 0;
    constexpr uint32_t transformBit = 1U << 1;
    constexpr uint32_t screenTransformBit = 1U << 2;
    constexpr uint32_t rowMask = 0x78U;
    LOGI("UI pipeline snapshot reason=%s screenBatches=%u vertices=%u eyeDraws=[%u,%u] "
         "corrections=[%u,%u] offsetsNdc=[%.6f,%.6f | %.6f,%.6f] "
         "lastFbo=%u program=%u uniformMask=0x%02x "
         "uniforms={enabled:%d transform:%d screenTransform:%d rows:%d} missing=%u "
         "finalBlits=%u readFbo=%u texture=%u sourcePhysical=%ux%u "
         "sourceLogical=%ux%u sourceRect=[%d,%d] destinationPhysical=%ux%u "
         "destinationLogical=%ux%u destinationRect=[%d,%d] filter=0x%x "
         "texrectScratch draws=%u rectangles=%u fbo=%u texture=%u size=%ux%u "
         "classification={source:%s destination:%s splitCount:%u}",
         reason, stats.screenSpaceBatches, stats.screenSpaceVertices,
         stats.leftEyeDraws, stats.rightEyeDraws, stats.leftCorrections,
         stats.rightCorrections, stats.leftOffsetXMicro / 1000000.0f,
         stats.leftOffsetYMicro / 1000000.0f,
         stats.rightOffsetXMicro / 1000000.0f,
         stats.rightOffsetYMicro / 1000000.0f, stats.lastFramebuffer,
         stats.lastProgram, stats.lastUniformMask,
         (stats.lastUniformMask & enabledBit) != 0 ? 1 : 0,
         (stats.lastUniformMask & transformBit) != 0 ? 1 : 0,
         (stats.lastUniformMask & screenTransformBit) != 0 ? 1 : 0,
         (stats.lastUniformMask & rowMask) == rowMask ? 1 : 0,
         stats.missingUniformDraws, stats.finalBlits,
         stats.finalReadFramebuffer, stats.finalTexture,
         stats.finalPhysicalSourceWidth, stats.finalSourceHeight,
         stats.finalLogicalSourceWidth, stats.finalSourceHeight,
         stats.finalSourceX0, stats.finalSourceX1,
         stats.finalPhysicalDestinationWidth, stats.finalDestinationHeight,
         stats.finalLogicalDestinationWidth, stats.finalDestinationHeight,
         stats.finalDestinationX0, stats.finalDestinationX1, stats.finalFilter,
         stats.scratchDraws, stats.scratchRectangles,
         stats.scratchFramebuffer, stats.scratchTexture,
         stats.scratchWidth, stats.scratchHeight,
         (stats.finalClassification & 1U) != 0 ? "PACKED_SBS" : "MONO",
         (stats.finalClassification & 2U) != 0 ? "PACKED_SBS" : "MONO",
         (stats.finalClassification & 2U) != 0 ? 1U : 0U);
}

void resolveInputBridge() {
    if (g.setVrInput != nullptr) {
        return;
    }

    void* inputPlugin = dlopen("libmupen64plus-input-android.so", RTLD_NOW | RTLD_NOLOAD);
    void* symbolScope = inputPlugin != nullptr ? inputPlugin : RTLD_DEFAULT;
    g.setVrInput = reinterpret_cast<SetVrInputFn>(dlsym(symbolScope, "M64PQuestVrSetInput"));
    if (g.setVrInput != nullptr) {
        g.inputLibraryHandle = inputPlugin;
        inputPlugin = nullptr;
        LOGI("Connected OpenXR Touch actions to Android N64 input overlay");
    } else if (!g.inputBridgeLogged) {
        LOGI("Android input plugin is not loaded yet; Touch input will be retried");
        g.inputBridgeLogged = true;
    }
    if (inputPlugin != nullptr) {
        dlclose(inputPlugin);
    }
}

bool readBooleanAction(XrAction action) {
    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = action;
    XrActionStateBoolean state{XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_SUCCEEDED(xrGetActionStateBoolean(g.session, &getInfo, &state)) &&
           state.isActive == XR_TRUE && state.currentState == XR_TRUE;
}

float readFloatAction(XrAction action) {
    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = action;
    XrActionStateFloat state{XR_TYPE_ACTION_STATE_FLOAT};
    if (XR_SUCCEEDED(xrGetActionStateFloat(g.session, &getInfo, &state)) &&
        state.isActive == XR_TRUE) {
        return state.currentState;
    }
    return 0.0f;
}

XrVector2f readVectorAction(XrAction action) {
    XrActionStateGetInfo getInfo{XR_TYPE_ACTION_STATE_GET_INFO};
    getInfo.action = action;
    XrActionStateVector2f state{XR_TYPE_ACTION_STATE_VECTOR2F};
    if (XR_SUCCEEDED(xrGetActionStateVector2f(g.session, &getInfo, &state)) &&
        state.isActive == XR_TRUE) {
        return state.currentState;
    }
    return {};
}

XrVector2f applyStickDeadzone(XrVector2f stick) {
    constexpr float deadzone = 0.15f;
    const float length = std::sqrt(stick.x * stick.x + stick.y * stick.y);
    if (length <= deadzone) {
        return {};
    }
    const float remappedLength = std::min(1.0f, (length - deadzone) / (1.0f - deadzone));
    const float scale = remappedLength / length;
    return {stick.x * scale, stick.y * scale};
}

void syncControllerInput() {
    resolveInputBridge();
    if (!g.touchControllerEnabled || g.controllerActions.actionSet == XR_NULL_HANDLE) {
        g.menuInputMask = 0;
        if (g.setVrInput != nullptr) {
            g.setVrInput(0, 0, 0.0f, 0.0f);
        }
        return;
    }

    const XrActiveActionSet activeSet{g.controllerActions.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    if (XR_FAILED(xrSyncActions(g.session, &syncInfo))) {
        g.menuInputMask = 0;
        if (g.setVrInput != nullptr) {
            g.setVrInput(0, 0, 0.0f, 0.0f);
        }
        return;
    }

    const bool aPressed = readBooleanAction(g.controllerActions.buttonA);
    const bool bPressed = readBooleanAction(g.controllerActions.buttonB);
    const bool xPressed = readBooleanAction(g.controllerActions.buttonX);
    const bool yPressed = readBooleanAction(g.controllerActions.buttonY);
    const float leftTriggerValue = readFloatAction(g.controllerActions.leftTrigger);
    const float rightTriggerValue = readFloatAction(g.controllerActions.rightTrigger);
    const float leftSqueezeValue = readFloatAction(g.controllerActions.leftSqueeze);
    const float rightSqueezeValue = readFloatAction(g.controllerActions.rightSqueeze);
    const XrVector2f leftStick =
            applyStickDeadzone(readVectorAction(g.controllerActions.leftStick));
    const XrVector2f rightStick =
            applyStickDeadzone(readVectorAction(g.controllerActions.rightStick));

    constexpr uint32_t menuUp = 1u << 0;
    constexpr uint32_t menuDown = 1u << 1;
    constexpr uint32_t menuLeft = 1u << 2;
    constexpr uint32_t menuRight = 1u << 3;
    constexpr uint32_t menuSelect = 1u << 4;
    constexpr uint32_t menuBack = 1u << 5;
    uint32_t menuInput = 0;
    if (leftStick.y > 0.45f || rightStick.y > 0.45f) menuInput |= menuUp;
    if (leftStick.y < -0.45f || rightStick.y < -0.45f) menuInput |= menuDown;
    if (leftStick.x < -0.45f || rightStick.x < -0.45f) menuInput |= menuLeft;
    if (leftStick.x > 0.45f || rightStick.x > 0.45f) menuInput |= menuRight;
    if (aPressed || leftTriggerValue > 0.45f || rightTriggerValue > 0.45f) {
        menuInput |= menuSelect;
    }
    if (bPressed) menuInput |= menuBack;
    g.menuInputMask = menuInput;

    // N64 button bits match mupen64plus-input-android's BUTTON_BITS table.
    constexpr uint32_t dpadLeft = 0x0002;
    constexpr uint32_t dpadUp = 0x0008;
    constexpr uint32_t start = 0x0010;
    constexpr uint32_t zTrigger = 0x0020;
    constexpr uint32_t bButton = 0x0040;
    constexpr uint32_t aButton = 0x0080;
    constexpr uint32_t cRight = 0x0100;
    constexpr uint32_t cLeft = 0x0200;
    constexpr uint32_t cDown = 0x0400;
    constexpr uint32_t cUp = 0x0800;
    constexpr uint32_t rTrigger = 0x1000;
    constexpr uint32_t lTrigger = 0x2000;

    uint32_t buttons = 0;
    if (aPressed) buttons |= aButton;
    if (bPressed) buttons |= bButton;
    if (xPressed) buttons |= dpadLeft;
    if (yPressed) buttons |= dpadUp;
    if (leftTriggerValue > 0.45f || rightTriggerValue > 0.45f) buttons |= zTrigger;
    if (leftSqueezeValue > 0.45f) buttons |= lTrigger;
    if (rightSqueezeValue > 0.45f) buttons |= rTrigger;

    if (rightStick.x > 0.45f) buttons |= cRight;
    if (rightStick.x < -0.45f) buttons |= cLeft;
    if (rightStick.y > 0.45f) buttons |= cUp;
    if (rightStick.y < -0.45f) buttons |= cDown;

    const bool leftClick = readBooleanAction(g.controllerActions.leftStickClick);
    const bool rightClick = readBooleanAction(g.controllerActions.rightStickClick);
    const bool recenterChord = leftClick && rightClick;
    if (recenterChord && !g.recenterChordDown) {
        g.screenRecenterPending = true;
        resolveRendererBridge();
        if (g.recenterVr != nullptr) {
            g.recenterVr();
        }
        LOGI("Requested game-screen and GLideN64 recenter from Touch thumbstick chord");
    } else if (rightClick && !leftClick) {
        buttons |= start;
    }
    g.recenterChordDown = recenterChord;

    if (g.setVrInput != nullptr) {
        g.setVrInput(1, buttons, leftStick.x, leftStick.y);
    }
}

void rememberPublishedViews(XrTime displayTime, uint32_t viewCount) {
    if (viewCount < 2) {
        return;
    }
    PoseHistoryEntry& entry = g.poseHistory[g.poseHistoryWriteIndex % g.poseHistory.size()];
    entry.displayTime = displayTime;
    entry.views[0] = g.views[0];
    entry.views[1] = g.views[1];
    entry.valid = true;
    ++g.poseHistoryWriteIndex;
}

void associateLatchedSourceFrame(int64_t textureTimestamp) {
    g.sourceTextureTimestamp = textureTimestamp;
    g.sourceViewsValid = false;
    if (!g.stereoSourceActive || g.getPresentedPoseTimestamp == nullptr) {
        return;
    }

    const XrTime renderedDisplayTime =
        static_cast<XrTime>(g.getPresentedPoseTimestamp());
    if (renderedDisplayTime == 0) {
        ++g.sourcePoseMisses;
        return;
    }
    for (const PoseHistoryEntry& entry : g.poseHistory) {
        if (entry.valid && entry.displayTime == renderedDisplayTime) {
            g.sourceViews = entry.views;
            g.sourceViewDisplayTime = renderedDisplayTime;
            g.sourceViewsValid = true;
            ++g.sourcePoseMatches;
            return;
        }
    }
    ++g.sourcePoseMisses;
}

std::array<float, 2> mapSourceUv(float u, float v) {
    return {{
        g.sourceTransform[0] * u + g.sourceTransform[4] * v + g.sourceTransform[12],
        g.sourceTransform[1] * u + g.sourceTransform[5] * v + g.sourceTransform[13],
    }};
}

void updateSourceTransform(JNIEnv* env, jfloatArray transformArray) {
    ++g.sourceTransformUpdates;
    if (transformArray == nullptr || env->GetArrayLength(transformArray) != 16) {
        if (g.sourceTransformUpdates <= 3) {
            LOGW("SurfaceTexture transform missing/invalid on update=%u; retaining previous matrix",
                 g.sourceTransformUpdates);
        }
        return;
    }

    std::array<float, 16> candidate{};
    env->GetFloatArrayRegion(transformArray, 0, static_cast<jsize>(candidate.size()),
                             candidate.data());
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
        LOGW("Unable to read SurfaceTexture transform on update=%u",
             g.sourceTransformUpdates);
        return;
    }
    if (!std::all_of(candidate.begin(), candidate.end(), [](float value) {
            return std::isfinite(value);
        })) {
        LOGW("Ignoring non-finite SurfaceTexture transform on update=%u",
             g.sourceTransformUpdates);
        return;
    }

    const bool changed = std::memcmp(candidate.data(), g.sourceTransform.data(),
                                     sizeof(candidate)) != 0;
    if (changed) {
        g.sourceTransform = candidate;
        ++g.sourceTransformChanges;
    }
    if (g.sourceTransformUpdates <= 3 || (changed && g.sourceTransformChanges <= 3)) {
        const auto bottomLeft = mapSourceUv(0.0f, 0.0f);
        const auto topRight = mapSourceUv(1.0f, 1.0f);
        const auto splitBottom = mapSourceUv(0.5f, 0.0f);
        const auto splitTop = mapSourceUv(0.5f, 1.0f);
        LOGI("SurfaceTexture transform update=%u changes=%u changed=%d "
             "matrix=[%.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f | "
             "%.5f %.5f %.5f %.5f | %.5f %.5f %.5f %.5f] "
             "mappedFull=[%.5f,%.5f -> %.5f,%.5f] "
             "mappedSplit=[%.5f,%.5f -> %.5f,%.5f]",
             g.sourceTransformUpdates, g.sourceTransformChanges, changed ? 1 : 0,
             g.sourceTransform[0], g.sourceTransform[1], g.sourceTransform[2],
             g.sourceTransform[3], g.sourceTransform[4], g.sourceTransform[5],
             g.sourceTransform[6], g.sourceTransform[7], g.sourceTransform[8],
             g.sourceTransform[9], g.sourceTransform[10], g.sourceTransform[11],
             g.sourceTransform[12], g.sourceTransform[13], g.sourceTransform[14],
             g.sourceTransform[15], bottomLeft[0], bottomLeft[1], topRight[0],
             topRight[1], splitBottom[0], splitBottom[1], splitTop[0], splitTop[1]);
    }
}

void publishHeadPose(XrTime displayTime, uint32_t viewCount) {
    resolveRendererBridge();
    if (g.setVrPose == nullptr || viewCount == 0) {
        return;
    }

    const XrPosef& pose = g.views[0].pose;
    XrVector3f position = pose.position;
    if (viewCount > 1) {
        position.x = (g.views[0].pose.position.x + g.views[1].pose.position.x) * 0.5f;
        position.y = (g.views[0].pose.position.y + g.views[1].pose.position.y) * 0.5f;
        position.z = (g.views[0].pose.position.z + g.views[1].pose.position.z) * 0.5f;
    }

    rememberPublishedViews(displayTime, viewCount);
    if (g.setVrViews != nullptr && viewCount >= 2) {
        const XrView& left = g.views[0];
        const XrView& right = g.views[1];
        g.setVrViews(
            left.pose.position.x, left.pose.position.y, left.pose.position.z,
            left.fov.angleLeft, left.fov.angleRight, left.fov.angleUp, left.fov.angleDown,
            right.pose.position.x, right.pose.position.y, right.pose.position.z,
            right.fov.angleLeft, right.fov.angleRight, right.fov.angleUp, right.fov.angleDown);
    }
    g.setVrPose(pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
                position.x, position.y, position.z, static_cast<int64_t>(displayTime));

    ++g.posePublicationCount;
    const bool boundedSnapshot = g.posePublicationCount == 60 ||
        g.posePublicationCount == 300 || g.posePublicationCount == 900 ||
        g.posePublicationCount == 1800 || g.posePublicationCount == 3600 ||
        g.posePublicationCount == 7200 || g.posePublicationCount == 18000 ||
        g.posePublicationCount == 36000;
    const bool boundedDebugSnapshot = g.debugLogging &&
        g.posePublicationCount % 300 == 0 && g.posePublicationCount <= 3600;
    if (boundedSnapshot || boundedDebugSnapshot) {
        uint32_t geometryDraws = 0;
        uint32_t rectangleDraws = 0;
        uint32_t eyeDraws = 0;
        uint32_t poseGeneration = 0;
        uint32_t targetWidthFallbacks = 0;
        uint32_t lastTargetWidth = 0;
        uint32_t perspectiveDraws = 0;
        uint32_t orthographicDraws = 0;
        uint32_t missingTransformProgramDraws = 0;
        uint32_t geometryVertices = 0;
        uint32_t modifiedPositionVertices = 0;
        uint32_t framebufferBlits = 0;
        uint32_t backgroundRectangles = 0;
        uint32_t sprite2DCommands = 0;
        uint32_t canonicalPerspectiveDraws = 0;
        uint32_t foldedPerspectiveDraws = 0;
        uint32_t projectionLoads = 0;
        uint32_t foldedProjectionMultiplies = 0;
        uint32_t framebufferAllocations = 0;
        uint32_t framebufferN64Width = 0;
        uint32_t framebufferN64Height = 0;
        uint32_t framebufferPhysicalWidth = 0;
        uint32_t framebufferPhysicalHeight = 0;
        uint32_t framebufferScaleMilli = 0;
        uint32_t nativeResolutionFactor = 0;
        uint32_t viewportWidth = 0;
        uint32_t viewportHeight = 0;
        uint32_t scissorWidth = 0;
        uint32_t scissorHeight = 0;
        if (g.getVrStats != nullptr) {
            g.getVrStats(&geometryDraws, &rectangleDraws, &eyeDraws, &poseGeneration,
                         &targetWidthFallbacks, &lastTargetWidth, &perspectiveDraws,
                         &orthographicDraws, &missingTransformProgramDraws,
                         &geometryVertices, &modifiedPositionVertices, &framebufferBlits,
                         &backgroundRectangles, &sprite2DCommands);
        }
        if (g.getVrPipelineStats != nullptr) {
            g.getVrPipelineStats(&canonicalPerspectiveDraws, &foldedPerspectiveDraws,
                                 &projectionLoads, &foldedProjectionMultiplies,
                                 &framebufferAllocations, &framebufferN64Width,
                                 &framebufferN64Height, &framebufferPhysicalWidth,
                                 &framebufferPhysicalHeight, &framebufferScaleMilli,
                                 &nativeResolutionFactor, &viewportWidth, &viewportHeight,
                                 &scissorWidth, &scissorHeight);
        }
        float runtimeIpd = 0.0f;
        if (viewCount >= 2) {
            const XrVector3f delta{
                g.views[1].pose.position.x - g.views[0].pose.position.x,
                g.views[1].pose.position.y - g.views[0].pose.position.y,
                g.views[1].pose.position.z - g.views[0].pose.position.z,
            };
            runtimeIpd = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
        }
        LOGI("pose q=[%.3f %.3f %.3f %.3f] p=[%.3f %.3f %.3f], "
             "runtimeIpd=%.4f fovL=[%.3f %.3f %.3f %.3f], "
             "draws geometry=%u perspective=%u orthographic=%u rect=%u "
             "backgroundRect=%u sprite2D=%u blits=%u "
             "projection canonical=%u foldedView=%u loads=%u foldedMultiplies=%u "
             "missingTransformProgram=%u vertices=%u modifiedXY=%u eyes=%u "
             "poseGeneration=%u targetWidth=%u targetFallbacks=%u "
             "internalFbo allocations=%u n64=%ux%u packed=%ux%u perEye=%ux%u "
             "scale=%.3f nativeFactor=%u viewport=%ux%u scissor=%ux%u",
             pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
             position.x, position.y, position.z, runtimeIpd,
             g.views[0].fov.angleLeft, g.views[0].fov.angleRight,
             g.views[0].fov.angleUp, g.views[0].fov.angleDown,
             geometryDraws, perspectiveDraws, orthographicDraws, rectangleDraws,
             backgroundRectangles, sprite2DCommands, framebufferBlits,
             canonicalPerspectiveDraws, foldedPerspectiveDraws, projectionLoads,
             foldedProjectionMultiplies,
             missingTransformProgramDraws, geometryVertices,
             modifiedPositionVertices, eyeDraws, poseGeneration, lastTargetWidth,
             targetWidthFallbacks, framebufferAllocations, framebufferN64Width,
             framebufferN64Height, framebufferPhysicalWidth, framebufferPhysicalHeight,
             framebufferPhysicalWidth / 2, framebufferPhysicalHeight,
             framebufferScaleMilli / 1000.0f, nativeResolutionFactor,
             viewportWidth, viewportHeight, scissorWidth, scissorHeight);
        logRendererUiStats(boundedDebugSnapshot ? "BOUNDED_DEBUG" : "BOUNDED_AUTO");
    }
}

bool validateRequiredInstanceExtensions() {
    uint32_t count = 0;
    if (!xrOk(xrEnumerateInstanceExtensionProperties(nullptr, 0, &count, nullptr),
              "xrEnumerateInstanceExtensionProperties(count)")) {
        return false;
    }
    std::vector<XrExtensionProperties> properties(count);
    for (XrExtensionProperties& property : properties) {
        property.type = XR_TYPE_EXTENSION_PROPERTIES;
        property.next = nullptr;
    }
    if (!xrOk(xrEnumerateInstanceExtensionProperties(nullptr, count, &count, properties.data()),
              "xrEnumerateInstanceExtensionProperties")) {
        return false;
    }

    const std::array<const char*, 2> required{
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
    };
    bool allFound = true;
    for (const char* requiredName : required) {
        const auto match = std::find_if(properties.begin(), properties.end(),
                [requiredName](const XrExtensionProperties& property) {
                    return std::strcmp(property.extensionName, requiredName) == 0;
                });
        if (match == properties.end()) {
            LOGE("Required OpenXR extension is unavailable: %s", requiredName);
            allFound = false;
        } else {
            LOGI("Required OpenXR extension available: %s version=%u", requiredName,
                 match->extensionVersion);
        }
    }
    return allFound;
}

bool selectEnvironmentBlendMode() {
    uint32_t count = 0;
    if (!xrOk(xrEnumerateEnvironmentBlendModes(g.instance, g.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &count, nullptr),
            "xrEnumerateEnvironmentBlendModes(count)") || count == 0) {
        return false;
    }
    std::vector<XrEnvironmentBlendMode> modes(count);
    if (!xrOk(xrEnumerateEnvironmentBlendModes(g.instance, g.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, count, &count, modes.data()),
            "xrEnumerateEnvironmentBlendModes")) {
        return false;
    }
    if (std::find(modes.begin(), modes.end(), XR_ENVIRONMENT_BLEND_MODE_OPAQUE) == modes.end()) {
        LOGE("Primary stereo configuration does not support XR_ENVIRONMENT_BLEND_MODE_OPAQUE");
        return false;
    }
    g.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    LOGI("Selected OpenXR environment blend mode OPAQUE from %u advertised modes", count);
    return true;
}

bool selectColorFormat() {
    uint32_t count = 0;
    if (!xrOk(xrEnumerateSwapchainFormats(g.session, 0, &count, nullptr), "xrEnumerateSwapchainFormats(count)") || count == 0) {
        return false;
    }
    std::vector<int64_t> formats(count);
    if (!xrOk(xrEnumerateSwapchainFormats(g.session, count, &count, formats.data()), "xrEnumerateSwapchainFormats")) {
        return false;
    }
    std::string offered;
    for (const int64_t format : formats) {
        if (!offered.empty()) offered += ",";
        offered += std::to_string(format);
    }
    LOGI("OpenXR GLES swapchain formats (%u): %s", count, offered.c_str());
    // GL_RGBA8 is the most conservative compositor format for the first Quest bring-up. Keep
    // sRGB available, but do not make correct sRGB sampling a prerequisite for a visible frame.
    constexpr std::array<int64_t, 3> preferred{GL_RGBA8, GL_SRGB8_ALPHA8, GL_RGB10_A2};
    for (const int64_t candidate : preferred) {
        if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) {
            g.colorFormat = candidate;
            LOGI("Selected OpenXR GLES swapchain format %lld",
                 static_cast<long long>(candidate));
            return true;
        }
    }
    LOGE("OpenXR runtime offered no supported GLES color swapchain format");
    return false;
}

bool createSwapchains() {
    uint32_t viewCount = 0;
    if (!xrOk(xrEnumerateViewConfigurationViews(g.instance, g.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 0, &viewCount, nullptr),
            "xrEnumerateViewConfigurationViews(count)") || viewCount != 2) {
        LOGE("Expected two primary stereo views, received %u", viewCount);
        return false;
    }

    g.configViews.assign(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    if (!xrOk(xrEnumerateViewConfigurationViews(g.instance, g.systemId,
            XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, viewCount, &viewCount, g.configViews.data()),
            "xrEnumerateViewConfigurationViews")) {
        return false;
    }

    if (!selectColorFormat()) {
        return false;
    }

    g.views.assign(viewCount, {XR_TYPE_VIEW});
    g.swapchains.resize(viewCount);
    for (uint32_t eye = 0; eye < viewCount; ++eye) {
        const XrViewConfigurationView& view = g.configViews[eye];
        LOGI("View %u recommended=%ux%u max=%ux%u samples=%u maxSamples=%u", eye,
             view.recommendedImageRectWidth, view.recommendedImageRectHeight,
             view.maxImageRectWidth, view.maxImageRectHeight,
             view.recommendedSwapchainSampleCount, view.maxSwapchainSampleCount);
        XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
        info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
        info.format = g.colorFormat;
        info.sampleCount = 1;
        info.width = view.recommendedImageRectWidth;
        info.height = view.recommendedImageRectHeight;
        info.faceCount = 1;
        info.arraySize = 1;
        info.mipCount = 1;

        Swapchain& swapchain = g.swapchains[eye];
        swapchain.width = info.width;
        swapchain.height = info.height;
        if (!xrOk(xrCreateSwapchain(g.session, &info, &swapchain.handle), "xrCreateSwapchain")) {
            return false;
        }

        uint32_t imageCount = 0;
        if (!xrOk(xrEnumerateSwapchainImages(swapchain.handle, 0, &imageCount, nullptr),
                "xrEnumerateSwapchainImages(count)")) {
            return false;
        }
        swapchain.images.assign(imageCount, {XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR});
        if (!xrOk(xrEnumerateSwapchainImages(swapchain.handle, imageCount, &imageCount,
                reinterpret_cast<XrSwapchainImageBaseHeader*>(swapchain.images.data())),
                "xrEnumerateSwapchainImages")) {
            return false;
        }
        LOGI("Created eye %u swapchain handle=%p size=%dx%d images=%u format=%lld", eye,
             reinterpret_cast<void*>(swapchain.handle), swapchain.width, swapchain.height,
             imageCount, static_cast<long long>(g.colorFormat));
    }
    return true;
}

void pollEvents() {
    if (g.instance == XR_NULL_HANDLE) {
        return;
    }

    XrEventDataBuffer event{XR_TYPE_EVENT_DATA_BUFFER};
    while (true) {
        const XrResult pollResult = xrPollEvent(g.instance, &event);
        if (pollResult == XR_EVENT_UNAVAILABLE) {
            break;
        }
        if (XR_FAILED(pollResult)) {
            LOGE("xrPollEvent -> %d (%s)", pollResult, xrResultName(pollResult));
            g.exitRequested = true;
            break;
        }
        const auto* header = reinterpret_cast<const XrEventDataBaseHeader*>(&event);
        LOGI("xrPollEvent -> %d (%s), eventType=%d", pollResult, xrResultName(pollResult),
             static_cast<int>(header->type));
        if (header->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(header);
            g.sessionState = changed->state;
            LOGI("OpenXR session state changed: session=%p time=%lld state=%s (%d) running=%d",
                 reinterpret_cast<void*>(changed->session), static_cast<long long>(changed->time),
                 sessionStateName(g.sessionState), static_cast<int>(g.sessionState),
                 g.sessionRunning ? 1 : 0);
            switch (g.sessionState) {
                case XR_SESSION_STATE_READY: {
                    if (!g.sessionRunning) {
                        XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                        begin.primaryViewConfigurationType =
                                XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                        if (xrOk(xrBeginSession(g.session, &begin), "xrBeginSession")) {
                            g.sessionRunning = true;
                            g.sessionWaitPollCount = 0;
                            LOGI("OpenXR session began; waiting for compositor visibility");
                        } else {
                            g.exitRequested = true;
                        }
                    } else {
                        LOGW("Ignoring duplicate READY event for an already-running session");
                    }
                    break;
                }
                case XR_SESSION_STATE_STOPPING:
                    if (g.sessionRunning) {
                        if (!xrOk(xrEndSession(g.session), "xrEndSession")) {
                            g.exitRequested = true;
                        }
                        g.sessionRunning = false;
                    } else {
                        LOGW("STOPPING received while sessionRunning=false; not calling xrEndSession");
                    }
                    break;
                case XR_SESSION_STATE_EXITING:
                case XR_SESSION_STATE_LOSS_PENDING:
                    g.sessionRunning = false;
                    g.exitRequested = true;
                    break;
                default:
                    break;
            }
        } else if (header->type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            const auto* loss = reinterpret_cast<const XrEventDataInstanceLossPending*>(header);
            LOGE("OpenXR instance loss pending at time=%lld",
                 static_cast<long long>(loss->lossTime));
            g.sessionRunning = false;
            g.exitRequested = true;
        } else if (header->type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            const auto* change =
                    reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(header);
            LOGI("OpenXR reference-space change pending: type=%d time=%lld poseValid=%d",
                 static_cast<int>(change->referenceSpaceType),
                 static_cast<long long>(change->changeTime), change->poseValid ? 1 : 0);
            resolveRendererBridge();
            g.screenRecenterPending = true;
            if (g.recenterVr != nullptr) {
                g.recenterVr();
            }
            LOGI("Queued game-screen and GLideN64 recenter after OpenXR reference-space change");
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

bool renderEye(uint32_t eye, uint32_t imageIndex, RenderContent content,
               PresentationMode presentationMode) {
    const Swapchain& swapchain = g.swapchains[eye];
    while (glGetError() != GL_NO_ERROR) {
        // Discard stale errors so diagnostics describe this eye submission.
    }
    glBindFramebuffer(GL_FRAMEBUFFER, g.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           swapchain.images[imageIndex].image, 0);
    const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("OpenXR eye framebuffer is incomplete: 0x%x", framebufferStatus);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    glViewport(0, 0, swapchain.width, swapchain.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    if (content == RenderContent::HeadLockedDiagnostic) {
        // A bright green head-locked panel is the first proof that any core composition layer is
        // reaching the compositor. It does not depend on tracking or the external texture shader.
        glClearColor(0.02f, 0.95f, 0.12f, 1.0f);
    } else if (content == RenderContent::StereoDiagnostic) {
        // Deliberately unmistakable projection proof: red left, blue right.
        glClearColor(eye == 0 ? 0.90f : 0.02f, 0.02f, eye == 0 ? 0.02f : 0.90f, 1.0f);
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);

    const bool drawSource = content == RenderContent::SourceTexture &&
                            g.sourceBlitReady && g.sourceTexture != 0;
    const bool drawCalibration = content == RenderContent::StereoDiagnostic &&
                                 g.sourceBlitReady && g.sourceTexture != 0;
    if (drawSource || drawCalibration) {
        glUseProgram(g.program);
        glBindVertexArray(g.vertexArray);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, g.sourceTexture);
        glUniform1i(g.sourceUniform, 0);
        glUniform1i(g.calibrationModeUniform, drawCalibration ? 1 : 0);
        glUniform1i(g.calibrationEyeUniform, static_cast<GLint>(eye));
        const float targetAspect = swapchain.height > 0
            ? swapchain.width / static_cast<float>(swapchain.height) : 1.0f;
        glUniform1f(g.targetAspectUniform, targetAspect);
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (drawCalibration) {
            static constexpr std::array<float, 16> identityTransform{{
                1.0f, 0.0f, 0.0f, 0.0f,
                0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f,
                0.0f, 0.0f, 0.0f, 1.0f,
            }};
            glUniform4f(g.uvTransformUniform, 1.0f, 1.0f, 0.0f, 0.0f);
            glUniformMatrix4fv(g.surfaceTransformUniform, 1, GL_FALSE,
                               identityTransform.data());
        } else {
            const bool stereo = g.stereoSourceActive;
            const uint32_t sourceEye = stereo && g.configurationSwapEyes ? 1U - eye : eye;
            const float uvScaleX = stereo ? 0.5f : 1.0f;
            const float uvOffsetX = stereo ? (sourceEye == 0 ? 0.0f : 0.5f) : 0.0f;
            glUniform4f(g.uvTransformUniform, uvScaleX, 1.0f, uvOffsetX, 0.0f);
            glUniformMatrix4fv(g.surfaceTransformUniform, 1, GL_FALSE,
                               g.sourceTransform.data());

            if (presentationMode == PresentationMode::ImmersiveProjection) {
                // Only aspect-fit a source that is a 4:3 picture. When the OpenXR FOV path is
                // active GLideN64 has already replaced the game's angular terms with this eye's
                // full runtime FOV, so NDC -1..1 in the source means the FOV edges, not the
                // edges of a 4:3 image. Fitting it then maps the full vertical FOV onto
                // targetAspect/sourceAspect of the eye while the layer still submits the full
                // FOV, and the world is compressed vertically by exactly that ratio: the
                // checked-in Quest 3S log shows effectiveNdcScale=1.0000x0.7159, a 28% vertical
                // squash. Stretching to the complete eye is the correct presentation for a
                // full-FOV render; the non-square source pixels are simply resampled.
                const float sourceAspect = std::clamp(g.sourceContentAspect, 0.5f, 3.0f);
                if (!effectiveUseOpenXrFov()) {
                    if (sourceAspect > targetAspect) {
                        scaleY = targetAspect / sourceAspect;
                    } else if (sourceAspect < targetAspect) {
                        scaleX = sourceAspect / targetAspect;
                    }
                }
                scaleX *= g.configurationImmersiveViewScale;
                scaleY *= g.configurationImmersiveViewScale;
                if (!g.immersiveScaleLogged) {
                    LOGI("Immersive aspect fit openXrFov=%d sourceAspect=%.4f targetAspect=%.4f "
                         "userScale=%.3f effectiveNdcScale=%.4fx%.4f sourcePerEye=%dx%d "
                         "consumerEye=%dx%d",
                         effectiveUseOpenXrFov() ? 1 : 0,
                         sourceAspect, targetAspect, g.configurationImmersiveViewScale,
                         scaleX, scaleY,
                         g.stereoSourceActive ? g.sourceWidth / 2 : g.sourceWidth,
                         g.sourceHeight, swapchain.width, swapchain.height);
                    g.immersiveScaleLogged = true;
                }
            }
            if (shouldLogDetailedFrame()) {
                const auto mappedStart = mapSourceUv(uvOffsetX, 0.0f);
                const auto mappedEnd = mapSourceUv(uvOffsetX + uvScaleX, 1.0f);
                LOGI("frame=%u OpenXR source sample destinationEye=%u sourceEye=%u "
                     "texture=%u baseUv=[%.5f,0 -> %.5f,1] "
                     "surfaceMappedUv=[%.5f,%.5f -> %.5f,%.5f] "
                     "source=%dx%d perEye=%dx%d destinationViewport=[0,0 %dx%d] "
                     "quadScale=%.5fx%.5f sourceFilter=GL_LINEAR",
                     g.begunFrameCount, eye, sourceEye, g.sourceTexture,
                     uvOffsetX, uvOffsetX + uvScaleX, mappedStart[0], mappedStart[1],
                     mappedEnd[0], mappedEnd[1], g.sourceWidth, g.sourceHeight,
                     stereo ? g.sourceWidth / 2 : g.sourceWidth, g.sourceHeight,
                     swapchain.width, swapchain.height, scaleX, scaleY);
            }
        }
        // Cinema quads carry the content aspect in their physical dimensions, so they continue
        // to fill the eye swapchain. Projection mode uses the aspect-fit scale above.
        glUniform2f(g.quadScaleUniform, scaleX, scaleY);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    const GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (content != RenderContent::SourceTexture) {
        // Remove driver/compositor ambiguity from the short diagnostic interval. Normal frames
        // use glFlush so this synchronization cost does not persist during emulation.
        glFinish();
    } else {
        glFlush();
    }
    if (error != GL_NO_ERROR) {
        LOGE("OpenXR eye %u GLES submission failed: 0x%x", eye, error);
        return false;
    }
    return true;
}

bool acquireRenderRelease(uint32_t eye, RenderContent content,
                          PresentationMode presentationMode) {
    Swapchain& swapchain = g.swapchains[eye];
    XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    uint32_t imageIndex = 0;
    const XrResult acquireResult =
            xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &imageIndex);
    if (!xrFrameOk(acquireResult, "xrAcquireSwapchainImage")) {
        return false;
    }
    if (imageIndex >= swapchain.images.size()) {
        LOGE("frame=%u eye=%u acquired invalid image index=%u imageCount=%zu swapchain=%p",
             g.begunFrameCount, eye, imageIndex, swapchain.images.size(),
             reinterpret_cast<void*>(swapchain.handle));
        g.exitRequested = true;
        return false;
    }
    if (shouldLogDetailedFrame()) {
        LOGI("frame=%u eye=%u acquired swapchain=%p imageIndex=%u texture=%u size=%dx%d",
             g.begunFrameCount, eye, reinterpret_cast<void*>(swapchain.handle), imageIndex,
             swapchain.images[imageIndex].image, swapchain.width, swapchain.height);
    }

    XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    imageWait.timeout = XR_INFINITE_DURATION;
    const XrResult waitResult = xrWaitSwapchainImage(swapchain.handle, &imageWait);
    if (!xrFrameOk(waitResult, "xrWaitSwapchainImage")) {
        // An acquired image may only be released after a successful wait. Force orderly teardown
        // rather than violating the swapchain call order on the next frame.
        g.exitRequested = true;
        return false;
    }

    const bool rendered = renderEye(eye, imageIndex, content, presentationMode);
    XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    const XrResult releaseResult = xrReleaseSwapchainImage(swapchain.handle, &releaseInfo);
    const bool released = xrFrameOk(releaseResult, "xrReleaseSwapchainImage");
    if (!rendered || !released) {
        g.exitRequested = true;
    }
    return rendered && released;
}

void destroyState(JNIEnv* env) {
    LOGI("Destroying OpenXR state on tid=%ld initialized=%d session=%p state=%s running=%d",
         QuestVrDiagnostics::currentThreadId(), g.initialized ? 1 : 0,
         reinterpret_cast<void*>(g.session), sessionStateName(g.sessionState),
         g.sessionRunning ? 1 : 0);
    if (g.setVrDiagnosticSink != nullptr) {
        g.setVrDiagnosticSink(nullptr);
    }
    if (g.setVrEnabled != nullptr) {
        g.setVrEnabled(0);
    }
    if (g.setVrInput != nullptr) {
        g.setVrInput(0, 0, 0.0f, 0.0f);
    }
    if (g.rendererLibraryHandle != nullptr) {
        dlclose(g.rendererLibraryHandle);
        g.rendererLibraryHandle = nullptr;
    }
    if (g.inputLibraryHandle != nullptr) {
        dlclose(g.inputLibraryHandle);
        g.inputLibraryHandle = nullptr;
    }

    if (g.program != 0) glDeleteProgram(g.program);
    if (g.framebuffer != 0) glDeleteFramebuffers(1, &g.framebuffer);
    if (g.vertexArray != 0) glDeleteVertexArrays(1, &g.vertexArray);
    g.program = 0;
    g.framebuffer = 0;
    g.vertexArray = 0;

    if (g.sessionRunning && g.session != XR_NULL_HANDLE) {
        if (g.sessionState == XR_SESSION_STATE_STOPPING) {
            xrOk(xrEndSession(g.session), "xrEndSession(shutdown)");
        } else {
            // Android can tear down the SurfaceView before the runtime's STOPPING event reaches
            // this render thread. Request an orderly exit instead of calling xrEndSession from an
            // invalid running state; xrDestroySession below remains the final synchronous cleanup.
            xrOk(xrRequestExitSession(g.session), "xrRequestExitSession(shutdown)");
        }
        g.sessionRunning = false;
    }
    if (g.controllerActions.actionSet != XR_NULL_HANDLE) {
        xrOk(xrDestroyActionSet(g.controllerActions.actionSet), "xrDestroyActionSet");
        g.controllerActions = {};
    }
    for (Swapchain& swapchain : g.swapchains) {
        if (swapchain.handle != XR_NULL_HANDLE) {
            xrOk(xrDestroySwapchain(swapchain.handle), "xrDestroySwapchain");
        }
    }
    g.swapchains.clear();
    if (g.viewSpace != XR_NULL_HANDLE) {
        xrOk(xrDestroySpace(g.viewSpace), "xrDestroySpace(VIEW)");
    }
    if (g.localSpace != XR_NULL_HANDLE) {
        xrOk(xrDestroySpace(g.localSpace), "xrDestroySpace(LOCAL)");
    }
    if (g.session != XR_NULL_HANDLE) {
        xrOk(xrDestroySession(g.session), "xrDestroySession");
    }
    if (g.instance != XR_NULL_HANDLE) {
        const XrResult result = xrDestroyInstance(g.instance);
        LOGI("xrDestroyInstance -> %d", result);
    }
    g.localSpace = XR_NULL_HANDLE;
    g.viewSpace = XR_NULL_HANDLE;
    g.session = XR_NULL_HANDLE;
    g.instance = XR_NULL_HANDLE;

    if (env != nullptr && g.activity != nullptr) {
        env->DeleteGlobalRef(g.activity);
    }
    JavaVM* vm = g.vm;
    g = {};
    g.vm = vm;
}

bool initializeOpenXr(JNIEnv* env, jobject activity) {
    if (g.initialized) {
        return true;
    }

    const jint vmResult = env->GetJavaVM(&g.vm);
    if (vmResult != JNI_OK || g.vm == nullptr) {
        LOGE("GetJavaVM failed: result=%d vm=%p", vmResult, g.vm);
        return false;
    }
    g.activity = env->NewGlobalRef(activity);
    if (g.activity == nullptr) {
        LOGE("NewGlobalRef(Activity) failed");
        return false;
    }
    LOGI("OpenXR initialization entry tid=%ld JavaVM=%p Activity(global)=%p",
         QuestVrDiagnostics::currentThreadId(), g.vm, g.activity);
    logCurrentEglState("nativeInitialize-entry");

    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    XrResult result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                            reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader));
    LOGI("xrGetInstanceProcAddr(xrInitializeLoaderKHR) -> %d function=%p", result,
         reinterpret_cast<void*>(initializeLoader));
    if (XR_SUCCEEDED(result) && initializeLoader != nullptr) {
        XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loaderInfo.applicationVM = g.vm;
        loaderInfo.applicationContext = g.activity;
        if (!xrOk(initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)),
                "xrInitializeLoaderKHR")) {
            return false;
        }
    } else {
        LOGW("xrInitializeLoaderKHR is unavailable; continuing for loader compatibility as PPSSPP does");
    }

    if (!validateRequiredInstanceExtensions()) {
        return false;
    }

    const std::array<const char*, 2> extensions{
        XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
        XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
    };
    XrInstanceCreateInfoAndroidKHR androidInfo{XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    androidInfo.applicationVM = g.vm;
    androidInfo.applicationActivity = g.activity;

    XrInstanceCreateInfo instanceInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.next = &androidInfo;
    std::strncpy(instanceInfo.applicationInfo.applicationName, "Mupen64Plus AE Quest VR",
                 XR_MAX_APPLICATION_NAME_SIZE - 1);
    instanceInfo.applicationInfo.applicationVersion = 1;
    std::strncpy(instanceInfo.applicationInfo.engineName, "Mupen64Plus-AE",
                 XR_MAX_ENGINE_NAME_SIZE - 1);
    instanceInfo.applicationInfo.engineVersion = 1;
    instanceInfo.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    instanceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    instanceInfo.enabledExtensionNames = extensions.data();
    if (!xrOk(xrCreateInstance(&instanceInfo, &g.instance), "xrCreateInstance")) {
        return false;
    }

    XrInstanceProperties properties{XR_TYPE_INSTANCE_PROPERTIES};
    if (xrOk(xrGetInstanceProperties(g.instance, &properties), "xrGetInstanceProperties")) {
        LOGI("OpenXR runtime: %s %u.%u.%u", properties.runtimeName,
             XR_VERSION_MAJOR(properties.runtimeVersion), XR_VERSION_MINOR(properties.runtimeVersion),
             XR_VERSION_PATCH(properties.runtimeVersion));
    }

    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO};
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    if (!xrOk(xrGetSystem(g.instance, &systemInfo, &g.systemId), "xrGetSystem")) {
        return false;
    }
    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
    if (!xrOk(xrGetSystemProperties(g.instance, g.systemId, &systemProperties),
              "xrGetSystemProperties")) {
        return false;
    }
    LOGI("OpenXR system id=%llu name=%s vendor=%u maxLayerCount=%u maxSwapchain=%ux%u "
         "trackingOrientation=%d trackingPosition=%d",
         static_cast<unsigned long long>(g.systemId), systemProperties.systemName,
         systemProperties.vendorId, systemProperties.graphicsProperties.maxLayerCount,
         systemProperties.graphicsProperties.maxSwapchainImageWidth,
         systemProperties.graphicsProperties.maxSwapchainImageHeight,
         systemProperties.trackingProperties.orientationTracking ? 1 : 0,
         systemProperties.trackingProperties.positionTracking ? 1 : 0);
    if (!selectEnvironmentBlendMode()) {
        return false;
    }

    PFN_xrGetOpenGLESGraphicsRequirementsKHR getRequirements = nullptr;
    if (!xrOk(xrGetInstanceProcAddr(g.instance, "xrGetOpenGLESGraphicsRequirementsKHR",
            reinterpret_cast<PFN_xrVoidFunction*>(&getRequirements)),
            "xrGetInstanceProcAddr(xrGetOpenGLESGraphicsRequirementsKHR)")) {
        return false;
    }
    XrGraphicsRequirementsOpenGLESKHR requirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (!xrOk(getRequirements(g.instance, g.systemId, &requirements), "xrGetOpenGLESGraphicsRequirementsKHR")) {
        return false;
    }
    LOGI("OpenXR GLES requirements min=%u.%u.%u max=%u.%u.%u",
         XR_VERSION_MAJOR(requirements.minApiVersionSupported),
         XR_VERSION_MINOR(requirements.minApiVersionSupported),
         XR_VERSION_PATCH(requirements.minApiVersionSupported),
         XR_VERSION_MAJOR(requirements.maxApiVersionSupported),
         XR_VERSION_MINOR(requirements.maxApiVersionSupported),
         XR_VERSION_PATCH(requirements.maxApiVersionSupported));

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);
    const XrVersion currentVersion = XR_MAKE_VERSION(major, minor, 0);
    if (currentVersion < requirements.minApiVersionSupported || currentVersion > requirements.maxApiVersionSupported) {
        LOGE("Current GLES %d.%d is outside runtime range %u.%u - %u.%u", major, minor,
             XR_VERSION_MAJOR(requirements.minApiVersionSupported), XR_VERSION_MINOR(requirements.minApiVersionSupported),
             XR_VERSION_MAJOR(requirements.maxApiVersionSupported), XR_VERSION_MINOR(requirements.maxApiVersionSupported));
        return false;
    }

    XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    graphicsBinding.display = eglGetCurrentDisplay();
    graphicsBinding.context = eglGetCurrentContext();
    if (graphicsBinding.display == EGL_NO_DISPLAY || graphicsBinding.context == EGL_NO_CONTEXT) {
        LOGE("No current EGL display/context while creating OpenXR session");
        return false;
    }
    EGLint configId = 0;
    if (eglQueryContext(graphicsBinding.display, graphicsBinding.context, EGL_CONFIG_ID, &configId) != EGL_TRUE) {
        LOGE("Unable to query current EGL context configuration: 0x%x", eglGetError());
        return false;
    }
    const EGLint configAttributes[] = {EGL_CONFIG_ID, configId, EGL_NONE};
    EGLint configCount = 0;
    if (eglChooseConfig(graphicsBinding.display, configAttributes, &graphicsBinding.config, 1,
            &configCount) != EGL_TRUE || configCount != 1) {
        LOGE("Unable to resolve current EGLConfig %d: 0x%x", configId, eglGetError());
        return false;
    }
    g.graphicsDisplay = graphicsBinding.display;
    g.graphicsConfig = graphicsBinding.config;
    g.graphicsContext = graphicsBinding.context;
    logCurrentEglState("pre-xrCreateSession");
    LOGI("OpenXR GLES binding display=%p config=%p configId=%d context=%p currentThread=%ld",
         g.graphicsDisplay, g.graphicsConfig, configId, g.graphicsContext,
         QuestVrDiagnostics::currentThreadId());

    XrSessionCreateInfo sessionInfo{XR_TYPE_SESSION_CREATE_INFO};
    sessionInfo.next = &graphicsBinding;
    sessionInfo.systemId = g.systemId;
    if (!xrOk(xrCreateSession(g.instance, &sessionInfo, &g.session), "xrCreateSession")) {
        return false;
    }

    if (!createControllerActions()) {
        LOGW("OpenXR Touch action setup failed; paired Android controllers remain available");
    }

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    setIdentity(spaceInfo.poseInReferenceSpace);
    if (!xrOk(xrCreateReferenceSpace(g.session, &spaceInfo, &g.localSpace), "xrCreateReferenceSpace")) {
        return false;
    }
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
    if (!xrOk(xrCreateReferenceSpace(g.session, &spaceInfo, &g.viewSpace),
              "xrCreateReferenceSpace(VIEW)")) {
        return false;
    }

    if (!createSwapchains() || !createGlResources()) {
        return false;
    }

    g.initialized = true;
    LOGI("OpenXR presentation bridge initialized on GLES %d.%d; native presentation mode is "
         "selected by configuration before the first submitted frame (proof layers debug-only)",
         major, minor);
    return true;
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeInitialize(
        JNIEnv* env, jclass, jobject activity) {
    if (!initializeOpenXr(env, activity)) {
        LOGE("OpenXR initialization failed; normal Android presentation remains available");
        destroyState(env);
        return JNI_FALSE;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeSetSourceTexture(
        JNIEnv*, jclass, jint texture, jint width, jint height, jboolean requestStereo,
        jfloat contentAspect) {
    g.sourceTexture = static_cast<GLuint>(texture);
    g.sourceWidth = width;
    g.sourceHeight = height;
    g.sourceContentAspect = std::isfinite(contentAspect)
        ? std::clamp(static_cast<float>(contentAspect), 0.5f, 3.0f)
        : 4.0f / 3.0f;
    g.immersiveScaleLogged = false;
    g.stereoRequested = requestStereo == JNI_TRUE;
    resolveRendererBridge();
    g.stereoSourceActive = g.setVrEnabled != nullptr && g.stereoRequested &&
                           g.configurationStereoEnabled;
    LOGI("Source texture update tid=%ld texture=%d total=%dx%d perEye=%dx%d "
         "contentAspect=%.4f "
         "requestStereo=%d stereoActive=%d blitReady=%d currentContext=%p",
         QuestVrDiagnostics::currentThreadId(), texture, width, height,
         requestStereo == JNI_TRUE ? width / 2 : width, height,
         g.sourceContentAspect, requestStereo == JNI_TRUE ? 1 : 0,
         g.stereoSourceActive ? 1 : 0,
         g.sourceBlitReady ? 1 : 0, eglGetCurrentContext());
    if (texture == 0) {
        g.sourceViewsValid = false;
        g.sourceViewDisplayTime = 0;
        g.sourceTextureTimestamp = 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeOnSourceFrameLatched(
        JNIEnv* env, jclass, jlong textureTimestamp, jfloatArray surfaceTransform) {
    ++g.sourceFrameLatchCalls;
    const int64_t previousTimestamp = g.sourceTextureTimestamp;
    updateSourceTransform(env, surfaceTransform);
    resolveRendererBridge();
    associateLatchedSourceFrame(static_cast<int64_t>(textureTimestamp));
    if (textureTimestamp > 0 && textureTimestamp != previousTimestamp) {
        ++g.newSourceFrameCount;
    }
    if (g.sourceFrameLatchCalls <= 10 || g.sourceFrameLatchCalls % 300 == 0) {
        LOGI("SurfaceTexture latch call=%u timestamp=%lld previous=%lld newFrames=%u "
             "sourceTexture=%u poseMatched=%d tid=%ld",
             g.sourceFrameLatchCalls, static_cast<long long>(textureTimestamp),
             static_cast<long long>(previousTimestamp), g.newSourceFrameCount, g.sourceTexture,
             g.sourceViewsValid ? 1 : 0, QuestVrDiagnostics::currentThreadId());
    }
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeCaptureSourceFrame(
        JNIEnv* env, jclass) {
    if (!g.sourceBlitReady || g.program == 0 || g.vertexArray == 0 ||
            g.sourceTexture == 0 || g.sourceWidth <= 0 || g.sourceHeight <= 0 ||
            !validateFrameEglBinding()) {
        LOGW("Pre-OpenXR source capture unavailable: ready=%d program=%u vao=%u "
             "source=%u size=%dx%d", g.sourceBlitReady ? 1 : 0, g.program,
             g.vertexArray, g.sourceTexture, g.sourceWidth, g.sourceHeight);
        return nullptr;
    }

    while (glGetError() != GL_NO_ERROR) {
        // Ensure this one-shot diagnostic reports its own GL work only.
    }

    GLint maximumTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximumTextureSize);
    const uint64_t byteCount64 = static_cast<uint64_t>(g.sourceWidth) *
                                 static_cast<uint64_t>(g.sourceHeight) * 4U;
    if (g.sourceWidth > maximumTextureSize || g.sourceHeight > maximumTextureSize ||
            byteCount64 == 0 ||
            byteCount64 > static_cast<uint64_t>(std::numeric_limits<jsize>::max())) {
        LOGE("Pre-OpenXR source capture dimensions rejected: %dx%d bytes=%llu maxTexture=%d",
             g.sourceWidth, g.sourceHeight,
             static_cast<unsigned long long>(byteCount64), maximumTextureSize);
        return nullptr;
    }

    GLint previousDrawFramebuffer = 0;
    GLint previousReadFramebuffer = 0;
    GLint previousProgram = 0;
    GLint previousVertexArray = 0;
    GLint previousActiveTexture = 0;
    GLint previousExternalTexture = 0;
    GLint previousTexture2D = 0;
    GLint previousPackAlignment = 4;
    GLint previousViewport[4]{};
    GLfloat previousClearColor[4]{};
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
    glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVertexArray);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &previousActiveTexture);
    glGetIntegerv(GL_VIEWPORT, previousViewport);
    glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, previousClearColor);
    const GLboolean depthEnabled = glIsEnabled(GL_DEPTH_TEST);
    const GLboolean cullEnabled = glIsEnabled(GL_CULL_FACE);
    const GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    const GLboolean scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);

    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_EXTERNAL_OES, &previousExternalTexture);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture2D);

    GLuint captureTexture = 0;
    GLuint captureFramebuffer = 0;
    glGenTextures(1, &captureTexture);
    glBindTexture(GL_TEXTURE_2D, captureTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, g.sourceWidth, g.sourceHeight, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glGenFramebuffers(1, &captureFramebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, captureFramebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           captureTexture, 0);

    bool success = captureTexture != 0 && captureFramebuffer != 0 &&
                   glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    std::vector<uint8_t> pixels;
    if (success) {
        pixels.resize(static_cast<size_t>(byteCount64));
        glViewport(0, 0, g.sourceWidth, g.sourceHeight);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glUseProgram(g.program);
        glBindVertexArray(g.vertexArray);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, g.sourceTexture);
        glUniform1i(g.sourceUniform, 0);
        glUniform1i(g.calibrationModeUniform, 0);
        glUniform1i(g.calibrationEyeUniform, 0);
        glUniform1f(g.targetAspectUniform, g.sourceHeight > 0
            ? g.sourceWidth / static_cast<float>(g.sourceHeight) : 1.0f);
        glUniform4f(g.uvTransformUniform, 1.0f, 1.0f, 0.0f, 0.0f);
        glUniform2f(g.quadScaleUniform, 1.0f, 1.0f);
        glUniformMatrix4fv(g.surfaceTransformUniform, 1, GL_FALSE,
                           g.sourceTransform.data());
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, g.sourceWidth, g.sourceHeight, GL_RGBA,
                     GL_UNSIGNED_BYTE, pixels.data());
        success = glGetError() == GL_NO_ERROR;
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
    if (captureFramebuffer != 0)
        glDeleteFramebuffers(1, &captureFramebuffer);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture2D));
    if (captureTexture != 0)
        glDeleteTextures(1, &captureTexture);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES,
                  static_cast<GLuint>(previousExternalTexture));
    glUseProgram(static_cast<GLuint>(previousProgram));
    glBindVertexArray(static_cast<GLuint>(previousVertexArray));
    glActiveTexture(static_cast<GLenum>(previousActiveTexture));
    glViewport(previousViewport[0], previousViewport[1], previousViewport[2],
               previousViewport[3]);
    glClearColor(previousClearColor[0], previousClearColor[1],
                 previousClearColor[2], previousClearColor[3]);
    glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
    if (depthEnabled) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (cullEnabled) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
    if (blendEnabled) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    if (scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);

    if (!success) {
        LOGE("Pre-OpenXR source capture failed: source=%u size=%dx%d captureFbo=%u "
             "captureTexture=%u", g.sourceTexture, g.sourceWidth, g.sourceHeight,
             captureFramebuffer, captureTexture);
        return nullptr;
    }

    jbyteArray result = env->NewByteArray(static_cast<jsize>(pixels.size()));
    if (result == nullptr)
        return nullptr;
    env->SetByteArrayRegion(result, 0, static_cast<jsize>(pixels.size()),
                            reinterpret_cast<const jbyte*>(pixels.data()));
    if (env->ExceptionCheck())
        return nullptr;
    LOGI("Captured complete pre-OpenXR source texture=%u packed=%dx%d perEye=%dx%d "
         "bytes=%zu SurfaceTextureTransformChanges=%u", g.sourceTexture,
         g.sourceWidth, g.sourceHeight,
         g.stereoSourceActive ? g.sourceWidth / 2 : g.sourceWidth, g.sourceHeight,
         pixels.size(), g.sourceTransformChanges);
    logRendererUiStats("SOURCE_CAPTURE");
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeConfigure(
        JNIEnv*, jclass, jboolean stereoEnabled, jboolean swapEyes, jfloat ipdMeters,
        jint presentationMode, jfloat immersiveViewScale,
        jfloat screenScale, jfloat screenDistanceMeters, jboolean startupProofLayers,
        jfloat worldUnitsPerMeter, jfloat rotationStrength, jboolean positionEnabled,
        jfloat maxTranslationMeters, jfloat cameraOffsetX, jfloat cameraOffsetY,
        jfloat cameraOffsetZ, jboolean useOpenXrFov, jboolean marioKartProfileEnabled,
        jfloat marioKartCameraOffsetY, jfloat marioKartCameraOffsetZ,
        jboolean touchControllerEnabled, jboolean debugLogging) {
    g.configurationStereoEnabled = stereoEnabled == JNI_TRUE;
    g.configurationSwapEyes = swapEyes == JNI_TRUE;
    g.configurationIpdMeters = ipdMeters;
    g.configurationPresentationMode = presentationMode ==
            static_cast<jint>(PresentationMode::CinemaScreen)
        ? PresentationMode::CinemaScreen : PresentationMode::ImmersiveProjection;
    g.configurationImmersiveViewScale = std::isfinite(immersiveViewScale)
        ? std::clamp(static_cast<float>(immersiveViewScale), 0.50f, 1.00f) : 1.0f;
    g.immersiveScaleLogged = false;
    g.configurationScreenScale = std::isfinite(screenScale)
        ? std::clamp(static_cast<float>(screenScale), 0.35f, 2.0f) : 1.0f;
    g.configurationScreenDistanceMeters = std::isfinite(screenDistanceMeters)
        ? std::clamp(static_cast<float>(screenDistanceMeters), 0.75f, 6.0f) : 2.0f;
    g.configurationStartupProofLayers = startupProofLayers == JNI_TRUE;
    g.screenRecenterPending = true;
    g.configurationWorldUnitsPerMeter = worldUnitsPerMeter;
    g.configurationRotationStrength = rotationStrength;
    g.configurationPositionEnabled = positionEnabled == JNI_TRUE;
    g.configurationMaxTranslationMeters = maxTranslationMeters;
    g.configurationCameraOffsetX = cameraOffsetX;
    g.configurationCameraOffsetY = cameraOffsetY;
    g.configurationCameraOffsetZ = cameraOffsetZ;
    g.configurationUseOpenXrFov = useOpenXrFov == JNI_TRUE;
    g.configurationMarioKartProfileEnabled = marioKartProfileEnabled == JNI_TRUE;
    g.configurationMarioKartCameraOffsetY = marioKartCameraOffsetY;
    g.configurationMarioKartCameraOffsetZ = marioKartCameraOffsetZ;
    g.touchControllerEnabled = touchControllerEnabled == JNI_TRUE;
    g.debugLogging = debugLogging == JNI_TRUE;
    LOGI("Quest VR configuration presentation=%s immersiveScale=%.3f stereo=%d "
         "swapEyes=%d ipd=%.4f screenScale=%.3f "
         "screenDistance=%.3fm proofLayers=%d worldUnits=%.2f "
         "rotationUser=%.2f rotationLateClip=%.2f "
         "position=%d maxTranslation=%.3f useOpenXrFov=%d marioKartProfile=%d "
         "marioKartOffsetYZ=[%.3f %.3f] sourceBlitReady=%d debug=%d",
         presentationModeName(g.configurationPresentationMode),
         g.configurationImmersiveViewScale,
         g.configurationStereoEnabled ? 1 : 0, g.configurationSwapEyes ? 1 : 0,
         g.configurationIpdMeters, g.configurationScreenScale,
         g.configurationScreenDistanceMeters,
         g.configurationStartupProofLayers ? 1 : 0, g.configurationWorldUnitsPerMeter,
         effectiveRotationStrength(), -effectiveRotationStrength(),
         effectivePositionEnabled() ? 1 : 0,
         g.configurationMaxTranslationMeters, effectiveUseOpenXrFov() ? 1 : 0,
         g.configurationMarioKartProfileEnabled ? 1 : 0,
         g.configurationMarioKartCameraOffsetY, g.configurationMarioKartCameraOffsetZ,
         g.sourceBlitReady ? 1 : 0,
         g.debugLogging ? 1 : 0);
    if (g.configureVr != nullptr) {
        g.configureVr(g.configurationStereoEnabled ? 1 : 0, g.configurationIpdMeters,
                      g.configurationWorldUnitsPerMeter, effectiveRotationStrength(),
                      effectivePositionEnabled() ? 1 : 0, g.configurationMaxTranslationMeters,
                      g.configurationCameraOffsetX, g.configurationCameraOffsetY,
                      g.configurationCameraOffsetZ, effectiveUseOpenXrFov() ? 1 : 0,
                      g.configurationMarioKartProfileEnabled ? 1 : 0,
                      g.configurationMarioKartCameraOffsetY,
                      g.configurationMarioKartCameraOffsetZ);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeRecenter(
        JNIEnv*, jclass) {
    g.screenRecenterPending = true;
    resolveRendererBridge();
    if (g.recenterVr != nullptr) {
        g.recenterVr();
    }
    LOGI("Requested presentation recenter mode=%s; cinema reanchors in LOCAL and "
         "GLideN64 resets its pose origin",
         presentationModeName(g.configurationPresentationMode));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeRenderFrame(
        JNIEnv*, jclass) {
    ++g.frameLoopCallCount;
    if (!g.initialized || g.exitRequested) {
        return JNI_FALSE;
    }

    pollEvents();
    if (!g.sessionRunning) {
        ++g.sessionWaitPollCount;
        if (g.sessionWaitPollCount == 1 || g.sessionWaitPollCount % 100 == 0) {
            LOGW("Waiting for a runnable OpenXR session: state=%s (%d), polls=%u",
                 sessionStateName(g.sessionState), static_cast<int>(g.sessionState),
                 g.sessionWaitPollCount);
        }
        return JNI_FALSE;
    }

    ++g.begunFrameCount;
    if (!validateFrameEglBinding()) {
        g.exitRequested = true;
        return JNI_FALSE;
    }

    syncControllerInput();

    using Clock = std::chrono::steady_clock;
    const Clock::time_point nativeFrameStart = Clock::now();
    const Clock::time_point waitFrameStart = Clock::now();
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (!xrFrameOk(xrWaitFrame(g.session, &waitInfo, &frameState), "xrWaitFrame")) {
        g.exitRequested = true;
        return JNI_FALSE;
    }
    const Clock::time_point waitFrameEnd = Clock::now();
    if (shouldLogDetailedFrame()) {
        LOGI("frame=%u wait complete predictedDisplayTime=%lld predictedPeriod=%lld "
             "shouldRender=%d sessionState=%s",
             g.begunFrameCount, static_cast<long long>(frameState.predictedDisplayTime),
             static_cast<long long>(frameState.predictedDisplayPeriod),
             frameState.shouldRender == XR_TRUE ? 1 : 0, sessionStateName(g.sessionState));
    }
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (!xrFrameOk(xrBeginFrame(g.session, &beginInfo), "xrBeginFrame")) {
        g.exitRequested = true;
        return JNI_FALSE;
    }

    std::vector<XrCompositionLayerProjectionView> layerViews;
    XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    XrCompositionLayerQuad diagnosticQuad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    std::array<XrCompositionLayerQuad, 2> gameQuads{};
    for (XrCompositionLayerQuad& quad : gameQuads) {
        quad.type = XR_TYPE_COMPOSITION_LAYER_QUAD;
    }
    std::array<const XrCompositionLayerBaseHeader*, 2> layers{};
    uint32_t layerCount = 0;
    RenderContent content = RenderContent::HeadLockedDiagnostic;

    if (frameState.shouldRender == XR_TRUE) {
        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = frameState.predictedDisplayTime;
        locateInfo.space = g.localSpace;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 0;
        const XrResult locateResult = xrLocateViews(g.session, &locateInfo, &viewState,
                static_cast<uint32_t>(g.views.size()), &viewCount, g.views.data());
        xrFrameOk(locateResult, "xrLocateViews");
        const bool validPose = XR_SUCCEEDED(locateResult) && viewCount == g.views.size() &&
            (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0 &&
            (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
        if (validPose) {
            publishHeadPose(frameState.predictedDisplayTime, viewCount);
            if (g.configurationPresentationMode == PresentationMode::CinemaScreen &&
                    (g.screenRecenterPending || !g.screenPoseValid)) {
                anchorScreenToCurrentView(viewCount);
            } else if (g.configurationPresentationMode ==
                    PresentationMode::ImmersiveProjection) {
                // Recenter still resets GLideN64's pose origin. Immersive projection has no
                // separate world-space screen anchor to update.
                g.screenRecenterPending = false;
            }
            if (shouldLogDetailedFrame()) {
                for (uint32_t eye = 0; eye < viewCount; ++eye) {
                    const XrView& view = g.views[eye];
                    LOGI("frame=%u located eye=%u pose=[q %.4f %.4f %.4f %.4f, p %.4f %.4f %.4f] "
                         "fov=[L %.4f R %.4f U %.4f D %.4f] flags=0x%llx",
                         g.begunFrameCount, eye, view.pose.orientation.x,
                         view.pose.orientation.y, view.pose.orientation.z,
                         view.pose.orientation.w, view.pose.position.x, view.pose.position.y,
                         view.pose.position.z, view.fov.angleLeft, view.fov.angleRight,
                         view.fov.angleUp, view.fov.angleDown,
                         static_cast<unsigned long long>(viewState.viewStateFlags));
                }
            }
        } else {
            ++g.locateFallbackCount;
            if (g.locateFallbackCount <= 10 || g.locateFallbackCount % 300 == 0) {
                LOGW("Tracked views unavailable (result=%s count=%u flags=0x%llx); "
                     "presentation=%s will use its last valid/fallback pose",
                     xrResultName(locateResult), viewCount,
                     static_cast<unsigned long long>(viewState.viewStateFlags),
                     presentationModeName(g.configurationPresentationMode));
            }
        }

        std::array<XrView, 2> fallbackViews{};
        if (!validPose) {
            const float halfIpd = std::clamp(g.configurationIpdMeters, 0.04f, 0.08f) * 0.5f;
            for (uint32_t eye = 0; eye < fallbackViews.size(); ++eye) {
                XrView& fallback = fallbackViews[eye];
                fallback.type = XR_TYPE_VIEW;
                setIdentity(fallback.pose);
                fallback.pose.position.x = eye == 0 ? -halfIpd : halfIpd;
                fallback.fov.angleLeft = -0.7853982f;
                fallback.fov.angleRight = 0.7853982f;
                fallback.fov.angleUp = 0.7853982f;
                fallback.fov.angleDown = -0.7853982f;
            }
        }

        const bool headLockedProof = g.configurationStartupProofLayers &&
                g.visibleLayerSubmissionCount < STARTUP_QUAD_VISIBLE_LAYER_COUNT;
        const bool projectionProof = g.configurationStartupProofLayers &&
                g.visibleLayerSubmissionCount >=
                    STARTUP_QUAD_VISIBLE_LAYER_COUNT &&
                g.visibleLayerSubmissionCount < STARTUP_QUAD_VISIBLE_LAYER_COUNT +
                    STARTUP_PROJECTION_VISIBLE_LAYER_COUNT;

        if (headLockedProof) {
            content = RenderContent::HeadLockedDiagnostic;
            if (acquireRenderRelease(0, content, PresentationMode::CinemaScreen)) {
                const Swapchain& swapchain = g.swapchains[0];
                diagnosticQuad.layerFlags = 0;
                diagnosticQuad.space = g.viewSpace;
                diagnosticQuad.eyeVisibility = XR_EYE_VISIBILITY_BOTH;
                diagnosticQuad.subImage.swapchain = swapchain.handle;
                diagnosticQuad.subImage.imageRect.offset = {0, 0};
                diagnosticQuad.subImage.imageRect.extent =
                        {swapchain.width, swapchain.height};
                diagnosticQuad.subImage.imageArrayIndex = 0;
                setIdentity(diagnosticQuad.pose);
                diagnosticQuad.pose.position.z = -1.5f;
                diagnosticQuad.size = {1.60f, 0.90f};
                layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                        &diagnosticQuad);
                layerCount = 1;
                if (shouldLogDetailedFrame()) {
                    LOGI("frame=%u layer=QUAD space=%p eye=BOTH swapchain=%p "
                         "rect=[0,0 %dx%d] array=0 pose=[identity, 0 0 -1.5] size=1.60x0.90",
                         g.begunFrameCount, reinterpret_cast<void*>(diagnosticQuad.space),
                         reinterpret_cast<void*>(swapchain.handle), swapchain.width,
                         swapchain.height);
                }
            }
        } else if (projectionProof) {
            content = RenderContent::StereoDiagnostic;
            const uint32_t compositionViewCount =
                    static_cast<uint32_t>(g.swapchains.size());
            layerViews.assign(compositionViewCount,
                              {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
            bool rendered = true;
            for (uint32_t eye = 0; eye < compositionViewCount; ++eye) {
                if (!acquireRenderRelease(eye, content,
                                          PresentationMode::ImmersiveProjection)) {
                    rendered = false;
                    break;
                }

                Swapchain& swapchain = g.swapchains[eye];
                XrCompositionLayerProjectionView& view = layerViews[eye];
                const XrView& renderedView = validPose ? g.views[eye] : fallbackViews[eye];
                view.pose = renderedView.pose;
                view.fov = renderedView.fov;
                view.subImage.swapchain = swapchain.handle;
                view.subImage.imageRect.offset = {0, 0};
                view.subImage.imageRect.extent = {swapchain.width, swapchain.height};
                view.subImage.imageArrayIndex = 0;
                if (shouldLogDetailedFrame()) {
                    LOGI("frame=%u projectionView=%u content=%s swapchain=%p "
                         "rect=[0,0 %dx%d] array=0 pose=[q %.4f %.4f %.4f %.4f, "
                         "p %.4f %.4f %.4f] fov=[L %.4f R %.4f U %.4f D %.4f]",
                         g.begunFrameCount, eye, renderContentName(content),
                         reinterpret_cast<void*>(swapchain.handle), swapchain.width,
                         swapchain.height, view.pose.orientation.x, view.pose.orientation.y,
                         view.pose.orientation.z, view.pose.orientation.w,
                         view.pose.position.x, view.pose.position.y, view.pose.position.z,
                         view.fov.angleLeft, view.fov.angleRight, view.fov.angleUp,
                         view.fov.angleDown);
                }
            }

            if (rendered) {
                projectionLayer.layerFlags = 0;
                projectionLayer.space = validPose ? g.localSpace : g.viewSpace;
                projectionLayer.viewCount = static_cast<uint32_t>(layerViews.size());
                projectionLayer.views = layerViews.data();
                layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                        &projectionLayer);
                layerCount = 1;
            }
        } else {
            const bool sourceReady = g.sourceBlitReady && g.sourceTexture != 0 &&
                                     g.newSourceFrameCount != 0;
            content = RenderContent::SourceTexture;
            if (!sourceReady &&
                    (g.begunFrameCount <= STARTUP_DETAILED_FRAME_COUNT ||
                     g.begunFrameCount % PERIODIC_DETAILED_FRAME_INTERVAL == 0)) {
                LOGW("%s waiting for emulator source: blitReady=%d texture=%u "
                     "latchCalls=%u newFrames=%u; submitting a black layer",
                     presentationModeName(g.configurationPresentationMode),
                     g.sourceBlitReady ? 1 : 0, g.sourceTexture, g.sourceFrameLatchCalls,
                     g.newSourceFrameCount);
            }
            if (g.configurationPresentationMode ==
                    PresentationMode::ImmersiveProjection) {
                const uint32_t compositionViewCount =
                        static_cast<uint32_t>(g.swapchains.size());
                layerViews.assign(compositionViewCount,
                                  {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
                const bool useMatchedSourcePose = sourceReady && g.stereoSourceActive &&
                                                  g.sourceViewsValid;
                bool rendered = true;
                for (uint32_t eye = 0; eye < compositionViewCount; ++eye) {
                    if (!acquireRenderRelease(eye, content,
                                              PresentationMode::ImmersiveProjection)) {
                        rendered = false;
                        break;
                    }

                    const uint32_t sourceEye = g.stereoSourceActive &&
                            g.configurationSwapEyes ? 1U - eye : eye;
                    const XrView& renderedView = useMatchedSourcePose
                            ? g.sourceViews[sourceEye]
                            : (validPose ? g.views[eye] : fallbackViews[eye]);
                    const char* poseSource = useMatchedSourcePose
                            ? "MATCHED_SOURCE_FRAME" : (validPose ? "CURRENT" : "FALLBACK");
                    const Swapchain& swapchain = g.swapchains[eye];
                    XrCompositionLayerProjectionView& view = layerViews[eye];
                    view.pose = renderedView.pose;
                    view.fov = renderedView.fov;
                    view.subImage.swapchain = swapchain.handle;
                    view.subImage.imageRect.offset = {0, 0};
                    view.subImage.imageRect.extent = {swapchain.width, swapchain.height};
                    view.subImage.imageArrayIndex = 0;
                    if (shouldLogDetailedFrame()) {
                        const double poseAgeMilliseconds = useMatchedSourcePose
                            ? static_cast<double>(frameState.predictedDisplayTime -
                                  g.sourceViewDisplayTime) / 1.0e6 : -1.0;
                        LOGI("frame=%u immersiveProjectionView=%u sourceEye=%u poseSource=%s "
                             "poseAge=%.2fms swapchain=%p rect=[0,0 %dx%d] array=0 "
                             "pose=[q %.4f %.4f %.4f %.4f, p %.4f %.4f %.4f] "
                             "fov=[L %.4f R %.4f U %.4f D %.4f] source=%dx%d "
                             "perEye=%dx%d ready=%d",
                             g.begunFrameCount, eye, sourceEye, poseSource,
                             poseAgeMilliseconds,
                             reinterpret_cast<void*>(swapchain.handle), swapchain.width,
                             swapchain.height, view.pose.orientation.x,
                             view.pose.orientation.y, view.pose.orientation.z,
                             view.pose.orientation.w, view.pose.position.x,
                             view.pose.position.y, view.pose.position.z,
                             view.fov.angleLeft, view.fov.angleRight, view.fov.angleUp,
                             view.fov.angleDown, g.sourceWidth, g.sourceHeight,
                             g.stereoSourceActive ? g.sourceWidth / 2 : g.sourceWidth,
                             g.sourceHeight, sourceReady ? 1 : 0);
                    }
                }
                if (rendered) {
                    projectionLayer.layerFlags = 0;
                    projectionLayer.space = (useMatchedSourcePose || validPose)
                            ? g.localSpace : g.viewSpace;
                    projectionLayer.viewCount = static_cast<uint32_t>(layerViews.size());
                    projectionLayer.views = layerViews.data();
                    layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                            &projectionLayer);
                    layerCount = 1;
                }
            } else {
                XrPosef screenPose = g.screenPose;
                if (!g.screenPoseValid) {
                    setIdentity(screenPose);
                    screenPose.position.z = -g.configurationScreenDistanceMeters;
                }
                const float contentAspect = std::clamp(g.sourceContentAspect, 0.5f, 3.0f);
                const float screenWidthMeters = 2.4f * g.configurationScreenScale;
                const float screenHeightMeters = screenWidthMeters / contentAspect;
                const bool stereoQuads = sourceReady && g.stereoSourceActive;
                const uint32_t quadCount = stereoQuads ? 2U : 1U;
                bool rendered = true;
                for (uint32_t layerIndex = 0; layerIndex < quadCount; ++layerIndex) {
                    const uint32_t eye = stereoQuads ? layerIndex : 0U;
                    if (!acquireRenderRelease(eye, content,
                                              PresentationMode::CinemaScreen)) {
                        rendered = false;
                        break;
                    }

                    const Swapchain& swapchain = g.swapchains[eye];
                    XrCompositionLayerQuad& quad = gameQuads[layerIndex];
                    quad.layerFlags = 0;
                    quad.space = g.localSpace;
                    quad.eyeVisibility = stereoQuads
                            ? (eye == 0 ? XR_EYE_VISIBILITY_LEFT : XR_EYE_VISIBILITY_RIGHT)
                            : XR_EYE_VISIBILITY_BOTH;
                    quad.subImage.swapchain = swapchain.handle;
                    quad.subImage.imageRect.offset = {0, 0};
                    quad.subImage.imageRect.extent = {swapchain.width, swapchain.height};
                    quad.subImage.imageArrayIndex = 0;
                    quad.pose = screenPose;
                    quad.size = {screenWidthMeters, screenHeightMeters};
                    layers[layerIndex] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(
                            &quad);

                    if (shouldLogDetailedFrame()) {
                        LOGI("frame=%u gameQuad=%u eyeVisibility=%s space=LOCAL swapchain=%p "
                             "rect=[0,0 %dx%d] array=0 size=%.3fx%.3fm aspect=%.4f "
                             "pose=[q %.4f %.4f %.4f %.4f, p %.4f %.4f %.4f] "
                             "source=%dx%d perEye=%dx%d ready=%d stereo=%d",
                             g.begunFrameCount, layerIndex,
                             stereoQuads ? (eye == 0 ? "LEFT" : "RIGHT") : "BOTH",
                             reinterpret_cast<void*>(swapchain.handle), swapchain.width,
                             swapchain.height, screenWidthMeters, screenHeightMeters,
                             contentAspect, screenPose.orientation.x,
                             screenPose.orientation.y, screenPose.orientation.z,
                             screenPose.orientation.w, screenPose.position.x,
                             screenPose.position.y, screenPose.position.z,
                             g.sourceWidth, g.sourceHeight,
                             stereoQuads ? g.sourceWidth / 2 : g.sourceWidth,
                             g.sourceHeight, sourceReady ? 1 : 0,
                             stereoQuads ? 1 : 0);
                    }
                }
                if (rendered) {
                    layerCount = quadCount;
                }
            }
        }
    } else {
        ++g.shouldRenderFalseCount;
        if (g.shouldRenderFalseCount <= 10 || g.shouldRenderFalseCount % 300 == 0) {
            LOGW("xrWaitFrame returned shouldRender=false in session state %s (%d), count=%u",
                 sessionStateName(g.sessionState), static_cast<int>(g.sessionState),
                 g.shouldRenderFalseCount);
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = g.environmentBlendMode;
    endInfo.layerCount = layerCount;
    endInfo.layers = layerCount == 0 ? nullptr : layers.data();
    if (shouldLogDetailedFrame()) {
        LOGI("frame=%u xrEndFrame input displayTime=%lld blendMode=%d layerCount=%u "
             "content=%s presentation=%s layerPtr=%p",
             g.begunFrameCount, static_cast<long long>(endInfo.displayTime),
             static_cast<int>(endInfo.environmentBlendMode), layerCount,
             renderContentName(content), presentationModeName(g.configurationPresentationMode),
             layerCount == 0 ? nullptr : layers[0]);
    }
    const bool submitted = xrFrameOk(xrEndFrame(g.session, &endInfo), "xrEndFrame");
    const Clock::time_point nativeFrameEnd = Clock::now();
    if (!submitted) {
        return JNI_FALSE;
    }

    const double waitMilliseconds =
        std::chrono::duration<double, std::milli>(waitFrameEnd - waitFrameStart).count();
    const double nativeMilliseconds =
        std::chrono::duration<double, std::milli>(nativeFrameEnd - nativeFrameStart).count();
    const double workMilliseconds = std::max(0.0, nativeMilliseconds - waitMilliseconds);
    ++g.submittedFrameCount;
    ++g.timingSampleCount;
    if (layerCount != 0) {
        ++g.submittedLayerCount;
        ++g.renderedLayerFrameCount;
        if (isSessionVisible()) {
            ++g.visibleLayerSubmissionCount;
            if (g.visibleLayerSubmissionCount == 1) {
                if (g.configurationStartupProofLayers) {
                    LOGI("First VISIBLE OpenXR layer submitted: debug green head-locked quad");
                } else {
                    LOGI("First VISIBLE OpenXR layer submitted: presentation=%s "
                         "(proof layers disabled)",
                         presentationModeName(g.configurationPresentationMode));
                }
            } else if (g.configurationStartupProofLayers &&
                    g.visibleLayerSubmissionCount == STARTUP_QUAD_VISIBLE_LAYER_COUNT) {
                LOGI("Green head-locked proof complete after %u visible layers; next frame starts "
                     "red/blue projection proof", g.visibleLayerSubmissionCount);
            } else if (g.configurationStartupProofLayers &&
                    g.visibleLayerSubmissionCount == STARTUP_QUAD_VISIBLE_LAYER_COUNT +
                    STARTUP_PROJECTION_VISIBLE_LAYER_COUNT) {
                LOGI("Projection proof complete after %u visible layers; next frame uses the "
                     "emulator SurfaceTexture when ready", g.visibleLayerSubmissionCount);
            }
        }
    }
    g.waitFrameMilliseconds += waitMilliseconds;
    g.nativeFrameMilliseconds += nativeMilliseconds;
    g.workMilliseconds += workMilliseconds;
    g.maximumWorkMilliseconds = std::max(g.maximumWorkMilliseconds, workMilliseconds);

    if (g.timingSampleCount >= PERIODIC_DETAILED_FRAME_INTERVAL) {
        const double divisor = static_cast<double>(g.timingSampleCount);
        const int eyeWidth = g.swapchains.empty() ? 0 : g.swapchains[0].width;
        const int eyeHeight = g.swapchains.empty() ? 0 : g.swapchains[0].height;
        const double sourcePoseAgeMilliseconds = g.sourceViewsValid
            ? static_cast<double>(frameState.predictedDisplayTime - g.sourceViewDisplayTime) / 1.0e6
            : -1.0;
        LOGI("periodic status samples=%u submitted=%u layers=%u visibleLayers=%u state=%s "
             "avgWait=%.2fms "
             "avgNative=%.2fms avgWork=%.2fms maxWork=%.2fms "
             "presentation=%s immersiveScale=%.3f sourceTotal=%dx%d sourcePerEye=%dx%d "
             "contentAspect=%.4f stereo=%d swapEyes=%d consumerEye=%dx%d "
             "screenScale=%.3f screenDistance=%.3fm screenAnchorValid=%d "
             "proofLayers=%d sourcePoseAge=%.2fms "
             "poseMatches=%u poseMisses=%u textureTs=%lld latchCalls=%u newSourceFrames=%u "
             "sourceBlitReady=%d",
             g.timingSampleCount, g.submittedFrameCount, g.renderedLayerFrameCount,
             g.visibleLayerSubmissionCount, sessionStateName(g.sessionState),
             g.waitFrameMilliseconds / divisor, g.nativeFrameMilliseconds / divisor,
             g.workMilliseconds / divisor, g.maximumWorkMilliseconds,
             presentationModeName(g.configurationPresentationMode),
             g.configurationImmersiveViewScale, g.sourceWidth, g.sourceHeight,
             g.stereoSourceActive ? g.sourceWidth / 2 : g.sourceWidth, g.sourceHeight,
             g.sourceContentAspect,
             g.stereoSourceActive ? 1 : 0, g.configurationSwapEyes ? 1 : 0,
             eyeWidth, eyeHeight, g.configurationScreenScale,
             g.configurationScreenDistanceMeters, g.screenPoseValid ? 1 : 0,
             g.configurationStartupProofLayers ? 1 : 0,
             sourcePoseAgeMilliseconds, g.sourcePoseMatches, g.sourcePoseMisses,
             static_cast<long long>(g.sourceTextureTimestamp), g.sourceFrameLatchCalls,
             g.newSourceFrameCount, g.sourceBlitReady ? 1 : 0);
        g.timingSampleCount = 0;
        g.renderedLayerFrameCount = 0;
        g.waitFrameMilliseconds = 0.0;
        g.nativeFrameMilliseconds = 0.0;
        g.workMilliseconds = 0.0;
        g.maximumWorkMilliseconds = 0.0;
    }
    return JNI_TRUE;
}

extern "C" JNIEXPORT jint JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeGetMenuInputState(
        JNIEnv*, jclass) {
    return static_cast<jint>(g.menuInputMask);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeIsExitRequested(
        JNIEnv*, jclass) {
    return g.exitRequested ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeIsStereoSourceActive(
        JNIEnv*, jclass) {
    return g.stereoSourceActive ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeShutdown(
        JNIEnv* env, jclass) {
    destroyState(env);
    LOGI("OpenXR presentation bridge shut down");
}
