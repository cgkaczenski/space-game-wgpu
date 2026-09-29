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
//   resource 3000 6000 6                    x y amount (gameplay roadmap L3)
//   marker gate 15000 -1000                 kind x y: gate
//   ring 9000 -1500 12000                   x y radius (gameplay roadmap L4)
//   asteroid 3000 -800 900 1234             x y radius seed (gameplay roadmap A1)
//   field 77 400 300                        seed maxSize maxGap (A1b), then:
//   paint 1200 -400 350                     x y radius: brush stamps adding
//   erase 1300 -350 200                     ... and cutting the field's area
//   core -5000 -1600                        x y: the field's core, if moved (A2)
//
// `marker resource x y` from before L3 still loads, as a deposit of the
// default amount.
//   scenery Planet1 -6000 9000 2700 0                art x y size depth
//
// Scenery depth is parallax: 0 moves with the world, and toward 1 a piece
// moves less. 0 is the one that reads as distant here: the starfield's
// furthest layer moves with the world and its nearer layers faster, so a piece
// moving slower than the world is slower than every star behind it and looks
// towed along by the ship. Above 0 is for something that should seem to hang
// in the sky. A piece is exactly at its x y when the camera is centred there.
//
// Rings are the closing circle's stages: the arena first, then each ring in
// turn, largest to smallest, then the last one closes to nothing. Order in the
// file does not matter -- they are sorted by radius when the round starts -- so
// the editor can add them in any order. A level with no rings does not close.

#include <enemy.h>
#include <glm/vec2.hpp>
#include <cstdint>
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

	struct Resource
	{
		glm::vec2 position = {};
		float amount = 6.f;    // how much is in it to mine
	};

	struct Marker
	{
		// Only the gate so far. Resources were markers before L3 gave them
		// something to be.
		enum class Kind { Gate };
		Kind kind = Kind::Gate;
		glm::vec2 position = {};
	};

	struct Scenery
	{
		std::string art;       // a name scenery.cpp knows, e.g. Planet1
		glm::vec2 position = {};
		float size = 2000.f;   // world units across
		float depth = 0.f;     // 0 .. <1; see above
	};

	// One stage of the closing circle: where it closes to.
	struct Ring
	{
		glm::vec2 position = {}; // the centre
		float radius = 5000.f;
	};

	// A rock: where, how big, and the seed its shape grows from. The same
	// seed is the same rock every time, so four numbers are the whole of it.
	struct Asteroid
	{
		glm::vec2 position = {};
		float radius = 800.f;
		uint32_t seed = 1;
	};

	// An asteroid field (A1b): an area painted with a brush, and rocks
	// scattered through it from a seed. The area is the brush's stamps, in
	// order -- a point is in the field if the last stamp covering it painted
	// rather than erased, so painting over an erased patch fills it again.
	struct FieldStamp
	{
		glm::vec2 position = {};
		float radius = 400.f;
		bool erase = false;
	};

	struct AsteroidField
	{
		uint32_t seed = 1;
		float maxSize = 400.f;  // the largest rock's radius
		float maxGap = 40.f;    // room between rocks, on top of their size
		std::vector<FieldStamp> stamps;

		// The core (A2): a big rock that holds the field together. It sits at
		// the painted area's middle unless the designer has dragged it, in
		// which case this is where they put it.
		bool coreMoved = false;
		glm::vec2 core = {};

		bool contains(glm::vec2 point) const
		{
			bool inside = false;
			for (const FieldStamp &s : stamps)
			{
				const glm::vec2 d = point - s.position;
				if (d.x * d.x + d.y * d.y <= s.radius * s.radius) { inside = !s.erase; }
			}
			return inside;
		}
	};

	struct Level
	{
		float arenaRadius = 20000.f;
		glm::vec2 start = {};
		float startFacingDegrees = 0.f;
		std::vector<EnemyPlacement> enemies;
		std::vector<Resource> resources;
		std::vector<Marker> markers;
		std::vector<Ring> rings;
		std::vector<Asteroid> asteroids;
		std::vector<AsteroidField> fields;
		std::vector<Scenery> scenery;
	};

	// Reads `path` into `out`. False only if the file cannot be opened; a line
	// that does not parse is reported with its number on stderr and skipped,
	// so one typo does not cost the whole level.
	bool load(const char *path, Level &out);

	// Writes `level` in the same format. False if the file cannot be written.
	bool save(const char *path, const Level &level);

	// The level files in `directory`: every `.txt`, by file name, sorted. Empty
	// if the directory cannot be read.
	std::vector<std::string> list(const std::string &directory);

	// Unit vector for a facing in degrees, in the file's convention.
	glm::vec2 direction(float degrees);
}
