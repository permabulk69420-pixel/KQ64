cmake_minimum_required(VERSION 3.22.1)
project(mupen64plus_quest_vr LANGUAGES C CXX)

find_package(OpenXR REQUIRED CONFIG)

add_library(mupen64plus-quest-vr SHARED QuestVrBridge.cpp)

target_compile_definitions(mupen64plus-quest-vr PRIVATE
    XR_USE_PLATFORM_ANDROID
    XR_USE_GRAPHICS_API_OPENGL_ES
)

target_link_libraries(mupen64plus-quest-vr PRIVATE
    OpenXR::openxr_loader
    android
    dl
    EGL
    GLESv3
    log
)
