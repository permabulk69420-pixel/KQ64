#include "QuestVr.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "Config.h"
#include "Log.h"
#include "gSP.h"
#include "Graphics/OpenGLContext/GLFunctions.h"

namespace {

struct AtomicPose {
	std::atomic<float> qx{0.0f};
	std::atomic<float> qy{0.0f};
	std::atomic<float> qz{0.0f};
	std::atomic<float> qw{1.0f};
	std::atomic<float> px{0.0f};
	std::atomic<float> py{0.0f};
	std::atomic<float> pz{0.0f};
};

struct AtomicView {
	std::atomic<float> px{0.0f};
	std::atomic<float> py{0.0f};
	std::atomic<float> pz{0.0f};
	std::atomic<float> angleLeft{-0.8f};
	std::atomic<float> angleRight{0.8f};
	std::atomic<float> angleUp{0.8f};
	std::atomic<float> angleDown{-0.8f};
};

struct Quaternion {
	float x;
	float y;
	float z;
	float w;
};

struct Vector3 {
	float x;
	float y;
	float z;
};

struct ProgramUniforms {
	GLint enabled{-1};
	GLint eye{-1};
	std::array<GLint, 4> rows{{-1, -1, -1, -1}};
};

struct RectState {
	int x{0};
	int y{0};
	int width{0};
	int height{0};
};

std::atomic<bool> s_enabled{false};
std::atomic<bool> s_stereoEnabled{true};
std::atomic<bool> s_positionEnabled{false};
std::atomic<bool> s_useOpenXrFov{true};
std::atomic<bool> s_haveRuntimeViews{false};
std::atomic<bool> s_marioKartProfileEnabled{true};
std::atomic<bool> s_marioKartProfileLogged{false};
std::atomic<bool> s_haveRecenterPose{false};
std::atomic<bool> s_recenterRequested{true};
std::atomic<float> s_ipdMeters{0.064f};
std::atomic<float> s_worldUnitsPerMeter{64.0f};
std::atomic<float> s_rotationStrength{1.0f};
std::atomic<float> s_maxTranslationMeters{0.15f};
std::atomic<float> s_cameraOffsetX{0.0f};
std::atomic<float> s_cameraOffsetY{0.0f};
std::atomic<float> s_cameraOffsetZ{0.0f};
std::atomic<float> s_marioKartCameraOffsetY{-0.20f};
std::atomic<float> s_marioKartCameraOffsetZ{-0.75f};
std::atomic<unsigned int> s_poseGeneration{1};
std::atomic<unsigned int> s_configGeneration{1};
std::atomic<unsigned int> s_geometryDraws{0};
std::atomic<unsigned int> s_rectangleDraws{0};
std::atomic<unsigned int> s_eyeDraws{0};

AtomicPose s_pose;
AtomicPose s_recenterPose;
std::array<AtomicView, 2> s_runtimeViews;
RectState s_viewport;
RectState s_scissor;
bool s_scissorEnabled{false};
GLuint s_currentProgram{0};
std::unordered_map<GLuint, ProgramUniforms> s_programs;

struct TransformCache {
	bool valid{false};
	unsigned int poseGeneration{0};
	unsigned int configGeneration{0};
	std::array<float, 16> projection{};
	std::array<std::array<float, 16>, 2> eyes{};
};

TransformCache s_transformCache;

Quaternion normalize(Quaternion q)
{
	const float lengthSquared = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
	if (lengthSquared < 1.0e-12f)
		return {0.0f, 0.0f, 0.0f, 1.0f};
	const float inverseLength = 1.0f / std::sqrt(lengthSquared);
	q.x *= inverseLength;
	q.y *= inverseLength;
	q.z *= inverseLength;
	q.w *= inverseLength;
	return q;
}

Quaternion conjugate(const Quaternion& q)
{
	return {-q.x, -q.y, -q.z, q.w};
}

Quaternion multiply(const Quaternion& a, const Quaternion& b)
{
	return {
		a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
		a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
		a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
	};
}

Quaternion scaleRotation(Quaternion q, float strength)
{
	q = normalize(q);
	if (q.w < 0.0f) {
		q.x = -q.x;
		q.y = -q.y;
		q.z = -q.z;
		q.w = -q.w;
	}
	const float clampedW = std::max(-1.0f, std::min(1.0f, q.w));
	const float halfAngle = std::acos(clampedW);
	const float sinHalfAngle = std::sin(halfAngle);
	if (std::abs(sinHalfAngle) < 1.0e-6f)
		return {0.0f, 0.0f, 0.0f, 1.0f};
	const float scaledHalfAngle = halfAngle * strength;
	const float scale = std::sin(scaledHalfAngle) / sinHalfAngle;
	return normalize({q.x * scale, q.y * scale, q.z * scale, std::cos(scaledHalfAngle)});
}

Vector3 rotate(const Quaternion& q, const Vector3& v)
{
	const Quaternion vector{v.x, v.y, v.z, 0.0f};
	const Quaternion rotated = multiply(multiply(q, vector), conjugate(q));
	return {rotated.x, rotated.y, rotated.z};
}

Quaternion loadOrientation(const AtomicPose& pose)
{
	return normalize({pose.qx.load(std::memory_order_relaxed),
		pose.qy.load(std::memory_order_relaxed),
		pose.qz.load(std::memory_order_relaxed),
		pose.qw.load(std::memory_order_relaxed)});
}

Vector3 loadPosition(const AtomicPose& pose)
{
	return {pose.px.load(std::memory_order_relaxed),
		pose.py.load(std::memory_order_relaxed),
		pose.pz.load(std::memory_order_relaxed)};
}

Vector3 loadViewPosition(const AtomicView& view)
{
	return {view.px.load(std::memory_order_relaxed),
		view.py.load(std::memory_order_relaxed),
		view.pz.load(std::memory_order_relaxed)};
}

void storePose(AtomicPose& destination, const Quaternion& orientation, const Vector3& position)
{
	destination.qx.store(orientation.x, std::memory_order_relaxed);
	destination.qy.store(orientation.y, std::memory_order_relaxed);
	destination.qz.store(orientation.z, std::memory_order_relaxed);
	destination.qw.store(orientation.w, std::memory_order_relaxed);
	destination.px.store(position.x, std::memory_order_relaxed);
	destination.py.store(position.y, std::memory_order_relaxed);
	destination.pz.store(position.z, std::memory_order_relaxed);
}

void identity(float* matrix)
{
	std::fill(matrix, matrix + 16, 0.0f);
	matrix[0] = 1.0f;
	matrix[5] = 1.0f;
	matrix[10] = 1.0f;
	matrix[15] = 1.0f;
}

void multiplyMatrices(const float* a, const float* b, float* destination)
{
	std::array<float, 16> result{};
	for (int column = 0; column < 4; ++column) {
		for (int row = 0; row < 4; ++row) {
			float value = 0.0f;
			for (int index = 0; index < 4; ++index)
				value += a[index * 4 + row] * b[column * 4 + index];
			result[column * 4 + row] = value;
		}
	}
	std::copy(result.begin(), result.end(), destination);
}

bool inverseMatrix(const float* matrix, float* destination)
{
	double augmented[4][8]{};
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column)
			augmented[row][column] = matrix[column * 4 + row];
		augmented[row][4 + row] = 1.0;
	}

	for (int column = 0; column < 4; ++column) {
		int pivot = column;
		for (int row = column + 1; row < 4; ++row) {
			if (std::abs(augmented[row][column]) > std::abs(augmented[pivot][column]))
				pivot = row;
		}
		if (std::abs(augmented[pivot][column]) < 1.0e-12)
			return false;
		if (pivot != column)
			for (int entry = 0; entry < 8; ++entry)
				std::swap(augmented[pivot][entry], augmented[column][entry]);

		const double divisor = augmented[column][column];
		for (double& entry : augmented[column])
			entry /= divisor;
		for (int row = 0; row < 4; ++row) {
			if (row == column)
				continue;
			const double factor = augmented[row][column];
			for (int entry = 0; entry < 8; ++entry)
				augmented[row][entry] -= factor * augmented[column][entry];
		}
	}

	for (int row = 0; row < 4; ++row)
		for (int column = 0; column < 4; ++column)
			destination[column * 4 + row] = static_cast<float>(augmented[row][4 + column]);
	return true;
}

void quaternionMatrix(const Quaternion& q, float* matrix)
{
	identity(matrix);
	const float xx = q.x * q.x;
	const float yy = q.y * q.y;
	const float zz = q.z * q.z;
	const float xy = q.x * q.y;
	const float xz = q.x * q.z;
	const float yz = q.y * q.z;
	const float xw = q.x * q.w;
	const float yw = q.y * q.w;
	const float zw = q.z * q.w;

	matrix[0] = 1.0f - 2.0f * (yy + zz);
	matrix[4] = 2.0f * (xy - zw);
	matrix[8] = 2.0f * (xz + yw);
	matrix[1] = 2.0f * (xy + zw);
	matrix[5] = 1.0f - 2.0f * (xx + zz);
	matrix[9] = 2.0f * (yz - xw);
	matrix[2] = 2.0f * (xz - yw);
	matrix[6] = 2.0f * (yz + xw);
	matrix[10] = 1.0f - 2.0f * (xx + yy);
}

bool isPerspectiveProjection(const float* projection)
{
	return std::abs(projection[11]) >= 0.5f && std::abs(projection[15]) <= 0.001f;
}

void applyRuntimeProjection(unsigned int eye, float* projection)
{
	if (!s_useOpenXrFov.load(std::memory_order_relaxed) ||
		!s_haveRuntimeViews.load(std::memory_order_acquire))
		return;

	// GLideN64 projection matrices use OpenGL column-major layout. Only replace the angular
	// terms for perspective world draws; retain the game's depth mapping and leave orthographic
	// HUD/background projections alone.
	if (!isPerspectiveProjection(projection))
		return;
	const AtomicView& view = s_runtimeViews[std::min(eye, 1U)];
	const float tanLeft = std::tan(view.angleLeft.load(std::memory_order_relaxed));
	const float tanRight = std::tan(view.angleRight.load(std::memory_order_relaxed));
	const float tanUp = std::tan(view.angleUp.load(std::memory_order_relaxed));
	const float tanDown = std::tan(view.angleDown.load(std::memory_order_relaxed));
	const float width = tanRight - tanLeft;
	const float height = tanUp - tanDown;
	if (!std::isfinite(width) || !std::isfinite(height) || width < 0.01f || height < 0.01f)
		return;
	projection[0] = 2.0f / width;
	projection[5] = 2.0f / height;
	projection[8] = (tanRight + tanLeft) / width;
	projection[9] = (tanUp + tanDown) / height;
}

void buildEyeTransform(unsigned int eye, const float* projection, float* destination)
{
	const Quaternion recenterOrientation = loadOrientation(s_recenterPose);
	const Quaternion currentOrientation = loadOrientation(s_pose);
	Quaternion headOrientation = multiply(conjugate(recenterOrientation), currentOrientation);
	headOrientation = scaleRotation(headOrientation, s_rotationStrength.load(std::memory_order_relaxed));

	Vector3 headPosition{0.0f, 0.0f, 0.0f};
	if (s_positionEnabled.load(std::memory_order_relaxed)) {
		const Vector3 recenterPosition = loadPosition(s_recenterPose);
		const Vector3 currentPosition = loadPosition(s_pose);
		headPosition = rotate(conjugate(recenterOrientation), {
			currentPosition.x - recenterPosition.x,
			currentPosition.y - recenterPosition.y,
			currentPosition.z - recenterPosition.z,
		});
		const float maximum = std::max(0.0f, s_maxTranslationMeters.load(std::memory_order_relaxed));
		const float length = std::sqrt(headPosition.x * headPosition.x +
			headPosition.y * headPosition.y + headPosition.z * headPosition.z);
		if (maximum > 0.0f && length > maximum) {
			const float scale = maximum / length;
			headPosition.x *= scale;
			headPosition.y *= scale;
			headPosition.z *= scale;
		}
	}

	Vector3 eyeOffset{0.0f, 0.0f, 0.0f};
	if (s_haveRuntimeViews.load(std::memory_order_acquire)) {
		const Vector3 currentCenter = loadPosition(s_pose);
		const Vector3 trackedEye = loadViewPosition(s_runtimeViews[std::min(eye, 1U)]);
		const Vector3 trackedLeft = loadViewPosition(s_runtimeViews[0]);
		const Vector3 trackedRight = loadViewPosition(s_runtimeViews[1]);
		const float measuredIpd = std::sqrt(
			(trackedRight.x - trackedLeft.x) * (trackedRight.x - trackedLeft.x) +
			(trackedRight.y - trackedLeft.y) * (trackedRight.y - trackedLeft.y) +
			(trackedRight.z - trackedLeft.z) * (trackedRight.z - trackedLeft.z));
		const float eyeScale = measuredIpd > 0.001f
			? s_ipdMeters.load(std::memory_order_relaxed) / measuredIpd : 1.0f;
		eyeOffset = rotate(conjugate(recenterOrientation), {
			(trackedEye.x - currentCenter.x) * eyeScale,
			(trackedEye.y - currentCenter.y) * eyeScale,
			(trackedEye.z - currentCenter.z) * eyeScale,
		});
	} else {
		const float eyeSign = eye == 0 ? -0.5f : 0.5f;
		eyeOffset = rotate(headOrientation,
			{eyeSign * s_ipdMeters.load(std::memory_order_relaxed), 0.0f, 0.0f});
	}
	float profileOffsetY = 0.0f;
	float profileOffsetZ = 0.0f;
	const bool marioKartWorldPass = s_marioKartProfileEnabled.load(std::memory_order_relaxed) &&
		(config.generalEmulation.hacks & hack_MK64) != 0 &&
		(gSP.geometryMode & G_ZBUFFER) != 0;
	if (marioKartWorldPass) {
		if (!s_marioKartProfileLogged.exchange(true, std::memory_order_relaxed))
			LOG(LOG_MINIMAL, "Quest VR Mario Kart 64 close-chase profile active");
		profileOffsetY = s_marioKartCameraOffsetY.load(std::memory_order_relaxed);
		profileOffsetZ = s_marioKartCameraOffsetZ.load(std::memory_order_relaxed);
	}
	const Vector3 localCameraOffset{
		s_cameraOffsetX.load(std::memory_order_relaxed),
		s_cameraOffsetY.load(std::memory_order_relaxed) + profileOffsetY,
		s_cameraOffsetZ.load(std::memory_order_relaxed) + profileOffsetZ,
	};
	const Vector3 rotatedCameraOffset = rotate(headOrientation, localCameraOffset);
	const float worldScale = std::max(0.001f, s_worldUnitsPerMeter.load(std::memory_order_relaxed));
	const Vector3 cameraPosition{
		(headPosition.x + eyeOffset.x + rotatedCameraOffset.x) * worldScale,
		(headPosition.y + eyeOffset.y + rotatedCameraOffset.y) * worldScale,
		(headPosition.z + eyeOffset.z + rotatedCameraOffset.z) * worldScale,
	};

	const Quaternion inverseHeadOrientation = conjugate(headOrientation);
	std::array<float, 16> view{};
	quaternionMatrix(inverseHeadOrientation, view.data());
	const Vector3 viewTranslation = rotate(inverseHeadOrientation,
		{-cameraPosition.x, -cameraPosition.y, -cameraPosition.z});
	view[12] = viewTranslation.x;
	view[13] = viewTranslation.y;
	view[14] = viewTranslation.z;

	std::array<float, 16> inverseProjection{};
	if (!inverseMatrix(projection, inverseProjection.data())) {
		identity(destination);
		// Preserve a small, depth-dependent fallback disparity for unusual matrices.
		destination[12] = eye == 0 ? 0.004f : -0.004f;
		return;
	}
	std::array<float, 16> eyeProjection{};
	std::copy(projection, projection + 16, eyeProjection.begin());
	applyRuntimeProjection(eye, eyeProjection.data());
	std::array<float, 16> projectionView{};
	multiplyMatrices(eyeProjection.data(), view.data(), projectionView.data());
	multiplyMatrices(projectionView.data(), inverseProjection.data(), destination);
}

const std::array<float, 16>& getEyeTransform(unsigned int eye)
{
	std::array<float, 16> projection{};
	std::memcpy(projection.data(), gSP.matrix.projection, sizeof(float) * 16);
	const unsigned int poseGeneration = s_poseGeneration.load(std::memory_order_acquire);
	const unsigned int configGeneration = s_configGeneration.load(std::memory_order_acquire);
	if (!s_transformCache.valid || s_transformCache.poseGeneration != poseGeneration ||
		s_transformCache.configGeneration != configGeneration ||
		std::memcmp(s_transformCache.projection.data(), projection.data(), sizeof(float) * 16) != 0) {
		s_transformCache.projection = projection;
		for (unsigned int index = 0; index < 2; ++index)
			buildEyeTransform(index, projection.data(), s_transformCache.eyes[index].data());
		s_transformCache.poseGeneration = poseGeneration;
		s_transformCache.configGeneration = configGeneration;
		s_transformCache.valid = true;
	}
	return s_transformCache.eyes[std::min(eye, 1U)];
}

void setProgramEye(unsigned int eye, bool enabled, bool transformGeometry)
{
	const auto program = s_programs.find(s_currentProgram);
	if (program == s_programs.end() || program->second.enabled < 0)
		return;
	const ProgramUniforms& uniforms = program->second;
	if (!enabled) {
		glUniform1i(uniforms.enabled, 0);
		return;
	}
	if (uniforms.eye >= 0)
		glUniform1i(uniforms.eye, static_cast<GLint>(eye));
	if (!transformGeometry) {
		glUniform1i(uniforms.enabled, 1);
		return;
	}
	if (!isPerspectiveProjection(reinterpret_cast<const float*>(gSP.matrix.projection))) {
		// Keep orthographic and unusual screen-space triangle passes at zero disparity. They are
		// still duplicated into both eye viewports, like rectangle HUD elements.
		glUniform1i(uniforms.enabled, 0);
		return;
	}

	const std::array<float, 16>& matrix = getEyeTransform(eye);
	for (int row = 0; row < 4; ++row) {
		if (uniforms.rows[row] >= 0) {
			glUniform4f(uniforms.rows[row], matrix[row], matrix[4 + row],
				matrix[8 + row], matrix[12 + row]);
		}
	}
	glUniform1i(uniforms.enabled, 1);
}

int mapCoordinate(int value, int sourceOrigin, int sourceSize, int targetOrigin, int targetSize)
{
	if (sourceSize <= 0)
		return targetOrigin;
	return targetOrigin + static_cast<int>(std::floor(
		static_cast<double>(value - sourceOrigin) * targetSize / sourceSize));
}

} // namespace

namespace QuestVr {

bool isStereoEnabled()
{
	return s_enabled.load(std::memory_order_acquire) &&
		s_stereoEnabled.load(std::memory_order_relaxed);
}

void registerProgram(unsigned int program)
{
	ProgramUniforms uniforms;
	uniforms.enabled = glGetUniformLocation(program, "uQuestVrEnabled");
	uniforms.eye = glGetUniformLocation(program, "uQuestVrEye");
	uniforms.rows[0] = glGetUniformLocation(program, "uQuestVrClipRow0");
	uniforms.rows[1] = glGetUniformLocation(program, "uQuestVrClipRow1");
	uniforms.rows[2] = glGetUniformLocation(program, "uQuestVrClipRow2");
	uniforms.rows[3] = glGetUniformLocation(program, "uQuestVrClipRow3");
	s_programs[program] = uniforms;
}

void unregisterProgram(unsigned int program)
{
	s_programs.erase(program);
	if (s_currentProgram == program)
		s_currentProgram = 0;
}

void setCurrentProgram(unsigned int program)
{
	s_currentProgram = program;
}

void setViewport(int x, int y, int width, int height)
{
	s_viewport = {x, y, width, height};
}

void setScissor(int x, int y, int width, int height)
{
	s_scissor = {x, y, width, height};
}

void setScissorEnabled(bool enabled)
{
	s_scissorEnabled = enabled;
}

void resetGraphicsState()
{
	s_viewport = {};
	s_scissor = {};
	s_scissorEnabled = false;
	s_currentProgram = 0;
	s_transformCache.valid = false;
}

DrawScope::DrawScope(bool transformGeometry)
	: m_active(isStereoEnabled() && s_viewport.width >= 2 && s_viewport.height > 0)
	, m_transformGeometry(transformGeometry)
{
	if (m_active) {
		if (m_transformGeometry)
			s_geometryDraws.fetch_add(1, std::memory_order_relaxed);
		else
			s_rectangleDraws.fetch_add(1, std::memory_order_relaxed);
	}
}

DrawScope::~DrawScope()
{
	if (!m_active)
		return;
	setProgramEye(0, false, m_transformGeometry);
	glViewport(s_viewport.x, s_viewport.y, s_viewport.width, s_viewport.height);
	if (s_scissorEnabled)
		glScissor(s_scissor.x, s_scissor.y, s_scissor.width, s_scissor.height);
}

unsigned int DrawScope::eyeCount() const
{
	return m_active ? 2U : 1U;
}

void DrawScope::selectEye(unsigned int eye)
{
	if (!m_active)
		return;
	s_eyeDraws.fetch_add(1, std::memory_order_relaxed);

	const int leftWidth = s_viewport.width / 2;
	const int eyeWidth = eye == 0 ? leftWidth : s_viewport.width - leftWidth;
	const int eyeX = eye == 0 ? s_viewport.x : s_viewport.x + leftWidth;
	glViewport(eyeX, s_viewport.y, eyeWidth, s_viewport.height);

	if (s_scissorEnabled) {
		const int mappedStart = mapCoordinate(s_scissor.x, s_viewport.x, s_viewport.width, eyeX, eyeWidth);
		const int mappedEnd = mapCoordinate(s_scissor.x + s_scissor.width, s_viewport.x,
			s_viewport.width, eyeX, eyeWidth);
		const int clampedStart = std::max(eyeX, std::min(eyeX + eyeWidth, mappedStart));
		const int clampedEnd = std::max(clampedStart, std::min(eyeX + eyeWidth, mappedEnd));
		glScissor(clampedStart, s_scissor.y, clampedEnd - clampedStart, s_scissor.height);
	}

	setProgramEye(eye, true, m_transformGeometry);
}

} // namespace QuestVr

#if defined(__GNUC__)
#define QUEST_VR_EXPORT __attribute__((visibility("default")))
#else
#define QUEST_VR_EXPORT
#endif

extern "C" QUEST_VR_EXPORT void M64PQuestVrSetEnabled(int enabled)
{
	s_enabled.store(enabled != 0, std::memory_order_release);
	if (enabled != 0) {
		s_recenterRequested.store(true, std::memory_order_release);
		s_haveRuntimeViews.store(false, std::memory_order_release);
		s_marioKartProfileLogged.store(false, std::memory_order_relaxed);
	}
	s_configGeneration.fetch_add(1, std::memory_order_release);
	LOG(LOG_MINIMAL, "Quest VR geometry path %s", enabled != 0 ? "enabled" : "disabled");
}

extern "C" QUEST_VR_EXPORT void M64PQuestVrSetPose(float qx, float qy, float qz, float qw,
	float px, float py, float pz, std::int64_t timestamp)
{
	(void)timestamp;
	const Quaternion orientation = normalize({qx, qy, qz, qw});
	const Vector3 position{px, py, pz};
	if (s_recenterRequested.exchange(false, std::memory_order_acq_rel) ||
		!s_haveRecenterPose.load(std::memory_order_acquire)) {
		storePose(s_recenterPose, orientation, position);
		s_haveRecenterPose.store(true, std::memory_order_release);
	}
	storePose(s_pose, orientation, position);
	s_poseGeneration.fetch_add(1, std::memory_order_release);
}

extern "C" QUEST_VR_EXPORT void M64PQuestVrSetViews(
	float leftPx, float leftPy, float leftPz,
	float leftAngleLeft, float leftAngleRight, float leftAngleUp, float leftAngleDown,
	float rightPx, float rightPy, float rightPz,
	float rightAngleLeft, float rightAngleRight, float rightAngleUp, float rightAngleDown)
{
	const std::array<std::array<float, 7>, 2> views{{
		{{leftPx, leftPy, leftPz, leftAngleLeft, leftAngleRight, leftAngleUp, leftAngleDown}},
		{{rightPx, rightPy, rightPz, rightAngleLeft, rightAngleRight, rightAngleUp, rightAngleDown}},
	}};
	for (unsigned int eye = 0; eye < 2; ++eye) {
		AtomicView& destination = s_runtimeViews[eye];
		destination.px.store(views[eye][0], std::memory_order_relaxed);
		destination.py.store(views[eye][1], std::memory_order_relaxed);
		destination.pz.store(views[eye][2], std::memory_order_relaxed);
		destination.angleLeft.store(views[eye][3], std::memory_order_relaxed);
		destination.angleRight.store(views[eye][4], std::memory_order_relaxed);
		destination.angleUp.store(views[eye][5], std::memory_order_relaxed);
		destination.angleDown.store(views[eye][6], std::memory_order_relaxed);
	}
	s_haveRuntimeViews.store(true, std::memory_order_release);
	s_poseGeneration.fetch_add(1, std::memory_order_release);
}

extern "C" QUEST_VR_EXPORT void M64PQuestVrConfigure(int stereoEnabled, float ipdMeters,
	float worldUnitsPerMeter, float rotationStrength, int positionEnabled,
	float maxTranslationMeters, float cameraOffsetX, float cameraOffsetY, float cameraOffsetZ,
	int useOpenXrFov, int marioKartProfileEnabled,
	float marioKartCameraOffsetY, float marioKartCameraOffsetZ)
{
	s_stereoEnabled.store(stereoEnabled != 0, std::memory_order_relaxed);
	s_ipdMeters.store(std::max(0.0f, ipdMeters), std::memory_order_relaxed);
	s_worldUnitsPerMeter.store(std::max(0.001f, worldUnitsPerMeter), std::memory_order_relaxed);
	s_rotationStrength.store(rotationStrength, std::memory_order_relaxed);
	s_positionEnabled.store(positionEnabled != 0, std::memory_order_relaxed);
	s_maxTranslationMeters.store(std::max(0.0f, maxTranslationMeters), std::memory_order_relaxed);
	s_cameraOffsetX.store(cameraOffsetX, std::memory_order_relaxed);
	s_cameraOffsetY.store(cameraOffsetY, std::memory_order_relaxed);
	s_cameraOffsetZ.store(cameraOffsetZ, std::memory_order_relaxed);
	s_useOpenXrFov.store(useOpenXrFov != 0, std::memory_order_relaxed);
	s_marioKartProfileEnabled.store(marioKartProfileEnabled != 0, std::memory_order_relaxed);
	s_marioKartCameraOffsetY.store(marioKartCameraOffsetY, std::memory_order_relaxed);
	s_marioKartCameraOffsetZ.store(marioKartCameraOffsetZ, std::memory_order_relaxed);
	s_configGeneration.fetch_add(1, std::memory_order_release);
}

extern "C" QUEST_VR_EXPORT void M64PQuestVrRecenter()
{
	s_recenterRequested.store(true, std::memory_order_release);
}

extern "C" QUEST_VR_EXPORT void M64PQuestVrGetStats(unsigned int* geometryDraws,
	unsigned int* rectangleDraws, unsigned int* eyeDraws, unsigned int* poseGeneration)
{
	if (geometryDraws != nullptr)
		*geometryDraws = s_geometryDraws.load(std::memory_order_relaxed);
	if (rectangleDraws != nullptr)
		*rectangleDraws = s_rectangleDraws.load(std::memory_order_relaxed);
	if (eyeDraws != nullptr)
		*eyeDraws = s_eyeDraws.load(std::memory_order_relaxed);
	if (poseGeneration != nullptr)
		*poseGeneration = s_poseGeneration.load(std::memory_order_relaxed);
}
