#include "QuestVr.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "Config.h"
#include "DisplayWindow.h"
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

struct RuntimeViewState {
	Vector3 position{};
	float angleLeft{-0.8f};
	float angleRight{0.8f};
	float angleUp{0.8f};
	float angleDown{-0.8f};
};

struct FramePoseState {
	bool valid{false};
	bool haveRuntimeViews{false};
	unsigned int generation{0};
	std::int64_t timestamp{0};
	Quaternion orientation{0.0f, 0.0f, 0.0f, 1.0f};
	Quaternion recenterOrientation{0.0f, 0.0f, 0.0f, 1.0f};
	Vector3 position{};
	Vector3 recenterPosition{};
	std::array<RuntimeViewState, 2> views{};
};

struct ProgramUniforms {
	GLint enabled{-1};
	GLint transformEnabled{-1};
	GLint eye{-1};
	std::array<GLint, 4> rows{{-1, -1, -1, -1}};
};

struct ResourceDimensions {
	int width{0};
	int height{0};
};

struct FramebufferTarget {
	GLuint resource{0};
	bool renderbuffer{false};
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
std::atomic<float> s_marioKartCameraOffsetY{0.0f};
std::atomic<float> s_marioKartCameraOffsetZ{0.0f};
std::atomic<unsigned int> s_poseGeneration{0};
std::atomic<unsigned int> s_configGeneration{1};
std::atomic<unsigned int> s_geometryDraws{0};
std::atomic<unsigned int> s_rectangleDraws{0};
std::atomic<unsigned int> s_eyeDraws{0};
std::atomic<unsigned int> s_perspectiveDraws{0};
std::atomic<unsigned int> s_orthographicDraws{0};
std::atomic<unsigned int> s_canonicalPerspectiveDraws{0};
std::atomic<unsigned int> s_foldedPerspectiveDraws{0};
std::atomic<unsigned int> s_projectionLoads{0};
std::atomic<unsigned int> s_foldedProjectionMultiplies{0};
std::atomic<unsigned int> s_missingTransformProgramDraws{0};
std::atomic<unsigned int> s_geometryVertices{0};
std::atomic<unsigned int> s_modifiedPositionVertices{0};
std::atomic<unsigned int> s_framebufferBlits{0};
std::atomic<unsigned int> s_backgroundRectangles{0};
std::atomic<unsigned int> s_sprite2DCommands{0};
std::atomic<unsigned int> s_targetWidthFallbacks{0};
std::atomic<unsigned int> s_lastTargetWidth{0};
std::atomic<unsigned int> s_framebufferAllocations{0};
std::atomic<unsigned int> s_lastFramebufferN64Width{0};
std::atomic<unsigned int> s_lastFramebufferN64Height{0};
std::atomic<unsigned int> s_lastFramebufferPhysicalWidth{0};
std::atomic<unsigned int> s_lastFramebufferPhysicalHeight{0};
std::atomic<unsigned int> s_lastFramebufferScaleMilli{0};
std::atomic<unsigned int> s_lastNativeResolutionFactor{0};
std::atomic<unsigned int> s_lastViewportWidth{0};
std::atomic<unsigned int> s_lastViewportHeight{0};
std::atomic<unsigned int> s_lastScissorWidth{0};
std::atomic<unsigned int> s_lastScissorHeight{0};
std::atomic<std::int64_t> s_poseTimestamp{0};
std::atomic<std::int64_t> s_transformPoseTimestamp{0};
std::atomic<std::int64_t> s_presentedPoseTimestamp{0};

AtomicPose s_pose;
AtomicPose s_recenterPose;
std::array<AtomicView, 2> s_runtimeViews;
std::array<AtomicView, 2> s_pendingRuntimeViews;
std::atomic<bool> s_havePendingRuntimeViews{false};
std::atomic<bool> s_framePoseNeedsLatch{true};
FramePoseState s_framePose;
RectState s_viewport;
RectState s_scissor;
bool s_scissorEnabled{false};
GLuint s_currentProgram{0};
GLuint s_drawFramebuffer{0};
std::unordered_map<GLuint, ProgramUniforms> s_programs;
std::unordered_map<GLuint, ResourceDimensions> s_textureDimensions;
std::unordered_map<GLuint, ResourceDimensions> s_renderbufferDimensions;
std::unordered_map<GLuint, FramebufferTarget> s_framebufferTargets;
std::unordered_set<GLuint> s_packedFramebufferTextures;
std::array<float, 16> s_baseProjection{};
bool s_baseProjectionValid{false};
bool s_projectionContainsView{false};

struct TransformCache {
	bool valid{false};
	bool marioKartWorldPass{false};
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

RuntimeViewState loadRuntimeView(const AtomicView& view)
{
	return {
		loadViewPosition(view),
		view.angleLeft.load(std::memory_order_relaxed),
		view.angleRight.load(std::memory_order_relaxed),
		view.angleUp.load(std::memory_order_relaxed),
		view.angleDown.load(std::memory_order_relaxed),
	};
}

void storeRuntimeView(AtomicView& destination, const RuntimeViewState& source)
{
	destination.px.store(source.position.x, std::memory_order_relaxed);
	destination.py.store(source.position.y, std::memory_order_relaxed);
	destination.pz.store(source.position.z, std::memory_order_relaxed);
	destination.angleLeft.store(source.angleLeft, std::memory_order_relaxed);
	destination.angleRight.store(source.angleRight, std::memory_order_relaxed);
	destination.angleUp.store(source.angleUp, std::memory_order_relaxed);
	destination.angleDown.store(source.angleDown, std::memory_order_relaxed);
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

void latchFramePose()
{
	if (!s_framePoseNeedsLatch.exchange(false, std::memory_order_acq_rel) && s_framePose.valid)
		return;

	unsigned int generationBefore = 0;
	unsigned int generationAfter = 0;
	do {
		generationBefore = s_poseGeneration.load(std::memory_order_acquire);
		if ((generationBefore & 1U) != 0)
			continue;
		s_framePose.orientation = loadOrientation(s_pose);
		s_framePose.recenterOrientation = loadOrientation(s_recenterPose);
		s_framePose.position = loadPosition(s_pose);
		s_framePose.recenterPosition = loadPosition(s_recenterPose);
		s_framePose.haveRuntimeViews = s_haveRuntimeViews.load(std::memory_order_acquire);
		for (unsigned int eye = 0; eye < 2; ++eye)
			s_framePose.views[eye] = loadRuntimeView(s_runtimeViews[eye]);
		s_framePose.timestamp = s_poseTimestamp.load(std::memory_order_relaxed);
		generationAfter = s_poseGeneration.load(std::memory_order_acquire);
	} while (generationBefore != generationAfter || (generationAfter & 1U) != 0);

	s_framePose.generation = generationAfter;
	s_framePose.valid = true;
	s_transformPoseTimestamp.store(s_framePose.timestamp, std::memory_order_release);
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

bool getWorldProjection(std::array<float, 16>& projection, bool& containsView)
{
	if (s_baseProjectionValid) {
		if (s_projectionContainsView &&
			(config.generalEmulation.hacks & hack_MK64) != 0 &&
			!s_marioKartProfileEnabled.load(std::memory_order_relaxed)) {
			containsView = false;
			return false;
		}
		projection = s_baseProjection;
		containsView = s_projectionContainsView;
		return true;
	}

	std::memcpy(projection.data(), gSP.matrix.projection, sizeof(float) * 16);
	containsView = false;
	return isPerspectiveProjection(projection.data());
}

void applyRuntimeProjection(unsigned int eye, const FramePoseState& framePose, float* projection)
{
	if (!s_useOpenXrFov.load(std::memory_order_relaxed) ||
		!framePose.haveRuntimeViews)
		return;

	// GLideN64 projection matrices use OpenGL column-major layout. Only replace the angular
	// terms for perspective world draws; retain the game's depth mapping and leave orthographic
	// HUD/background projections alone.
	if (!isPerspectiveProjection(projection))
		return;
	const RuntimeViewState& view = framePose.views[std::min(eye, 1U)];
	const float tanLeft = std::tan(view.angleLeft);
	const float tanRight = std::tan(view.angleRight);
	const float tanUp = std::tan(view.angleUp);
	const float tanDown = std::tan(view.angleDown);
	const float width = tanRight - tanLeft;
	const float height = tanUp - tanDown;
	if (!std::isfinite(width) || !std::isfinite(height) || width < 0.01f || height < 0.01f)
		return;
	projection[0] = 2.0f / width;
	projection[5] = 2.0f / height;
	projection[8] = (tanRight + tanLeft) / width;
	projection[9] = (tanUp + tanDown) / height;
}

void buildEyeTransform(unsigned int eye, const FramePoseState& framePose,
	bool marioKartWorldPass, const float* projection, float* destination)
{
	const Quaternion recenterOrientation = framePose.recenterOrientation;
	const Quaternion currentOrientation = framePose.orientation;
	Quaternion headOrientation = multiply(conjugate(recenterOrientation), currentOrientation);
	// This matrix is applied after the N64 camera has already produced clip coordinates. At that
	// late boundary the OpenXR view rotation has the opposite sign from an ordinary pre-projection
	// camera transform. Convert it here so user-facing +1 follows the headset; a negative setting
	// remains an intentional inversion.
	const float userRotationStrength = s_rotationStrength.load(std::memory_order_relaxed);
	headOrientation = scaleRotation(headOrientation, -userRotationStrength);

	Vector3 headPosition{0.0f, 0.0f, 0.0f};
	if (s_positionEnabled.load(std::memory_order_relaxed)) {
		const Vector3 recenterPosition = framePose.recenterPosition;
		const Vector3 currentPosition = framePose.position;
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
	if (framePose.haveRuntimeViews) {
		const Vector3 currentCenter = framePose.position;
		const Vector3 trackedEye = framePose.views[std::min(eye, 1U)].position;
		const Vector3 trackedLeft = framePose.views[0].position;
		const Vector3 trackedRight = framePose.views[1].position;
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
	if (marioKartWorldPass) {
		if (!s_marioKartProfileLogged.exchange(true, std::memory_order_relaxed))
			LOG(LOG_MINIMAL, "Quest VR Mario Kart 64 race-camera recovery active");
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
	applyRuntimeProjection(eye, framePose, eyeProjection.data());
	std::array<float, 16> projectionView{};
	multiplyMatrices(eyeProjection.data(), view.data(), projectionView.data());
	multiplyMatrices(projectionView.data(), inverseProjection.data(), destination);
}

const std::array<float, 16>& getEyeTransform(unsigned int eye)
{
	latchFramePose();
	std::array<float, 16> projection{};
	bool projectionContainsView = false;
	if (!getWorldProjection(projection, projectionContainsView)) {
		identity(projection.data());
	}
	const bool marioKartWorldPass = s_marioKartProfileEnabled.load(std::memory_order_relaxed) &&
		(config.generalEmulation.hacks & hack_MK64) != 0 &&
		(gSP.geometryMode & G_ZBUFFER) != 0;
	const unsigned int poseGeneration = s_framePose.generation;
	const unsigned int configGeneration = s_configGeneration.load(std::memory_order_acquire);
	if (!s_transformCache.valid || s_transformCache.poseGeneration != poseGeneration ||
		s_transformCache.marioKartWorldPass != marioKartWorldPass ||
		s_transformCache.configGeneration != configGeneration ||
		std::memcmp(s_transformCache.projection.data(), projection.data(), sizeof(float) * 16) != 0) {
		s_transformCache.projection = projection;
		for (unsigned int index = 0; index < 2; ++index)
			buildEyeTransform(index, s_framePose, marioKartWorldPass, projection.data(),
				s_transformCache.eyes[index].data());
		s_transformCache.marioKartWorldPass = marioKartWorldPass;
		s_transformCache.poseGeneration = poseGeneration;
		s_transformCache.configGeneration = configGeneration;
		s_transformCache.valid = true;
	}
	return s_transformCache.eyes[std::min(eye, 1U)];
}

void setProgramEye(unsigned int eye, bool enabled, bool transformGeometry)
{
	const auto program = s_programs.find(s_currentProgram);
	if (program == s_programs.end())
		return;
	const ProgramUniforms& uniforms = program->second;
	if (!enabled) {
		if (uniforms.enabled >= 0)
			glUniform1i(uniforms.enabled, 0);
		if (uniforms.transformEnabled >= 0)
			glUniform1i(uniforms.transformEnabled, 0);
		return;
	}
	if (uniforms.enabled >= 0)
		glUniform1i(uniforms.enabled, 1);
	if (uniforms.eye >= 0)
		glUniform1i(uniforms.eye, static_cast<GLint>(eye));

	std::array<float, 16> worldProjection{};
	bool projectionContainsView = false;
	const bool applyTransform = transformGeometry &&
		getWorldProjection(worldProjection, projectionContainsView);
	if (uniforms.transformEnabled >= 0)
		glUniform1i(uniforms.transformEnabled, applyTransform ? 1 : 0);
	if (!applyTransform)
		return;

	const std::array<float, 16>& matrix = getEyeTransform(eye);
	for (int row = 0; row < 4; ++row) {
		if (uniforms.rows[row] >= 0) {
			glUniform4f(uniforms.rows[row], matrix[row], matrix[4 + row],
				matrix[8 + row], matrix[12 + row]);
		}
	}
}

int mapCoordinate(int value, int sourceOrigin, int sourceSize, int targetOrigin, int targetSize)
{
	if (sourceSize <= 0)
		return targetOrigin;
	return targetOrigin + static_cast<int>(std::floor(
		static_cast<double>(value - sourceOrigin) * targetSize / sourceSize));
}

int getFramebufferWidth(GLuint framebuffer)
{
	const int screenWidth = static_cast<int>(dwnd().getScreenWidth());
	if (framebuffer == 0)
		return screenWidth;

	const auto target = s_framebufferTargets.find(framebuffer);
	if (target == s_framebufferTargets.end()) {
		return 0;
	}
	const auto& dimensions = target->second.renderbuffer
		? s_renderbufferDimensions : s_textureDimensions;
	const auto resource = dimensions.find(target->second.resource);
	if (resource == dimensions.end())
		return 0;
	return resource->second.width;
}

bool isPackedFramebuffer(GLuint framebuffer)
{
	if (framebuffer == 0)
		return s_enabled.load(std::memory_order_acquire) &&
			s_stereoEnabled.load(std::memory_order_relaxed);

	const auto target = s_framebufferTargets.find(framebuffer);
	if (target == s_framebufferTargets.end() || target->second.renderbuffer)
		return false;
	return s_packedFramebufferTextures.count(target->second.resource) != 0;
}

int getFramebufferCoordinateWidth(GLuint framebuffer, int physicalWidth)
{
	return isPackedFramebuffer(framebuffer)
		? std::max(1, physicalWidth / 2) : physicalWidth;
}

int getDrawTargetWidth()
{
	int width = getFramebufferWidth(s_drawFramebuffer);
	if (width <= 0) {
		s_targetWidthFallbacks.fetch_add(1, std::memory_order_relaxed);
		width = static_cast<int>(dwnd().getScreenWidth());
	}
	s_lastTargetWidth.store(static_cast<unsigned int>(std::max(0, width)),
		std::memory_order_relaxed);
	return width;
}

int getDrawCoordinateWidth(int targetWidth)
{
	// Direct draws to the Android/default surface historically provide physical
	// SBS viewport coordinates. GLideN64 framebuffer objects provide per-eye
	// logical coordinates and are explicitly marked as packed.
	if (s_drawFramebuffer == 0)
		return targetWidth;
	return getFramebufferCoordinateWidth(s_drawFramebuffer, targetWidth);
}

int mapEyeCoordinate(int value, int physicalWidth, int coordinateWidth, unsigned int eye)
{
	const int leftWidth = physicalWidth / 2;
	const int eyeOrigin = eye == 0 ? 0 : leftWidth;
	const int eyeWidth = eye == 0 ? leftWidth : physicalWidth - leftWidth;
	return mapCoordinate(value, 0, coordinateWidth, eyeOrigin, eyeWidth);
}

} // namespace

namespace QuestVr {

bool isStereoEnabled()
{
	return s_enabled.load(std::memory_order_acquire) &&
		s_stereoEnabled.load(std::memory_order_relaxed);
}

void noteProjectionMatrix(const float* matrix, bool load)
{
	if (matrix == nullptr)
		return;

	if (load) {
		s_projectionLoads.fetch_add(1, std::memory_order_relaxed);
		s_projectionContainsView = false;
		s_baseProjectionValid = isPerspectiveProjection(matrix);
		if (s_baseProjectionValid)
			std::copy(matrix, matrix + 16, s_baseProjection.begin());
	} else if (s_baseProjectionValid) {
		// A projection-stack multiply after a canonical perspective load is commonly a
		// view matrix in Mario Kart 64.  Keep the original perspective matrix: inverse(P)
		// then reconstructs camera-space vertices from the already folded P*V clip value.
		s_projectionContainsView = true;
		s_foldedProjectionMultiplies.fetch_add(1, std::memory_order_relaxed);
	}
	s_transformCache.valid = false;
}

void resetProjectionTracking()
{
	s_baseProjectionValid = false;
	s_projectionContainsView = false;
	s_transformCache.valid = false;
}

unsigned int framebufferWidthMultiplier()
{
	return isStereoEnabled() ? 2U : 1U;
}

float framebufferScale(float windowScaleX, float windowScaleY)
{
	if (!isStereoEnabled()) {
		// Preserve a usable Android fallback when a VR-sized (two-eye-wide) producer
		// was selected but OpenXR initialization later failed.
		if (windowScaleY > 0.0f && windowScaleX >= windowScaleY * 1.9f)
			return std::max(std::min(windowScaleX * 0.5f, windowScaleY), 1.0f);
		return std::max(windowScaleX, 1.0f);
	}
	// The Android target is one side-by-side row.  GLideN64's normal stretch aspect
	// makes scaleX describe both eyes, while scaleY already describes one eye.
	// Keep one scalar N64-to-eye scale and add the second eye only to physical width.
	const float perEyeScaleX = windowScaleX * 0.5f;
	const float safeScaleY = windowScaleY > 0.0f ? windowScaleY : perEyeScaleX;
	return std::max(std::min(perEyeScaleX, safeScaleY), 1.0f);
}

void markPackedFramebufferTexture(unsigned int texture)
{
	if (texture != 0 && isStereoEnabled())
		s_packedFramebufferTextures.insert(texture);
}

void noteFramebufferAllocation(unsigned int n64Width, unsigned int n64Height,
	float scale, unsigned int physicalWidth, unsigned int physicalHeight,
	unsigned int nativeResolutionFactor)
{
	if (!isStereoEnabled())
		return;
	s_framebufferAllocations.fetch_add(1, std::memory_order_relaxed);
	const std::uint64_t candidatePixels = static_cast<std::uint64_t>(physicalWidth) *
		physicalHeight;
	const std::uint64_t recordedPixels = static_cast<std::uint64_t>(
		s_lastFramebufferPhysicalWidth.load(std::memory_order_relaxed)) *
		s_lastFramebufferPhysicalHeight.load(std::memory_order_relaxed);
	if (candidatePixels < recordedPixels)
		return;
	s_lastFramebufferN64Width.store(n64Width, std::memory_order_relaxed);
	s_lastFramebufferN64Height.store(n64Height, std::memory_order_relaxed);
	s_lastFramebufferPhysicalWidth.store(physicalWidth, std::memory_order_relaxed);
	s_lastFramebufferPhysicalHeight.store(physicalHeight, std::memory_order_relaxed);
	s_lastFramebufferScaleMilli.store(static_cast<unsigned int>(
		std::max(0.0f, scale) * 1000.0f + 0.5f), std::memory_order_relaxed);
	s_lastNativeResolutionFactor.store(nativeResolutionFactor, std::memory_order_relaxed);
}

void registerProgram(unsigned int program)
{
	ProgramUniforms uniforms;
	uniforms.enabled = glGetUniformLocation(program, "uQuestVrEnabled");
	uniforms.transformEnabled = glGetUniformLocation(program, "uQuestVrTransformEnabled");
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

void registerTexture(unsigned int texture, int width, int height)
{
	if (texture != 0 && width > 0 && height > 0)
		s_textureDimensions[texture] = {width, height};
}

void unregisterTexture(unsigned int texture)
{
	s_textureDimensions.erase(texture);
	s_packedFramebufferTextures.erase(texture);
}

void registerRenderbuffer(unsigned int renderbuffer, int width, int height)
{
	if (renderbuffer != 0 && width > 0 && height > 0)
		s_renderbufferDimensions[renderbuffer] = {width, height};
}

void registerFramebufferTarget(unsigned int framebuffer, unsigned int attachment,
	unsigned int target, unsigned int resource)
{
	if (framebuffer == 0 || attachment != GL_COLOR_ATTACHMENT0)
		return;
	s_framebufferTargets[framebuffer] = {resource, target == GL_RENDERBUFFER};
}

void unregisterFramebuffer(unsigned int framebuffer)
{
	s_framebufferTargets.erase(framebuffer);
	if (s_drawFramebuffer == framebuffer)
		s_drawFramebuffer = 0;
}

void setFramebufferBinding(unsigned int target, unsigned int framebuffer)
{
	if (target == GL_FRAMEBUFFER || target == GL_DRAW_FRAMEBUFFER)
		s_drawFramebuffer = framebuffer;
}

void setViewport(int x, int y, int width, int height)
{
	s_viewport = {x, y, width, height};
	s_lastViewportWidth.store(static_cast<unsigned int>(std::max(0, width)),
		std::memory_order_relaxed);
	s_lastViewportHeight.store(static_cast<unsigned int>(std::max(0, height)),
		std::memory_order_relaxed);
}

void setScissor(int x, int y, int width, int height)
{
	s_scissor = {x, y, width, height};
	s_lastScissorWidth.store(static_cast<unsigned int>(std::max(0, width)),
		std::memory_order_relaxed);
	s_lastScissorHeight.store(static_cast<unsigned int>(std::max(0, height)),
		std::memory_order_relaxed);
}

void setScissorEnabled(bool enabled)
{
	s_scissorEnabled = enabled;
}

void resetGraphicsState()
{
	resetProjectionTracking();
	s_viewport = {};
	s_scissor = {};
	s_scissorEnabled = false;
	s_currentProgram = 0;
	s_drawFramebuffer = 0;
	s_textureDimensions.clear();
	s_renderbufferDimensions.clear();
	s_framebufferTargets.clear();
	s_packedFramebufferTextures.clear();
	s_transformCache.valid = false;
	s_framePose.valid = false;
	s_framePoseNeedsLatch.store(true, std::memory_order_release);
}

void markFramePresented()
{
	if (!isStereoEnabled())
		return;
	// Perspective world draws normally latch the frame pose. Menu/HUD-only
	// frames may contain no transformed draw at all, so associate those frames
	// with the latest complete pose snapshot at presentation time.
	latchFramePose();
	s_presentedPoseTimestamp.store(
		s_transformPoseTimestamp.load(std::memory_order_acquire),
		std::memory_order_release);
	s_framePoseNeedsLatch.store(true, std::memory_order_release);
}

void noteGeometryVertices(unsigned int totalVertices, unsigned int modifiedPositionVertices)
{
	if (!isStereoEnabled())
		return;
	s_geometryVertices.fetch_add(totalVertices, std::memory_order_relaxed);
	s_modifiedPositionVertices.fetch_add(modifiedPositionVertices,
		std::memory_order_relaxed);
}

void noteBackgroundRectangle()
{
	if (isStereoEnabled())
		s_backgroundRectangles.fetch_add(1, std::memory_order_relaxed);
}

void noteSprite2D()
{
	if (isStereoEnabled())
		s_sprite2DCommands.fetch_add(1, std::memory_order_relaxed);
}

BlitScope::BlitScope(unsigned int readFramebuffer, unsigned int drawFramebuffer)
	: m_active(isStereoEnabled())
	, m_sourcePacked(isPackedFramebuffer(readFramebuffer))
	, m_destinationPacked(isPackedFramebuffer(drawFramebuffer))
	, m_sourceWidth(getFramebufferWidth(readFramebuffer))
	, m_destinationWidth(getFramebufferWidth(drawFramebuffer))
	, m_sourceCoordinateWidth(getFramebufferCoordinateWidth(readFramebuffer, m_sourceWidth))
	, m_destinationCoordinateWidth(getFramebufferCoordinateWidth(drawFramebuffer,
		m_destinationWidth))
{
	// Stereo duplication is driven by the destination. A mono source is sampled
	// unchanged for both eyes; a packed source selects the corresponding eye.
	m_active = m_active && m_destinationPacked && m_destinationWidth >= 2 &&
		m_sourceWidth > 0;
	if (m_active)
		s_framebufferBlits.fetch_add(1, std::memory_order_relaxed);
}

unsigned int BlitScope::eyeCount() const
{
	return m_active ? 2U : 1U;
}

int BlitScope::mapSourceX(int value, unsigned int eye) const
{
	return m_active && m_sourcePacked
		? mapEyeCoordinate(value, m_sourceWidth, m_sourceCoordinateWidth, eye)
		: value;
}

int BlitScope::mapDestinationX(int value, unsigned int eye) const
{
	return m_active && m_destinationPacked
		? mapEyeCoordinate(value, m_destinationWidth, m_destinationCoordinateWidth, eye)
		: value;
}

DrawScope::DrawScope(bool transformGeometry)
	: m_active(isStereoEnabled() && isPackedFramebuffer(s_drawFramebuffer) &&
		s_viewport.width >= 2 && s_viewport.height > 0)
	, m_transformGeometry(transformGeometry)
	, m_targetWidth(getDrawTargetWidth())
	, m_coordinateWidth(getDrawCoordinateWidth(m_targetWidth))
{
	m_active = m_active && m_targetWidth >= 2;
	if (m_active) {
		if (m_transformGeometry) {
			s_geometryDraws.fetch_add(1, std::memory_order_relaxed);
			std::array<float, 16> worldProjection{};
			bool projectionContainsView = false;
			const bool perspective = getWorldProjection(worldProjection,
				projectionContainsView);
			if (perspective) {
				s_perspectiveDraws.fetch_add(1, std::memory_order_relaxed);
				if (projectionContainsView)
					s_foldedPerspectiveDraws.fetch_add(1, std::memory_order_relaxed);
				else
					s_canonicalPerspectiveDraws.fetch_add(1, std::memory_order_relaxed);
				const auto program = s_programs.find(s_currentProgram);
				if (program == s_programs.end() || program->second.transformEnabled < 0 ||
					program->second.rows[0] < 0 || program->second.rows[1] < 0 ||
					program->second.rows[2] < 0 || program->second.rows[3] < 0) {
					s_missingTransformProgramDraws.fetch_add(1,
						std::memory_order_relaxed);
				}
			} else {
				s_orthographicDraws.fetch_add(1, std::memory_order_relaxed);
			}
		} else {
			s_rectangleDraws.fetch_add(1, std::memory_order_relaxed);
		}
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

	const int leftTargetWidth = m_targetWidth / 2;
	const int eyeTargetX = eye == 0 ? 0 : leftTargetWidth;
	const int eyeTargetWidth = eye == 0 ? leftTargetWidth : m_targetWidth - leftTargetWidth;
	const int viewportStart = mapCoordinate(s_viewport.x, 0, m_coordinateWidth,
		eyeTargetX, eyeTargetWidth);
	const int viewportEnd = mapCoordinate(s_viewport.x + s_viewport.width, 0, m_coordinateWidth,
		eyeTargetX, eyeTargetWidth);
	glViewport(viewportStart, s_viewport.y, viewportEnd - viewportStart, s_viewport.height);

	if (s_scissorEnabled) {
		const int mappedStart = mapCoordinate(s_scissor.x, 0, m_coordinateWidth,
			eyeTargetX, eyeTargetWidth);
		const int mappedEnd = mapCoordinate(s_scissor.x + s_scissor.width, 0,
			m_coordinateWidth, eyeTargetX, eyeTargetWidth);
		const int clampedStart = std::max(eyeTargetX,
			std::min(eyeTargetX + eyeTargetWidth, mappedStart));
		const int clampedEnd = std::max(clampedStart,
			std::min(eyeTargetX + eyeTargetWidth, mappedEnd));
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
		QuestVr::resetProjectionTracking();
		s_geometryDraws.store(0, std::memory_order_relaxed);
		s_rectangleDraws.store(0, std::memory_order_relaxed);
		s_eyeDraws.store(0, std::memory_order_relaxed);
		s_perspectiveDraws.store(0, std::memory_order_relaxed);
		s_orthographicDraws.store(0, std::memory_order_relaxed);
		s_canonicalPerspectiveDraws.store(0, std::memory_order_relaxed);
		s_foldedPerspectiveDraws.store(0, std::memory_order_relaxed);
		s_projectionLoads.store(0, std::memory_order_relaxed);
		s_foldedProjectionMultiplies.store(0, std::memory_order_relaxed);
		s_missingTransformProgramDraws.store(0, std::memory_order_relaxed);
		s_geometryVertices.store(0, std::memory_order_relaxed);
		s_modifiedPositionVertices.store(0, std::memory_order_relaxed);
		s_framebufferBlits.store(0, std::memory_order_relaxed);
		s_backgroundRectangles.store(0, std::memory_order_relaxed);
		s_sprite2DCommands.store(0, std::memory_order_relaxed);
		s_targetWidthFallbacks.store(0, std::memory_order_relaxed);
		s_lastTargetWidth.store(0, std::memory_order_relaxed);
		s_framebufferAllocations.store(0, std::memory_order_relaxed);
		s_lastFramebufferN64Width.store(0, std::memory_order_relaxed);
		s_lastFramebufferN64Height.store(0, std::memory_order_relaxed);
		s_lastFramebufferPhysicalWidth.store(0, std::memory_order_relaxed);
		s_lastFramebufferPhysicalHeight.store(0, std::memory_order_relaxed);
		s_lastFramebufferScaleMilli.store(0, std::memory_order_relaxed);
		s_lastNativeResolutionFactor.store(0, std::memory_order_relaxed);
		s_lastViewportWidth.store(0, std::memory_order_relaxed);
		s_lastViewportHeight.store(0, std::memory_order_relaxed);
		s_lastScissorWidth.store(0, std::memory_order_relaxed);
		s_lastScissorHeight.store(0, std::memory_order_relaxed);
		s_recenterRequested.store(true, std::memory_order_release);
		s_haveRuntimeViews.store(false, std::memory_order_release);
		s_havePendingRuntimeViews.store(false, std::memory_order_release);
		s_marioKartProfileLogged.store(false, std::memory_order_relaxed);
		s_transformPoseTimestamp.store(0, std::memory_order_relaxed);
		s_presentedPoseTimestamp.store(0, std::memory_order_relaxed);
		s_framePoseNeedsLatch.store(true, std::memory_order_release);
	}
	s_configGeneration.fetch_add(1, std::memory_order_release);
	LOG(LOG_MINIMAL, "Quest VR geometry path %s", enabled != 0 ? "enabled" : "disabled");
}

extern "C" QUEST_VR_EXPORT void M64PQuestVrSetPose(float qx, float qy, float qz, float qw,
	float px, float py, float pz, std::int64_t timestamp)
{
	// Odd generations mark a publication in progress; the GL thread only latches even snapshots.
	s_poseGeneration.fetch_add(1, std::memory_order_acq_rel);
	const Quaternion orientation = normalize({qx, qy, qz, qw});
	const Vector3 position{px, py, pz};
	if (s_recenterRequested.exchange(false, std::memory_order_acq_rel) ||
		!s_haveRecenterPose.load(std::memory_order_acquire)) {
		storePose(s_recenterPose, orientation, position);
		s_haveRecenterPose.store(true, std::memory_order_release);
	}
	storePose(s_pose, orientation, position);
	if (s_havePendingRuntimeViews.load(std::memory_order_acquire)) {
		for (unsigned int eye = 0; eye < 2; ++eye)
			storeRuntimeView(s_runtimeViews[eye], loadRuntimeView(s_pendingRuntimeViews[eye]));
		s_haveRuntimeViews.store(true, std::memory_order_relaxed);
	}
	s_poseTimestamp.store(timestamp, std::memory_order_relaxed);
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
		AtomicView& destination = s_pendingRuntimeViews[eye];
		destination.px.store(views[eye][0], std::memory_order_relaxed);
		destination.py.store(views[eye][1], std::memory_order_relaxed);
		destination.pz.store(views[eye][2], std::memory_order_relaxed);
		destination.angleLeft.store(views[eye][3], std::memory_order_relaxed);
		destination.angleRight.store(views[eye][4], std::memory_order_relaxed);
		destination.angleUp.store(views[eye][5], std::memory_order_relaxed);
		destination.angleDown.store(views[eye][6], std::memory_order_relaxed);
	}
	s_havePendingRuntimeViews.store(true, std::memory_order_release);
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
	unsigned int* rectangleDraws, unsigned int* eyeDraws, unsigned int* poseGeneration,
	unsigned int* targetWidthFallbacks, unsigned int* lastTargetWidth,
	unsigned int* perspectiveDraws, unsigned int* orthographicDraws,
	unsigned int* missingTransformProgramDraws, unsigned int* geometryVertices,
	unsigned int* modifiedPositionVertices, unsigned int* framebufferBlits,
	unsigned int* backgroundRectangles, unsigned int* sprite2DCommands)
{
	if (geometryDraws != nullptr)
		*geometryDraws = s_geometryDraws.load(std::memory_order_relaxed);
	if (rectangleDraws != nullptr)
		*rectangleDraws = s_rectangleDraws.load(std::memory_order_relaxed);
	if (eyeDraws != nullptr)
		*eyeDraws = s_eyeDraws.load(std::memory_order_relaxed);
	if (poseGeneration != nullptr)
		*poseGeneration = s_poseGeneration.load(std::memory_order_relaxed);
	if (targetWidthFallbacks != nullptr)
		*targetWidthFallbacks = s_targetWidthFallbacks.load(std::memory_order_relaxed);
	if (lastTargetWidth != nullptr)
		*lastTargetWidth = s_lastTargetWidth.load(std::memory_order_relaxed);
	if (perspectiveDraws != nullptr)
		*perspectiveDraws = s_perspectiveDraws.load(std::memory_order_relaxed);
	if (orthographicDraws != nullptr)
		*orthographicDraws = s_orthographicDraws.load(std::memory_order_relaxed);
	if (missingTransformProgramDraws != nullptr)
		*missingTransformProgramDraws =
			s_missingTransformProgramDraws.load(std::memory_order_relaxed);
	if (geometryVertices != nullptr)
		*geometryVertices = s_geometryVertices.load(std::memory_order_relaxed);
	if (modifiedPositionVertices != nullptr)
		*modifiedPositionVertices =
			s_modifiedPositionVertices.load(std::memory_order_relaxed);
	if (framebufferBlits != nullptr)
		*framebufferBlits = s_framebufferBlits.load(std::memory_order_relaxed);
	if (backgroundRectangles != nullptr)
		*backgroundRectangles = s_backgroundRectangles.load(std::memory_order_relaxed);
	if (sprite2DCommands != nullptr)
		*sprite2DCommands = s_sprite2DCommands.load(std::memory_order_relaxed);
}

extern "C" QUEST_VR_EXPORT void M64PQuestVrGetPipelineStats(
	unsigned int* canonicalPerspectiveDraws, unsigned int* foldedPerspectiveDraws,
	unsigned int* projectionLoads, unsigned int* foldedProjectionMultiplies,
	unsigned int* framebufferAllocations, unsigned int* framebufferN64Width,
	unsigned int* framebufferN64Height, unsigned int* framebufferPhysicalWidth,
	unsigned int* framebufferPhysicalHeight, unsigned int* framebufferScaleMilli,
	unsigned int* nativeResolutionFactor, unsigned int* viewportWidth,
	unsigned int* viewportHeight, unsigned int* scissorWidth,
	unsigned int* scissorHeight)
{
	if (canonicalPerspectiveDraws != nullptr)
		*canonicalPerspectiveDraws =
			s_canonicalPerspectiveDraws.load(std::memory_order_relaxed);
	if (foldedPerspectiveDraws != nullptr)
		*foldedPerspectiveDraws = s_foldedPerspectiveDraws.load(std::memory_order_relaxed);
	if (projectionLoads != nullptr)
		*projectionLoads = s_projectionLoads.load(std::memory_order_relaxed);
	if (foldedProjectionMultiplies != nullptr)
		*foldedProjectionMultiplies =
			s_foldedProjectionMultiplies.load(std::memory_order_relaxed);
	if (framebufferAllocations != nullptr)
		*framebufferAllocations = s_framebufferAllocations.load(std::memory_order_relaxed);
	if (framebufferN64Width != nullptr)
		*framebufferN64Width = s_lastFramebufferN64Width.load(std::memory_order_relaxed);
	if (framebufferN64Height != nullptr)
		*framebufferN64Height = s_lastFramebufferN64Height.load(std::memory_order_relaxed);
	if (framebufferPhysicalWidth != nullptr)
		*framebufferPhysicalWidth =
			s_lastFramebufferPhysicalWidth.load(std::memory_order_relaxed);
	if (framebufferPhysicalHeight != nullptr)
		*framebufferPhysicalHeight =
			s_lastFramebufferPhysicalHeight.load(std::memory_order_relaxed);
	if (framebufferScaleMilli != nullptr)
		*framebufferScaleMilli = s_lastFramebufferScaleMilli.load(std::memory_order_relaxed);
	if (nativeResolutionFactor != nullptr)
		*nativeResolutionFactor = s_lastNativeResolutionFactor.load(std::memory_order_relaxed);
	if (viewportWidth != nullptr)
		*viewportWidth = s_lastViewportWidth.load(std::memory_order_relaxed);
	if (viewportHeight != nullptr)
		*viewportHeight = s_lastViewportHeight.load(std::memory_order_relaxed);
	if (scissorWidth != nullptr)
		*scissorWidth = s_lastScissorWidth.load(std::memory_order_relaxed);
	if (scissorHeight != nullptr)
		*scissorHeight = s_lastScissorHeight.load(std::memory_order_relaxed);
}

extern "C" QUEST_VR_EXPORT std::int64_t M64PQuestVrGetPresentedPoseTimestamp()
{
	return s_presentedPoseTimestamp.load(std::memory_order_acquire);
}
