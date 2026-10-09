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
// Whether ImGui has the input is decided here too, by the device a binding is
// on (K2): a key is the panel's only while text is being typed into it, a
// mouse button or the wheel while the pointer is over it. Not
// WantCaptureKeyboard: with ImGui's keyboard navigation on, that is true
// whenever the panel has focus, and the ship would stop flying while tuning.
//
// The player rebinds from the menu (K2). Each action has two slots, a primary
// and a secondary; the rest of the engine's four are not used by the game.

#include <engine/actions.h>
#include <iosfwd>
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

	// ---- Rebinding (K2) ----------------------------------------------------

	constexpr int slotCount = 2;  // primary, secondary

	// What the player may rebind: flying, weapons and abilities. Not the
	// wheel's actions (Cycle weapon, Zoom), which need a wheel; not Pause and
	// the menu's keys, which are how a player gets back out of a bad binding.
	bool rebindable(Action action);

	const char *label(Action action);  // "Weapon 1"
	actions::Binding binding(Action action, int slot);
	std::string slotName(Action action, int slot);  // "" when unbound

	// Puts `b` in `slot` of `action`. If another rebindable action had it,
	// that slot is cleared and returned, so the caller can make the player
	// fill it; otherwise `taken.action` is Count. The same action's other
	// slot holding it is simply cleared.
	struct Taken
	{
		Action action = Action::Count;
		int slot = -1;
	};
	Taken bind(Action action, int slot, actions::Binding b);

	// Every binding back to the defaults.
	void resetDefaults();

	// For waiting on "press a key": the first key or mouse button pressed this
	// frame, or false. Raw, ignoring the table and ImGui.
	bool firstPressed(actions::Binding &out);
	// Anything held at all -- waiting for the press that opened the wait to
	// be let go before listening.
	bool anythingHeld();

	// The player's settings file: one line per binding that differs from the
	// default ("bind fire 0 mouse 0", "bind forward 1 none"). `read` takes one
	// line and says whether it was a binding.
	void write(std::ostream &out);
	bool read(const std::string &line);

	// The table, read-only, with each action's bindings and whether it is
	// held this frame.
	void debugUi();
}
