#pragma once

namespace QuestVr {

bool isStereoEnabled();

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
};

} // namespace QuestVr
