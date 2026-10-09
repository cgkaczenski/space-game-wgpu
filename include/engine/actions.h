#pragma once

// Actions: one table between the inputs and the game (hints roadmap K1).
//
// A game asks "is Fire held", never "is the left mouse button held". What
// Fire is bound to lives in a table the game fills, and changing a binding --
// a player rebinding a key, a second key for the same action -- changes no
// code that reads it. The table can also name an action's bindings, which is
// what a hint prints ("PRESS 4 OR WHEEL").
//
// No window library here: the table reads input through a Source, a handful
// of functions the application supplies over whatever input layer it has. An
// input's `code` is the application's own key or button number; this file
// never interprets one, except to ask the Source for its name.

#include <string>
#include <vector>

namespace actions
{
	enum class Device
	{
		Key,
		Mouse,  // code: 0 left, 1 right
		Wheel,  // code: 0 vertical, 1 horizontal
	};

	// One way of triggering an action: an input, and optionally a key that
	// must be held with it (Ctrl + wheel).
	struct Binding
	{
		Device device = Device::Key;
		int code = -1;
		int modifier = -1;   // a key code, or -1 for none
	};

	constexpr int maxBindings = 4;

	struct Action
	{
		const char *name = "";
		Binding bindings[maxBindings] = {};
		int count = 0;
	};

	// Indexed by the game's own enum.
	struct Table
	{
		std::vector<Action> actions;
	};

	// How the table reads input. Plain functions, so a Source is a value.
	struct Source
	{
		bool (*keyHeld)(int code) = nullptr;
		bool (*keyPressed)(int code) = nullptr;    // went down this frame
		bool (*keyReleased)(int code) = nullptr;   // went up this frame
		bool (*keyRepeated)(int code) = nullptr;   // pressed, or auto-repeating while held
		bool (*mouseHeld)(int button) = nullptr;
		bool (*mousePressed)(int button) = nullptr;
		bool (*mouseReleased)(int button) = nullptr;
		float (*wheel)(int axis) = nullptr;        // notches this frame, may be fractional
		const char *(*keyName)(int code) = nullptr;
	};

	// Any binding held. A wheel binding is "held" on a frame it moved.
	bool held(const Table &table, const Source &source, int action);
	bool pressed(const Table &table, const Source &source, int action);
	bool released(const Table &table, const Source &source, int action);
	// Pressed, or repeating while held -- for stepping through a menu.
	bool repeated(const Table &table, const Source &source, int action);
	// Wheel notches through its wheel bindings this frame, summed.
	float steps(const Table &table, const Source &source, int action);

	// A binding as a player reads it: "4", "LEFT MOUSE", "CTRL + WHEEL".
	std::string name(const Source &source, const Binding &binding);
	// Every binding of an action, joined by `separator`: "4 / WHEEL".
	std::string names(const Table &table, const Source &source, int action, const char *separator = " / ");
}
