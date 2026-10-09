#pragma once

// A level's hint script, played (hints roadmap H2). The steps are the
// level's (`hint` lines, level.h); the sequence is engine/sequence; the
// drawing is hints (H1). What is here is what a step's words mean.
//
// What finishes a step -- `until` and its arguments:
//
//   pressed <action>     the action is pressed: pressed cloak
//   selected <slot>      weapon slot 1-4 is selected: selected 4
//   hold <orbs>          the hold has at least this many orbs of ore: hold 1
//   near <x> <y> <r>     the ship is within r of x y: near 15000 -1000 1500
//   mode <flight|fight>  the ship is in that mode
//   cloaked              the ship is cloaked
//   kills <n>            n enemies killed since the step began
//   seconds <s>          s seconds of game time since the step began
//   (nothing)            only the skip key ends it
//
// The game keeps running while a step is up. The bubble's bottom corners are
// two small buttons: SKIP on the left, which moves on as the Skip hint action
// (Enter by default) also does, and a box on the right that switches this
// level's hints off for good: the rest of the script, and on every later
// load. Settings > Hints switches every level's off, and
// switched back on, brings back those switched off from the box too.
//
// A script starts again with every round: every load of a level, and every
// restart in it.

#include <level.h>
#include <render/wgpu2d.h>
#include <string>
#include <vector>

namespace hintScript
{
	// What a step's condition can read this frame. The game fills it.
	struct Context
	{
		glm::vec2 ship = {};
		int selectedWeapon = 0;   // 0-based
		float held = 0.f;         // ore in the hold
		bool flight = false;
		bool cloaked = false;
		int kills = 0;            // this round
		bool live = false;        // the controls act: Playing, not paused or dying
		float gameDeltaTime = 0.f;
	};

	// A new round: the script from its first step. `level` is the level's
	// file name, which is what its hints are switched off by.
	void start(const std::vector<level::HintStep> &steps, const std::string &level);

	// Once a frame, before the trigger is read and before hud::draw: checks
	// the current step, moves on, and hands it to hints to draw. Not live,
	// the step stays up but nothing moves on. True when this frame's mouse
	// press went to one of the bubble's buttons, so the game holds its
	// trigger.
	bool update(const Context &context, int width, int height);

	// For the editor: the condition words, what each wants after it, and the
	// HUD elements a step can point at.
	const std::vector<std::string> &conditions();
	const char *conditionArgs(const std::string &condition);
	const std::vector<std::string> &hudElements();

	void debugUi();
}
