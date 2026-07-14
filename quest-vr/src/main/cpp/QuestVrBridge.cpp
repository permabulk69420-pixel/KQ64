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
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr const char* TAG = "M64P-QuestVR";

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
using ConfigureVrFn = void (*)(int stereoEnabled, float ipdMeters, float worldUnitsPerMeter,
                               float rotationStrength, int positionEnabled, float maxTranslationMeters,
                               float cameraOffsetX, float cameraOffsetY, float cameraOffsetZ);
using RecenterVrFn = void (*)();
using GetVrStatsFn = void (*)(uint32_t* geometryDraws, uint32_t* rectangleDraws,
                              uint32_t* eyeDraws, uint32_t* poseGeneration);

struct State {
    JavaVM* vm{nullptr};
    jobject activity{nullptr};

    XrInstance instance{XR_NULL_HANDLE};
    XrSystemId systemId{XR_NULL_SYSTEM_ID};
    XrSession session{XR_NULL_HANDLE};
    XrSpace localSpace{XR_NULL_HANDLE};
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
    ConfigureVrFn configureVr{nullptr};
    RecenterVrFn recenterVr{nullptr};
    GetVrStatsFn getVrStats{nullptr};
    bool rendererBridgeLogged{false};

    bool configurationStereoEnabled{true};
    float configurationIpdMeters{0.064f};
    float configurationWorldUnitsPerMeter{64.0f};
    float configurationRotationStrength{1.0f};
    bool configurationPositionEnabled{false};
    float configurationMaxTranslationMeters{0.15f};
    float configurationCameraOffsetX{0.0f};
    float configurationCameraOffsetY{0.0f};
    float configurationCameraOffsetZ{0.0f};
    bool debugLogging{false};
    uint32_t submittedFrameCount{0};
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
    g.configureVr = reinterpret_cast<ConfigureVrFn>(dlsym(symbolScope, "M64PQuestVrConfigure"));
    g.recenterVr = reinterpret_cast<RecenterVrFn>(dlsym(symbolScope, "M64PQuestVrRecenter"));
    g.getVrStats = reinterpret_cast<GetVrStatsFn>(dlsym(symbolScope, "M64PQuestVrGetStats"));

    const bool found = g.setVrEnabled != nullptr && g.setVrPose != nullptr;
    if (found) {
        if (g.configureVr != nullptr) {
            g.configureVr(g.configurationStereoEnabled ? 1 : 0, g.configurationIpdMeters,
                          g.configurationWorldUnitsPerMeter, g.configurationRotationStrength,
                          g.configurationPositionEnabled ? 1 : 0, g.configurationMaxTranslationMeters,
                          g.configurationCameraOffsetX, g.configurationCameraOffsetY,
                          g.configurationCameraOffsetZ);
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

    g.setVrPose(pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
                position.x, position.y, position.z, static_cast<int64_t>(displayTime));

    ++g.submittedFrameCount;
    if (g.debugLogging && g.submittedFrameCount % 300 == 0) {
        uint32_t geometryDraws = 0;
        uint32_t rectangleDraws = 0;
        uint32_t eyeDraws = 0;
        uint32_t poseGeneration = 0;
        if (g.getVrStats != nullptr) {
            g.getVrStats(&geometryDraws, &rectangleDraws, &eyeDraws, &poseGeneration);
        }
        LOGI("pose q=[%.3f %.3f %.3f %.3f] p=[%.3f %.3f %.3f], "
             "draws geometry=%u rect=%u eyes=%u poseGeneration=%u",
             pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w,
             position.x, position.y, position.z, geometryDraws, rectangleDraws, eyeDraws,
             poseGeneration);
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
    constexpr std::array<int64_t, 3> preferred{GL_SRGB8_ALPHA8, GL_RGBA8, GL_RGB10_A2};
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
    while (xrPollEvent(g.instance, &event) == XR_SUCCESS) {
        const auto* header = reinterpret_cast<const XrEventDataBaseHeader*>(&event);
        if (header->type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const auto* changed = reinterpret_cast<const XrEventDataSessionStateChanged*>(header);
            g.sessionState = changed->state;
            LOGI("OpenXR session state changed to %d", static_cast<int>(g.sessionState));
            switch (g.sessionState) {
                case XR_SESSION_STATE_READY: {
                    XrSessionBeginInfo begin{XR_TYPE_SESSION_BEGIN_INFO};
                    begin.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (xrOk(xrBeginSession(g.session, &begin), "xrBeginSession")) {
                        g.sessionRunning = true;
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

void renderEye(uint32_t eye, uint32_t imageIndex) {
    const Swapchain& swapchain = g.swapchains[eye];
    glBindFramebuffer(GL_FRAMEBUFFER, g.framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           swapchain.images[imageIndex].image, 0);
    const GLenum framebufferStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (framebufferStatus != GL_FRAMEBUFFER_COMPLETE) {
        LOGE("OpenXR eye framebuffer is incomplete: 0x%x", framebufferStatus);
        return;
    }
    glViewport(0, 0, swapchain.width, swapchain.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (g.sourceTexture == 0) {
        return;
    }

    glUseProgram(g.program);
    glBindVertexArray(g.vertexArray);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, g.sourceTexture);
    glUniform1i(g.sourceUniform, 0);

    const bool stereo = g.stereoSourceActive;
    const float uvScaleX = stereo ? 0.5f : 1.0f;
    const float uvOffsetX = stereo ? (eye == 0 ? 0.0f : 0.5f) : 0.0f;
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

void destroyState(JNIEnv* env) {
    if (g.setVrEnabled != nullptr) {
        g.setVrEnabled(0);
    }

    if (g.program != 0) glDeleteProgram(g.program);
    if (g.framebuffer != 0) glDeleteFramebuffers(1, &g.framebuffer);
    if (g.vertexArray != 0) glDeleteVertexArrays(1, &g.vertexArray);
    g.program = 0;
    g.framebuffer = 0;
    g.vertexArray = 0;

    if (g.sessionRunning && g.session != XR_NULL_HANDLE) {
        xrEndSession(g.session);
        g.sessionRunning = false;
    }
    for (Swapchain& swapchain : g.swapchains) {
        if (swapchain.handle != XR_NULL_HANDLE) xrDestroySwapchain(swapchain.handle);
    }
    g.swapchains.clear();
    if (g.localSpace != XR_NULL_HANDLE) xrDestroySpace(g.localSpace);
    if (g.session != XR_NULL_HANDLE) xrDestroySession(g.session);
    if (g.instance != XR_NULL_HANDLE) xrDestroyInstance(g.instance);
    g.localSpace = XR_NULL_HANDLE;
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

    XrReferenceSpaceCreateInfo spaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    spaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
    setIdentity(spaceInfo.poseInReferenceSpace);
    if (!xrOk(xrCreateReferenceSpace(g.session, &spaceInfo, &g.localSpace), "xrCreateReferenceSpace")) {
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
}

extern "C" JNIEXPORT void JNICALL
Java_paulscode_android_mupen64plusae_questvr_QuestVrBridge_nativeConfigure(
        JNIEnv*, jclass, jboolean stereoEnabled, jfloat ipdMeters,
        jfloat worldUnitsPerMeter, jfloat rotationStrength, jboolean positionEnabled,
        jfloat maxTranslationMeters, jfloat cameraOffsetX, jfloat cameraOffsetY,
        jfloat cameraOffsetZ, jboolean debugLogging) {
    g.configurationStereoEnabled = stereoEnabled == JNI_TRUE;
    g.configurationIpdMeters = ipdMeters;
    g.configurationWorldUnitsPerMeter = worldUnitsPerMeter;
    g.configurationRotationStrength = rotationStrength;
    g.configurationPositionEnabled = positionEnabled == JNI_TRUE;
    g.configurationMaxTranslationMeters = maxTranslationMeters;
    g.configurationCameraOffsetX = cameraOffsetX;
    g.configurationCameraOffsetY = cameraOffsetY;
    g.configurationCameraOffsetZ = cameraOffsetZ;
    g.debugLogging = debugLogging == JNI_TRUE;
    if (g.configureVr != nullptr) {
        g.configureVr(g.configurationStereoEnabled ? 1 : 0, g.configurationIpdMeters,
                      g.configurationWorldUnitsPerMeter, g.configurationRotationStrength,
                      g.configurationPositionEnabled ? 1 : 0, g.configurationMaxTranslationMeters,
                      g.configurationCameraOffsetX, g.configurationCameraOffsetY,
                      g.configurationCameraOffsetZ);
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
        return JNI_FALSE;
    }

    XrFrameWaitInfo waitInfo{XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState frameState{XR_TYPE_FRAME_STATE};
    if (!xrOk(xrWaitFrame(g.session, &waitInfo, &frameState), "xrWaitFrame")) {
        return JNI_FALSE;
    }
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
            layerViews.assign(viewCount, {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW});

            bool rendered = true;
            for (uint32_t eye = 0; eye < viewCount; ++eye) {
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
                if (!xrOk(xrWaitSwapchainImage(swapchain.handle, &imageWait), "xrWaitSwapchainImage")) {
                    rendered = false;
                } else {
                    renderEye(eye, imageIndex);
                }
                XrSwapchainImageReleaseInfo releaseInfo{XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                xrOk(xrReleaseSwapchainImage(swapchain.handle, &releaseInfo), "xrReleaseSwapchainImage");
                if (!rendered) break;

                XrCompositionLayerProjectionView& view = layerViews[eye];
                view.pose = g.views[eye].pose;
                view.fov = g.views[eye].fov;
                view.subImage.swapchain = swapchain.handle;
                view.subImage.imageRect.offset = {0, 0};
                view.subImage.imageRect.extent = {swapchain.width, swapchain.height};
                view.subImage.imageArrayIndex = 0;
            }

            if (rendered) {
                layer.space = g.localSpace;
                layer.viewCount = static_cast<uint32_t>(layerViews.size());
                layer.views = layerViews.data();
                layers[0] = reinterpret_cast<const XrCompositionLayerBaseHeader*>(&layer);
                layerCount = 1;
            }
        }
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    XrFrameEndInfo endInfo{XR_TYPE_FRAME_END_INFO};
    endInfo.displayTime = frameState.predictedDisplayTime;
    endInfo.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    endInfo.layerCount = layerCount;
    endInfo.layers = layerCount == 0 ? nullptr : layers.data();
    return xrOk(xrEndFrame(g.session, &endInfo), "xrEndFrame") ? JNI_TRUE : JNI_FALSE;
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
