#pragma once

namespace QuestVr {

bool isStereoEnabled();

/**
 * Records projection-matrix commands before GLideN64 mutates its active matrix.
 * Mario Kart 64 commonly loads guPerspective and then multiplies guLookAt into
 * the projection stack.  Retaining the first matrix lets the late eye transform
 * recover camera space without mistaking the folded result for an orthographic
 * HUD projection.
 */
void noteProjectionMatrix(const float* matrix, bool load);
void resetProjectionTracking();

/** Physical horizontal packing used by GLideN64 framebuffer objects in SBS mode. */
unsigned int framebufferWidthMultiplier();
float framebufferScale(float windowScaleX, float windowScaleY);
void markPackedFramebufferTexture(unsigned int texture);
void noteFramebufferAllocation(unsigned int n64Width, unsigned int n64Height,
	float scale, unsigned int physicalWidth, unsigned int physicalHeight,
	unsigned int nativeResolutionFactor);

void registerProgram(unsigned int program);
void unregisterProgram(unsigned int program);
void setCurrentProgram(unsigned int program);

void registerTexture(unsigned int texture, int width, int height);
void unregisterTexture(unsigned int texture);
void registerRenderbuffer(unsigned int renderbuffer, int width, int height);
void registerFramebufferTarget(unsigned int framebuffer, unsigned int attachment,
	unsigned int target, unsigned int resource);
void unregisterFramebuffer(unsigned int framebuffer);
void setFramebufferBinding(unsigned int target, unsigned int framebuffer);

void setViewport(int x, int y, int width, int height);
void setScissor(int x, int y, int width, int height);
void setScissorEnabled(bool enabled);
void resetGraphicsState();
void markFramePresented();
void noteGeometryVertices(unsigned int totalVertices, unsigned int modifiedPositionVertices);
void noteBackgroundRectangle();
void noteSprite2D();

/**
 * Maps an emulator-logical framebuffer blit into the packed left/right halves
 * of both source and destination targets. A non-stereo or untracked target is
 * left as one ordinary blit.
 */
class BlitScope {
public:
	BlitScope(unsigned int readFramebuffer, unsigned int drawFramebuffer);

	unsigned int eyeCount() const;
	int mapSourceX(int value, unsigned int eye) const;
	int mapDestinationX(int value, unsigned int eye) const;

private:
	bool m_active;
	int m_sourceWidth;
	int m_destinationWidth;
};

/**
 * Duplicates a GL draw into side-by-side eye viewports. Geometry scopes also
 * upload the current head/eye clip transform to GLideN64 combiner shaders.
 */
class DrawScope {
public:
	explicit DrawScope(bool transformGeometry);
	~DrawScope();

	DrawScope(const DrawScope&) = delete;
	DrawScope& operator=(const DrawScope&) = delete;

	unsigned int eyeCount() const;
	void selectEye(unsigned int eye);

private:
	bool m_active;
	bool m_transformGeometry;
	int m_targetWidth;
	int m_coordinateWidth;
};

} // namespace QuestVr
