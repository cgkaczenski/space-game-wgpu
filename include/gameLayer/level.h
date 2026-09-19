#pragma once

// A level: what is placed where (gameplay roadmap L2). Plain data -- a
// procedural generator will fill the same struct later, so nothing here knows
// where a Level came from.
//
// On disk it is a text file, one thing per line, `#` to the end of a line a
// comment. Angles are degrees, 0 pointing right and 90 down the screen (the
// world is y-down, like the screen):
//
//   arena 20000                             radius; the arena is centred on 0,0
//   start -17000 0 0                        x y facing
//   enemy rusher 4000 1200 180              kind x y facing: rusher | sniper
//   marker resource 3000 6000               kind x y: resource | gate
//   scenery Planet1 -6000 9000 2700 0                art x y size depth
//
// Scenery depth is parallax: 0 moves with the world, and toward 1 a piece
// moves less. 0 is the one that reads as distant here: the starfield's
// furthest layer moves with the world and its nearer layers faster, so a piece
// moving slower than the world is slower than every star behind it and looks
// towed along by the ship. Above 0 is for something that should seem to hang
// in the sky. A piece is exactly at its x y when the camera is centred there.

#include <enemy.h>
#include <glm/vec2.hpp>
#include <string>
#include <vector>

namespace level
{
	struct EnemyPlacement
	{
		Enemy::Behaviour behaviour = Enemy::Behaviour::CloseIn;
		glm::vec2 position = {};
		float facingDegrees = 0.f;
	};

	struct Marker
	{
		enum class Kind { Resource, Gate };
		Kind kind = Kind::Resource;
		glm::vec2 position = {};
	};

	struct Scenery
	{
		std::string art;       // a name scenery.cpp knows, e.g. Planet1
		glm::vec2 position = {};
		float size = 2000.f;   // world units across
		float depth = 0.f;     // 0 .. <1; see above
	};

	struct Level
	{
		float arenaRadius = 20000.f;
		glm::vec2 start = {};
		float startFacingDegrees = 0.f;
		std::vector<EnemyPlacement> enemies;
		std::vector<Marker> markers;
		std::vector<Scenery> scenery;
	};

	// Reads `path` into `out`. False only if the file cannot be opened; a line
	// that does not parse is reported with its number on stderr and skipped,
	// so one typo does not cost the whole level.
	bool load(const char *path, Level &out);

	// Writes `level` in the same format. False if the file cannot be written.
	bool save(const char *path, const Level &level);

	// Unit vector for a facing in degrees, in the file's convention.
	glm::vec2 direction(float degrees);
}
