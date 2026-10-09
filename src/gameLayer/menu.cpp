#include <menu.h>
#include <controls.h>
#include <playerSettings.h>
#include <textLook.h>
#include <gameLayer.h>
#include <platformInput.h>
#include <playerMove.h>

#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>
#include <vector>

namespace menu
{

namespace
{
	enum class Page { Main, Settings, Controls };
	Page page = Page::Main;

	// One widget per page, so each keeps its own selection: coming back from
	// Settings lands on Settings, not on Resume.
	wgpu2d::Menu mainMenu;
	wgpu2d::Menu settingsMenu;
	wgpu2d::Menu controlsMenu;

	// ---- The controls page (hints roadmap K2) ----------------------------

	// A cell pressed: waiting for the key that goes in it. `armed` once the
	// press that opened the wait has been let go, so a click on the cell is
	// not taken as the new binding.
	struct Listening
	{
		controls::Action action = controls::Action::Count;
		int slot = -1;
		bool armed = false;
		bool active() const { return action != controls::Action::Count; }
	};
	Listening listening;

	// Slots another action's new binding took (the author's rule): the page
	// cannot be left until each has a key again.
	std::vector<controls::Taken> unbound;

	const glm::vec4 needColour = {1.f, 0.45f, 0.35f, 1.f};
	const char *const schemeNames[] = {"MOUSE THRUST", "TURN WITH KEYS", "SCREEN DIRECTIONS"};

	bool isUnbound(controls::Action a, int slot)
	{
		for (const controls::Taken &t : unbound) { if (t.action == a && t.slot == slot) { return true; } }
		return false;
	}

	std::string upper(std::string s)
	{
		for (char &c : s) { c = (char)std::toupper((unsigned char)c); }
		return s;
	}

	// Waits on the next key or mouse button for the listening cell. Escape
	// cancels: it is the way out of the menu, so it is never a binding.
	void listen()
	{
		if (!listening.active()) { return; }
		if (!listening.armed)
		{
			listening.armed = !controls::anythingHeld();
			return;
		}
		actions::Binding b;
		if (!controls::firstPressed(b)) { return; }
		if (!(b.device == actions::Device::Key && b.code == platform::Button::Escape))
		{
			const controls::Taken taken = controls::bind(listening.action, listening.slot, b);
			unbound.erase(std::remove_if(unbound.begin(), unbound.end(), [](const controls::Taken &t)
				{ return t.action == listening.action && t.slot == listening.slot; }), unbound.end());
			if (taken.action != controls::Action::Count && !isUnbound(taken.action, taken.slot))
			{
				unbound.push_back(taken);
			}
			playerSettings::save();
		}
		listening = {};
	}

	bool lastByPointer = false;

	// The look. Widths in characters of the font, so the menu scales with the
	// text; the top as a fraction of the screen height.
	const float widthChars = 26.f;
	const float topPerc = 0.28f;
	const float controlsWidthChars = 46.f;
	const float controlsTopPerc = 0.05f;
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
	// repeat when held (typed); confirm and back do not. The keys are the
	// action table's, which leaves them to the debug panel only while typing
	// into it; the pointer is raw, so it is checked here.
	wgpu2d::MenuInput readInput(int w, int h)
	{
		wgpu2d::MenuInput in;
		const ImGuiIO &io = ImGui::GetIO();
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
	else if (page == Page::Settings)
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
		if (m.button("CONTROLS")) { page = Page::Controls; controlsMenu.selected = 0; }
		m.space(0.5f);
		if (m.button("BACK") || m.backPressed()) { page = Page::Main; }
		m.end();
		if (changed) { playerSettings::set(set); }
	}
	else
	{
		// While a cell waits for its key, the menu takes no input at all: the
		// key is the binding, not a move. That includes the frame the wait
		// ends, or the Escape that cancels it would also leave the page, and
		// an Enter just bound would press the cell again.
		const bool wasListening = listening.active();
		listen();
		const wgpu2d::MenuInput pageInput = wasListening ? wgpu2d::MenuInput{} : in;

		// Seventeen rows of actions: tighter rows, a smaller heading, wider
		// for two key columns, and nearer the top, so it fits any window.
		wgpu2d::MenuStyle cs = s;
		cs.titleScale = textLook::screenScale(h, 2);
		cs.rowHeight = 1.35f;
		cs.width = std::round(controlsWidthChars * textLook::font().glyphs[0].advance * cs.scale);
		cs.panel.a = 0.75f; // a table of text over the world wants more behind it

		wgpu2d::Menu &m = controlsMenu;
		m.begin(renderer, pageInput, {top.x, std::round(h * controlsTopPerc)}, cs);
		m.title("CONTROLS");

		// The scheme reads playerMove, which the debug panel can change too.
		int scheme = (int)playerMove::scheme();
		if (m.choice("SCHEME", scheme, schemeNames, 3))
		{
			playerSettings::Settings set = playerSettings::get();
			set.scheme = scheme;
			playerSettings::set(set);
		}
		m.space(0.3f);

		for (int a = 0; a < (int)controls::Action::Count; a++)
		{
			const controls::Action action = (controls::Action)a;
			if (!controls::rebindable(action)) { continue; }

			std::string text[controls::slotCount];
			const char *values[controls::slotCount];
			wgpu2d::Color4f colours[controls::slotCount] = {};
			for (int slot = 0; slot < controls::slotCount; slot++)
			{
				if (listening.action == action && listening.slot == slot) { text[slot] = "PRESS A KEY"; }
				else
				{
					text[slot] = controls::slotName(action, slot);
					if (text[slot].empty())
					{
						const bool needed = isUnbound(action, slot);
						text[slot] = needed ? "NEEDS A KEY" : "-";
						if (needed) { colours[slot] = needColour; }
					}
				}
				values[slot] = text[slot].c_str();
			}
			const int cell = m.fields(upper(controls::label(action)).c_str(), values, controls::slotCount, colours);
			if (cell >= 0) { listening = {action, cell, false}; }
		}

		m.space(0.3f);
		if (m.button("RESET TO DEFAULTS"))
		{
			controls::resetDefaults();
			unbound.clear();
			playerSettings::save();
		}
		const bool leave = m.button("BACK") || m.backPressed();
		if (listening.active()) { m.note("ESC TO CANCEL", cs.text); }
		else if (!unbound.empty())
		{
			m.note(("GIVE " + upper(controls::label(unbound.front().action)) + " A KEY FIRST").c_str(), needColour);
		}
		m.end();
		if (leave && unbound.empty()) { page = Page::Settings; }
	}
	renderer.popCamera();

	if (choice != Choice::None) { lastByPointer = in.pointerPressed && !in.confirm && !in.back; }
	return choice;
}

bool choseWithPointer() { return lastByPointer; }

}
