#include <jni.h>
#include <android/log.h>
#include <dlfcn.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* TAG = "M64P-QuestVR";
constexpr uint32_t STARTUP_DIAGNOSTIC_LAYER_COUNT = 180;

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

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
using GetVrStatsFn = void (*)(uint32_t* geometryDraws, uint32_t* rectangleDraws,
                              uint32_t* eyeDraws, uint32_t* poseGeneration,
                              uint32_t* targetWidthFallbacks, uint32_t* lastTargetWidth);
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

    GLuint framebuffer{0};
    GLuint program{0};
    GLuint vertexArray{0};
    GLint sourceUniform{-1};
    GLint uvTransformUniform{-1};
    GLint quadScaleUniform{-1};

    GLuint sourceTexture{0};
    int sourceWidth{0};
    int sourceHeight{0};
    bool stereoRequested{false};
    bool stereoSourceActive{false};

    SetVrEnabledFn setVrEnabled{nullptr};
    SetVrPoseFn setVrPose{nullptr};
    SetVrViewsFn setVrViews{nullptr};
    ConfigureVrFn configureVr{nullptr};
    RecenterVrFn recenterVr{nullptr};
    GetVrStatsFn getVrStats{nullptr};
    GetPresentedPoseTimestampFn getPresentedPoseTimestamp{nullptr};
    SetVrInputFn setVrInput{nullptr};
    void* rendererLibraryHandle{nullptr};
    void* inputLibraryHandle{nullptr};
    bool rendererBridgeLogged{false};
    bool inputBridgeLogged{false};

    ControllerActions controllerActions;
    bool recenterChordDown{false};

    bool configurationStereoEnabled{true};
    bool configurationSwapEyes{false};
    float configurationIpdMeters{0.064f};
    float configurationWorldUnitsPerMeter{64.0f};
    float configurationRotationStrength{1.0f};
    bool configurationPositionEnabled{false};
    float configurationMaxTranslationMeters{0.15f};
    float configurationCameraOffsetX{0.0f};
    float configurationCameraOffsetY{0.0f};
    float configurationCameraOffsetZ{0.0f};
    bool configurationUseOpenXrFov{true};
    bool configurationMarioKartProfileEnabled{true};
    float configurationMarioKartCameraOffsetY{-0.20f};
    float configurationMarioKartCameraOffsetZ{-0.75f};
    bool touchControllerEnabled{true};
    bool debugLogging{false};
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
        LOGE("%s failed: %s", operation, xrResultName(result));
        return false;
    }
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
    static constexpr const char* vertexSource = R"glsl(#version 300 es
precision highp float;
out vec2 vUv;
uniform vec4 uUvTransform;
uniform vec2 uQuadScale;
const vec2 positions[6] = vec2[6](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2(-1.0,  1.0),
    vec2(-1.0,  1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0));
const vec2 texcoords[6] = vec2[6](
    vec2(0.0, 1.0), vec2(1.0, 1.0), vec2(0.0, 0.0),
    vec2(0.0, 0.0), vec2(1.0, 1.0), vec2(1.0, 0.0));
void main() {
    gl_Position = vec4(positions[gl_VertexID] * uQuadScale, 0.0, 1.0);
    vUv = texcoords[gl_VertexID] * uUvTransform.xy + uUvTransform.zw;
}
)glsl";

    static constexpr const char* fragmentSource = R"glsl(#version 300 es
#extension GL_OES_EGL_image_external_essl3 : require
precision mediump float;
in vec2 vUv;
layout(location = 0) out vec4 outColor;
uniform samplerExternalOES uSource;
void main() {
    outColor = texture(uSource, vUv);
}
)glsl";

    const GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    const GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vertexShader == 0 || fragmentShader == 0) {
        if (vertexShader != 0) glDeleteShader(vertexShader);
        if (fragmentShader != 0) glDeleteShader(fragmentShader);
        return false;
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
        return false;
    }

    g.sourceUniform = glGetUniformLocation(g.program, "uSource");
    g.uvTransformUniform = glGetUniformLocation(g.program, "uUvTransform");
    g.quadScaleUniform = glGetUniformLocation(g.program, "uQuadScale");
    glGenFramebuffers(1, &g.framebuffer);
    glGenVertexArrays(1, &g.vertexArray);
    return glGetError() == GL_NO_ERROR;
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
    g.getVrStats = reinterpret_cast<GetVrStatsFn>(dlsym(symbolScope, "M64PQuestVrGetStats"));
    g.getPresentedPoseTimestamp = reinterpret_cast<GetPresentedPoseTimestampFn>(
        dlsym(symbolScope, "M64PQuestVrGetPresentedPoseTimestamp"));

    const bool found = g.setVrEnabled != nullptr && g.setVrPose != nullptr;
    if (found) {
        // Keep the plugin mapped until destroyState has disabled VR. The core may unload its
        // reference first during activity/surface teardown, which would otherwise leave these
        // function pointers dangling.
        g.rendererLibraryHandle = videoPlugin;
        videoPlugin = nullptr;
        if (g.configureVr != nullptr) {
            g.configureVr(g.configurationStereoEnabled ? 1 : 0, g.configurationIpdMeters,
                          g.configurationWorldUnitsPerMeter, g.configurationRotationStrength,
                          g.configurationPositionEnabled ? 1 : 0, g.configurationMaxTranslationMeters,
                          g.configurationCameraOffsetX, g.configurationCameraOffsetY,
                          g.configurationCameraOffsetZ, g.configurationUseOpenXrFov ? 1 : 0,
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
    if (g.setVrInput == nullptr) {
        return;
    }
    if (!g.touchControllerEnabled || g.controllerActions.actionSet == XR_NULL_HANDLE) {
        g.setVrInput(0, 0, 0.0f, 0.0f);
        return;
    }

    const XrActiveActionSet activeSet{g.controllerActions.actionSet, XR_NULL_PATH};
    XrActionsSyncInfo syncInfo{XR_TYPE_ACTIONS_SYNC_INFO};
    syncInfo.countActiveActionSets = 1;
    syncInfo.activeActionSets = &activeSet;
    if (XR_FAILED(xrSyncActions(g.session, &syncInfo))) {
        g.setVrInput(0, 0, 0.0f, 0.0f);
        return;
    }

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
    if (readBooleanAction(g.controllerActions.buttonA)) buttons |= aButton;
    if (readBooleanAction(g.controllerActions.buttonB)) buttons |= bButton;
    if (readBooleanAction(g.controllerActions.buttonX)) buttons |= dpadLeft;
    if (readBooleanAction(g.controllerActions.buttonY)) buttons |= dpadUp;
    if (readFloatAction(g.controllerActions.leftTrigger) > 0.45f ||
        readFloatAction(g.controllerActions.rightTrigger) > 0.45f) buttons |= zTrigger;
    if (readFloatAction(g.controllerActions.leftSqueeze) > 0.45f) buttons |= lTrigger;
    if (readFloatAction(g.controllerActions.rightSqueeze) > 0.45f) buttons |= rTrigger;

    const XrVector2f rightStick = applyStickDeadzone(readVectorAction(g.controllerActions.rightStick));
    if (rightStick.x > 0.45f) buttons |= cRight;
    if (rightStick.x < -0.45f) buttons |= cLeft;
    if (rightStick.y > 0.45f) buttons |= cUp;
    if (rightStick.y < -0.45f) buttons |= cDown;

    const bool leftClick = readBooleanAction(g.controllerActions.leftStickClick);
    const bool rightClick = readBooleanAction(g.controllerActions.rightStickClick);
    const bool recenterChord = leftClick && rightClick;
    if (recenterChord && !g.recenterChordDown) {
        resolveRendererBridge();
        if (g.recenterVr != nullptr) {
            g.recenterVr();
            LOGI("Recentered VR view from Touch thumbstick chord");
        }
    } else if (rightClick && !leftClick) {
        buttons |= start;
    }
    g.recenterChordDown = recenterChord;

    const XrVector2f leftStick = applyStickDeadzone(readVectorAction(g.controllerActions.leftStick));
    g.setVrInput(1, buttons, leftStick.x, leftStick.y);
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
    if (g.debugLogging && g.posePublicationCount % 300 == 0) {
        uint32_t geometryDraws = 0;
        uint32_t rectangleDraws = 0;
        uint32_t eyeDraws = 0;
        uint32_t poseGeneration = 0;
        uint32_t targetWidthFallbacks = 0;
        uint32_t lastTargetWidth = 0;
        if (g.getVrStats != nullptr) {
            g.getVrStats(&geometryDraws, &rectangleDraws, &eyeDraws, &poseGeneration,
                         &targetWidthFallbacks, &lastTargetWidth);
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
             "draws geometry=%u rect=%u eyes=%u poseGeneration=%u "
             "targetWidth=%u targetFallbacks=%u",
             pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
             position.x, position.y, position.z, runtimeIpd,
             g.views[0].fov.angleLeft, g.views[0].fov.angleRight,
             g.views[0].fov.angleUp, g.views[0].fov.angleDown,
             geometryDraws, rectangleDraws, eyeDraws, poseGeneration,
             lastTargetWidth, targetWidthFallbacks);
    }
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
    // GL_RGBA8 is the most conservative compositor format for the first Quest bring-up. Keep
    // sRGB available, but do not make correct sRGB sampling a prerequisite for a visible frame.
    constexpr std::array<int64_t, 3> preferred{GL_RGBA8, GL_SRGB8_ALPHA8, GL_RGB10_A2};
    for (const int64_t candidate : preferred) {
        if (std::find(formats.begin(), formats.end(), candidate) != formats.end()) {
            g.colorFormat = candidate;
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
        LOGI("Created eye %u swapchain: %dx%d, %u images", eye, swapchain.width, swapchain.height, imageCount);
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
            LOGE("xrPollEvent failed: %s", xrResultName(pollResult));
            g.exitRequested = true;
            break;
        }
        const auto* header = reinterpret_cast<const XrEventDataBaseHeader*>(&event);
        if (header->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(header);
            g.sessionState = changed->state;
            LOGI("OpenXR session state changed to %s (%d)", sessionStateName(g.sessionState),
                 static_cast<int>(g.sessionState));
            switch (g.sessionState) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (xrOk(xrBeginSession(g.session, &begin), "xrBeginSession")) {
                        g.sessionRunning = true;
                        g.sessionWaitPollCount = 0;
                        LOGI("OpenXR session began; waiting for compositor visibility");
                    }
                    break;
                }
                case XR_SESSION_STATE_STOPPING:
                    g.sessionRunning = false;
                    xrOk(xrEndSession(g.session), "xrEndSession");
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
            g.sessionRunning = false;
            g.exitRequested = true;
        } else if (header->type == XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) {
            resolveRendererBridge();
            if (g.recenterVr != nullptr) {
                g.recenterVr();
                LOGI("Recentered GLideN64 head pose after OpenXR reference-space change");
            }
        }
        event = {XR_TYPE_EVENT_DATA_BUFFER};
    }
}

bool renderEye(uint32_t eye, uint32_t imageIndex, bool diagnosticColor) {
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
    if (diagnosticColor) {
        // Deliberately unmistakable during compositor bring-up: red left, blue right. This clear
        // happens after image acquisition and no longer depends on a valid tracked view pose.
        glClearColor(eye == 0 ? 0.90f : 0.02f, 0.02f, eye == 0 ? 0.02f : 0.90f, 1.0f);
    } else {
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT);

    if (!diagnosticColor && g.sourceTexture != 0) {
        glUseProgram(g.program);
        glBindVertexArray(g.vertexArray);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_EXTERNAL_OES, g.sourceTexture);
        glUniform1i(g.sourceUniform, 0);

        const bool stereo = g.stereoSourceActive;
        const uint32_t sourceEye = stereo && g.configurationSwapEyes ? 1U - eye : eye;
        const float uvScaleX = stereo ? 0.5f : 1.0f;
        const float uvOffsetX = stereo ? (sourceEye == 0 ? 0.0f : 0.5f) : 0.0f;
        glUniform4f(g.uvTransformUniform, uvScaleX, 1.0f, uvOffsetX, 0.0f);

        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (!stereo && g.sourceWidth > 0 && g.sourceHeight > 0) {
            const float sourceAspect = static_cast<float>(g.sourceWidth) / static_cast<float>(g.sourceHeight);
            const float targetAspect = static_cast<float>(swapchain.width) / static_cast<float>(swapchain.height);
            if (sourceAspect > targetAspect) {
                scaleY = targetAspect / sourceAspect;
            } else {
                scaleX = sourceAspect / targetAspect;
            }
        }
        glUniform2f(g.quadScaleUniform, scaleX, scaleY);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    const GLenum error = glGetError();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (diagnosticColor) {
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

void destroyState(JNIEnv* env) {
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
        xrDestroyActionSet(g.controllerActions.actionSet);
        g.controllerActions = {};
    }
    for (Swapchain& swapchain : g.swapchains) {
        if (swapchain.handle != XR_NULL_HANDLE) xrDestroySwapchain(swapchain.handle);
    }
    g.swapchains.clear();
    if (g.viewSpace != XR_NULL_HANDLE) xrDestroySpace(g.viewSpace);
    if (g.localSpace != XR_NULL_HANDLE) xrDestroySpace(g.localSpace);
    if (g.session != XR_NULL_HANDLE) xrDestroySession(g.session);
    if (g.instance != XR_NULL_HANDLE) xrDestroyInstance(g.instance);
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

    env->GetJavaVM(&g.vm);
    g.activity = env->NewGlobalRef(activity);

    PFN_xrInitializeLoaderKHR initializeLoader = nullptr;
    XrResult result = xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrInitializeLoaderKHR",
                                            reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoader));
    if (XR_SUCCEEDED(result) && initializeLoader != nullptr) {
        XrLoaderInitInfoAndroidKHR loaderInfo{XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
        loaderInfo.applicationVM = g.vm;
        loaderInfo.applicationContext = g.activity;
        if (!xrOk(initializeLoader(reinterpret_cast<const XrLoaderInitInfoBaseHeaderKHR*>(&loaderInfo)),
                "xrInitializeLoaderKHR")) {
            return false;
        }
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
    LOGI("OpenXR presentation bridge initialized on GLES %d.%d", major, minor);
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
        JNIEnv*, jclass, jint texture, jint width, jint height, jboolean requestStereo) {
    g.sourceTexture = static_cast<GLuint>(texture);
    g.sourceWidth = width;
    g.sourceHeight = height;
    g.stereoRequested = requestStereo == JNI_TRUE;
    resolveRendererBridge();
    g.stereoSourceActive = g.setVrEnabled != nullptr && g.stereoRequested &&
                           g.configurationStereoEnabled;
    if (texture == 0) {
        g.sourceViewsValid = false;
        g.sourceViewDisplayTime = 0;
        g.sourceTextureTimestamp = 0;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeOnSourceFrameLatched(
        JNIEnv*, jclass, jlong textureTimestamp) {
    resolveRendererBridge();
    associateLatchedSourceFrame(static_cast<int64_t>(textureTimestamp));
}

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeConfigure(
        JNIEnv*, jclass, jboolean stereoEnabled, jboolean swapEyes, jfloat ipdMeters,
        jfloat worldUnitsPerMeter, jfloat rotationStrength, jboolean positionEnabled,
        jfloat maxTranslationMeters, jfloat cameraOffsetX, jfloat cameraOffsetY,
        jfloat cameraOffsetZ, jboolean useOpenXrFov, jboolean marioKartProfileEnabled,
        jfloat marioKartCameraOffsetY, jfloat marioKartCameraOffsetZ,
        jboolean touchControllerEnabled, jboolean debugLogging) {
    g.configurationStereoEnabled = stereoEnabled == JNI_TRUE;
    g.configurationSwapEyes = swapEyes == JNI_TRUE;
    g.configurationIpdMeters = ipdMeters;
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
    if (g.configureVr != nullptr) {
        g.configureVr(g.configurationStereoEnabled ? 1 : 0, g.configurationIpdMeters,
                      g.configurationWorldUnitsPerMeter, g.configurationRotationStrength,
                      g.configurationPositionEnabled ? 1 : 0, g.configurationMaxTranslationMeters,
                      g.configurationCameraOffsetX, g.configurationCameraOffsetY,
                      g.configurationCameraOffsetZ, g.configurationUseOpenXrFov ? 1 : 0,
                      g.configurationMarioKartProfileEnabled ? 1 : 0,
                      g.configurationMarioKartCameraOffsetY,
                      g.configurationMarioKartCameraOffsetZ);
    }
}

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeRecenter(
        JNIEnv*, jclass) {
    resolveRendererBridge();
    if (g.recenterVr != nullptr) {
        g.recenterVr();
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeRenderFrame(
        JNIEnv*, jclass) {
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

    syncControllerInput();

    using Clock = std::chrono::steady_clock;
    const Clock::time_point nativeFrameStart = Clock::now();
    const Clock::time_point waitFrameStart = Clock::now();
    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (!xrOk(xrWaitFrame(g.session, &waitInfo, &frameState), "xrWaitFrame")) {
        return JNI_FALSE;
    }
    const Clock::time_point waitFrameEnd = Clock::now();
    XrFrameBeginInfo beginInfo{XR_TYPE_FRAME_BEGIN_INFO};
    if (!xrOk(xrBeginFrame(g.session, &beginInfo), "xrBeginFrame")) {
        return JNI_FALSE;
    }

    std::vector<XrCompositionLayerProjectionView> layerViews;
    XrCompositionLayerProjection layer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
    std::array<const XrCompositionLayerBaseHeader*, 1> layers{};
    uint32_t layerCount = 0;

    if (frameState.shouldRender == XR_TRUE) {
        XrViewLocateInfo locateInfo{XR_TYPE_VIEW_LOCATE_INFO};
        locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
        locateInfo.displayTime = frameState.predictedDisplayTime;
        locateInfo.space = g.localSpace;
        XrViewState viewState{XR_TYPE_VIEW_STATE};
        uint32_t viewCount = 0;
        const XrResult locateResult = xrLocateViews(g.session, &locateInfo, &viewState,
                                                    static_cast<uint32_t>(g.views.size()), &viewCount, g.views.data());
        const bool validPose = XR_SUCCEEDED(locateResult) && viewCount == g.views.size() &&
            (viewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) != 0 &&
            (viewState.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) != 0;
        if (validPose) {
            publishHeadPose(frameState.predictedDisplayTime, viewCount);
        } else {
            ++g.locateFallbackCount;
            if (g.locateFallbackCount <= 10 || g.locateFallbackCount % 300 == 0) {
                LOGW("Tracked views unavailable (result=%s count=%u flags=0x%llx); "
                     "submitting VIEW-space fallback",
                     xrResultName(locateResult), viewCount,
                     static_cast<unsigned long long>(viewState.viewStateFlags));
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

        const uint32_t compositionViewCount = static_cast<uint32_t>(g.swapchains.size());
        layerViews.assign(compositionViewCount, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});
        const bool diagnosticColor = g.submittedLayerCount < STARTUP_DIAGNOSTIC_LAYER_COUNT;
        bool rendered = true;
        for (uint32_t eye = 0; eye < compositionViewCount; ++eye) {
            Swapchain& swapchain = g.swapchains[eye];
            XrSwapchainImageAcquireInfo acquireInfo{XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
            uint32_t imageIndex = 0;
            if (!xrOk(xrAcquireSwapchainImage(swapchain.handle, &acquireInfo, &imageIndex),
                    "xrAcquireSwapchainImage")) {
                rendered = false;
                break;
            }

            XrSwapchainImageWaitInfo imageWait{XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
            imageWait.timeout = XR_INFINITE_DURATION;
            const bool imageReady = xrOk(xrWaitSwapchainImage(swapchain.handle, &imageWait),
                                         "xrWaitSwapchainImage");
            if (!imageReady) {
                // A successfully acquired image cannot be safely released until its wait has
                // completed. Tear down the session on the next Java iteration instead.
                g.exitRequested = true;
                rendered = false;
                break;
            }

            rendered = renderEye(eye, imageIndex, diagnosticColor);
            XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
            const bool released = xrOk(xrReleaseSwapchainImage(swapchain.handle, &releaseInfo),
                                       "xrReleaseSwapchainImage");
            if (!rendered || !released) {
                rendered = false;
                break;
            }

            XrCompositionLayerProjectionView& view = layerViews[eye];
            const bool useSourceView = validPose && g.stereoSourceActive &&
                                       g.sourceViewsValid && eye < 2;
            const uint32_t sourceEye = g.configurationSwapEyes && eye < 2 ? 1U - eye : eye;
            const XrView& renderedView = useSourceView
                ? g.sourceViews[sourceEye]
                : (validPose ? g.views[eye] : fallbackViews[eye]);
            view.pose = renderedView.pose;
            view.fov = renderedView.fov;
            view.subImage.swapchain = swapchain.handle;
            view.subImage.imageRect.offset = {0, 0};
            view.subImage.imageRect.extent = {swapchain.width, swapchain.height};
            view.subImage.imageArrayIndex = 0;
        }

        if (rendered) {
            layer.space = validPose ? g.localSpace : g.viewSpace;
            layer.viewCount = static_cast<uint32_t>(layerViews.size());
            layer.views = layerViews.data();
            layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
            layerCount = 1;
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
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layerCount;
    endInfo.layers = layerCount == 0 ? nullptr : layers.data();
    const bool submitted = xrOk(xrEndFrame(g.session, &endInfo), "xrEndFrame");
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
        if (g.submittedLayerCount == 1) {
            LOGI("First OpenXR projection layer submitted successfully (startup red/blue active)");
        } else if (g.submittedLayerCount == STARTUP_DIAGNOSTIC_LAYER_COUNT) {
            LOGI("Startup red/blue interval complete; presenting emulator source texture");
        }
    }
    g.waitFrameMilliseconds += waitMilliseconds;
    g.nativeFrameMilliseconds += nativeMilliseconds;
    g.workMilliseconds += workMilliseconds;
    g.maximumWorkMilliseconds = std::max(g.maximumWorkMilliseconds, workMilliseconds);

    if (g.debugLogging && g.timingSampleCount >= 300) {
        const double divisor = static_cast<double>(g.timingSampleCount);
        const int eyeWidth = g.swapchains.empty() ? 0 : g.swapchains[0].width;
        const int eyeHeight = g.swapchains.empty() ? 0 : g.swapchains[0].height;
        const double sourcePoseAgeMilliseconds = g.sourceViewsValid
            ? static_cast<double>(frameState.predictedDisplayTime - g.sourceViewDisplayTime) / 1.0e6
            : -1.0;
        LOGI("timing samples=%u submitted=%u layers=%u avgWait=%.2fms "
             "avgNative=%.2fms avgWork=%.2fms maxWork=%.2fms "
             "source=%dx%d stereo=%d swapEyes=%d eye=%dx%d sourcePoseAge=%.2fms "
             "poseMatches=%u poseMisses=%u textureTs=%lld",
             g.timingSampleCount, g.submittedFrameCount, g.renderedLayerFrameCount,
             g.waitFrameMilliseconds / divisor, g.nativeFrameMilliseconds / divisor,
             g.workMilliseconds / divisor, g.maximumWorkMilliseconds,
             g.sourceWidth, g.sourceHeight,
             g.stereoSourceActive ? 1 : 0, g.configurationSwapEyes ? 1 : 0,
             eyeWidth, eyeHeight,
             sourcePoseAgeMilliseconds, g.sourcePoseMatches, g.sourcePoseMisses,
             static_cast<long long>(g.sourceTextureTimestamp));
        g.timingSampleCount = 0;
        g.renderedLayerFrameCount = 0;
        g.waitFrameMilliseconds = 0.0;
        g.nativeFrameMilliseconds = 0.0;
        g.workMilliseconds = 0.0;
        g.maximumWorkMilliseconds = 0.0;
    }
    return JNI_TRUE;
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
