#include <assert.h>
#include <cstdlib>
#include "Config.h"
#include "RSP.h"
#include "VI.h"
#include "Graphics/Context.h"
#include "DisplayWindow.h"
#include "PluginAPI.h"
#include "FrameBuffer.h"
#include "QuestVr.h"

bool DisplayWindow::start()
{
	if (!_start())
		return false;

	graphics::ObjectHandle::defaultFramebuffer = _getDefaultFramebuffer();

	gfxContext.init();
	m_drawer._initData();
	m_buffersSwapCount = 0;

	// only query max MSAA when needed
	if (m_maxMsaa == 0) {
		m_maxMsaa = gfxContext.getMaxMSAALevel();
	}

	if (m_maxAnisotropy == 0) {
		m_maxAnisotropy = static_cast<u32>(gfxContext.getMaxAnisotropy());
	}

	return true;
}

void DisplayWindow::stop()
{
	m_drawer._destroyData();
	gfxContext.destroy();
	_stop();
}

void DisplayWindow::restart()
{
	_restart();
	m_bResizeWindow = true;
}

void DisplayWindow::swapBuffers()
{
	m_drawer.drawOSD();
	m_drawer.clearStatistics();
	QuestVr::markFramePresented();
	_swapBuffers();
	if (!RSP.LLE) {
		if ((config.generalEmulation.hacks & hack_doNotResetOtherModeL) == 0)
			gDP.otherMode.l = 0;
		if ((config.generalEmulation.hacks & hack_doNotResetOtherModeH) == 0)
			gDP.otherMode.h = 0x0CFF;
	}
	++m_buffersSwapCount;
}

void DisplayWindow::setCaptureScreen(const char * const _strDirectory)
{
	::mbstowcs(m_strScreenDirectory, _strDirectory, PLUGIN_PATH_SIZE - 1);
	m_bCaptureScreen = true;
}

void DisplayWindow::saveScreenshot()
{
	if (!m_bCaptureScreen)
		return;
	_saveScreenshot();
	m_bCaptureScreen = false;
}

void DisplayWindow::saveBufferContent(FrameBuffer * _pBuffer)
{
	saveBufferContent(_pBuffer->m_FBO, _pBuffer->m_pTexture);
}

void DisplayWindow::saveBufferContent(graphics::ObjectHandle _fbo, CachedTexture *_pTexture)
{
	if (wcslen(m_strScreenDirectory) == 0) {
		api().FindPluginPath(m_strScreenDirectory);
		std::wstring pluginPath(m_strScreenDirectory);
		if (pluginPath.back() != L'/')
			pluginPath += L'/';
		::wcsncpy(m_strScreenDirectory, pluginPath.c_str(), std::min(size_t(PLUGIN_PATH_SIZE), pluginPath.length() + 1));
	}
	_saveBufferContent(_fbo, _pTexture);
}

bool DisplayWindow::changeWindow()
{
	if (!m_bToggleFullscreen)
		return false;
	m_drawer._destroyData();
	_changeWindow();
	updateScale();
	m_drawer._initData();
	m_bToggleFullscreen = false;
	return true;
}

void DisplayWindow::closeWindow()
{
	if (!m_bToggleFullscreen || !m_bFullscreen)
		return;
	m_drawer._destroyData();
	_changeWindow();
	m_bToggleFullscreen = false;
}


void DisplayWindow::setWindowSize(u32 _width, u32 _height)
{
	if (m_width != _width || m_height != _height) {
		m_resizeWidth = _width;
		m_resizeHeight = _height;
		m_bResizeWindow = true;
	}
}

bool DisplayWindow::resizeWindow()
{
	if (!m_bResizeWindow)
		return false;
	m_drawer._destroyData();
	if (!_resizeWindow())
		if(!_start())
			return false;
	updateScale();
	m_drawer._initData();
	m_bResizeWindow = false;
	return true;
}

void DisplayWindow::updateScale()
{
	if (VI.width == 0 || VI.height == 0)
		return;
	m_scaleX = static_cast<f32>(m_width) / static_cast<f32>(VI.width);
	m_scaleY = static_cast<f32>(m_height) / static_cast<f32>(VI.height);
}

void DisplayWindow::_setBufferSize()
{
	m_bAdjustScreen = false;

	// Every decision below is about the shape of one eye's image. In VR the window is a
	// packed side-by-side pair, so m_screenWidth spans two eyes and comparing it against
	// the height makes a 4:3 per-eye target look like an ultrawide screen: "adjust" then
	// computes a half-width squeeze that should never fire, and the forced ratios clamp
	// the width below the real buffer so the second eye region is left uncovered. Reason
	// in one-eye width and expand back afterwards. Outside VR the multiplier is 1 and this
	// is the original calculation.
	const u32 eyeCount = QuestVr::framebufferWidthMultiplier() == 0 ?
		1 : QuestVr::framebufferWidthMultiplier();
	const u32 screenWidth = m_screenWidth / eyeCount;
	const u32 screenHeight = m_screenHeight;
	u32 width = screenWidth;
	u32 height = screenHeight;

	switch (config.frameBufferEmulation.aspect) {
	case Config::aStretch: // stretch
		width = screenWidth;
		height = screenHeight;
		break;
	case Config::a43: // force 4/3
		if (screenWidth * 3 / 4 > screenHeight) {
			height = screenHeight;
			width = screenHeight * 4 / 3;
		} else if (screenHeight * 4 / 3 > screenWidth) {
			width = screenWidth;
			height = screenWidth * 3 / 4;
		} else {
			width = screenWidth;
			height = screenHeight;
		}
		break;
	case Config::a169: // force 16/9
		if (screenWidth * 9 / 16 > screenHeight) {
			height = screenHeight;
			width = screenHeight * 16 / 9;
		} else if (screenHeight * 16 / 9 > screenWidth) {
			width = screenWidth;
			height = screenWidth * 9 / 16;
		} else {
			width = screenWidth;
			height = screenHeight;
		}
		break;
	case Config::aAdjust: // adjust
		width = screenWidth;
		height = screenHeight;
		if (screenWidth * 3 / 4 > screenHeight) {
			f32 width43 = screenHeight * 4.0f / 3.0f;
			m_adjustScale = width43 / screenWidth;
			m_bAdjustScreen = true;
		}
		break;
	default:
		assert(false && "Unknown aspect ratio");
		width = screenWidth;
		height = screenHeight;
	}

	m_width = width * eyeCount;
	m_height = height;
}

void DisplayWindow::readScreen(void **_pDest, long *_pWidth, long *_pHeight)
{
	_readScreen(_pDest, _pWidth, _pHeight);
}

void DisplayWindow::readScreen2(void * _dest, int * _width, int * _height, int _front)
{
	_readScreen2(_dest, _width, _height, _front);
}

u32 DisplayWindow::maxMSAALevel() const
{
	return m_maxMsaa;
}

u32 DisplayWindow::maxAnisotropy() const
{
	return m_maxAnisotropy;
}
