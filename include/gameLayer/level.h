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
//   enemy rusher 4000 1200 180 gun:missile:spread=yes shield:yes
//                                           kind x y facing: rusher | sniper |
//                                           boss (B2); then optionally its
//                                           weapons, one gun: per slot -- a
//                                           kind, burst | heavy | missile |
//                                           laser | random, then any of
//                                           :stun= :lockdown= :spread= yes or
//                                           random (without one, no). No gun:
//                                           at all, they are rolled. A bare
//                                           kind (older files) is one gun with
//                                           its modifiers rolled. Then
//                                           shield:, cloak:, ram: yes or
//                                           random (without one, no; a boss
//                                           has every ability)
//   marker gate 15000 -1000                 kind x y: gate
//   ring 9000 -1500 12000                   x y radius (gameplay roadmap L4)
//   asteroid 3000 -800 900 1234             x y radius seed (gameplay roadmap A1)
//   field 77 400 300                        seed maxSize maxGap (A1b), then:
//   paint 1200 -400 350                     x y radius: brush stamps adding
//   erase 1300 -350 200                     ... and cutting the field's area
//   core -5000 -1600                        x y: one of the field's cores, placed
//                                           by hand (A2); any number (W1). None:
//                                           one, at the painted area's middle
//
// Deposits are gone (A4): asteroids are the ore now. A `resource x y amount`
// line from before -- or the older `marker resource x y` -- still loads, as a
// single asteroid sized by the amount, and is saved as one.
//   scenery Planet1 -6000 9000 2700 0                art x y size depth
//
// Scenery depth is parallax: 0 moves with the world, and toward 1 a piece
// moves less -- further away. The starfield's layers move at 0.2 to 0.6 of the
// world's speed, so a planet belongs between them and the play: 0.25 (moving
// 0.75) is just behind it. At 0 a piece moves with the ships and rocks, and
// reads as being at their depth. (Before A3 the stars moved *faster* than the
// world, and 0 was the one that read as distant.) A piece is exactly at its
// x y when the camera is centred there.
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
		// Its weapons, slot by slot (B2): each a kind of weapons::shipWeapon or
		// rolled, each modifier yes, no or rolled. Empty, they are rolled.
		std::vector<GunChoice> guns;
		// Whether it has a shield: no unless the level says, and Random rolls
		// it each round at the Shield chance.
		AbilityChoice shield = AbilityChoice::No;
		AbilityChoice cloak = AbilityChoice::No;   // likewise its cloak
		AbilityChoice ram = AbilityChoice::No;     // and its ram
	};

	struct Marker
	{
		// Only the gate so far. Resources were markers before L3 gave them
		// something to be, and are asteroids since A4.
		enum class Kind { Gate };
		Kind kind = Kind::Gate;
		glm::vec2 position = {};
	};

	struct Scenery
	{
		std::string art;       // a name scenery.cpp knows, e.g. Planet1
		glm::vec2 position = {};
		float size = 2000.f;   // world units across
		float depth = 0.25f;   // 0 .. <1; see above
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
		float maxSize = 90.f;   // the largest rock's radius: small rocks, many of them (level3's look)
		float maxGap = 40.f;    // room between rocks, on top of their size
		std::vector<FieldStamp> stamps;

		// The cores (A2): big rocks that hold the field together. With none
		// placed, the field has one, at the painted area's middle. Otherwise
		// one at each of these, where the designer put them: a level that is
		// mostly one field needs several (sight roadmap W1).
		std::vector<glm::vec2> cores;

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

	// The fields as they are played (sight roadmap W1): each with a clearing
	// of `gateClearing` erased round every gate, last, so the gate sits in
	// open space wherever it is put -- moving the gate moves its clearing.
	// Not saved: the file keeps only what was painted.
	std::vector<AsteroidField> fieldsAsPlayed(const Level &level, float gateClearing);

	// The level files in `directory`: every `.txt`, by file name, sorted. Empty
	// if the directory cannot be read.
	std::vector<std::string> list(const std::string &directory);

	// Unit vector for a facing in degrees, in the file's convention.
	glm::vec2 direction(float degrees);
}
