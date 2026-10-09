#pragma once

// This game's actions and their default bindings (hints roadmap K1). The
// table and the rules for reading it are engine/actions; what is here is the
// part no other game wants -- that there is a Ram and a Scope, and that Cloak
// is E.
//
// Every gameplay input goes through here. What still reads keys directly:
// the level editor (a tool, not the game), the menu's pointer, and the debug
// panel, which is ImGui's.
//
// Not here: whether ImGui has the keyboard or the mouse. Each call site still
// checks that, exactly as it did before K1. With ImGui's keyboard navigation
// on, ImGui claims the keyboard whenever the debug panel is focused, so a
// blanket rule would stop the ship flying while the panel is being tuned.

#include <string>

namespace controls
{
	enum class Action
	{
		// Flying. What these mean depends on the control scheme (playerMove).
		Forward,
		Back,
		Left,
		Right,
		Brake,

		Fire,
		Weapon1,
		Weapon2,
		Weapon3,
		Weapon4,      // the beam, which mines
		CycleWeapon,  // wheel: steps, up goes back a slot

		Ram,          // hold to aim, let go to ram
		Cloak,
		Mode,         // fight or flight
		Scope,
		Map,

		Zoom,         // wheel: steps
		ZoomIn,       // held
		ZoomOut,      // held

		Pause,

		// The menu's keys. Separate from flying's, though they default to the
		// same keys, so rebinding one does not move the other.
		MenuUp,
		MenuDown,
		MenuLeft,
		MenuRight,
		MenuConfirm,
		MenuBack,

		Count
	};

	bool held(Action action);
	bool pressed(Action action);
	bool released(Action action);
	bool repeated(Action action);  // pressed, or repeating while held
	float steps(Action action);    // wheel notches this frame

	// What a hint prints: "4", or every binding, "4 / WHEEL".
	std::string name(Action action);
	std::string names(Action action);

	// The table, read-only, with each action's bindings and whether it is
	// held this frame.
	void debugUi();
}
