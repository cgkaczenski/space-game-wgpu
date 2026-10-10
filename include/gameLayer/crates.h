#pragma once

// Weapon crates (inventory roadmap I3): where new weapons come from.
//
//   Salvage    a killed enemy leaves a crate, at a tunable chance; a boss
//              always does, and a full one. The crate is thrown out of the
//              wreck and drifts to a stop.
//   Placed     the level can place crates, with what is in them set or
//              rolled (level.h: crate lines).
//   Dropped    a weapon the player lets go of outside the loadout menu
//              leaves a crate holding it.
//   Opening    near the ship, hold the pointer on a crate: a ring fills over
//              3 s, and the loadout menu opens with the crate's own hold
//              beside the player's.
//   Inside     4 x 2 squares, every thing in one square whatever its shape.
//              Rolled weapons start unknown -- a grey box -- and show what
//              they are once dragged over the player's hold or slots. Ore
//              and the player's own weapons can be put in too.
//   After      a crate stays where it is until it is empty, then goes.
//
// What a rolled crate holds: 1 to 3 weapons (weighted toward 1), each kind
// and modifier by tunable percentages.

#include <inventory.h>
#include <level.h>
#include <render/wgpu2d.h>
#include <engine/hold.h>
#include <functional>
#include <vector>

namespace crates
{
	// One thing in a crate.
	struct Thing
	{
		bool isOre = false;
		inventory::Item item;
		int ore = 0;
		bool revealed = false;   // a weapon whose kind is known
	};

	struct Crate
	{
		int id = 0;
		glm::vec2 position = {};
		glm::vec2 velocity = {};
		float angle = 0.f;            // degrees, for the look
		float spin = 0.f;             // degrees per second, dying away
		hold::Grid grid;              // 4 x 2, each piece one square
		std::vector<std::pair<int, Thing>> things;
		float hover = 0.f;            // seconds the pointer has been on it
	};

	constexpr int width = 4;
	constexpr int height = 2;

	// A new round: no crates, then the level's placed ones.
	void reset(const std::vector<level::CratePlacement> &placed);

	// A kill: a crate at the chance, rolled. A boss's always comes, with 3.
	void enemyKilled(glm::vec2 at, bool boss);

	// A crate holding `thing`, thrown from `at` along `direction`.
	void dropped(glm::vec2 at, glm::vec2 direction, const Thing &thing);

	// Whether a crate at a point may be seen (the fog). Empty: all of them.
	using Shown = std::function<bool(glm::vec2)>;

	// Game time. Moves the crates, and fills the hover ring of the one the
	// pointer (`pointer`, world) is on while `canOpen` -- the player is
	// flying, the menu shut -- and it is within reach of `ship`. Returns the
	// id of a crate whose ring has just filled, or -1. `keepOpen` is the
	// crate open in the menu now: an empty crate goes, but not that one.
	int update(float gameDeltaTime, glm::vec2 ship, glm::vec2 pointer, bool canOpen, int keepOpen,
		const Shown &shown = nullptr);

	Crate *find(int id);

	// Inside a crate. Each happens whole or not at all.
	bool take(Crate &crate, int thingId, Thing &out);
	bool put(Crate &crate, const Thing &thing, glm::ivec2 square);
	bool putAnywhere(Crate &crate, const Thing &thing);
	bool move(Crate &crate, int thingId, glm::ivec2 square);
	const Thing *thingAt(const Crate &crate, int thingId);

	// How far from the ship a crate can be opened.
	float reach();

	// World space, in the world's pass: each crate seen, and the ring.
	void draw(wgpu2d::Renderer2D &renderer, const Shown &shown = nullptr);

	void debugUi();
}
