#include <controls.h>
#include <platformInput.h>

#include "imgui.h"
#include <engine/actions.h>

namespace controls
{

namespace
{
	using actions::Binding;
	using actions::Device;
	using B = platform::Button;

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
		return t;
	}

	actions::Table table = defaults();

	const actions::Source &source()
	{
		static const actions::Source s = platform::actionSource();
		return s;
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
	return action.count > 0 ? actions::name(source(), action.bindings[0]) : std::string();
}

std::string names(Action a) { return actions::names(table, source(), (int)a); }

void debugUi()
{
	ImGui::TextDisabled("Read-only until K2. Lit: held this frame");
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
