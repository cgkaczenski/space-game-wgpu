#pragma once

// Saved tuning: the debug panel's numbers, kept in named sets apart from the
// levels, with what differs from the code's defaults marked and listed.
//
// **How a variable takes part.** It registers once, at start-up, with a key
// that names it in files and a reference to where it lives. Each file does
// this in one block, a Group, placed after the variables it lists, so the
// value each has when it registers is the one its declaration gave it -- the
// code's default:
//
//     float thrust = 9000.f;
//     ...
//     const tuning::Group tuned("enemies", {
//         {"thrust", thrust},
//     });
//
// That is all a tunable needs. Its control in the panel uses the `tune::`
// wrapper in place of ImGui's -- the same arguments -- and is highlighted
// whenever its value is not the default. A wrapper given a variable that never
// registered draws exactly as ImGui's would, so the swap is safe anywhere.
// Controls that do not bind a variable directly (a radio group over a local
// copy) take a `tune::Highlight` for the variable around them instead.
//
// **Sets.** A set is a file in the tuning folder listing only what differs
// from the defaults, as `key value...` lines. Loading one returns everything
// to the defaults first, then applies the file -- so a default changed in the
// code reaches every set that does not override it. "Defaults" is always in
// the picklist and restores them. The last set chosen is remembered, like the
// last level, and loaded at launch.
//
// A key in a file that no variable registers -- a renamed or removed one --
// is reported on stderr and skipped.

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <initializer_list>
#include <string>
#include <type_traits>

namespace tuning
{
	// One variable: a key, and where it lives. Enums are kept as their int.
	struct Entry
	{
		enum class Type { Float, Int, Bool, Vec2, Vec3, Vec4 };
		const char *key;
		Type type;
		void *variable;

		Entry(const char *k, float &v) : key(k), type(Type::Float), variable(&v) {}
		Entry(const char *k, int &v) : key(k), type(Type::Int), variable(&v) {}
		Entry(const char *k, bool &v) : key(k), type(Type::Bool), variable(&v) {}
		Entry(const char *k, glm::vec2 &v) : key(k), type(Type::Vec2), variable(&v) {}
		Entry(const char *k, glm::vec3 &v) : key(k), type(Type::Vec3), variable(&v) {}
		Entry(const char *k, glm::vec4 &v) : key(k), type(Type::Vec4), variable(&v) {}
		template <class E, class = std::enable_if_t<std::is_enum_v<E> && sizeof(E) == sizeof(int)>>
		Entry(const char *k, E &v) : key(k), type(Type::Int), variable(&v) {}
	};

	// Registers each entry as `prefix.key`, its current value its default.
	struct Group
	{
		Group(const char *prefix, std::initializer_list<Entry> entries);
	};

	// Where sets live, and what to call after a set (or the defaults) has
	// been applied -- the game restarts the round, so values read once at
	// spawn or when rocks are grown take effect. Then loads the last set
	// chosen. Call once at start-up, after every Group has registered.
	void init(const std::string &directory, const std::string &lastRecordFile, void (*applied)());

	// Writes what differs from the defaults to `file` in the tuning folder,
	// which becomes the set chosen: what the panel's Save does.
	bool saveAs(const std::string &file);

	// Whether `variable` is registered and differs from its default.
	bool changed(const void *variable);

	// The picklist, Save / Save as, and the list of every changed value with
	// a reset for each. For the bottom of the debug panel.
	void debugUi();
}

// The panel's controls, highlighted when what they show is not the default.
// Same arguments as ImGui's.
namespace tune
{
	// Pushes the highlight for as long as it lives, if `variable` (or
	// `second`) differs from its default.
	struct Highlight
	{
		explicit Highlight(const void *variable, const void *second = nullptr);
		~Highlight();
		Highlight(const Highlight &) = delete;
		Highlight &operator=(const Highlight &) = delete;
		int pushed = 0;
	};

	bool SliderFloat(const char *label, float *v, float min, float max, const char *format = "%.3f", int flags = 0);
	bool SliderInt(const char *label, int *v, int min, int max, const char *format = "%d", int flags = 0);
	bool DragFloat(const char *label, float *v, float speed = 1.f, float min = 0.f, float max = 0.f,
		const char *format = "%.3f", int flags = 0);
	bool DragFloatRange2(const char *label, float *lo, float *hi, float speed = 1.f, float min = 0.f,
		float max = 0.f, const char *format = "%.3f", const char *formatMax = nullptr, int flags = 0);
	bool Checkbox(const char *label, bool *v);
	bool ColorEdit3(const char *label, float *rgb, int flags = 0);
	bool ColorEdit4(const char *label, float *rgba, int flags = 0);
}
