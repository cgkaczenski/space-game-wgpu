#include <controls.h>
#include <platformInput.h>

#include "imgui.h"
#include <engine/actions.h>
#include <cctype>
#include <cstdlib>
#include <ostream>
#include <sstream>

namespace controls
{

namespace
{
	using actions::Binding;
	using actions::Device;
	using B = platform::Button;

	// Names in the settings file, in the order of Action. Stable: changing
	// one forgets every player's binding for it.
	const char *const ids[(int)Action::Count] = {
		"forward", "back", "left", "right", "brake",
		"fire", "weapon1", "weapon2", "weapon3", "weapon4", "cycleWeapon",
		"ram", "cloak", "mode", "scope", "map", "skipHint", "loadout",
		"zoom", "zoomIn", "zoomOut",
		"pause",
		"menuUp", "menuDown", "menuLeft", "menuRight", "menuConfirm", "menuBack", "menuRotate",
	};

	Binding key(int code) { return {Device::Key, code, -1}; }
	Binding mouse(int button) { return {Device::Mouse, button, -1}; }
	Binding wheel(int axis, int modifier = -1) { return {Device::Wheel, axis, modifier}; }

	void bind(actions::Table &t, Action a, const char *name, std::initializer_list<Binding> bindings)
	{
		actions::Action &slot = t.actions[(int)a];
		slot.name = name;
		slot.count = 0;
		for (const Binding &b : bindings)
		{
			if (slot.count < actions::maxBindings) { slot.bindings[slot.count++] = b; }
		}
	}

	// The defaults: what the game read before K1, key for key.
	actions::Table defaults()
	{
		actions::Table t;
		t.actions.resize((int)Action::Count);

		bind(t, Action::Forward, "Forward", {key(B::W), key(B::Up)});
		bind(t, Action::Back, "Back", {key(B::S), key(B::Down)});
		bind(t, Action::Left, "Left", {key(B::A), key(B::Left)});
		bind(t, Action::Right, "Right", {key(B::D), key(B::Right)});
		bind(t, Action::Brake, "Brake", {key(B::Shift)});

		bind(t, Action::Fire, "Fire", {mouse(0)});
		bind(t, Action::Weapon1, "Weapon 1", {key(B::NR1)});
		bind(t, Action::Weapon2, "Weapon 2", {key(B::NR2)});
		bind(t, Action::Weapon3, "Weapon 3", {key(B::NR3)});
		bind(t, Action::Weapon4, "Weapon 4", {key(B::NR4)});
		// The plain wheel. Ctrl + wheel is zoom's, and shadows this while Ctrl
		// is held -- the rule that replaced weapons' own "not with Ctrl".
		bind(t, Action::CycleWeapon, "Cycle weapon", {wheel(0)});

		bind(t, Action::Ram, "Ram", {mouse(1)});
		bind(t, Action::Cloak, "Cloak", {key(B::E)});
		bind(t, Action::Mode, "Fight / flight", {key(B::Tab)});
		bind(t, Action::Scope, "Scope", {key(B::V)});
		bind(t, Action::Map, "Map", {key(B::M)});
		bind(t, Action::SkipHint, "Skip hint", {key(B::Enter)});
		bind(t, Action::Loadout, "Loadout", {key(B::I)});

		// Vertical, or horizontal for a wheel that reports it that way.
		bind(t, Action::Zoom, "Zoom", {wheel(0, B::LeftCtrl), wheel(1, B::LeftCtrl)});
		bind(t, Action::ZoomIn, "Zoom in", {key(B::Equal)});
		bind(t, Action::ZoomOut, "Zoom out", {key(B::Minus)});

		bind(t, Action::Pause, "Pause", {key(B::Escape)});

		bind(t, Action::MenuUp, "Menu up", {key(B::Up), key(B::W)});
		bind(t, Action::MenuDown, "Menu down", {key(B::Down), key(B::S)});
		bind(t, Action::MenuLeft, "Menu left", {key(B::Left), key(B::A)});
		bind(t, Action::MenuRight, "Menu right", {key(B::Right), key(B::D)});
		bind(t, Action::MenuConfirm, "Menu confirm", {key(B::Enter), key(B::Space)});
		bind(t, Action::MenuBack, "Menu back", {key(B::Escape)});
		bind(t, Action::MenuRotate, "Menu rotate", {key(B::R), mouse(1)});

		// Every rebindable action has both slots, the second empty if it has
		// no second default, so the menu has a cell to put one in.
		for (int a = 0; a < (int)Action::Count; a++)
		{
			if (rebindable((Action)a) && t.actions[a].count < slotCount) { t.actions[a].count = slotCount; }
		}
		return t;
	}

	actions::Table table = defaults();

	// Who has the input: ImGui, while the player is typing into the panel or
	// the pointer is over it -- decided by the binding's device, so an action
	// rebound from a key to a mouse button follows the mouse's rule.
	bool typing() { return ImGui::GetCurrentContext() && ImGui::GetIO().WantTextInput; }
	bool overPanel() { return ImGui::GetCurrentContext() && ImGui::GetIO().WantCaptureMouse; }

	const actions::Source &source()
	{
		static const actions::Source s = []
		{
			actions::Source g = platform::actionSource();
			g.keyHeld = [](int k) { return !typing() && platform::isButtonHeld(k) != 0; };
			g.keyPressed = [](int k) { return !typing() && platform::isButtonPressedOn(k) != 0; };
			g.keyReleased = [](int k) { return platform::isButtonReleased(k) != 0; };
			g.keyRepeated = [](int k) { return !typing() && platform::isButtonTyped(k) != 0; };
			g.mouseHeld = [](int b) { return !overPanel() && platform::actionSource().mouseHeld(b); };
			g.mousePressed = [](int b) { return !overPanel() && platform::actionSource().mousePressed(b); };
			g.wheel = [](int axis) { return overPanel() ? 0.f : platform::actionSource().wheel(axis); };
			return g;
		}();
		return s;
	}

	std::string bindingWord(const Binding &b)
	{
		if (!b.bound()) { return "none"; }
		switch (b.device)
		{
		case Device::Key:
		{
			const char *n = platform::buttonName(b.code);
			return std::string("key ") + (n ? n : "?");
		}
		case Device::Mouse: return "mouse " + std::to_string(b.code);
		case Device::Wheel: return "wheel " + std::to_string(b.code);
		}
		return "none";
	}

	int keyNamed(const std::string &name)
	{
		for (int k = 0; k < B::BUTTONS_COUNT; k++)
		{
			const char *n = platform::buttonName(k);
			if (n && name == n) { return k; }
		}
		return -1;
	}
}

bool held(Action a) { return actions::held(table, source(), (int)a); }
bool pressed(Action a) { return actions::pressed(table, source(), (int)a); }
bool released(Action a) { return actions::released(table, source(), (int)a); }
bool repeated(Action a) { return actions::repeated(table, source(), (int)a); }
float steps(Action a) { return actions::steps(table, source(), (int)a); }

std::string name(Action a)
{
	const actions::Action &action = table.actions[(int)a];
	for (int i = 0; i < action.count; i++)
	{
		if (action.bindings[i].bound()) { return actions::name(source(), action.bindings[i]); }
	}
	return {};
}

std::string names(Action a) { return actions::names(table, source(), (int)a); }

bool rebindable(Action a)
{
	switch (a)
	{
	case Action::CycleWeapon:
	case Action::Zoom:
	case Action::Pause:
	case Action::MenuUp:
	case Action::MenuDown:
	case Action::MenuLeft:
	case Action::MenuRight:
	case Action::MenuConfirm:
	case Action::MenuBack:
	case Action::MenuRotate:
	case Action::Count:
		return false;
	default:
		return true;
	}
}

const char *label(Action a) { return table.actions[(int)a].name; }

Action fromId(const std::string &id)
{
	for (int i = 0; i < (int)Action::Count; i++)
	{
		// Either case: a level file writes {Weapon4} as readily as {weapon4}.
		const char *a = ids[i];
		size_t k = 0;
		while (a[k] && k < id.size() && std::tolower((unsigned char)a[k]) == std::tolower((unsigned char)id[k])) { k++; }
		if (!a[k] && k == id.size()) { return (Action)i; }
	}
	return Action::Count;
}

Binding binding(Action a, int slot)
{
	const actions::Action &action = table.actions[(int)a];
	return slot >= 0 && slot < action.count ? action.bindings[slot] : Binding{};
}

std::string slotName(Action a, int slot) { return actions::name(source(), binding(a, slot)); }

Taken bind(Action a, int slot, Binding b)
{
	Taken taken;
	if (!rebindable(a) || slot < 0 || slot >= slotCount) { return taken; }

	if (b.bound())
	{
		for (int other = 0; other < (int)Action::Count; other++)
		{
			if (!rebindable((Action)other)) { continue; }
			actions::Action &o = table.actions[other];
			for (int i = 0; i < o.count; i++)
			{
				if (!(o.bindings[i] == b) || (other == (int)a && i == slot)) { continue; }
				o.bindings[i] = Binding{};
				if (other != (int)a) { taken = {(Action)other, i}; }
			}
		}
	}
	table.actions[(int)a].bindings[slot] = b;
	return taken;
}

void resetDefaults() { table = defaults(); }

bool firstPressed(Binding &out)
{
	for (int k = 0; k < B::BUTTONS_COUNT; k++)
	{
		if (platform::isButtonPressedOn(k)) { out = key(k); return true; }
	}
	if (platform::isLMousePressed()) { out = mouse(0); return true; }
	if (platform::isRMousePressed()) { out = mouse(1); return true; }
	return false;
}

bool anythingHeld()
{
	for (int k = 0; k < B::BUTTONS_COUNT; k++)
	{
		if (platform::isButtonHeld(k)) { return true; }
	}
	return platform::isLMouseHeld() || platform::isRMouseHeld();
}

void write(std::ostream &out)
{
	const actions::Table d = defaults();
	for (int a = 0; a < (int)Action::Count; a++)
	{
		if (!rebindable((Action)a)) { continue; }
		for (int i = 0; i < slotCount; i++)
		{
			const Binding now = binding((Action)a, i);
			const Binding was = i < d.actions[a].count ? d.actions[a].bindings[i] : Binding{};
			if (!(now == was)) { out << "bind " << ids[a] << " " << i << " " << bindingWord(now) << "\n"; }
		}
	}
}

bool read(const std::string &line)
{
	std::istringstream words(line);
	std::string word, id, device, code;
	int slot = -1;
	if (!(words >> word) || word != "bind") { return false; }
	if (!(words >> id >> slot >> device)) { return true; }

	int a = -1;
	for (int i = 0; i < (int)Action::Count; i++) { if (id == ids[i]) { a = i; } }
	if (a < 0 || !rebindable((Action)a) || slot < 0 || slot >= slotCount) { return true; }

	Binding b;
	if (device == "key" && (words >> code)) { b = key(keyNamed(code)); }
	else if (device == "mouse" && (words >> code)) { b = mouse(std::atoi(code.c_str())); }
	else if (device == "wheel" && (words >> code)) { b = wheel(std::atoi(code.c_str())); }
	// "none", or a name this build does not know: the slot is left empty.
	if (b.device == Device::Key && b.code < 0) { b = Binding{}; }
	table.actions[a].bindings[slot] = b;
	return true;
}

void debugUi()
{
	ImGui::TextDisabled("Rebind in the menu: Settings > Controls. Lit: held this frame");
	if (ImGui::BeginTable("##controls", 2, ImGuiTableFlags_SizingStretchProp))
	{
		for (int i = 0; i < (int)Action::Count; i++)
		{
			const bool on = held((Action)i);
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			if (on) { ImGui::TextColored({1.f, 0.9f, 0.4f, 1.f}, "%s", table.actions[i].name); }
			else { ImGui::TextUnformatted(table.actions[i].name); }
			ImGui::TableNextColumn();
			ImGui::TextDisabled("%s", names((Action)i).c_str());
		}
		ImGui::EndTable();
	}
}

}
