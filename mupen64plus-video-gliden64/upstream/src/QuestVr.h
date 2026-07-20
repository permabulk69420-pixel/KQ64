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
bool isPackedFramebufferTexture(unsigned int texture);
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
void noteScreenSpaceBatch(unsigned int vertices);
void noteBackgroundRectangle();
void noteSprite2D();
void noteTexrectScratchTarget(unsigned int framebuffer, unsigned int texture,
	unsigned int width, unsigned int height);
void noteTexrectScratchDraw(unsigned int rectangles);
void noteFinalFramebufferBlit(unsigned int readFramebuffer, unsigned int texture,
	unsigned int physicalSourceWidth, unsigned int logicalSourceWidth,
	unsigned int sourceHeight, unsigned int physicalDestinationWidth,
	unsigned int logicalDestinationWidth, unsigned int destinationHeight,
	int sourceX0, int sourceX1, int destinationX0, int destinationX1,
	unsigned int filter);

/**
 * Maps framebuffer copies according to each target independently. Packed
 * sources select the matching eye half, mono sources are reused unchanged,
 * and packed destinations receive one copy per eye. Mono destinations remain
 * one ordinary copy.
 */
class BlitScope {
public:
	BlitScope(unsigned int readFramebuffer, unsigned int drawFramebuffer);

	unsigned int eyeCount() const;
	int mapSourceX(int value, unsigned int eye) const;
	int mapDestinationX(int value, unsigned int eye) const;

private:
	bool m_active;
	bool m_sourcePacked;
	bool m_destinationPacked;
	int m_sourceWidth;
	int m_destinationWidth;
	int m_sourceCoordinateWidth;
	int m_destinationCoordinateWidth;
};

/**
 * Duplicates a GL draw into side-by-side eye viewports. Geometry scopes upload
 * the current head/eye clip transform; selected screen-space scopes instead
 * apply only the OpenXR optical-centre correction needed for binocular fusion.
 */
class DrawScope {
public:
	enum class PrimitiveClass {
		Triangles,
		Rectangles,
		Lines,
	};

	explicit DrawScope(PrimitiveClass primitiveClass, bool transformGeometry,
		bool correctScreenSpaceProjection = false,
		bool sourceTexturePacked = false,
		unsigned int vertices = 0,
		unsigned int modifiedPositionVertices = 0);
	~DrawScope();

	DrawScope(const DrawScope&) = delete;
	DrawScope& operator=(const DrawScope&) = delete;

	unsigned int eyeCount() const;
	void selectEye(unsigned int eye);

private:
	bool m_active;
	bool m_transformGeometry;
	bool m_correctScreenSpaceProjection;
	bool m_sourceTexturePacked;
	int m_targetWidth;
	int m_coordinateWidth;
};

} // namespace QuestVr
