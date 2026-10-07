#include <asteroids.h>
#include <tuning.h>

#include <engine/polygon.h>
#include <engine/rigidBody.h>
#include <engine/scatter.h>
#include <engine/regionMask.h>
#include <engine/spatialGrid.h>
#include <effects.h>
#include <resources.h>
#include "imgui.h"
#include <platformTools.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace asteroids
{

namespace
{
	// A3: one texture holding brightness, the normal's x and y, and height
	// (resources/asteroid/rock_packed.png), read by the asteroid shader.
	wgpu2d::Texture rockTexture;
	wgpu2d::Effect rockEffect;

	struct Rock
	{
		polygon::Placement placement;   // where the outline's frame is: from the body
		std::vector<glm::vec2> outline; // around the origin, in the rock's frame
		float bound = 0.f;              // bounding radius: the broad phase
		glm::vec2 uvOffset = {};        // which part of the texture it wears
		bool inField = false;           // drawn over the ships, not under

		// A2. The body turns about the centre of mass, which is not quite the
		// point the outline was grown round. That point must stay the fan's
		// apex -- it is the one every edge can be seen from -- so the outline
		// keeps its frame, and the frame is placed from the body each step.
		rigid::Body body;
		glm::vec2 centroid = {};        // the centre of mass, in the outline's frame
		float collideRadius = 0.f;      // a circle of the same area: rock against rock
		glm::vec2 home = {};            // where a field rock springs back to
		bool awake = false;             // moving, so stepped and collided
		unsigned ramHitOn = 0;          // the ram that last struck it, by its serial: once per ram
		float sinceStruck = 0.f;        // seconds since last pushed or bumped: the spring waits

		int field = -1;                 // which field it belongs to; -1 a single rock
		bool core = false;              // its field's core: immovable, never struck, solid to ships
		// The chunk it was made for (W3), or -1: a single rock or a core,
		// made with the round and kept all of it. And where scatter put it,
		// which with its field is what it is: made again, it is the same rock.
		long long chunk = -1;
		glm::vec2 origin = {};
		// Knocked out of its field's paint (S1): then it is a rock on its own,
		// solid to sight and shots. By the field's own stamps, exactly -- the
		// grid's stair-step edge would call some rocks at rest outside --
		// and checked as it moves, not on every query.
		bool outOfField = false;

		// A3: the beam's heat, 0 .. 1, and where it burns, in the rock's frame.
		float heat = 0.f;
		glm::vec2 heatAt = {};
		float sinceBeamed = 1e9f;       // it only cools once the beam has left it
		// Finished off by the beam: its ore bursts out as it breaks, thrown
		// back the way the beam came, from where it was burning.
		bool beamKill = false;
		glm::vec2 beamDirection = {};
		glm::vec2 beamPoint = {};

		// ---- A4: breaking, and ore ----
		uint32_t seed = 0;
		float area = 0.f;
		float health = 0.f, healthFull = 0.f;
		float ore = 0.f;                // left in it to mine
		float oreFull = 0.f;            // what it held when it was made
		float oreLoose = 0.f;           // mined but not yet a whole orb

		// Its crack network, worked out the first time it is hurt: the Voronoi
		// pieces it will break into, and the borders between them as segments
		// in its own frame, each shown once the damage passes its threshold.
		struct Crack { glm::vec2 a, b; float threshold; };
		bool cracksBuilt = false;
		std::vector<polygon::Piece> fracture;
		std::vector<Crack> cracks;
	};

	std::vector<Rock> rocks;
	int orbsThrown = 0;             // varies each orb's angle

	// What a broken rock becomes (A4): shards that fly apart, then sink into
	// the background -- never solid, like an enemy's wreck -- and gather back
	// where the rock stood. Not rocks: nothing hits them, so they are only
	// moved and drawn.
	struct Shard
	{
		std::vector<glm::vec2> outline; // around its centroid
		std::vector<int> triangles;     // for a shape no fan from the centroid covers
		float bound = 0.f;
		glm::vec2 uvOffset = {};
		glm::vec2 position = {}, velocity = {};
		float angle = 0.f, spin = 0.f;
		// Its place in the rock, with the rock back at home -- the pieces
		// gather into its shape, a little apart.
		glm::vec2 slot = {};
		float age = 0.f;
		float fadeStart = -1.f;         // past the limit: shrinking away
	};
	std::vector<Shard> shards;          // oldest first

	// A3: small dark rocks in front of everything, never solid, moving faster
	// than the world as the camera moves -- nearer the eye than the play. They
	// are drawn, not simulated: a shape, a place and a turn.
	struct Debris
	{
		std::vector<glm::vec2> outline;
		glm::vec2 position = {};
		float angle = 0.f;
		float bound = 0.f;
		glm::vec2 uvOffset = {};
	};
	std::vector<Debris> debris;

	// ---- The look (A3) ----
	// The light: the direction it comes from, round the screen (0 from the
	// right, 90 from below -- the world is y-down) and how high above the
	// plane. One for the game for now; per level later.
	float lightAzimuth = 225.f;    // from the upper left
	float lightElevation = 40.f;
	float lightStrength = 1.1f;
	float coarsen = 0.3f;          // 0 photographic .. 1 blocky and banded

	// A5: what a rock is made of, as the shader sees it. Every rock but the
	// cores shares one; the cores have their own, so the one rock that never
	// moves, can't be mined and hurts to touch doesn't look like the others.
	struct Surface
	{
		// Its hue, which the packed texture's brightness channel dropped. For
		// the ordinary stone, its average colour over its average brightness,
		// as the texture tool prints it.
		glm::vec3 tint;
		float ambient;             // light that reaches every side
		float macro;               // the texture again at 5x size: big features on big rocks
		float round;               // how far the edges turn away: the dome's slope at the rim
		float selfShadow;          // how dark ridges' shadows fall across the cracks
		float shine;               // a specular highlight, for a hard, glassy stone
	};
	Surface stone = {{1.154f, 0.976f, 0.783f}, 0.35f, 0.3f, 1.6f, 0.8f, 0.06f};
	// Cores: dark, cool and glossy -- basalt, not sandstone -- with broad
	// features, so something three times the size of its field's rocks reads
	// as a massif rather than a pebble blown up.
	Surface coreStone = {{0.62f, 0.68f, 0.82f}, 0.2f, 0.8f, 2.5f, 1.f, 0.5f};
	// Heat: rises while the beam is on a rock, and cools after.
	glm::vec3 heatColour = {1.f, 0.42f, 0.08f};
	float heatReach = 0.8f;        // how far from the deepest cracks toward the ridges full heat glows
	float heatRise = 0.8f;         // per second under the beam
	// Quick to cool: the glow means "shedding ore now", so it should not
	// outlast the beam by much (A4).
	float heatCool = 1.5f;         // per second after
	float heatRadius = 380.f;      // how far round the burn it spreads
	// Shadows fall only on ships. A field rock, above the ships, casts its
	// outline away from the light; a ship inside that is darkened by how much
	// of its hull is covered. Nothing is drawn on the starfield: it is far
	// behind everything, and a rock in space cannot shade it -- drawn there,
	// the shadows read as dark smudges hanging in space. Single rocks are
	// under the ships and cast nothing on them.
	float shadowDistance = 160.f;
	float shadowAlpha = 0.6f;      // how dark a ship fully in shadow goes
	// Foreground debris: how much faster than the world it moves (0 moves
	// with it), how much of it there is, and how dark.
	float debrisParallax = 0.35f;
	float debrisFill = 0.f;        // off by default: it crowded the foreground (A3 playtest)
	float debrisShade = 0.25f;
	float debrisMargin = 2500.f;   // it reaches this far past a field's paint

	glm::vec2 lightFlat()
	{
		const float a = glm::radians(lightAzimuth);
		return {std::cos(a), std::sin(a)};
	}
	std::vector<level::Asteroid> placedCopy; // to regrow when a shape slider moves
	std::vector<level::AsteroidField> fieldsCopy;

	// A field's stamps in buckets (sight roadmap W3): whether a point is in
	// its paint, looking only at the stamps near it. The same answer as
	// AsteroidField::contains -- the last stamp covering the point decides --
	// but a level that is mostly one field has hundreds of stamps, and
	// scattering it asks millions of times.
	struct FieldArea
	{
		std::vector<level::FieldStamp> stamps;
		spatial::Grid grid;
	};

	FieldArea areaOf(const level::AsteroidField &f)
	{
		FieldArea a;
		a.stamps = f.stamps;
		std::vector<collision::Circle> circles;
		circles.reserve(f.stamps.size());
		for (const level::FieldStamp &s : f.stamps) { circles.push_back({s.position, s.radius}); }
		spatial::build(a.grid, circles, 2000.f);
		return a;
	}

	bool areaContains(const FieldArea &a, glm::vec2 p)
	{
		if (a.stamps.empty()) { return false; }
		int last = -1;
		spatial::query(a.grid, p, p, [&](int i)
		{
			const level::FieldStamp &s = a.stamps[(size_t)i];
			const glm::vec2 d = p - s.position;
			if (i > last && d.x * d.x + d.y * d.y <= s.radius * s.radius) { last = i; }
			return true;
		});
		return last >= 0 && !a.stamps[(size_t)last].erase;
	}

	std::vector<FieldArea> areas;   // one per field this round, beside fieldsCopy

	// ---- Chunks (sight roadmap W3) ----
	//
	// A field's rocks are made a square chunk at a time, for the chunks round
	// the view, and dropped once the view is well away -- a level five times
	// the size is twenty-five times the rocks, more than fit. engine/scatter's
	// scatterPart makes each chunk exactly as the whole field would have it,
	// so a chunk made again is the same rocks. What happened to them is kept
	// by each rock's identity -- its field and where scatter put it: broken
	// ones stay broken, and damage and ore taken stay taken. Rocks only
	// knocked about spring home anyway.
	//
	// Nothing in the rules needs a field rock out of sight: weapons stop only
	// at cores (M1), sight walks the paint mask (S1), and ships bump only
	// cores. Cores and single rocks are made with the round and always there.
	float chunkSize = 4000.f;       // world units, square
	float chunkMargin = 1.f;        // chunks made past the view on every side
	float chunkBudgetMs = 4.f;      // making rocks, per frame, once the round is under way

	std::unordered_map<long long, int> loadedChunks; // key -> field
	std::vector<glm::vec4> fieldBoxes;               // each field's painted bounds: lo x, lo y, hi x, hi y
	std::vector<bool> fieldHasPaint;
	std::vector<std::vector<scatter::Params::KeepOut>> fieldKeepOut; // its cores
	bool streamedThisRound = false;

	struct Kept { bool broken = false; float health = 0.f, ore = 0.f, oreLoose = 0.f; };
	struct RockId
	{
		int field;
		float x, y;
		bool operator==(const RockId &o) const { return field == o.field && x == o.x && y == o.y; }
	};
	struct RockIdHash
	{
		size_t operator()(const RockId &id) const
		{
			uint32_t x, y;
			std::memcpy(&x, &id.x, 4);
			std::memcpy(&y, &id.y, 4);
			return std::hash<uint64_t>()(((uint64_t)x << 32 | y) ^ ((uint64_t)(uint32_t)id.field * 0x9e3779b97f4a7c15ULL));
		}
	};
	std::unordered_map<RockId, Kept, RockIdHash> kept;

	// ---- Seeing the chunks (debug) ----
	//
	// The view is the screen, so the chunks being made and dropped at its
	// edges are never seen. Streaming for a smaller rectangle round the view's
	// middle -- **Stream for** below 1 -- brings those edges on screen, and
	// drawing can be culled to the same rectangle to show what culling skips.
	bool showChunkGrid = false;     // every chunk's edges, in view
	bool showChunkState = false;    // made, flashed as made or dropped, waiting
	bool showStreamRects = false;   // what streams, what is made, what is kept
	bool showKept = false;          // the remembered rocks: broken, damaged
	float streamViewScale = 1.f;    // of the real view, round its middle
	bool cullDrawToStream = false;  // draw only rocks touching the streamed view
	float chunkFlashSeconds = 0.8f;

	glm::vec4 streamedView = {};    // the last rectangles stream used
	glm::vec2 madeLo = {}, madeHi = {}, keptLo = {}, keptHi = {};
	std::vector<long long> waiting; // wanted, and left for a later frame by the budget
	struct ChunkEvent { long long key; bool made; double at; };
	std::vector<ChunkEvent> chunkEvents;
	int madeLastFrame = 0, droppedLastFrame = 0;
	double streamMsLastFrame = 0.0;

	double debugNow()
	{
		static const auto start = std::chrono::steady_clock::now();
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	long long chunkKey(int field, int x, int y)
	{
		return ((long long)field << 48) | ((long long)(uint32_t)(x & 0xffffff) << 24) | (long long)(uint32_t)(y & 0xffffff);
	}
	glm::ivec2 chunkCell(long long key)
	{
		auto unpack = [](long long v) { int i = (int)(v & 0xffffff); return (i & 0x800000) ? i - 0x1000000 : i; };
		return {unpack(key >> 24), unpack(key)};
	}

	// The fields' painted area as a grid (sight roadmap S1): what inField
	// answers from, and what sight walks along a line. Built from the stamps
	// once a round. Its stair-step edge is the truth for hiding, sight and
	// shots alike, so they all agree on where a field ends.
	region::Mask mask;
	float maskCell = 50.f;          // world units per cell

	void buildMask()
	{
		std::vector<std::vector<region::Stamp>> layers;
		layers.reserve(fieldsCopy.size());
		for (const level::AsteroidField &f : fieldsCopy)
		{
			std::vector<region::Stamp> &layer = layers.emplace_back();
			for (const level::FieldStamp &s : f.stamps) { layer.push_back({s.position, s.radius, s.erase}); }
		}
		mask = region::build(layers, maskCell);
	}

	// The rocks in buckets (sight roadmap S1b): what every hit test, the
	// rocks' bumping and the drawing look in, instead of at every rock.
	//
	// Each rock is listed under one circle round its centre of mass, big
	// enough for both of its shapes -- the outline, round a frame that orbits
	// the centre as the rock turns, and the equal-area circle it bumps with --
	// plus some **slack**. So the buckets stay true until a rock has moved
	// further than the slack, and only then are they rebuilt: a field rock
	// springing home, or one turning in place, costs nothing. Every query
	// still ends in the exact test, so the slack changes how often the
	// buckets are rebuilt, never an answer. Rebuilt lazily, the first time
	// they are asked for after that, or after rocks broke or were grown.
	spatial::Grid rockGrid;
	bool rockGridDirty = true;
	float rockGridCell = 400.f;     // world units per bucket
	float rockGridSlack = 100.f;    // how far a rock may move before the buckets are rebuilt
	std::vector<glm::vec2> listedAt; // each rock's centre when the buckets were built

	const spatial::Grid &rockIndex()
	{
		if (rockGridDirty)
		{
			static std::vector<collision::Circle> circles;
			circles.resize(rocks.size());
			listedAt.resize(rocks.size());
			for (size_t i = 0; i < rocks.size(); i++)
			{
				const Rock &r = rocks[i];
				const float offset = glm::distance(r.body.position, r.placement.position);
				circles[i] = {r.body.position, std::max(offset + r.bound, r.collideRadius) + rockGridSlack};
				listedAt[i] = r.body.position;
			}
			spatial::build(rockGrid, circles, rockGridCell);
			rockGridDirty = false;
		}
		return rockGrid;
	}

	// After rocks moved: the buckets go stale once any rock is further than
	// the slack from where it was listed. Only awake rocks move.
	void checkListed()
	{
		if (rockGridDirty || listedAt.size() != rocks.size()) { rockGridDirty = true; return; }
		const float slack2 = rockGridSlack * rockGridSlack;
		for (size_t i = 0; i < rocks.size(); i++)
		{
			if (!rocks[i].awake) { continue; }
			const glm::vec2 d = rocks[i].body.position - listedAt[i];
			if (glm::dot(d, d) > slack2) { rockGridDirty = true; return; }
		}
	}

	// Fields (A1b). A field's own numbers are its max rock size and the room
	// between rocks; these shape every field's spread.
	float fieldMinFraction = 0.12f; // the smallest rock, as a fraction of the largest
	int fieldLayers = 3;            // grids, coarse to fine: big rocks, then smaller between
	float fieldGapVariation = 1.f;  // 0: every rock keeps the full gap; 1: anywhere up to it
	// No clumping here. A painted area is the designer saying "this is
	// cover", and a clump's empty patch inside it would still hide the player
	// with nothing overhead. Clumps are the procedural generator's job: it
	// decides where to paint (engine/scatter's density), so paint and rocks
	// always agree.
	float fieldSmallBias = 1.5f;    // how strongly sizes lean small within a layer
	float fieldFill = 0.9f;         // the chance a cell holds a rock at all
	float areaDotSpacingPixels = 14.f; // the editor's tint of a painted area
	float shadeInField = 0.45f;     // a hidden ship's light

	// Shape. Corners are spaced by distance round the outline rather than
	// counted, so a big rock is as craggy up close as a small one.
	float cornerSpacing = 160.f;   // world units of outline per corner
	float roughness = 0.22f;
	float angleJitter = 0.6f;

	// The texture is laid on at a fixed scale -- one repeat per this many
	// world units -- so every rock is the same stone at the same grain, and a
	// big rock wraps onto the texture again (a repeating sampler) rather than
	// stretching it.
	float textureWorldSize = 1800.f;

	float brightness = 0.9f;

	bool showOutlines = false;

	// ---- Physics (A2) ----
	// Mass per unit area. Small, so masses are handy numbers: a rock of radius
	// 350 weighs about 38, a pebble of 40 about 0.5 -- sixty times lighter,
	// because mass grows with area.
	float density = 1e-4f;
	float shotPush = 12000.f;      // impulse per unit of a shot's damage
	float missilePush = 4.f;       // a missile pushes this many times harder
	// The beam barely moves a rock: a small steady force, and never faster
	// than a creep along the beam, or turning faster than a slow roll -- the
	// rock holds still to be mined. A force alone would not do it: a pebble
	// weighs a sixtieth of a big rock, and the same force hurls it.
	float beamPush = 400.f;        // steady force while the beam touches
	float beamCreep = 25.f;        // the fastest it pushes a rock, units per second
	float beamRoll = 0.25f;        // the fastest it turns one, rad/s
	float blastPush = 3000.f;      // at a blast's centre, falling to 0 at its reach
	float blastReach = 1800.f;
	// The ram throws rocks outward from the prow, along the line to each
	// rock's centre, with this much of the ram's own direction added -- so
	// rocks either side of it scatter sideways and a glancing strike spins
	// them. All along the ram, they moved as one, like snow before a plough.
	float ramPush = 30000.f;
	float ramForward = 0.7f;
	// Drift halves in about 1.4 s. A body at speed v with damping d glides
	// v / d before it stops: at 0.25 and a 2500 cap, a rammed pebble glided
	// 10000 units, clean out of its field.
	float linearDamping = 0.5f;
	float angularDamping = 0.3f;
	float maxSpeed = 1800.f;       // pebbles are light: without a cap a shot fires them off
	float maxSpin = 10.f;
	// Rock on rock: lively, and with friction at the contact, so a glancing
	// bump hands over spin and rocks tumble off each other.
	float restitution = 0.85f;
	float friction = 0.4f;
	// Slower than this, a touch is only a touch. Rocks drifting home together
	// bounced off each other at 0.85 on every gentle touch, and buzzed: 4000
	// bounces in the eight seconds it took a field to settle.
	float restingSpeed = 120.f;
	// Field rocks spring back home, all in about the same time whatever their
	// size -- the spring is an acceleration, not a force, so mass cancels. But
	// not at once: a struck rock is loose for `springDelay`, then the spring
	// eases in. With the spring on from the first moment, its damping alone
	// (2ζω, about 2.5 per second at 4 s and 0.8) halved a rock's speed in a
	// third of a second, and nothing had time to bounce.
	// And home is a drift, not a slingshot: critically damped, and no faster
	// than `returnSpeed`. A rock sent thousands out, pulled by an underdamped
	// spring, came back at the speed cap and overshot through the field.
	float springPeriod = 5.f;
	float springDamping = 1.f;     // 1 settles without overshoot; below, a little sway
	float springDelay = 3.f;
	float springEase = 1.f;
	float returnSpeed = 500.f;

	// ---- Breaking (A4) ----
	// Health grows with area to the three-quarters: a pebble pops in a shot or
	// two, a mid-sized rock takes a few seconds of beam, and a big one a
	// sustained effort -- more than a small one, but not in proportion to its
	// size, or a big rock would take minutes.
	float healthScale = 0.3f;      // health of a rock 10000 units in area (radius ~56)
	float beamRockScale = 4.f;     // the beam wears rock this many times faster than ships
	float blastDamage = 0.5f;      // at a blast's centre, falling like its push
	float ramDamage = 1.5f;
	// Ore grows with area. Only the beam sheds it; a rock broken any other
	// way loses what it held.
	float orePerArea = 1.2e-5f;
	float orbValue = 0.25f;        // ore per orb shed
	// The break: the rock splits along its cracks into Voronoi pieces, one per
	// this much area (2 to 6), which fly apart spinning. Smaller than
	// `minPieceRadius`, a piece is dust.
	float pieceArea = 70000.f;
	float minPieceRadius = 20.f;
	float breakKick = 220.f;       // outward, on top of the parent's own motion
	float breakSpin = 1.2f;
	// Then the pieces are shards, out of play. They fly for `shardFlight`,
	// slowing like wreckage, darkening and shrinking a little as they sink
	// into the background; then a spring eases in and gathers them where the
	// rock stood, each in its own place in it, spread a little so the cracks
	// show. Critically damped and slow, as the field's rocks come home.
	float shardFlight = 1.2f;      // seconds before the pull
	float shardDrag = 1.6f;        // per second, in flight, as the wreck field's
	float shardSink = 2.f;         // seconds to settle into the background
	float shardShade = 0.45f;      // of a rock's brightness, once sunk
	float shardScale = 0.85f;      // of its size, once sunk: further off
	float shardGather = 4.f;       // the pull's period, seconds
	float shardEase = 1.f;         // seconds for the pull to come fully on
	float shardReturnSpeed = 300.f;
	float shardSpread = 1.12f;     // how far apart the gathered pieces sit
	float shardSpinRest = 0.12f;   // the most a gathered shard still turns, rad/s
	int shardLimit = 400;          // past this the oldest shrink away, like the wreck field
	float shardFadeOut = 1.f;
	// The cracks as drawn: dark lines, and near where the beam burns, lines of
	// glowing orange.
	float crackWidthPixels = 2.f;
	float crackGlowWidthPixels = 3.5f;

	// ---- Cores ----
	// Each field has one: bigger than any of its rocks where the paint has
	// room, at the painted area's middle unless dragged. It is what the field's rocks are seen to fall
	// back toward. It never moves -- an immovable body, zero inverse mass --
	// so rocks bounce off it, and it is solid to ships.
	float coreScale = 3.f;         // its radius, in the field's max rock sizes
	// But never past its painted area: a thin strip painted with a big max
	// size had a core five times wider than the paint, covering the strip and
	// leaving no room for rocks (level1). Shrunk to fit -- though never below
	// this share of the field's max size, so one dragged out of the paint
	// stays a rock.
	float coreMinFit = 0.5f;
	CoreRules rules;
	int awakeCount = 0;            // last step's, for the panel

	void place(Rock &r)
	{
		const float c = std::cos(r.body.angle), s = std::sin(r.body.angle);
		const glm::vec2 back = {-r.centroid.x * c + r.centroid.y * s, -r.centroid.x * s - r.centroid.y * c};
		r.placement = {r.body.position + back, r.body.angle};
	}

	// Loose: struck recently, free of the spring. Returning: a field rock whose
	// loose time is over, drifting home.
	bool loose(const Rock &r) { return r.sinceStruck < springDelay; }
	bool returning(const Rock &r) { return r.inField && !loose(r); }

	// Pushed or bumped: moving, and loose of the spring for a while.
	void wake(Rock &r)
	{
		r.awake = true;
		r.sinceStruck = 0.f;
	}

	void clampMotion(Rock &r)
	{
		const float speed = glm::length(r.body.velocity);
		if (speed > maxSpeed) { r.body.velocity *= maxSpeed / speed; }
		r.body.spin = std::clamp(r.body.spin, -maxSpin, maxSpin);
	}

	// A little hash so a seed also picks the texture window.
	uint32_t mix(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
		return x;
	}

	// Health and ore for a rock of `area`: health fresh, ore at full.
	void setAmounts(Rock &r, float area)
	{
		r.area = area;
		r.healthFull = healthScale * std::pow(std::max(area, 1.f) / 10000.f, 0.75f);
		r.health = r.healthFull;
		r.oreFull = orePerArea * area;
	}

	// `shrink` scales the outline once it is made, keeping its corners: a core
	// fitted to its field is the same shape, smaller.
	Rock grow(const level::Asteroid &a, float shrink = 1.f)
	{
		Rock r;
		r.placement = {a.position, 0.f};
		polygon::StarParams params;
		const float circumference = 6.2831853f * a.radius;
		params.vertexCount = std::clamp((int)(circumference / std::max(cornerSpacing, 10.f)), 12, 96);
		params.roughness = roughness;
		params.angleJitter = angleJitter;
		r.outline = polygon::starShaped(a.seed, a.radius, params);
		for (glm::vec2 &p : r.outline) { p *= shrink; }
		r.bound = polygon::boundingRadius(r.outline);
		const uint32_t h = mix(a.seed ^ 0x9e3779b9U);
		r.uvOffset = {(h & 0xffff) / 65535.f, (h >> 16) / 65535.f};

		// Mass and turning from the same fan that is drawn and hit.
		const rigid::MassProperties mass = rigid::fromFan(r.outline, {0.f, 0.f}, density);
		r.centroid = mass.centroid;
		r.body = rigid::makeBody(mass, a.position + mass.centroid);
		r.home = r.body.position;
		r.collideRadius = std::sqrt(mass.area / 3.1415927f);
		r.seed = a.seed;
		setAmounts(r, mass.area);
		r.ore = r.oreFull;
		place(r);
		return r;
	}

	// The fan, as renderTriangles takes it: three corners per triangle, the
	// rock's centre first. Texture coordinates come from where each corner
	// sits *on the rock*, so the texture is fixed to it and turns with it;
	// positions are those corners placed in the world, scaled by `grow` (the
	// foreground debris is drawn larger, being nearer).
	//
	// The vertex colour is not a colour here: it is what the asteroid shader
	// needs per rock (see asteroid.wgsl) -- which way is outward and how far
	// to the edge, and the beam's heat or a shade. Carried on the vertices,
	// every rock of a surface shares one set of effect parameters and so one
	// draw. `shade` is a share of a rock's light, for shards and debris;
	// something shaded is never hot.
	void drawFan(wgpu2d::Renderer2D &renderer, const std::vector<glm::vec2> &outline,
		const polygon::Placement &at, glm::vec2 uvOffset, float shade, float heat, glm::vec2 heatAt,
		float grow = 1.f, const std::vector<int> *triangles = nullptr)
	{
		const size_t n = outline.size();
		if (n < 3 || rockTexture.id == 0) { return; }

		static std::vector<glm::vec2> positions, uvs;
		static std::vector<glm::vec4> colours;
		positions.clear(); uvs.clear(); colours.clear();

		auto heatOf = [&](glm::vec2 local)
		{
			if (heat <= 0.f) { return 0.f; }
			const float t = std::clamp(1.f - glm::distance(local, heatAt) / std::max(heatRadius, 1.f), 0.f, 1.f);
			return heat * t * t * (3.f - 2.f * t);
		};
		// Heat, or a shade as a negative number: never 0, which would read as
		// "unshaded".
		const bool shaded = shade < 0.999f;
		auto last = [&](glm::vec2 local) { return shaded ? -std::max(shade, 0.001f) : heatOf(local); };
		auto outward = [](glm::vec2 p) { const float l = glm::length(p); return l > 1e-6f ? p / l : glm::vec2(0.f); };
		const float scale = 1.f / std::max(textureWorldSize, 1.f);
		auto uvOf = [&](glm::vec2 local) { return local * scale + uvOffset; };

		// A shard no fan covers (A4): its ear-clipped triangles. With no centre
		// there is no exact distance to the edge, so it says so (-1) and the
		// shader makes do with the corners' directions.
		if (triangles && !triangles->empty())
		{
			for (size_t t = 0; t + 2 < triangles->size(); t += 3)
			{
				for (int k = 0; k < 3; k++)
				{
					const glm::vec2 p = outline[(*triangles)[t + k]];
					positions.push_back(polygon::toWorld(at, p * grow));
					uvs.push_back(uvOf(p));
					colours.push_back({outward(p), -1.f, last(p)});
				}
			}
			renderer.renderTriangles(positions.data(), uvs.data(), colours.data(), positions.size(), rockTexture);
			return;
		}

		// Centre: nowhere outward, no way to the edge. Corners: their own
		// direction, all the way there. Across each triangle the GPU blends
		// them, and because each triangle's far side is one straight edge,
		// the blend of "how far" is exact.
		const glm::vec4 centre = {0.f, 0.f, 0.f, last({0.f, 0.f})};
		for (size_t i = 0; i < n; i++)
		{
			const glm::vec2 a = outline[i];
			const glm::vec2 b = outline[(i + 1) % n];
			positions.push_back(at.position);
			positions.push_back(polygon::toWorld(at, a * grow));
			positions.push_back(polygon::toWorld(at, b * grow));
			uvs.push_back(uvOf({0.f, 0.f}));
			uvs.push_back(uvOf(a));
			uvs.push_back(uvOf(b));
			colours.push_back(centre);
			colours.push_back({outward(a), 1.f, last(a)});
			colours.push_back({outward(b), 1.f, last(b)});
		}
		renderer.renderTriangles(positions.data(), uvs.data(), colours.data(), positions.size(), rockTexture);
	}

	void drawRock(wgpu2d::Renderer2D &renderer, const Rock &r)
	{
		drawFan(renderer, r.outline, r.placement, r.uvOffset, 1.f, r.heat, r.heatAt);
	}

	// The asteroid shader and its parameters: the same for every rock of a
	// surface, which is what lets them all go in one draw. The light comes in
	// the world's frame; the shader turns it into each rock's.
	void beginRocks(wgpu2d::Renderer2D &renderer, const Surface &surface = stone)
	{
		wgpu2d::EffectParams params;
		const float strength = std::max(lightStrength, 0.01f);
		params.a = {surface.tint * (brightness * strength), surface.ambient / strength};
		params.b = {lightFlat(), std::sin(glm::radians(lightElevation)), surface.macro};
		params.c = {heatColour, heatReach};
		params.d = {coarsen, surface.round, surface.selfShadow, surface.shine};
		renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
		renderer.setEffect(rockEffect, params);
	}

	void endRocks(wgpu2d::Renderer2D &renderer) { renderer.clearEffect(); }

	void drawOutline(wgpu2d::Renderer2D &renderer, const Rock &r)
	{
		const float px = 1.f / std::max(renderer.currentCamera.zoom, 0.001f);
		const size_t n = r.outline.size();
		for (size_t i = 0; i < n; i++)
		{
			renderer.renderLine(polygon::toWorld(r.placement, r.outline[i]),
				polygon::toWorld(r.placement, r.outline[(i + 1) % n]), {0.4f, 1.f, 0.4f, 1.f}, 2.f * px);
		}
	}

	bool onScreen(const glm::vec4 &view, const Rock &r)
	{
		const glm::vec2 p = r.placement.position;
		return !(p.x + r.bound < view.x || p.x - r.bound > view.x + view.z
			|| p.y + r.bound < view.y || p.y - r.bound > view.y + view.w);
	}

	// The rectangle a field's painted stamps reach. Erasers only take away,
	// so they cannot widen it.
	bool fieldBounds(const level::AsteroidField &f, glm::vec2 &lo, glm::vec2 &hi)
	{
		bool any = false;
		for (const level::FieldStamp &s : f.stamps)
		{
			if (s.erase) { continue; }
			const glm::vec2 a = s.position - glm::vec2(s.radius), b = s.position + glm::vec2(s.radius);
			lo = any ? glm::min(lo, a) : a;
			hi = any ? glm::max(hi, b) : b;
			any = true;
		}
		return any;
	}

	// Where a field's core goes when nobody has dragged it: the middle of the
	// painted area, found by sampling it on a grid and averaging the points
	// inside. A C-shaped field's middle can land in the empty part, so then
	// it is moved to the painted sample nearest to it.
	glm::vec2 autoCore(const level::AsteroidField &f)
	{
		glm::vec2 lo, hi;
		if (!fieldBounds(f, lo, hi)) { return {}; }
		constexpr int samples = 48;
		glm::vec2 sum = {};
		int count = 0;
		std::vector<glm::vec2> inside;
		for (int y = 0; y < samples; y++)
		{
			for (int x = 0; x < samples; x++)
			{
				const glm::vec2 p = lo + (hi - lo) * glm::vec2((x + 0.5f) / samples, (y + 0.5f) / samples);
				if (!f.contains(p)) { continue; }
				sum += p;
				count++;
				inside.push_back(p);
			}
		}
		if (count == 0) { return (lo + hi) * 0.5f; }
		const glm::vec2 middle = sum / (float)count;
		if (f.contains(middle)) { return middle; }
		glm::vec2 nearest = inside.front();
		for (const glm::vec2 &p : inside)
		{
			if (glm::distance(p, middle) < glm::distance(nearest, middle)) { nearest = p; }
		}
		return nearest;
	}

	// The first core's seed is what the field's only core always had, so a
	// level from before W1 keeps its core's shape.
	uint32_t coreSeed(const level::AsteroidField &f, int core)
	{
		const uint32_t first = mix(f.seed ^ 0xc0e0c0e0U);
		return core == 0 ? first : mix(first ^ (0x9e3779b9U * (uint32_t)core));
	}

	// Where each of a field's cores is: where the designer placed them, or,
	// with none placed, one at the painted area's middle.
	std::vector<glm::vec2> coresOf(const level::AsteroidField &f)
	{
		if (!f.cores.empty()) { return f.cores; }
		return {autoCore(f)};
	}

	// A field: its core first, then its rocks -- layers of grids, one rock per
	// cell, the big rocks first and smaller ones filling between them, never
	// closer than the field's gap, and clear of the core (engine/scatter).
	// Each cell's rock is decided by the field's seed and that cell, so
	// painting more area adds rocks and leaves the old ones where they were.
	// A field's core: grown at full size, then shrunk until every corner is
	// inside the painted area. Each corner is walked out along its own
	// direction from the centre to find where the paint ends that way, so it
	// is the rock's real shape that fits, lobes and all, not a circle round it.
	//
	// Painted, not erased: a clearing erased round the core is the core's
	// room, not the field's edge (level2 has one), so erasers don't count.
	Rock growCore(const level::AsteroidField &f, glm::vec2 at, int index)
	{
		auto painted = [&](glm::vec2 p)
		{
			for (const level::FieldStamp &st : f.stamps)
			{
				if (!st.erase && glm::distance(p, st.position) <= st.radius) { return true; }
			}
			return false;
		};
		const level::Asteroid full = {at, f.maxSize * coreScale, coreSeed(f, index)};
		const Rock probe = grow(full);
		const float step = std::max(f.maxSize * 0.02f, 5.f);
		float shrink = 1.f;
		for (const glm::vec2 &corner : probe.outline)
		{
			const float reach = glm::length(corner);
			if (reach < 1e-3f) { continue; }
			const glm::vec2 way = corner / reach;
			for (float t = step; t <= reach; t += step)
			{
				if (!painted(full.position + way * t))
				{
					shrink = std::min(shrink, (t - step) / reach);
					break;
				}
			}
		}
		shrink = std::max(shrink, coreMinFit / std::max(coreScale, 0.01f));
		return shrink < 1.f ? grow(full, shrink) : probe;
	}

	// How a field's rocks are spread: its own numbers and the shared field
	// tuning. Every chunk of it, in play or in the editor, scatters with these.
	scatter::Params fieldParams(const level::AsteroidField &f, const std::vector<scatter::Params::KeepOut> &keepOut)
	{
		scatter::Params params;
		params.keepOut = keepOut;
		params.seed = f.seed;
		params.maxRadius = f.maxSize;
		params.minRadius = f.maxSize * fieldMinFraction;
		params.gap = f.maxGap;
		params.gapVariation = fieldGapVariation;
		params.layers = fieldLayers;
		params.smallBias = fieldSmallBias;
		params.fill = fieldFill;
		return params;
	}

	// A field's cores, into `out`, and the circles its rocks keep clear of.
	void growCores(const level::AsteroidField &f, int index, std::vector<Rock> &out,
		std::vector<scatter::Params::KeepOut> &keepOut)
	{
		keepOut.clear();
		glm::vec2 lo, hi;
		if (!fieldBounds(f, lo, hi)) { return; }
		const std::vector<glm::vec2> cores = coresOf(f);
		for (int c = 0; c < (int)cores.size(); c++)
		{
			Rock core = growCore(f, cores[(size_t)c], c);
			core.inField = true;
			core.core = true;
			core.field = index;
			core.body.inverseMass = 0.f;     // immovable: nothing can push it
			core.body.inverseInertia = 0.f;
			core.ore = core.oreFull = 0.f;   // and it holds nothing to mine (A4)
			keepOut.push_back({core.placement.position, core.bound});
			out.push_back(std::move(core));
		}
	}

	// One chunk of a field's rocks, into `out`: those whose spot is in the
	// square at chunk (x, y), exactly as scattering the whole field would
	// place them. `onEach` sees each before it goes in.
	template <class F>
	void growChunk(const level::AsteroidField &f, const FieldArea &area, int index,
		const std::vector<scatter::Params::KeepOut> &keepOut, int x, int y, float size,
		std::vector<Rock> &out, F &&onEach)
	{
		const glm::vec2 lo = glm::vec2((float)x, (float)y) * size;
		const long long key = chunkKey(index, x, y);
		for (const scatter::Item &item : scatter::scatterPart(lo, lo + glm::vec2(size),
			[&](glm::vec2 p) { return areaContains(area, p); }, fieldParams(f, keepOut)))
		{
			Rock r = grow({item.position, item.radius, item.seed});
			r.inField = true;
			r.field = index;
			r.chunk = key;
			r.origin = item.position;
			if (onEach(r)) { out.push_back(std::move(r)); }
		}
	}

	// The editor's view of a painted area: dots on a screen-spaced grid
	// wherever the area is, so overlapping brush stamps read as one flat
	// region instead of stacking up.
	void drawArea(wgpu2d::Renderer2D &renderer, const level::AsteroidField &f, const FieldArea &area)
	{
		glm::vec2 lo, hi;
		if (!fieldBounds(f, lo, hi)) { return; }
		const glm::vec4 view = renderer.getViewRect();
		lo = glm::max(lo, glm::vec2(view.x, view.y));
		hi = glm::min(hi, glm::vec2(view.x + view.z, view.y + view.w));
		const float step = std::max(areaDotSpacingPixels, 4.f) / std::max(renderer.currentCamera.zoom, 0.001f);
		const float dot = step * 0.3f;
		// Snapped to the step, so the dots stay put in the world as it pans.
		for (float y = std::floor(lo.y / step) * step; y <= hi.y; y += step)
		{
			for (float x = std::floor(lo.x / step) * step; x <= hi.x; x += step)
			{
				if (!areaContains(area, {x, y})) { continue; }
				renderer.renderRectangle({x - dot * 0.5f, y - dot * 0.5f, dot, dot}, {0.4f, 1.f, 0.6f, 0.55f});
			}
		}
	}

	// ---- Breaking (A4) ----

	// A rock's crack network, worked out the first time it is needed -- a
	// field of six hundred rocks costs nothing until one is hurt. Sites are
	// scattered inside it from its seed, one per `pieceArea` of rock; the
	// Voronoi pieces round them are what it will break into, and the borders
	// between them are its cracks. Each border shows once the damage passes
	// its own threshold, so cracks appear one by one rather than all at once.
	void ensureCracks(Rock &r)
	{
		if (r.cracksBuilt) { return; }
		r.cracksBuilt = true;

		uint32_t state = mix(r.seed ^ 0x5eed5eedU);
		auto next = [&]() { state = mix(state + 0x9e3779b9U); return (state >> 8) * (1.f / 16777216.f); };

		const int wanted = std::clamp((int)std::lround(r.area / std::max(pieceArea, 1.f)), 2, 6);
		std::vector<glm::vec2> sites;
		for (int tries = 0; tries < 200 && (int)sites.size() < wanted; tries++)
		{
			const glm::vec2 p = {(next() * 2.f - 1.f) * r.bound, (next() * 2.f - 1.f) * r.bound};
			if (polygon::contains(r.outline, p)) { sites.push_back(p); }
		}
		if (sites.size() < 2) { return; }

		r.fracture = polygon::voronoiFracture(r.outline, sites);
		for (size_t i = 0; i < r.fracture.size(); i++)
		{
			const polygon::Piece &piece = r.fracture[i];
			const size_t n = piece.points.size();
			for (size_t e = 0; e < n; e++)
			{
				// Each border belongs to two pieces; keep it once, from the
				// lower-numbered side.
				if (piece.neighbour[e] <= (int)i) { continue; }
				r.cracks.push_back({piece.points[e], piece.points[(e + 1) % n], 0.05f + 0.8f * next()});
			}
		}
	}

	// An orb of `value` thrown off `r` at `point`, back along the beam's
	// `direction`, turned by up to `spread` radians either way.
	void throwOrb(const Rock &r, glm::vec2 point, glm::vec2 direction, float spread, float value)
	{
		const float turn = ((mix(r.seed + (uint32_t)orbsThrown++) & 0xffff) / 65535.f - 0.5f) * 2.f * spread;
		const glm::vec2 back = -direction;
		const glm::vec2 out = {back.x * std::cos(turn) - back.y * std::sin(turn),
			back.x * std::sin(turn) + back.y * std::cos(turn)};
		resources::emitOrb(point + out * 40.f, out, value);
	}

	// Hurt: the core never is. Returns true if it is now broken.
	bool hurt(Rock &r, float damage)
	{
		if (r.core || damage <= 0.f) { return false; }
		r.health -= damage;
		ensureCracks(r);
		return r.health <= 0.f;
	}

	// One rock into its shards: each piece of its crack network, moving as
	// that spot of the rock was (v + ω × r), kicked outward, spinning. Pieces
	// too small to see are dust.
	void breakInto(const Rock &r)
	{
		effects::rockBurst(r.placement.position, r.bound * 2.4f);

		// Finished off by the beam: everything it still held bursts out as
		// orbs, the part not yet a whole orb included -- so even a pebble,
		// holding less than one orb, gives one. Like a spent deposit's last
		// ore. Broken any other way, its ore is lost with it: only the beam
		// mines.
		if (r.beamKill)
		{
			for (float left = r.ore + r.oreLoose; left > 0.0005f; left -= orbValue)
			{
				throwOrb(r, r.beamPoint, r.beamDirection, 1.6f, std::min(orbValue, left));
			}
		}
		if (r.fracture.empty()) { return; }
		const float minArea = 3.1415927f * minPieceRadius * minPieceRadius;
		const float scale = 1.f / std::max(textureWorldSize, 1.f);
		// The rock's frame with it back home, turned as it is now.
		const polygon::Placement home = {r.home, r.body.angle};

		for (size_t i = 0; i < r.fracture.size(); i++)
		{
			const std::vector<glm::vec2> &points = r.fracture[i].points;
			const float area = std::abs(polygon::signedArea(points));
			if (area < minArea) { continue; }

			// The piece in its own frame, around its centroid -- the point it
			// is drawn and turned about -- fanned from there if it can be,
			// ear-clipped if not.
			const glm::vec2 c = polygon::centroid(points);
			Shard q;
			for (const glm::vec2 &p : points) { q.outline.push_back(p - c); }
			if (!polygon::fanWorksFrom(q.outline, {0.f, 0.f})) { q.triangles = polygon::earClip(q.outline); }
			q.bound = polygon::boundingRadius(q.outline);

			// Where that part of the rock was, and the same texture on it.
			const uint32_t h = mix(r.seed ^ mix((uint32_t)i + 1u));
			q.uvOffset = r.uvOffset + c * scale;
			q.position = polygon::toWorld(r.placement, c);
			q.angle = r.body.angle;
			const glm::vec2 fromCentre = q.position - r.body.position;
			const float d = glm::length(fromCentre);
			q.velocity = r.body.velocity + glm::vec2(-r.body.spin * fromCentre.y, r.body.spin * fromCentre.x)
				+ (d > 1e-3f ? fromCentre / d : glm::vec2(0.f)) * breakKick * (0.6f + 0.6f * ((h & 0xff) / 255.f));
			q.spin = r.body.spin + breakSpin * (((h >> 8) & 0xff) / 127.5f - 1.f);
			q.slot = polygon::toWorld(home, (c - r.centroid) * shardSpread);
			shards.push_back(std::move(q));
		}
	}

	// Breaks every rock whose health has run out, after whatever loop hurt
	// them -- breaking mid-loop would remove rocks under it.
	void processBreaks()
	{
		for (size_t i = 0; i < rocks.size(); )
		{
			if (!rocks[i].core && rocks[i].health <= 0.f)
			{
				// A chunk's rock stays broken when the chunk is made again (W3).
				if (rocks[i].chunk >= 0) { kept[{rocks[i].field, rocks[i].origin.x, rocks[i].origin.y}].broken = true; }
				breakInto(rocks[i]);
				rocks.erase(rocks.begin() + (long)i);
				rockGridDirty = true; // the rocks after it are renumbered
			}
			else { i++; }
		}
	}

	void updateShards(float dt)
	{
		const float omega = 6.2831853f / std::max(shardGather, 0.1f);
		const float settle = 1.f - std::exp(-dt); // spin eases toward rest
		for (Shard &s : shards)
		{
			s.age += dt;
			// Flying: wreckage's drag. Then the pull eases in, with damping of
			// its own, so the drag eases out as it does.
			const float hold = std::clamp((s.age - shardFlight) / std::max(shardEase, 0.01f), 0.f, 1.f);
			s.velocity *= std::exp(-shardDrag * (1.f - hold) * dt);
			s.velocity += hold * (-omega * omega * (s.position - s.slot) - 2.f * omega * s.velocity) * dt;
			const float speed = glm::length(s.velocity);
			if (hold > 0.f && speed > shardReturnSpeed) { s.velocity *= shardReturnSpeed / speed; }
			s.position += s.velocity * dt;
			s.angle = std::remainder(s.angle + s.spin * dt, 6.2831853f);
			const float rest = std::copysign(std::min(std::fabs(s.spin), shardSpinRest), s.spin);
			s.spin += (rest - s.spin) * settle;
		}

		// Oldest first, so those past the limit are the first ones: they
		// shrink away rather than vanish.
		const int excess = (int)shards.size() - shardLimit;
		for (int i = 0; i < excess; i++)
		{
			if (shards[i].fadeStart < 0.f) { shards[i].fadeStart = shards[i].age; }
		}
		shards.erase(std::remove_if(shards.begin(), shards.end(),
			[](const Shard &s) { return s.fadeStart >= 0.f && s.age - s.fadeStart >= shardFadeOut; }),
			shards.end());
	}

	// Under everything else of the asteroids': darker and a little smaller as
	// they sink, so they read as behind the play, not in it.
	void drawShards(wgpu2d::Renderer2D &renderer)
	{
		if (shards.empty()) { return; }
		const glm::vec4 view = renderer.getViewRect();
		beginRocks(renderer);
		for (const Shard &s : shards)
		{
			const float reach = s.bound;
			if (s.position.x + reach < view.x || s.position.x - reach > view.x + view.z
				|| s.position.y + reach < view.y || s.position.y - reach > view.y + view.w) { continue; }
			const float sunk = std::min(1.f, s.age / std::max(shardSink, 0.01f));
			const float fade = s.fadeStart < 0.f ? 1.f
				: std::clamp(1.f - (s.age - s.fadeStart) / std::max(shardFadeOut, 0.01f), 0.f, 1.f);
			drawFan(renderer, s.outline, {s.position, s.angle}, s.uvOffset,
				glm::mix(1.f, shardShade, sunk), 0.f, {},
				glm::mix(1.f, shardScale, sunk) * fade, &s.triangles);
		}
		endRocks(renderer);
	}

	// The cracks: dark lines once damage has shown them, and orange ones
	// glowing near where the beam burns -- on a rock shedding ore, and only
	// then. Line widths are held on screen at any zoom.
	void drawCracks(wgpu2d::Renderer2D &renderer, const Rock &r, float px, bool glowPass)
	{
		if (r.cracks.empty()) { return; }
		const float damage = r.healthFull > 0.f ? 1.f - std::max(r.health, 0.f) / r.healthFull : 0.f;
		for (const Rock::Crack &c : r.cracks)
		{
			const glm::vec2 a = polygon::toWorld(r.placement, c.a), b = polygon::toWorld(r.placement, c.b);
			if (!glowPass)
			{
				if (damage < c.threshold) { continue; }
				const float grown = std::min(1.f, (damage - c.threshold) * 4.f + 0.4f);
				renderer.renderLine(a, b, {0.05f, 0.04f, 0.03f, 0.85f * grown}, crackWidthPixels * px);
				continue;
			}
			if (r.heat <= 0.f) { continue; }
			const glm::vec2 mid = (c.a + c.b) * 0.5f;
			const float t = std::clamp(1.f - glm::distance(mid, r.heatAt) / std::max(heatRadius, 1.f), 0.f, 1.f);
			const float glow = r.heat * t;
			if (glow <= 0.02f) { continue; }
			renderer.renderLine(a, b, glm::vec4(heatColour * (1.6f * glow), 1.f), crackGlowWidthPixels * px);
		}
	}

}

bool init()
{
	// Smooth rather than pixelated: it is a photograph, and nearest filtering
	// on one at a fraction of its size shimmers as the camera moves (the
	// shader coarsens it on purpose instead). Mipmaps for the same reason when
	// zoomed out. Repeating, for big rocks.
	const char *path = RESOURCES_PATH "asteroid/rock_packed.png";
	rockTexture.loadFromFile(path, false, true, true);
	if (rockTexture.id == 0)
	{
		std::cerr << "asteroids: failed to load " << path << "\n";
		return false;
	}

	const char *shaderPath = RESOURCES_PATH "shaders/asteroid.wgsl";
	std::ifstream file(shaderPath, std::ios::binary);
	std::stringstream source;
	source << file.rdbuf();
	rockEffect = wgpu2d::createEffect(source.str().c_str(), "asteroid");
	if (!file.is_open() || rockEffect.id == 0)
	{
		std::cerr << "asteroids: " << shaderPath << " is missing or did not compile\n";
		return false;
	}
	return true;
}

void cleanup()
{
	rockTexture.cleanup();
}

void reset(const std::vector<level::Asteroid> &placed, const std::vector<level::AsteroidField> &fields)
{
	placedCopy = placed;
	fieldsCopy = fields;
	buildMask();
	rocks.clear();
	for (const level::Asteroid &a : placed) { rocks.push_back(grow(a)); }

	// The fields' cores now, and their rocks a chunk at a time round the
	// view: `stream`, from the first frame (W3).
	areas.clear();
	fieldBoxes.clear();
	fieldHasPaint.clear();
	fieldKeepOut.assign(fields.size(), {});
	for (int i = 0; i < (int)fields.size(); i++)
	{
		areas.push_back(areaOf(fields[(size_t)i]));
		glm::vec2 lo = {}, hi = {};
		fieldHasPaint.push_back(fieldBounds(fields[(size_t)i], lo, hi));
		fieldBoxes.push_back({lo, hi});
		growCores(fields[(size_t)i], i, rocks, fieldKeepOut[(size_t)i]);
	}
	loadedChunks.clear();
	kept.clear();
	streamedThisRound = false;
	rockGridDirty = true;
	shards.clear();
	orbsThrown = 0;

	// The foreground debris: sparse and small, round each field and a way
	// past it, from the field's seed.
	debris.clear();
	for (const level::AsteroidField &f : fields)
	{
		glm::vec2 lo, hi;
		if (!fieldBounds(f, lo, hi)) { continue; }
		scatter::Params params;
		params.seed = mix(f.seed ^ 0xdeb415U);
		params.maxRadius = f.maxSize * 0.6f;
		params.minRadius = f.maxSize * 0.15f;
		params.gap = f.maxSize * 4.f;
		params.layers = 1;
		params.fill = debrisFill;
		const glm::vec2 margin(debrisMargin);
		for (const scatter::Item &item : scatter::scatter(lo - margin, hi + margin,
			[](glm::vec2) { return true; }, params))
		{
			polygon::StarParams shape;
			shape.vertexCount = 14;
			Debris d;
			d.outline = polygon::starShaped(item.seed, item.radius, shape);
			d.bound = polygon::boundingRadius(d.outline);
			d.position = item.position;
			const uint32_t h = mix(item.seed);
			d.angle = (h & 0xffff) / 65535.f * 6.2831853f;
			d.uvOffset = {((h >> 16) & 0xff) / 255.f, (h >> 24) / 255.f};
			debris.push_back(std::move(d));
		}
	}
}

namespace
{
	// What a chunk's rock carries away when the chunk goes: only what differs
	// from a rock made fresh.
	void remember(const Rock &r)
	{
		if (r.chunk < 0) { return; }
		if (r.health >= r.healthFull && r.ore >= r.oreFull && r.oreLoose <= 0.f) { return; }
		Kept &k = kept[{r.field, r.origin.x, r.origin.y}];
		k.health = r.health;
		k.ore = r.ore;
		k.oreLoose = r.oreLoose;
	}

	void loadChunk(int field, int x, int y)
	{
		growChunk(fieldsCopy[(size_t)field], areas[(size_t)field], field, fieldKeepOut[(size_t)field],
			x, y, chunkSize, rocks, [](Rock &r)
		{
			const auto found = kept.find({r.field, r.origin.x, r.origin.y});
			if (found == kept.end()) { return true; }
			if (found->second.broken) { return false; }
			r.health = found->second.health;
			r.ore = found->second.ore;
			r.oreLoose = found->second.oreLoose;
			if (r.health < r.healthFull) { ensureCracks(r); } // its damage shows again
			return true;
		});
		loadedChunks[chunkKey(field, x, y)] = field;
		rockGridDirty = true;
	}
}

void stream(glm::vec4 view)
{
	if (fieldsCopy.empty()) { return; }
	const auto streamStart = std::chrono::steady_clock::now();
	if (streamViewScale < 1.f)
	{
		// Debug: a smaller rectangle round the view's middle, so its edges,
		// where chunks come and go, are on screen.
		const glm::vec2 middle = {view.x + view.z * 0.5f, view.y + view.w * 0.5f};
		const glm::vec2 half = glm::vec2(view.z, view.w) * (0.5f * std::max(streamViewScale, 0.05f));
		view = {middle - half, half * 2.f};
	}
	streamedView = view;
	madeLastFrame = droppedLastFrame = 0;
	const float size = std::max(chunkSize, 500.f);
	const float margin = std::max(chunkMargin, 0.f) * size;
	const glm::vec2 wantLo = glm::vec2(view.x, view.y) - glm::vec2(margin);
	const glm::vec2 wantHi = glm::vec2(view.x + view.z, view.y + view.w) + glm::vec2(margin);
	// Dropped one chunk further out than they are made, so a view moving
	// back and forth over a chunk's edge does not make and drop it each time.
	const glm::vec2 keepLo = wantLo - glm::vec2(size), keepHi = wantHi + glm::vec2(size);
	madeLo = wantLo; madeHi = wantHi; keptLo = keepLo; keptHi = keepHi;

	std::unordered_set<long long> drop;
	for (const auto &[key, field] : loadedChunks)
	{
		const glm::vec2 lo = glm::vec2(chunkCell(key)) * size;
		if (lo.x + size <= keepLo.x || lo.y + size <= keepLo.y || lo.x >= keepHi.x || lo.y >= keepHi.y) { drop.insert(key); }
	}
	if (!drop.empty())
	{
		for (const Rock &r : rocks) { if (r.chunk >= 0 && drop.count(r.chunk)) { remember(r); } }
		rocks.erase(std::remove_if(rocks.begin(), rocks.end(),
			[&](const Rock &r) { return r.chunk >= 0 && drop.count(r.chunk) > 0; }), rocks.end());
		for (long long key : drop) { loadedChunks.erase(key); chunkEvents.push_back({key, false, debugNow()}); }
		droppedLastFrame = (int)drop.size();
		rockGridDirty = true;
	}

	// What is wanted and missing, nearest the view's middle first.
	struct Todo { int field, x, y; float distance; };
	static std::vector<Todo> todo;
	todo.clear();
	const glm::vec2 middle = {view.x + view.z * 0.5f, view.y + view.w * 0.5f};
	for (int f = 0; f < (int)fieldsCopy.size(); f++)
	{
		if (!fieldHasPaint[(size_t)f]) { continue; }
		const glm::vec4 box = fieldBoxes[(size_t)f];
		const glm::vec2 lo = glm::max(wantLo, glm::vec2(box.x, box.y));
		const glm::vec2 hi = glm::min(wantHi, glm::vec2(box.z, box.w));
		if (lo.x >= hi.x || lo.y >= hi.y) { continue; }
		for (int y = (int)std::floor(lo.y / size); y <= (int)std::floor(hi.y / size); y++)
		{
			for (int x = (int)std::floor(lo.x / size); x <= (int)std::floor(hi.x / size); x++)
			{
				if (loadedChunks.count(chunkKey(f, x, y))) { continue; }
				const glm::vec2 centre = (glm::vec2((float)x, (float)y) + 0.5f) * size;
				todo.push_back({f, x, y, glm::distance(centre, middle)});
			}
		}
	}
	std::sort(todo.begin(), todo.end(), [](const Todo &a, const Todo &b) { return a.distance < b.distance; });

	// The first frame of a round makes everything wanted, so nothing pops in;
	// after that, a few milliseconds' worth a frame.
	const auto start = std::chrono::steady_clock::now();
	waiting.clear();
	size_t next = 0;
	for (; next < todo.size(); next++)
	{
		const Todo &t = todo[next];
		loadChunk(t.field, t.x, t.y);
		madeLastFrame++;
		chunkEvents.push_back({chunkKey(t.field, t.x, t.y), true, debugNow()});
		const double spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		if (streamedThisRound && spent >= chunkBudgetMs) { next++; break; }
	}
	for (; next < todo.size(); next++) { waiting.push_back(chunkKey(todo[next].field, todo[next].x, todo[next].y)); }
	streamedThisRound = true;

	const double now = debugNow();
	chunkEvents.erase(std::remove_if(chunkEvents.begin(), chunkEvents.end(),
		[&](const ChunkEvent &e) { return now - e.at > chunkFlashSeconds; }), chunkEvents.end());
	streamMsLastFrame = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - streamStart).count();
}

void drawChunkDebug(wgpu2d::Renderer2D &renderer)
{
	if (!showChunkGrid && !showChunkState && !showStreamRects && !showKept) { return; }
	const float size = std::max(chunkSize, 500.f);
	const float px = 1.f / std::max(renderer.currentCamera.zoom, 0.001f);
	const glm::vec4 view = renderer.getViewRect();
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	auto fill = [&](long long key, glm::vec4 colour)
	{
		const glm::vec2 lo = glm::vec2(chunkCell(key)) * size;
		renderer.renderRectangle({lo.x, lo.y, size, size}, colour);
	};
	auto outline = [&](glm::vec2 lo, glm::vec2 hi, glm::vec4 colour, float width)
	{
		renderer.renderLine(lo, {hi.x, lo.y}, colour, width * px);
		renderer.renderLine({hi.x, lo.y}, hi, colour, width * px);
		renderer.renderLine(hi, {lo.x, hi.y}, colour, width * px);
		renderer.renderLine({lo.x, hi.y}, lo, colour, width * px);
	};

	if (showChunkState)
	{
		// Made: a faint green. Waiting on the budget: amber. Just made: bright
		// green fading; just dropped: red fading.
		for (const auto &[key, field] : loadedChunks)
		{
			fill(key, {0.2f, 1.f, 0.4f, 0.12f});
			const glm::vec2 lo = glm::vec2(chunkCell(key)) * size;
			const float inset = 4.f * px; // inside the grid line, so both show
			outline(lo + glm::vec2(inset), lo + glm::vec2(size - inset), {0.3f, 1.f, 0.45f, 0.6f}, 1.5f);
		}
		for (long long key : waiting) { fill(key, {1.f, 0.7f, 0.15f, 0.25f}); }
		const double now = debugNow();
		for (const ChunkEvent &e : chunkEvents)
		{
			const float left = 1.f - (float)((now - e.at) / std::max((double)chunkFlashSeconds, 0.01));
			if (left <= 0.f) { continue; }
			fill(e.key, e.made ? glm::vec4(0.3f, 1.f, 0.5f, 0.45f * left) : glm::vec4(1.f, 0.2f, 0.2f, 0.45f * left));
		}
	}

	if (showChunkGrid)
	{
		const glm::vec4 line = {1.f, 1.f, 1.f, 0.35f};
		for (float x = std::floor(view.x / size) * size; x <= view.x + view.z; x += size)
		{
			renderer.renderLine({x, view.y}, {x, view.y + view.w}, line, 1.5f * px);
		}
		for (float y = std::floor(view.y / size) * size; y <= view.y + view.w; y += size)
		{
			renderer.renderLine({view.x, y}, {view.x + view.z, y}, line, 1.5f * px);
		}
	}

	if (showStreamRects)
	{
		// Cyan: the view chunks are made for. Green: made within this.
		// Red: dropped once outside this.
		outline({streamedView.x, streamedView.y}, {streamedView.x + streamedView.z, streamedView.y + streamedView.w},
			{0.3f, 0.9f, 1.f, 0.9f}, 3.f);
		outline(madeLo, madeHi, {0.3f, 1.f, 0.4f, 0.9f}, 2.f);
		outline(keptLo, keptHi, {1.f, 0.3f, 0.3f, 0.9f}, 2.f);
	}

	if (showKept)
	{
		// Remembered rocks where they were made: a red cross broken, an amber
		// ring damaged or mined.
		const float mark = 10.f * px;
		for (const auto &[id, k] : kept)
		{
			const glm::vec2 at = {id.x, id.y};
			if (at.x < view.x - mark || at.y < view.y - mark || at.x > view.x + view.z + mark || at.y > view.y + view.w + mark) { continue; }
			if (k.broken)
			{
				renderer.renderLine(at - glm::vec2(mark), at + glm::vec2(mark), {1.f, 0.25f, 0.2f, 1.f}, 2.f * px);
				renderer.renderLine(at + glm::vec2(-mark, mark), at + glm::vec2(mark, -mark), {1.f, 0.25f, 0.2f, 1.f}, 2.f * px);
			}
			else { renderer.renderCircleOutline(at, {1.f, 0.75f, 0.2f, 1.f}, mark, 2.f * px, 12); }
		}
	}
}

float hiddenShade() { return shadeInField; }


bool inField(glm::vec2 point)
{
	return region::labelAt(mask, point) >= 0;
}

const region::Mask &paintMask() { return mask; }

namespace
{
	// Whether `r` takes part in a query asking for `which` (sight roadmap
	// S1). A field rock knocked out of the paint is solid until its spring
	// brings it home: out there it is a rock on its own, not part of a field.
	bool counts(const Rock &r, Which which)
	{
		if (which == Which::All) { return true; }
		if (which == Which::Cores) { return r.core; }
		const bool fieldRock = r.field >= 0 && !r.core && !r.outOfField;
		return which == Which::InPaint ? fieldRock : !fieldRock;
	}
}

// The lowest-numbered rock the circle touches -- the one the loop over
// every rock used to find first -- so a shot touching two rocks strikes
// the same one it always did. That means testing every candidate rather
// than stopping at the first, which is a few more tests and no surprises.
int hitCircle(glm::vec2 centre, float radius, Which which)
{
	int found = -1;
	spatial::query(rockIndex(), centre - glm::vec2(radius), centre + glm::vec2(radius), [&](int i)
	{
		if (found >= 0 && i > found) { return true; }
		const Rock &r = rocks[i];
		// Broad phase: two circles that do not touch rule out the triangles.
		if (glm::distance(centre, r.placement.position) > r.bound + radius) { return true; }
		if (!counts(r, which)) { return true; }
		if (polygon::overlapsCircle(r.outline, polygon::toLocal(r.placement, centre), radius)) { found = i; }
		return true;
	});
	return found;
}

// The buckets along the ray, near to far, stopping at the first bucket that
// begins past the nearest hit so far: anything nearer would contain that
// nearer point, and be listed in a bucket entered before it. On an exact tie
// the later rock wins, as it did in the loop over every rock.
float raycast(glm::vec2 origin, glm::vec2 direction, float maxDistance, int *rock, Which which, int ignore)
{
	float nearest = -1.f;
	int nearestRock = -1;
	spatial::raycast(rockIndex(), origin, direction, maxDistance, [&](int i, float cellEntry)
	{
		if (nearest >= 0.f && cellEntry > nearest) { return false; }
		if (i == ignore) { return true; }
		const Rock &r = rocks[i];
		// Broad phase: how close the ray's line passes the rock's centre.
		const glm::vec2 toCentre = r.placement.position - origin;
		const float along = glm::dot(toCentre, direction);
		const float miss = glm::length(toCentre - direction * along);
		if (miss > r.bound || along < -r.bound || along > maxDistance + r.bound) { return true; }
		if (!counts(r, which)) { return true; }

		const float reach = nearest >= 0.f ? nearest : maxDistance;
		const float t = polygon::raycast(r.outline, polygon::toLocal(r.placement, origin),
			polygon::directionToLocal(r.placement, direction), reach);
		if (t >= 0.f && (nearest < 0.f || t < nearest || i > nearestRock))
		{
			nearest = t;
			nearestRock = i;
		}
		return true;
	});
	if (rock) { *rock = nearestRock; }
	return nearest;
}

void outlinesNear(glm::vec2 centre, float radius, Which which,
	const std::function<void(int rock, const std::vector<glm::vec2> &outline, glm::vec2 boundCentre, float bound)> &visit)
{
	static std::vector<glm::vec2> world;
	spatial::query(rockIndex(), centre - glm::vec2(radius), centre + glm::vec2(radius), [&](int i)
	{
		const Rock &r = rocks[i];
		if (glm::distance(centre, r.placement.position) > r.bound + radius) { return true; }
		if (!counts(r, which)) { return true; }
		world.resize(r.outline.size());
		for (size_t k = 0; k < r.outline.size(); k++) { world[k] = polygon::toWorld(r.placement, r.outline[k]); }
		visit(i, world, r.placement.position, r.bound);
		return true;
	});
}

void outlinesIn(glm::vec2 boxMin, glm::vec2 boxMax, Which which,
	const std::function<void(int rock, const std::vector<glm::vec2> &outline, glm::vec2 boundCentre, float bound)> &visit)
{
	static std::vector<glm::vec2> world;
	spatial::query(rockIndex(), boxMin, boxMax, [&](int i)
	{
		const Rock &r = rocks[i];
		if (!counts(r, which)) { return true; }
		world.resize(r.outline.size());
		for (size_t k = 0; k < r.outline.size(); k++) { world[k] = polygon::toWorld(r.placement, r.outline[k]); }
		visit(i, world, r.placement.position, r.bound);
		return true;
	});
}

bool blocksSight(glm::vec2 from, glm::vec2 to)
{
	const glm::vec2 line = to - from;
	const float length = glm::length(line);
	if (length <= 0.f) { return hitCircle(from, 0.f) >= 0; }
	return raycast(from, line / length, length) >= 0.f;
}

void shot(int rock, glm::vec2 point, glm::vec2 direction, float damage, bool missile)
{
	if (rock < 0 || rock >= (int)rocks.size() || rocks[rock].core) { return; }
	Rock &r = rocks[rock];
	const float push = damage * shotPush * (missile ? missilePush : 1.f);
	rigid::applyImpulse(r.body, point, direction * push);
	clampMotion(r);
	wake(r);
	// And it hurts it (A4): anything breaks a rock; only the beam mines one.
	if (hurt(r, damage)) { processBreaks(); }
}

void beam(int rock, glm::vec2 point, glm::vec2 direction, float damagePerSecond, float dt)
{
	if (rock < 0 || rock >= (int)rocks.size() || dt <= 0.f || rocks[rock].core) { return; }
	Rock &r = rocks[rock];
	// The push, then held to a creep -- only what the beam added: a rock
	// already flying from a shot is not slowed by it.
	const float alongBefore = glm::dot(r.body.velocity, direction);
	const float spinBefore = std::abs(r.body.spin);
	rigid::applyForce(r.body, point, direction * beamPush, dt);
	const float along = glm::dot(r.body.velocity, direction);
	const float allowed = std::max(beamCreep, alongBefore);
	if (along > allowed) { r.body.velocity -= direction * (along - allowed); }
	const float spinAllowed = std::max(beamRoll, spinBefore);
	if (std::abs(r.body.spin) > spinAllowed) { r.body.spin = std::copysign(spinAllowed, r.body.spin); }
	clampMotion(r);
	wake(r);

	// Mining (A4): the beam wears the rock and it sheds ore as orbs, thrown
	// back toward the beam. While a hit has the drill interrupted it only
	// pushes. The glow is the sign of shedding -- it heats only while ore
	// comes off, so a rock that glows is one that pays.
	if (resources::interrupted()) { return; }
	const float damage = damagePerSecond * beamRockScale * dt;
	const float shed = std::min(r.ore, r.healthFull > 0.f ? r.oreFull * damage / r.healthFull : 0.f);
	r.ore -= shed;
	r.oreLoose += shed;
	while (r.oreLoose >= orbValue)
	{
		r.oreLoose -= orbValue;
		throwOrb(r, point, direction, 0.7f, orbValue);
	}

	// And heats where it burns (A3), while ore is coming off it -- a rock
	// the beam already emptied does not glow. The spot follows the beam
	// gently, so sweeping it drags the glow along rather than jumping it.
	const glm::vec2 local = polygon::toLocal(r.placement, point);
	r.heatAt = r.heat > 0.f ? glm::mix(r.heatAt, local, std::min(1.f, dt * 10.f)) : local;
	if (shed > 0.f || r.oreLoose > 0.f) { r.heat = std::min(1.f, r.heat + heatRise * dt); }
	r.sinceBeamed = 0.f;
	// Last: breaking removes the rock, and `r` with it.
	if (hurt(r, damage))
	{
		r.beamKill = true;
		r.beamDirection = direction;
		r.beamPoint = point;
		processBreaks();
	}
}

void blast(glm::vec2 at, float strength)
{
	// Each rock on its own, so the order the buckets hand them out in does
	// not matter. Any rock whose near side is within reach is listed in a
	// bucket the reach's square covers.
	spatial::query(rockIndex(), at - glm::vec2(blastReach), at + glm::vec2(blastReach), [&](int i)
	{
		Rock &r = rocks[i];
		if (r.core) { return true; }
		const glm::vec2 away = r.body.position - at;
		const float distance = glm::length(away);
		// Measured to the rock's near side, so a big rock beside a blast is
		// not spared because its centre is far off.
		const float edge = std::max(distance - r.collideRadius, 0.f);
		if (edge >= blastReach || distance < 1e-3f) { return true; }
		// Through the centre: a blast shoves, it does not spin.
		const float falloff = 1.f - edge / blastReach;
		rigid::applyImpulse(r.body, r.body.position, away / distance * (blastPush * strength * falloff));
		clampMotion(r);
		wake(r);
		hurt(r, blastDamage * strength * falloff); // (A4) broken below, after the loop
		return true;
	});
	processBreaks();
}

void ram(glm::vec2 centre, float radius, glm::vec2 direction, unsigned ramSerial)
{
	spatial::query(rockIndex(), centre - glm::vec2(radius), centre + glm::vec2(radius), [&](int i)
	{
		Rock &r = rocks[i];
		if (r.core || r.ramHitOn == ramSerial) { return true; } // a core is the ship's problem, not the core's
		if (glm::distance(centre, r.placement.position) > r.bound + radius) { return true; }
		const glm::vec2 local = polygon::toLocal(r.placement, centre);
		if (!polygon::overlapsCircle(r.outline, local, radius)) { return true; }
		// Outward from the prow, plus some of the ram's own way, applied on the
		// rock's near side: the part along the ram, off the line through the
		// rock's centre, is what spins it.
		const glm::vec2 toRock = r.body.position - centre;
		const float d = glm::length(toRock);
		const glm::vec2 outward = d > 1e-3f ? toRock / d : direction;
		const glm::vec2 push = glm::normalize(outward + direction * ramForward);
		const glm::vec2 point = r.body.position - outward * std::min(r.collideRadius, d);
		rigid::applyImpulse(r.body, point, push * ramPush);
		clampMotion(r);
		wake(r);
		r.ramHitOn = ramSerial;
		hurt(r, ramDamage); // (A4) broken below, after the loop
		return true;
	});
	processBreaks();
}

void update(float dt)
{
	if (dt <= 0.f) { return; }
	updateShards(dt);

	// Heat cools on every rock, moving or not (A3) -- but only once the beam
	// has left it. Cooling all the time, faster than the beam heats (as the
	// glow's quick fade needs), kept a burning rock from ever warming.
	for (Rock &r : rocks)
	{
		r.sinceBeamed += dt;
		if (r.heat > 0.f && r.sinceBeamed > 0.1f) { r.heat = std::max(0.f, r.heat - heatCool * dt); }
	}

	// Motion: the awake ones only. A field rock also springs toward home --
	// an acceleration, so big and small settle in the same time.
	const float omega = 6.2831853f / std::max(springPeriod, 0.1f);
	awakeCount = 0;
	bool anyAwake = false;
	for (Rock &r : rocks)
	{
		if (!r.awake || r.core) { continue; }
		r.sinceStruck += dt;
		if (r.inField)
		{
			// Loose for a while after a hit, then the spring eases in.
			const float hold = std::clamp((r.sinceStruck - springDelay) / std::max(springEase, 0.01f), 0.f, 1.f);
			const glm::vec2 offset = r.body.position - r.home;
			r.body.velocity += hold * (-omega * omega * offset - 2.f * springDamping * omega * r.body.velocity) * dt;
			// On the way home, no faster than a drift.
			const float speed = glm::length(r.body.velocity);
			const float cap = glm::mix(maxSpeed, returnSpeed, hold);
			if (hold > 0.f && speed > cap) { r.body.velocity *= cap / speed; }
		}
		rigid::integrate(r.body, dt, linearDamping, angularDamping);
		clampMotion(r);
		anyAwake = true;
	}
	if (!anyAwake) { return; }

	// Rock against rock, as circles of the same area: only pairs where one
	// is moving. The buckets are rebuilt first if any has moved past the
	// slack; then each moving rock takes the rocks in the buckets round it,
	// in index order -- each pair is met once a frame, the lower index first.
	checkListed();
	const spatial::Grid &index = rockIndex();
	static std::vector<int> nearby;
	for (int i = 0; i < (int)rocks.size(); i++)
	{
		Rock &a = rocks[i];
		if (!a.awake) { continue; }
		nearby.clear();
		spatial::query(index, a.body.position - glm::vec2(a.collideRadius), a.body.position + glm::vec2(a.collideRadius),
			[&](int j) { nearby.push_back(j); return true; });
		std::sort(nearby.begin(), nearby.end());
		for (int j : nearby)
		{
			// Each pair once: two awake rocks by the lower index only.
			if (j == i || (rocks[j].awake && j < i)) { continue; }
			Rock &b = rocks[j];
			// A field rock on its way home only meets rocks still loose.
			// Its home lies through a crowd; colliding there, the return
			// became a jam -- rocks pushed by their springs into the
			// settled ones in their way, still 500 out after 14 s.
			// A core, though, is always solid.
			if (!a.core && !b.core
				&& ((returning(a) && !loose(b)) || (returning(b) && !loose(a)))) { continue; }
			if (rigid::collideCircles(a.body, a.collideRadius, b.body, b.collideRadius,
				restitution, friction, restingSpeed))
			{
				if (a.core || b.core) { continue; } // a core neither wakes nor loosens
				// Looseness spreads with the original clock: a rock knocked
				// by a loose one is loose until that one's spring returns,
				// so a whole ram's chain comes home together. A rock
				// already on its way home loosens nothing it passes --
				// when every bump restarted the clock, returning rocks
				// knocked the settled ones loose and the field churned on.
				const float clock = std::min(a.sinceStruck, b.sinceStruck);
				a.awake = b.awake = true;
				if (clock < springDelay) { a.sinceStruck = b.sinceStruck = clock; }
			}
		}
	}

	// Place the frames the draw and the hit tests use, and let the settled
	// ones sleep: still, and a field rock home again.
	for (Rock &r : rocks)
	{
		if (!r.awake) { continue; }
		place(r);
		if (r.field >= 0 && !r.core && r.field < (int)fieldsCopy.size())
		{
			r.outOfField = r.field < (int)areas.size() && !areaContains(areas[(size_t)r.field], r.placement.position);
		}
		awakeCount++;
		const bool still = glm::length(r.body.velocity) < 2.f && std::abs(r.body.spin) < 0.02f;
		const bool home = !r.inField || glm::distance(r.body.position, r.home) < 2.f;
		if (still && home) { r.awake = false; }
	}
	checkListed(); // where the bumps left them
}

const CoreRules &coreRules() { return rules; }

bool isCore(int rock, glm::vec2 *centre)
{
	if (rock < 0 || rock >= (int)rocks.size() || !rocks[rock].core) { return false; }
	if (centre) { *centre = rocks[rock].body.position; }
	return true;
}

std::vector<glm::vec2> fieldCores(const level::AsteroidField &f)
{
	glm::vec2 lo, hi;
	if (!fieldBounds(f, lo, hi)) { return {}; }
	return coresOf(f);
}

float coreRadius(const level::AsteroidField &f, int core)
{
	glm::vec2 lo, hi;
	if (!fieldBounds(f, lo, hi)) { return 0.f; }
	const std::vector<glm::vec2> cores = coresOf(f);
	if (core < 0 || core >= (int)cores.size()) { return 0.f; }
	return growCore(f, cores[(size_t)core], core).bound;
}

namespace
{
	// The lowest-numbered core the circle touches, or -1: the one the loop
	// over every rock found first.
	int touchedCore(glm::vec2 centre, float radius)
	{
		int found = -1;
		spatial::query(rockIndex(), centre - glm::vec2(radius), centre + glm::vec2(radius), [&](int i)
		{
			if (found >= 0 && i > found) { return true; }
			const Rock &r = rocks[i];
			if (!r.core) { return true; }
			if (glm::distance(centre, r.placement.position) > r.bound + radius) { return true; }
			if (polygon::overlapsCircle(r.outline, polygon::toLocal(r.placement, centre), radius)) { found = i; }
			return true;
		});
		return found;
	}
}

bool coreContact(glm::vec2 centre, float radius, CoreContact &out)
{
	const int core = touchedCore(centre, radius);
	if (core < 0) { return false; }

	const Rock &r = rocks[core];
	const glm::vec2 fromCore = centre - r.body.position;
	const float d = glm::length(fromCore);
	out.outward = d > 1e-3f ? fromCore / d : glm::vec2(1.f, 0.f);
	// The core's surface along that line: a ray from outside, back in.
	const glm::vec2 start = r.body.position + out.outward * (r.bound + radius + 10.f);
	const float t = polygon::raycast(r.outline, polygon::toLocal(r.placement, start),
		polygon::directionToLocal(r.placement, -out.outward), 2.f * (r.bound + radius + 10.f));
	const glm::vec2 surface = t >= 0.f ? start - out.outward * t : r.body.position + out.outward * r.bound;
	out.pushTo = surface + out.outward * radius;
	return true;
}

namespace
{
	// One layer of rocks through the asteroid shader, then any outlines.
	void drawLayer(wgpu2d::Renderer2D &renderer, bool fieldLayer)
	{
		// Off screen, skip it: the batch would draw it anyway.
		// Cores first, in their own stone: under their fields' rocks, as they
		// were when every rock was one draw.
		// The buckets the view covers give the candidates; sorted, they draw
		// in the order the rocks are stored, as they did when every rock was
		// looked at, so overlapping rocks keep which one is on top.
		// Debug: culled to the streamed rectangle, so what culling leaves out
		// shows.
		glm::vec4 view = renderer.getViewRect();
		if (cullDrawToStream && streamedView.z > 0.f) { view = streamedView; }
		static std::vector<int> visible;
		visible.clear();
		spatial::query(rockIndex(), {view.x, view.y}, {view.x + view.z, view.y + view.w}, [&](int i)
		{
			if (rocks[i].inField == fieldLayer && onScreen(view, rocks[i])) { visible.push_back(i); }
			return true;
		});
		std::sort(visible.begin(), visible.end());
		for (const bool cores : {true, false})
		{
			beginRocks(renderer, cores ? coreStone : stone);
			for (const int i : visible)
			{
				if (rocks[i].core == cores) { drawRock(renderer, rocks[i]); }
			}
			endRocks(renderer);
		}

		// The cracks over them (A4): dark where damage has opened them, then
		// glowing where the beam is shedding ore.
		const float px = 1.f / std::max(renderer.currentCamera.zoom, 0.001f);
		renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
		for (const int i : visible) { drawCracks(renderer, rocks[i], px, false); }
		renderer.setBlendMode(wgpu2d::BlendMode::Additive);
		for (const int i : visible)
		{
			if (rocks[i].heat > 0.f) { drawCracks(renderer, rocks[i], px, true); }
		}
		renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
		if (showOutlines)
		{
			for (const int i : visible) { drawOutline(renderer, rocks[i]); }
		}
	}
}

void draw(wgpu2d::Renderer2D &renderer)
{
	drawShards(renderer); // behind every rock, field or not (A4)
	drawLayer(renderer, false);
}

void drawFields(wgpu2d::Renderer2D &renderer) { drawLayer(renderer, true); }

float shadowOn(glm::vec2 centre, float radius)
{
	// Nine points over the hull -- its centre, and eight round it at two
	// thirds of its radius -- each in shadow or not: the share in shadow
	// darkens it, so a ship slides into a shadow rather than blinking into it.
	static const glm::vec2 spots[9] = {
		{0.f, 0.f}, {1.f, 0.f}, {-1.f, 0.f}, {0.f, 1.f}, {0.f, -1.f},
		{0.7071f, 0.7071f}, {-0.7071f, 0.7071f}, {0.7071f, -0.7071f}, {-0.7071f, -0.7071f}};
	const glm::vec2 away = lightFlat() * shadowDistance;
	// A rock's shadow is the rock moved by `away`, so the rocks that can
	// shade the hull are the ones listed near the hull moved back by it.
	const glm::vec2 near = centre + away;
	float darkest = 0.f;
	spatial::query(rockIndex(), near - glm::vec2(radius), near + glm::vec2(radius), [&](int i)
	{
		const Rock &r = rocks[i];
		if (!r.inField) { return true; } // single rocks lie under the ships
		polygon::Placement cast = r.placement;
		cast.position -= away;
		if (glm::distance(centre, cast.position) > r.bound + radius) { return true; }
		int covered = 0;
		for (const glm::vec2 &s : spots)
		{
			if (polygon::contains(r.outline, polygon::toLocal(cast, centre + s * (radius * 0.66f)))) { covered++; }
		}
		darkest = std::max(darkest, covered / 9.f);
		return darkest < 1.f;
	});
	return darkest * shadowAlpha;
}

void drawForeground(wgpu2d::Renderer2D &renderer)
{
	if (debris.empty() || debrisShade <= 0.f) { return; }
	const glm::vec4 view = renderer.getViewRect();
	const glm::vec2 camera = glm::vec2(view.x, view.y) + glm::vec2(view.z, view.w) * 0.5f;
	beginRocks(renderer);
	for (const Debris &d : debris)
	{
		// Nearer the eye than the play: it moves further than the world does
		// as the camera moves, and is drawn larger by the same factor.
		const glm::vec2 at = d.position + (d.position - camera) * debrisParallax;
		const float reach = d.bound * (1.f + debrisParallax);
		if (at.x + reach < view.x || at.x - reach > view.x + view.z
			|| at.y + reach < view.y || at.y - reach > view.y + view.w) { continue; }
		drawFan(renderer, d.outline, {at, d.angle}, d.uvOffset, debrisShade / std::max(brightness, 0.01f),
			0.f, {}, 1.f + debrisParallax);
	}
	endRocks(renderer);
}

namespace
{
	bool same(const level::Asteroid &a, const level::Asteroid &b)
	{
		return a.position == b.position && a.radius == b.radius && a.seed == b.seed;
	}

	bool same(const level::AsteroidField &a, const level::AsteroidField &b)
	{
		if (a.seed != b.seed || a.maxSize != b.maxSize || a.maxGap != b.maxGap
			|| a.cores != b.cores || a.stamps.size() != b.stamps.size()) { return false; }
		for (size_t i = 0; i < a.stamps.size(); i++)
		{
			const level::FieldStamp &s = a.stamps[i], &t = b.stamps[i];
			if (s.position != t.position || s.radius != t.radius || s.erase != t.erase) { return false; }
		}
		return true;
	}

	template <class T>
	bool same(const std::vector<T> &a, const std::vector<T> &b)
	{
		if (a.size() != b.size()) { return false; }
		for (size_t i = 0; i < a.size(); i++) { if (!same(a[i], b[i])) { return false; } }
		return true;
	}

	// The editor's rocks: single rocks until they change, and each field's
	// cores and chunks until that field changes. Only the chunks in view are
	// made -- a level of the size W3 allows is more rocks than fit -- a few
	// milliseconds' worth a frame, and none when the view takes in more than
	// `previewChunkLimit` chunks: zoomed that far out, the dots show the paint.
	std::vector<level::Asteroid> previewPlaced;
	std::vector<Rock> previewSingles;
	struct PreviewField
	{
		bool valid = false;
		level::AsteroidField field;
		FieldArea area;
		std::vector<Rock> cores;
		std::vector<scatter::Params::KeepOut> keepOut;
		glm::vec2 lo = {}, hi = {};
		bool painted = false;
		std::unordered_map<long long, std::vector<Rock>> chunks;
	};
	std::vector<PreviewField> preview;
	int previewChunkLimit = 120;
	float previewBudgetMs = 12.f;
	float previewChunkSize = 0.f;   // the chunk size the cache was made at
}

void drawPlacements(wgpu2d::Renderer2D &renderer, const std::vector<level::Asteroid> &placed,
	const std::vector<level::AsteroidField> &fields)
{
	if (!same(placed, previewPlaced))
	{
		previewSingles.clear();
		for (const level::Asteroid &a : placed) { previewSingles.push_back(grow(a)); }
		previewPlaced = placed;
	}
	const float size = std::max(chunkSize, 500.f);
	if (previewChunkSize != size) { preview.clear(); previewChunkSize = size; }
	preview.resize(fields.size());
	for (int i = 0; i < (int)fields.size(); i++)
	{
		PreviewField &p = preview[(size_t)i];
		if (p.valid && same(fields[(size_t)i], p.field)) { continue; }
		p.valid = true;
		p.field = fields[(size_t)i];
		p.area = areaOf(p.field);
		p.cores.clear();
		growCores(p.field, i, p.cores, p.keepOut);
		p.painted = fieldBounds(p.field, p.lo, p.hi);
		p.chunks.clear();
	}

	for (const PreviewField &p : preview) { drawArea(renderer, p.field, p.area); }

	// The chunks in view, made as the budget allows.
	const glm::vec4 view = renderer.getViewRect();
	const glm::ivec2 c0 = {(int)std::floor(view.x / size), (int)std::floor(view.y / size)};
	const glm::ivec2 c1 = {(int)std::floor((view.x + view.z) / size), (int)std::floor((view.y + view.w) / size)};
	const bool rocksShown = (c1.x - c0.x + 1) * (c1.y - c0.y + 1) <= previewChunkLimit;
	auto inField = [&](const PreviewField &p, int x, int y)
	{
		return p.painted && (x + 1) * size > p.lo.x && (y + 1) * size > p.lo.y && x * size <= p.hi.x && y * size <= p.hi.y;
	};
	if (rocksShown)
	{
		const auto start = std::chrono::steady_clock::now();
		bool spent = false;
		for (int i = 0; i < (int)preview.size() && !spent; i++)
		{
			PreviewField &p = preview[(size_t)i];
			for (int y = c0.y; y <= c1.y && !spent; y++)
			{
				for (int x = c0.x; x <= c1.x && !spent; x++)
				{
					const long long key = chunkKey(i, x, y);
					if (!inField(p, x, y) || p.chunks.count(key)) { continue; }
					growChunk(p.field, p.area, i, p.keepOut, x, y, size, p.chunks[key], [](Rock &) { return true; });
					spent = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= previewBudgetMs;
				}
			}
		}
	}

	auto forEachShown = [&](auto &&visit)
	{
		for (const Rock &r : previewSingles) { visit(r); }
		for (int i = 0; i < (int)preview.size(); i++)
		{
			const PreviewField &p = preview[(size_t)i];
			for (const Rock &r : p.cores) { visit(r); }
			if (!rocksShown) { continue; }
			for (int y = c0.y; y <= c1.y; y++)
			{
				for (int x = c0.x; x <= c1.x; x++)
				{
					const auto found = p.chunks.find(chunkKey(i, x, y));
					if (found == p.chunks.end()) { continue; }
					for (const Rock &r : found->second) { visit(r); }
				}
			}
		}
	};
	for (const bool cores : {true, false})
	{
		beginRocks(renderer, cores ? coreStone : stone);
		forEachShown([&](const Rock &r) { if (r.core == cores && onScreen(view, r)) { drawRock(renderer, r); } });
		endRocks(renderer);
	}
	// A field's rocks are too many to outline.
	forEachShown([&](const Rock &r) { if (!r.inField && onScreen(view, r)) { drawOutline(renderer, r); } });
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("asteroids", {
	{"shotPush", shotPush},
	{"missilePush", missilePush},
	{"beamPush", beamPush},
	{"beamCreep", beamCreep},
	{"beamRoll", beamRoll},
	{"blastPush", blastPush},
	{"blastReach", blastReach},
	{"ramPush", ramPush},
	{"ramForward", ramForward},
	{"friction", friction},
	{"restingSpeed", restingSpeed},
	{"springDelay", springDelay},
	{"springEase", springEase},
	{"returnSpeed", returnSpeed},
	{"linearDamping", linearDamping},
	{"angularDamping", angularDamping},
	{"maxSpeed", maxSpeed},
	{"maxSpin", maxSpin},
	{"restitution", restitution},
	{"springPeriod", springPeriod},
	{"springDamping", springDamping},
	{"density", density},
	{"lightAzimuth", lightAzimuth},
	{"lightElevation", lightElevation},
	{"lightStrength", lightStrength},
	{"coarsen", coarsen},
	{"stone.tint", stone.tint},
	{"stone.ambient", stone.ambient},
	{"stone.macro", stone.macro},
	{"stone.round", stone.round},
	{"stone.selfShadow", stone.selfShadow},
	{"stone.shine", stone.shine},
	{"coreStone.tint", coreStone.tint},
	{"coreStone.ambient", coreStone.ambient},
	{"coreStone.macro", coreStone.macro},
	{"coreStone.round", coreStone.round},
	{"coreStone.selfShadow", coreStone.selfShadow},
	{"coreStone.shine", coreStone.shine},
	{"heatColour", heatColour},
	{"heatReach", heatReach},
	{"heatRise", heatRise},
	{"heatCool", heatCool},
	{"heatRadius", heatRadius},
	{"shadowDistance", shadowDistance},
	{"shadowAlpha", shadowAlpha},
	{"debrisParallax", debrisParallax},
	{"debrisShade", debrisShade},
	{"debrisFill", debrisFill},
	{"healthScale", healthScale},
	{"beamRockScale", beamRockScale},
	{"blastDamage", blastDamage},
	{"ramDamage", ramDamage},
	{"orePerArea", orePerArea},
	{"orbValue", orbValue},
	{"pieceArea", pieceArea},
	{"minPieceRadius", minPieceRadius},
	{"breakKick", breakKick},
	{"breakSpin", breakSpin},
	{"shardFlight", shardFlight},
	{"shardDrag", shardDrag},
	{"shardSink", shardSink},
	{"shardShade", shardShade},
	{"shardScale", shardScale},
	{"shardGather", shardGather},
	{"shardReturnSpeed", shardReturnSpeed},
	{"shardSpread", shardSpread},
	{"shardLimit", shardLimit},
	{"crackWidthPixels", crackWidthPixels},
	{"crackGlowWidthPixels", crackGlowWidthPixels},
	{"coreScale", coreScale},
	{"coreMinFit", coreMinFit},
	{"core.damage", rules.damage},
	{"core.bounce", rules.bounce},
	{"core.minOutSpeed", rules.minOutSpeed},
	{"core.grace", rules.grace},
	{"core.enemyKnock", rules.enemyKnock},
	{"core.enemyStun", rules.enemyStun},
	{"cornerSpacing", cornerSpacing},
	{"roughness", roughness},
	{"angleJitter", angleJitter},
	{"fieldMinFraction", fieldMinFraction},
	{"fieldLayers", fieldLayers},
	{"fieldGapVariation", fieldGapVariation},
	{"fieldSmallBias", fieldSmallBias},
	{"fieldFill", fieldFill},
	{"areaDotSpacingPixels", areaDotSpacingPixels},
	{"shadeInField", shadeInField},
	{"maskCell", maskCell},
	{"rockGridCell", rockGridCell},
	{"rockGridSlack", rockGridSlack},
	{"chunkSize", chunkSize},
	{"chunkMargin", chunkMargin},
	{"chunkBudgetMs", chunkBudgetMs},
	{"textureWorldSize", textureWorldSize},
	{"brightness", brightness},
	{"showOutlines", showOutlines},
});

void debugUi()
{
	ImGui::Text("%d rocks, %d moving", (int)rocks.size(), awakeCount);
	if (ImGui::TreeNode("Physics"))
	{
		tune::SliderFloat("Shot push", &shotPush, 0.f, 60000.f, "%.0f per damage");
		tune::SliderFloat("Missile push", &missilePush, 0.f, 20.f, "x%.1f");
		tune::SliderFloat("Beam push", &beamPush, 0.f, 40000.f, "%.0f");
		tune::SliderFloat("Beam creep", &beamCreep, 0.f, 500.f, "%.0f u/s at most");
		tune::SliderFloat("Beam roll", &beamRoll, 0.f, 3.f, "%.2f rad/s at most");
		tune::SliderFloat("Blast push", &blastPush, 0.f, 20000.f, "%.0f");
		tune::SliderFloat("Blast reach", &blastReach, 0.f, 6000.f, "%.0f");
		tune::SliderFloat("Ram push", &ramPush, 0.f, 100000.f, "%.0f");
		tune::SliderFloat("Ram forward", &ramForward, 0.f, 3.f, "%.2f (0: straight out from the prow)");
		tune::SliderFloat("Friction", &friction, 0.f, 1.f, "%.2f (rock on rock)");
		tune::SliderFloat("Resting speed", &restingSpeed, 0.f, 600.f, "%.0f u/s: slower touches don't bounce");
		tune::SliderFloat("Spring delay", &springDelay, 0.f, 10.f, "%.1f s loose after a hit");
		tune::SliderFloat("Spring ease", &springEase, 0.01f, 5.f, "%.1f s");
		tune::SliderFloat("Return speed", &returnSpeed, 50.f, 3000.f, "%.0f u/s home");
		tune::SliderFloat("Drift damping", &linearDamping, 0.f, 3.f, "%.2f /s");
		tune::SliderFloat("Spin damping", &angularDamping, 0.f, 3.f, "%.2f /s");
		tune::SliderFloat("Max speed", &maxSpeed, 50.f, 6000.f, "%.0f");
		tune::SliderFloat("Max spin", &maxSpin, 0.1f, 20.f, "%.1f rad/s");
		tune::SliderFloat("Bounce", &restitution, 0.f, 1.f, "%.2f");
		tune::SliderFloat("Field spring", &springPeriod, 0.5f, 20.f, "%.1f s");
		tune::SliderFloat("Spring damping", &springDamping, 0.1f, 2.f, "%.2f");
		ImGui::TextDisabled("Density applies when rocks are grown (next round)");
		tune::SliderFloat("Density", &density, 1e-5f, 1e-3f, "%.5f", ImGuiSliderFlags_Logarithmic);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Look"))
	{
		tune::SliderFloat("Light from", &lightAzimuth, 0.f, 360.f, "%.0f deg (0 right, 90 below)");
		tune::SliderFloat("Light height", &lightElevation, 5.f, 90.f, "%.0f deg");
		tune::SliderFloat("Light strength", &lightStrength, 0.f, 3.f, "%.2f");
		tune::SliderFloat("Coarsen", &coarsen, 0.f, 1.f, "%.2f");
		// A5: the two stones.
		auto surfaceUi = [](const char *name, Surface &s)
		{
			if (!ImGui::TreeNode(name)) { return; }
			tune::ColorEdit3("Tint", &s.tint.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
			tune::SliderFloat("Ambient", &s.ambient, 0.f, 1.f, "%.2f");
			tune::SliderFloat("Large features", &s.macro, 0.f, 1.f, "%.2f");
			tune::SliderFloat("Round edges", &s.round, 0.f, 6.f, "%.2f (0 flat)");
			tune::SliderFloat("Self-shadow", &s.selfShadow, 0.f, 1.f, "%.2f");
			tune::SliderFloat("Shine", &s.shine, 0.f, 2.f, "%.2f");
			ImGui::TreePop();
		};
		surfaceUi("Stone", stone);
		surfaceUi("Core stone", coreStone);
		tune::ColorEdit3("Heat colour", &heatColour.x);
		tune::SliderFloat("Heat reach", &heatReach, 0.f, 1.f, "%.2f (0 deepest cracks only)");
		tune::SliderFloat("Heat rise", &heatRise, 0.05f, 5.f, "%.2f /s under the beam");
		tune::SliderFloat("Heat cool", &heatCool, 0.01f, 3.f, "%.2f /s");
		tune::SliderFloat("Heat radius", &heatRadius, 50.f, 2000.f, "%.0f");
		tune::SliderFloat("Shadow distance", &shadowDistance, 0.f, 600.f, "%.0f");
		tune::SliderFloat("Shadow darkness", &shadowAlpha, 0.f, 1.f, "%.2f on a ship fully in shadow");
		tune::SliderFloat("Debris parallax", &debrisParallax, 0.f, 1.5f, "%.2f");
		tune::SliderFloat("Debris shade", &debrisShade, 0.f, 1.f, "%.2f");
		if (tune::SliderFloat("Debris fill", &debrisFill, 0.f, 1.f, "%.2f")) { reset(placedCopy, fieldsCopy); }
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Breaking and ore"))
	{
		ImGui::TextDisabled("Health and ore apply to rocks grown after a change (next round)");
		tune::SliderFloat("Health scale", &healthScale, 0.01f, 3.f, "%.2f");
		tune::SliderFloat("Beam vs rock", &beamRockScale, 0.f, 20.f, "x%.1f");
		tune::SliderFloat("Blast damage", &blastDamage, 0.f, 5.f, "%.2f");
		tune::SliderFloat("Ram damage", &ramDamage, 0.f, 10.f, "%.2f");
		tune::SliderFloat("Ore per area", &orePerArea, 0.f, 1e-4f, "%.2e", ImGuiSliderFlags_Logarithmic);
		tune::SliderFloat("Ore per orb", &orbValue, 0.05f, 2.f, "%.2f");
		tune::SliderFloat("Piece area", &pieceArea, 10000.f, 400000.f, "%.0f");
		tune::SliderFloat("Min piece radius", &minPieceRadius, 10.f, 300.f, "%.0f");
		tune::SliderFloat("Break kick", &breakKick, 0.f, 1500.f, "%.0f");
		tune::SliderFloat("Break spin", &breakSpin, 0.f, 6.f, "%.1f");
		ImGui::TextDisabled("Shards: out of play, gathering where the rock stood");
		tune::SliderFloat("Flight", &shardFlight, 0.f, 5.f, "%.1f s before the pull");
		tune::SliderFloat("Flight drag", &shardDrag, 0.f, 5.f, "%.1f /s");
		tune::SliderFloat("Sink", &shardSink, 0.1f, 6.f, "%.1f s into the background");
		tune::SliderFloat("Sunk shade", &shardShade, 0.f, 1.f, "%.2f");
		tune::SliderFloat("Sunk size", &shardScale, 0.3f, 1.f, "%.2f");
		tune::SliderFloat("Gather", &shardGather, 0.5f, 20.f, "%.1f s spring");
		tune::SliderFloat("Gather speed", &shardReturnSpeed, 20.f, 2000.f, "%.0f u/s");
		tune::SliderFloat("Gather spread", &shardSpread, 1.f, 2.f, "%.2f");
		tune::SliderInt("Shard limit", &shardLimit, 10, 3000);
		if (ImGui::SmallButton("Clear shards")) { shards.clear(); }
		ImGui::SameLine();
		ImGui::Text("%d", (int)shards.size());
		tune::SliderFloat("Crack width", &crackWidthPixels, 0.5f, 6.f, "%.1f px");
		tune::SliderFloat("Crack glow", &crackGlowWidthPixels, 0.5f, 10.f, "%.1f px");
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Chunks (W3)"))
	{
		size_t chunkRocks = 0;
		for (const Rock &r : rocks) { if (r.chunk >= 0) { chunkRocks++; } }
		ImGui::Text("%d chunks made, %d rocks in them, %d remembered", (int)loadedChunks.size(),
			(int)chunkRocks, (int)kept.size());
		ImGui::TextDisabled("  of %d rocks all told (cores and single rocks always)", (int)rocks.size());
		ImGui::Text("Last frame: %d made, %d dropped, %d waiting, %.2f ms", madeLastFrame, droppedLastFrame,
			(int)waiting.size(), streamMsLastFrame);
		if (tune::SliderFloat("Chunk size", &chunkSize, 1000.f, 12000.f, "%.0f units")) { reset(placedCopy, fieldsCopy); }
		tune::SliderFloat("Made past the view", &chunkMargin, 0.f, 3.f, "%.1f chunks");
		tune::SliderFloat("Budget", &chunkBudgetMs, 0.5f, 20.f, "%.1f ms a frame");

		ImGui::SeparatorText("See it");
		ImGui::Checkbox("Chunk grid", &showChunkGrid);
		ImGui::SameLine();
		ImGui::Checkbox("Chunk state", &showChunkState);
		ImGui::TextDisabled("  green made, amber waiting on the budget; flashes: made, dropped");
		ImGui::Checkbox("Stream rectangles", &showStreamRects);
		ImGui::TextDisabled("  cyan streamed view, green made within, red dropped outside");
		ImGui::Checkbox("Remembered rocks", &showKept);
		ImGui::TextDisabled("  red cross broken, amber ring damaged or mined");
		ImGui::SliderFloat("Stream for", &streamViewScale, 0.1f, 1.f, "%.2f of the view");
		ImGui::TextDisabled("  below 1, chunks come and go on screen; try 0.3 with Made past 0");
		ImGui::Checkbox("Cull drawing to it too", &cullDrawToStream);
		ImGui::SliderFloat("Flash", &chunkFlashSeconds, 0.1f, 3.f, "%.1f s");
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Cores"))
	{
		if (tune::SliderFloat("Core size", &coreScale, 1.f, 5.f, "%.1f x max size")) { reset(placedCopy, fieldsCopy); }
		if (tune::SliderFloat("Core min fit", &coreMinFit, 0.1f, 3.f, "%.2f x max size, however small the paint")) { reset(placedCopy, fieldsCopy); }
		tune::SliderFloat("Hit damage", &rules.damage, 0.f, 1.f, "%.2f");
		tune::SliderFloat("Ship bounce", &rules.bounce, 0.f, 1.5f, "%.2f");
		tune::SliderFloat("Min bounce speed", &rules.minOutSpeed, 0.f, 3000.f, "%.0f");
		tune::SliderFloat("Hit grace", &rules.grace, 0.f, 3.f, "%.2f s");
		tune::SliderFloat("Enemy knock", &rules.enemyKnock, 0.f, 4000.f, "%.0f");
		tune::SliderFloat("Enemy stun", &rules.enemyStun, 0.f, 2.f, "%.2f s");
		ImGui::TreePop();
	}
	bool regrow = false;
	regrow |= tune::SliderFloat("Corner spacing", &cornerSpacing, 40.f, 600.f, "%.0f");
	regrow |= tune::SliderFloat("Roughness", &roughness, 0.f, 0.6f, "%.2f");
	regrow |= tune::SliderFloat("Angle jitter", &angleJitter, 0.f, 0.95f, "%.2f");
	ImGui::TextDisabled("Fields");
	regrow |= tune::SliderFloat("Smallest rock", &fieldMinFraction, 0.02f, 1.f, "%.2f of the largest");
	regrow |= tune::SliderInt("Layers", &fieldLayers, 1, 5);
	regrow |= tune::SliderFloat("Gap variation", &fieldGapVariation, 0.f, 1.f, "%.2f");
	regrow |= tune::SliderFloat("Lean small", &fieldSmallBias, 0.5f, 6.f, "%.1f within a layer");
	regrow |= tune::SliderFloat("Fill", &fieldFill, 0.1f, 1.f, "%.2f of cells");
	// The painted area as a grid (S1): hiding, sight and shots read it. Only
	// the grid is rebuilt; the rocks stay where they are.
	if (tune::SliderFloat("Mask cell", &maskCell, 25.f, 200.f, "%.0f units")) { buildMask(); }
	ImGui::TextDisabled("  %d x %d cells", mask.width, mask.height);
	// The rocks' buckets (S1b): what the hit tests, the bumping and the
	// drawing look in. Only the buckets are rebuilt.
	if (tune::SliderFloat("Rock buckets", &rockGridCell, 100.f, 1600.f, "%.0f units")) { rockGridDirty = true; }
	if (tune::SliderFloat("Bucket slack", &rockGridSlack, 0.f, 500.f, "%.0f units a rock moves before a rebuild")) { rockGridDirty = true; }
	ImGui::TextDisabled("  %d x %d buckets, %d listings for %d rocks", rockGrid.frame.width, rockGrid.frame.height,
		(int)rockGrid.items.size(), (int)rocks.size());
	if (regrow) { reset(placedCopy, fieldsCopy); }
	tune::SliderFloat("Area dots", &areaDotSpacingPixels, 4.f, 60.f, "%.0f px apart (editor)");
	tune::SliderFloat("Hidden shade", &shadeInField, 0.f, 1.f, "%.2f of the light");
	tune::SliderFloat("Texture scale", &textureWorldSize, 200.f, 6000.f, "%.0f world units");
	tune::SliderFloat("Brightness", &brightness, 0.2f, 1.5f, "%.2f");
	tune::Checkbox("Show outlines", &showOutlines);
}

}
