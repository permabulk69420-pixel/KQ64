from pathlib import Path


def replace_once(path_str: str, old: str, new: str) -> None:
    path = Path(path_str)
    text = path.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"Expected exactly one match in {path_str}, found {count}")
    path.write_text(text.replace(old, new, 1), encoding="utf-8")


quest_vr_h = "mupen64plus-video-gliden64/upstream/src/QuestVr.h"
quest_vr_cpp = "mupen64plus-video-gliden64/upstream/src/QuestVr.cpp"
unbuffered_cpp = "mupen64plus-video-gliden64/upstream/src/Graphics/OpenGLContext/opengl_UnbufferedDrawer.cpp"
buffered_cpp = "mupen64plus-video-gliden64/upstream/src/Graphics/OpenGLContext/opengl_BufferedDrawer.cpp"

replace_once(
    quest_vr_h,
    """\texplicit DrawScope(bool transformGeometry);\n""",
    """\texplicit DrawScope(bool transformGeometry,\n\t\tbool correctScreenSpaceProjection = false);\n""",
)

replace_once(
    quest_vr_h,
    """\tbool m_active;\n\tbool m_transformGeometry;\n\tint m_targetWidth;\n""",
    """\tbool m_active;\n\tbool m_transformGeometry;\n\tbool m_correctScreenSpaceProjection;\n\tint m_targetWidth;\n""",
)

replace_once(
    quest_vr_cpp,
    """TransformCache s_transformCache;\n\nQuaternion normalize(Quaternion q)\n""",
    """TransformCache s_transformCache;\n\nstruct ScreenSpaceTransformCache {\n\tbool cached{false};\n\tbool valid{false};\n\tunsigned int poseGeneration{0};\n\tstd::array<std::array<float, 16>, 2> eyes{};\n};\n\nScreenSpaceTransformCache s_screenSpaceTransformCache;\n\nQuaternion normalize(Quaternion q)\n""",
)

replace_once(
    quest_vr_cpp,
    """\treturn s_transformCache.eyes[std::min(eye, 1U)];\n}\n\nvoid setProgramEye(unsigned int eye, bool enabled, bool transformGeometry)\n""",
    """\treturn s_transformCache.eyes[std::min(eye, 1U)];\n}\n\nbool buildScreenSpaceEyeTransform(unsigned int eye, const FramePoseState& framePose,\n\tfloat* destination)\n{\n\tidentity(destination);\n\tif (!framePose.haveRuntimeViews)\n\t\treturn false;\n\n\tconst RuntimeViewState& view = framePose.views[std::min(eye, 1U)];\n\tconst float tanLeft = std::tan(view.angleLeft);\n\tconst float tanRight = std::tan(view.angleRight);\n\tconst float tanUp = std::tan(view.angleUp);\n\tconst float tanDown = std::tan(view.angleDown);\n\tconst float width = tanRight - tanLeft;\n\tconst float height = tanUp - tanDown;\n\tif (!std::isfinite(width) || !std::isfinite(height) ||\n\t\twidth < 0.01f || height < 0.01f)\n\t\treturn false;\n\n\tconst float offsetX = -(tanRight + tanLeft) / width;\n\tconst float offsetY = -(tanUp + tanDown) / height;\n\tif (!std::isfinite(offsetX) || !std::isfinite(offsetY))\n\t\treturn false;\n\n\t// Screen-space vertices currently use symmetric eye-image coordinates. OpenXR's\n\t// asymmetric per-eye frusta place the forward optical axis at a different NDC\n\t// location in each eye. Shift only the clip-space centre; keep HUD rotation,\n\t// translation and scale head-stable.\n\tdestination[12] = offsetX;\n\tdestination[13] = offsetY;\n\treturn true;\n}\n\nconst std::array<float, 16>* getScreenSpaceEyeTransform(unsigned int eye)\n{\n\tlatchFramePose();\n\tconst unsigned int poseGeneration = s_framePose.generation;\n\tif (!s_screenSpaceTransformCache.cached ||\n\t\ts_screenSpaceTransformCache.poseGeneration != poseGeneration) {\n\t\ts_screenSpaceTransformCache.cached = true;\n\t\ts_screenSpaceTransformCache.poseGeneration = poseGeneration;\n\t\ts_screenSpaceTransformCache.valid = s_framePose.haveRuntimeViews;\n\t\tfor (unsigned int index = 0; index < 2; ++index) {\n\t\t\tconst bool eyeValid = buildScreenSpaceEyeTransform(index, s_framePose,\n\t\t\t\ts_screenSpaceTransformCache.eyes[index].data());\n\t\t\ts_screenSpaceTransformCache.valid =\n\t\t\t\ts_screenSpaceTransformCache.valid && eyeValid;\n\t\t}\n\t}\n\n\tif (!s_screenSpaceTransformCache.valid)\n\t\treturn nullptr;\n\treturn &s_screenSpaceTransformCache.eyes[std::min(eye, 1U)];\n}\n\nvoid setProgramEye(unsigned int eye, bool enabled, bool transformGeometry,\n\tbool correctScreenSpaceProjection)\n""",
)

replace_once(
    quest_vr_cpp,
    """\tstd::array<float, 16> worldProjection{};\n\tbool projectionContainsView = false;\n\tconst bool applyTransform = transformGeometry &&\n\t\tgetWorldProjection(worldProjection, projectionContainsView);\n\tif (uniforms.transformEnabled >= 0)\n\t\tglUniform1i(uniforms.transformEnabled, applyTransform ? 1 : 0);\n\tif (!applyTransform)\n\t\treturn;\n\n\tconst std::array<float, 16>& matrix = getEyeTransform(eye);\n\tfor (int row = 0; row < 4; ++row) {\n\t\tif (uniforms.rows[row] >= 0) {\n\t\t\tglUniform4f(uniforms.rows[row], matrix[row], matrix[4 + row],\n\t\t\t\tmatrix[8 + row], matrix[12 + row]);\n\t\t}\n\t}\n""",
    """\tstd::array<float, 16> matrix{};\n\tbool applyTransform = false;\n\tif (transformGeometry) {\n\t\tstd::array<float, 16> worldProjection{};\n\t\tbool projectionContainsView = false;\n\t\tif (getWorldProjection(worldProjection, projectionContainsView)) {\n\t\t\tmatrix = getEyeTransform(eye);\n\t\t\tapplyTransform = true;\n\t\t}\n\t} else if (correctScreenSpaceProjection) {\n\t\tconst std::array<float, 16>* screenSpaceMatrix =\n\t\t\tgetScreenSpaceEyeTransform(eye);\n\t\tif (screenSpaceMatrix != nullptr) {\n\t\t\tmatrix = *screenSpaceMatrix;\n\t\t\tapplyTransform = true;\n\t\t}\n\t}\n\n\tif (uniforms.transformEnabled >= 0)\n\t\tglUniform1i(uniforms.transformEnabled, applyTransform ? 1 : 0);\n\tif (!applyTransform)\n\t\treturn;\n\n\tfor (int row = 0; row < 4; ++row) {\n\t\tif (uniforms.rows[row] >= 0) {\n\t\t\tglUniform4f(uniforms.rows[row], matrix[row], matrix[4 + row],\n\t\t\t\tmatrix[8 + row], matrix[12 + row]);\n\t\t}\n\t}\n""",
)

replace_once(
    quest_vr_cpp,
    """\ts_packedFramebufferTextures.clear();\n\ts_transformCache.valid = false;\n\ts_framePose.valid = false;\n""",
    """\ts_packedFramebufferTextures.clear();\n\ts_transformCache.valid = false;\n\ts_screenSpaceTransformCache.cached = false;\n\ts_framePose.valid = false;\n""",
)

replace_once(
    quest_vr_cpp,
    """DrawScope::DrawScope(bool transformGeometry)\n\t: m_active(isStereoEnabled() && isPackedFramebuffer(s_drawFramebuffer) &&\n\t\ts_viewport.width >= 2 && s_viewport.height > 0)\n\t, m_transformGeometry(transformGeometry)\n\t, m_targetWidth(getDrawTargetWidth())\n""",
    """DrawScope::DrawScope(bool transformGeometry, bool correctScreenSpaceProjection)\n\t: m_active(isStereoEnabled() && isPackedFramebuffer(s_drawFramebuffer) &&\n\t\ts_viewport.width >= 2 && s_viewport.height > 0)\n\t, m_transformGeometry(transformGeometry)\n\t, m_correctScreenSpaceProjection(correctScreenSpaceProjection)\n\t, m_targetWidth(getDrawTargetWidth())\n""",
)

replace_once(
    quest_vr_cpp,
    """\tsetProgramEye(0, false, m_transformGeometry);\n""",
    """\tsetProgramEye(0, false, m_transformGeometry, m_correctScreenSpaceProjection);\n""",
)

replace_once(
    quest_vr_cpp,
    """\tsetProgramEye(eye, true, m_transformGeometry);\n""",
    """\tsetProgramEye(eye, true, m_transformGeometry, m_correctScreenSpaceProjection);\n""",
)

for drawer_cpp in (unbuffered_cpp, buffered_cpp):
    replace_once(
        drawer_cpp,
        """\tconst bool transformGeometry = modifiedPositionVertices != _params.verticesCount;\n""",
        """\tconst bool screenSpaceGeometry =\n\t\tmodifiedPositionVertices == _params.verticesCount;\n\tconst bool transformGeometry = !screenSpaceGeometry;\n""",
    )
    replace_once(
        drawer_cpp,
        """\tQuestVr::DrawScope stereoDraw(transformGeometry);\n""",
        """\tQuestVr::DrawScope stereoDraw(transformGeometry, screenSpaceGeometry);\n""",
    )

print("Applied Quest VR screen-space optical-centre correction")
