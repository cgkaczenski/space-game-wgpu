#include <menu.h>
#include <controls.h>
#include <playerSettings.h>
#include <textLook.h>
#include <gameLayer.h>
#include <platformInput.h>

#include "imgui.h"
#include <algorithm>
#include <cmath>

namespace menu
{

namespace
{
	enum class Page { Main, Settings };
	Page page = Page::Main;

	// One widget per page, so each keeps its own selection: coming back from
	// Settings lands on Settings, not on Resume.
	wgpu2d::Menu mainMenu;
	wgpu2d::Menu settingsMenu;

	bool lastByPointer = false;

	// The look. Widths in characters of the font, so the menu scales with the
	// text; the top as a fraction of the screen height.
	const float widthChars = 26.f;
	const float topPerc = 0.28f;
	const int textSize = 1;
	const int titleSize = 3;

	wgpu2d::MenuStyle style(int h)
	{
		wgpu2d::MenuStyle s;
		s.font = &textLook::font();
		s.scale = textLook::screenScale(h, textSize);
		s.titleScale = textLook::screenScale(h, titleSize);
		s.width = std::round(widthChars * textLook::font().glyphs[0].advance * s.scale);
		s.title = {1.f, 1.f, 1.f, 1.f};
		s.text = {0.7f, 0.78f, 0.85f, 1.f};
		s.selected = {1.f, 0.92f, 0.65f, 1.f};      // the damage numbers' warm white
		s.highlight = {1.f, 0.92f, 0.65f, 0.12f};
		s.panel = {0.f, 0.f, 0.f, 0.45f};
		s.shadow = {0.f, 0.f, 0.f, 0.8f};
		s.track = {1.f, 1.f, 1.f, 0.18f};
		s.fill = {1.f, 0.92f, 0.65f, 0.9f};
		return s;
	}

	// The keys and the mouse as menu actions. Up and down, left and right
	// repeat when held (typed); confirm and back do not. Nothing while the
	// debug panel has the keyboard or the mouse.
	wgpu2d::MenuInput readInput(int w, int h)
	{
		wgpu2d::MenuInput in;
		const ImGuiIO &io = ImGui::GetIO();
		if (!io.WantCaptureKeyboard)
		{
			using controls::Action;
			in.up = controls::repeated(Action::MenuUp);
			in.down = controls::repeated(Action::MenuDown);
			in.left = controls::repeated(Action::MenuLeft);
			in.right = controls::repeated(Action::MenuRight);
			in.confirm = controls::pressed(Action::MenuConfirm);
			in.back = controls::pressed(Action::MenuBack);
		}
		if (!io.WantCaptureMouse)
		{
			// The cursor is in window coordinates; the menu is laid out in
			// framebuffer pixels, which differ on a high-density display.
			const glm::ivec2 window = platform::getWindowSize();
			const glm::vec2 toPixels = {window.x > 0 ? (float)w / window.x : 1.f,
				window.y > 0 ? (float)h / window.y : 1.f};
			in.pointerActive = true;
			in.pointer = glm::vec2(platform::getRelMousePosition()) * toPixels;
			in.pointerMoved = platform::mouseMoved();
			in.pointerPressed = platform::isLMousePressed();
			in.pointerHeld = platform::isLMouseHeld();
		}
		return in;
	}
}

void open()
{
	page = Page::Main;
	mainMenu.selected = 0;
}

Choice update(wgpu2d::Renderer2D &renderer, int w, int h, bool takeInput)
{
	const wgpu2d::MenuInput in = takeInput ? readInput(w, h) : wgpu2d::MenuInput{};
	const wgpu2d::MenuStyle s = style(h);
	const glm::vec2 top = {std::round(w * 0.5f), std::round(h * topPerc)};

	Choice choice = Choice::None;
	renderer.pushCamera();
	if (page == Page::Main)
	{
		wgpu2d::Menu &m = mainMenu;
		m.begin(renderer, in, top, s);
		m.title("PAUSED");
		m.space(0.5f);
		if (m.button("RESUME")) { choice = Choice::Resume; }
		if (m.button("RESTART")) { choice = Choice::Restart; }
		if (m.button("SETTINGS")) { page = Page::Settings; settingsMenu.selected = 0; }
		if (m.button("QUIT")) { choice = Choice::Quit; }
		if (m.backPressed()) { choice = Choice::Resume; }
		m.end();
	}
	else
	{
		wgpu2d::Menu &m = settingsMenu;
		playerSettings::Settings set = playerSettings::get();
		bool changed = false;
		m.begin(renderer, in, top, s);
		m.title("SETTINGS");
		m.space(0.5f);
		changed |= m.slider("VOLUME", set.volume, 0.f, 1.f, 0.1f);
		changed |= m.slider("CRT", set.crt, 0.f, 1.f, 0.1f);
		changed |= m.toggle("FULLSCREEN", set.fullscreen);
		m.space(0.5f);
		if (m.button("BACK") || m.backPressed()) { page = Page::Main; }
		m.end();
		if (changed) { playerSettings::set(set); }
	}
	renderer.popCamera();

	if (choice != Choice::None) { lastByPointer = in.pointerPressed && !in.confirm && !in.back; }
	return choice;
}

bool choseWithPointer() { return lastByPointer; }

}
