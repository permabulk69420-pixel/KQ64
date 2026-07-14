#pragma once

namespace QuestVr {

bool isStereoEnabled();

void registerProgram(unsigned int program);
void unregisterProgram(unsigned int program);
void setCurrentProgram(unsigned int program);

void setViewport(int x, int y, int width, int height);
void setScissor(int x, int y, int width, int height);
void setScissorEnabled(bool enabled);
void resetGraphicsState();

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
};

} // namespace QuestVr

