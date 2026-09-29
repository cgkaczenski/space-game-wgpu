#include <asteroids.h>

#include <engine/polygon.h>
#include <engine/rigidBody.h>
#include <engine/scatter.h>
#include "imgui.h"
#include <platformTools.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>

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
		int ramHitOn = -1;              // the ram that last struck it: once per ram
		float sinceStruck = 0.f;        // seconds since last pushed or bumped: the spring waits

		int field = -1;                 // which field it belongs to; -1 a single rock
		bool core = false;              // its field's core: immovable, never struck, solid to ships

		// A3: the beam's heat, 0 .. 1, and where it burns, in the rock's frame.
		float heat = 0.f;
		glm::vec2 heatAt = {};
	};

	std::vector<Rock> rocks;

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
	float ambient = 0.35f;
	// The rock's hue, which the packed texture's brightness channel dropped:
	// its average colour over its average brightness, as the texture tool
	// prints it.
	glm::vec3 tint = {1.154f, 0.976f, 0.783f};
	float coarsen = 0.3f;          // 0 photographic .. 1 blocky and banded
	// Heat: rises while the beam is on a rock, and cools after.
	glm::vec3 heatColour = {1.f, 0.42f, 0.08f};
	float heatReach = 0.8f;        // how far from the deepest cracks toward the ridges full heat glows
	float heatRise = 0.8f;         // per second under the beam
	float heatCool = 0.35f;        // per second after
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
	std::vector<level::AsteroidField> fieldsCopy; // also the areas inField tests

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

	// The rim: the fan's outer corners are shaded, the centre is not, and the
	// GPU blends between them across each triangle. So the rock darkens
	// toward its silhouette -- it reads as round before A3 lights it.
	float rimShade = 0.55f;
	float brightness = 0.9f;

	bool showOutlines = false;

	// ---- Physics (A2) ----
	// Mass per unit area. Small, so masses are handy numbers: a rock of radius
	// 350 weighs about 38, a pebble of 40 about 0.5 -- sixty times lighter,
	// because mass grows with area.
	float density = 1e-4f;
	float shotPush = 12000.f;      // impulse per unit of a shot's damage
	float missilePush = 4.f;       // a missile pushes this many times harder
	float beamPush = 6000.f;       // steady force while the beam touches
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
	int currentRam = 0;            // counts rams, so each strikes a rock once

	// ---- Cores ----
	// Each field has one: bigger than any of its rocks, at the painted area's
	// middle unless dragged. It is what the field's rocks are seen to fall
	// back toward. It never moves -- an immovable body, zero inverse mass --
	// so rocks bounce off it, and it is solid to ships.
	float coreScale = 3.f;         // its radius, in the field's max rock sizes
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

	Rock grow(const level::Asteroid &a)
	{
		Rock r;
		r.placement = {a.position, 0.f};
		polygon::StarParams params;
		const float circumference = 6.2831853f * a.radius;
		params.vertexCount = std::clamp((int)(circumference / std::max(cornerSpacing, 10.f)), 12, 96);
		params.roughness = roughness;
		params.angleJitter = angleJitter;
		r.outline = polygon::starShaped(a.seed, a.radius, params);
		r.bound = polygon::boundingRadius(r.outline);
		const uint32_t h = mix(a.seed ^ 0x9e3779b9U);
		r.uvOffset = {(h & 0xffff) / 65535.f, (h >> 16) / 65535.f};

		// Mass and turning from the same fan that is drawn and hit.
		const rigid::MassProperties mass = rigid::fromFan(r.outline, {0.f, 0.f}, density);
		r.centroid = mass.centroid;
		r.body = rigid::makeBody(mass, a.position + mass.centroid);
		r.home = r.body.position;
		r.collideRadius = std::sqrt(mass.area / 3.1415927f);
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
	// needs per rock (see asteroid.wgsl) -- the light's direction in the
	// rock's own frame, the rim, and the beam's heat. Carried on the vertices,
	// every rock shares one set of effect parameters and so one draw.
	void drawFan(wgpu2d::Renderer2D &renderer, const std::vector<glm::vec2> &outline,
		const polygon::Placement &at, glm::vec2 uvOffset, float shade, float heat, glm::vec2 heatAt,
		float grow = 1.f)
	{
		const size_t n = outline.size();
		if (n < 3 || rockTexture.id == 0) { return; }

		static std::vector<glm::vec2> positions, uvs;
		static std::vector<glm::vec4> colours;
		positions.clear(); uvs.clear(); colours.clear();

		// The light turned into the rock's frame: the one rotation the shader
		// would otherwise need the rock's angle for.
		const glm::vec2 light = polygon::directionToLocal(at, lightFlat()) * 0.5f + 0.5f;
		auto heatOf = [&](glm::vec2 local)
		{
			if (heat <= 0.f) { return 0.f; }
			const float t = std::clamp(1.f - glm::distance(local, heatAt) / std::max(heatRadius, 1.f), 0.f, 1.f);
			return heat * t * t * (3.f - 2.f * t);
		};
		const float scale = 1.f / std::max(textureWorldSize, 1.f);
		auto uvOf = [&](glm::vec2 local) { return local * scale + uvOffset; };
		const glm::vec4 centre = {light, shade, heatOf({0.f, 0.f})};

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
			colours.push_back({light, shade * rimShade, heatOf(a)});
			colours.push_back({light, shade * rimShade, heatOf(b)});
		}
		renderer.renderTriangles(positions.data(), uvs.data(), colours.data(), positions.size(), rockTexture);
	}

	void drawRock(wgpu2d::Renderer2D &renderer, const Rock &r)
	{
		drawFan(renderer, r.outline, r.placement, r.uvOffset, brightness, r.heat, r.heatAt);
	}

	// The asteroid shader and its parameters: the same for every rock, which
	// is what lets them all go in one draw.
	void beginRocks(wgpu2d::Renderer2D &renderer)
	{
		wgpu2d::EffectParams params;
		const float elevation = glm::radians(lightElevation);
		params.a = {tint, ambient};
		params.b = {std::cos(elevation), std::sin(elevation), lightStrength, coarsen};
		params.c = {heatColour, heatReach};
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

	uint32_t coreSeed(const level::AsteroidField &f) { return mix(f.seed ^ 0xc0e0c0e0U); }

	// A field: its core first, then its rocks -- layers of grids, one rock per
	// cell, the big rocks first and smaller ones filling between them, never
	// closer than the field's gap, and clear of the core (engine/scatter).
	// Each cell's rock is decided by the field's seed and that cell, so
	// painting more area adds rocks and leaves the old ones where they were.
	void growField(const level::AsteroidField &f, int index, std::vector<Rock> &out)
	{
		glm::vec2 lo, hi;
		if (!fieldBounds(f, lo, hi)) { return; }

		const glm::vec2 corePos = f.coreMoved ? f.core : autoCore(f);
		Rock core = grow({corePos, f.maxSize * coreScale, coreSeed(f)});
		core.inField = true;
		core.core = true;
		core.field = index;
		core.body.inverseMass = 0.f;     // immovable: nothing can push it
		core.body.inverseInertia = 0.f;
		const float coreBound = core.bound;
		const glm::vec2 coreCentre = core.placement.position;
		out.push_back(std::move(core));

		scatter::Params params;
		params.keepOut.push_back({coreCentre, coreBound});
		params.seed = f.seed;
		params.maxRadius = f.maxSize;
		params.minRadius = f.maxSize * fieldMinFraction;
		params.gap = f.maxGap;
		params.gapVariation = fieldGapVariation;
		params.layers = fieldLayers;
		params.smallBias = fieldSmallBias;
		params.fill = fieldFill;
		for (const scatter::Item &item : scatter::scatter(lo, hi,
			[&](glm::vec2 p) { return f.contains(p); }, params))
		{
			Rock r = grow({item.position, item.radius, item.seed});
			r.inField = true;
			r.field = index;
			out.push_back(std::move(r));
		}
	}

	// The editor's view of a painted area: dots on a screen-spaced grid
	// wherever the area is, so overlapping brush stamps read as one flat
	// region instead of stacking up.
	void drawArea(wgpu2d::Renderer2D &renderer, const level::AsteroidField &f)
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
				if (!f.contains({x, y})) { continue; }
				renderer.renderRectangle({x - dot * 0.5f, y - dot * 0.5f, dot, dot}, {0.4f, 1.f, 0.6f, 0.55f});
			}
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
	rocks.clear();
	for (const level::Asteroid &a : placed) { rocks.push_back(grow(a)); }
	for (int i = 0; i < (int)fields.size(); i++) { growField(fields[i], i, rocks); }

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

float hiddenShade() { return shadeInField; }

bool inField(glm::vec2 point)
{
	for (const level::AsteroidField &f : fieldsCopy)
	{
		if (f.contains(point)) { return true; }
	}
	return false;
}

int hitCircle(glm::vec2 centre, float radius)
{
	for (int i = 0; i < (int)rocks.size(); i++)
	{
		const Rock &r = rocks[i];
		// Broad phase: two circles that do not touch rule out the triangles.
		if (glm::distance(centre, r.placement.position) > r.bound + radius) { continue; }
		if (polygon::overlapsCircle(r.outline, polygon::toLocal(r.placement, centre), radius)) { return i; }
	}
	return -1;
}

float raycast(glm::vec2 origin, glm::vec2 direction, float maxDistance, int *rock)
{
	float nearest = -1.f;
	if (rock) { *rock = -1; }
	for (int i = 0; i < (int)rocks.size(); i++)
	{
		const Rock &r = rocks[i];
		// Broad phase: how close the ray's line passes the rock's centre.
		const glm::vec2 toCentre = r.placement.position - origin;
		const float along = glm::dot(toCentre, direction);
		const float miss = glm::length(toCentre - direction * along);
		if (miss > r.bound || along < -r.bound || along > maxDistance + r.bound) { continue; }

		const float reach = nearest >= 0.f ? nearest : maxDistance;
		const float t = polygon::raycast(r.outline, polygon::toLocal(r.placement, origin),
			polygon::directionToLocal(r.placement, direction), reach);
		if (t >= 0.f)
		{
			nearest = t;
			if (rock) { *rock = i; }
		}
	}
	return nearest;
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
}

void beam(int rock, glm::vec2 point, glm::vec2 direction, float dt)
{
	if (rock < 0 || rock >= (int)rocks.size() || dt <= 0.f || rocks[rock].core) { return; }
	Rock &r = rocks[rock];
	rigid::applyForce(r.body, point, direction * beamPush, dt);
	// And heats where it burns (A3). The spot follows the beam gently, so
	// sweeping it drags the glow along rather than jumping it.
	const glm::vec2 local = polygon::toLocal(r.placement, point);
	r.heatAt = r.heat > 0.f ? glm::mix(r.heatAt, local, std::min(1.f, dt * 10.f)) : local;
	r.heat = std::min(1.f, r.heat + heatRise * dt);
	clampMotion(r);
	wake(r);
}

void blast(glm::vec2 at, float strength)
{
	for (Rock &r : rocks)
	{
		if (r.core) { continue; }
		const glm::vec2 away = r.body.position - at;
		const float distance = glm::length(away);
		// Measured to the rock's near side, so a big rock beside a blast is
		// not spared because its centre is far off.
		const float edge = std::max(distance - r.collideRadius, 0.f);
		if (edge >= blastReach || distance < 1e-3f) { continue; }
		// Through the centre: a blast shoves, it does not spin.
		rigid::applyImpulse(r.body, r.body.position,
			away / distance * (blastPush * strength * (1.f - edge / blastReach)));
		clampMotion(r);
		wake(r);
	}
}

void ram(glm::vec2 centre, float radius, glm::vec2 direction, bool newRam)
{
	if (newRam) { currentRam++; }
	for (Rock &r : rocks)
	{
		if (r.core || r.ramHitOn == currentRam) { continue; } // a core is the ship's problem, not the core's
		if (glm::distance(centre, r.placement.position) > r.bound + radius) { continue; }
		const glm::vec2 local = polygon::toLocal(r.placement, centre);
		if (!polygon::overlapsCircle(r.outline, local, radius)) { continue; }
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
		r.ramHitOn = currentRam;
	}
}

void update(float dt)
{
	if (dt <= 0.f) { return; }

	// Heat cools on every rock, moving or not (A3).
	for (Rock &r : rocks)
	{
		if (r.heat > 0.f) { r.heat = std::max(0.f, r.heat - heatCool * dt); }
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
	// is moving. Every rock goes in a spatial hash -- a big one in every cell
	// its circle covers -- and each moving rock checks the cells round it.
	const float cell = 600.f;
	auto key = [](int x, int y) { return ((uint64_t)(uint32_t)x << 32) | (uint32_t)y; };
	static std::unordered_map<uint64_t, std::vector<int>> grid;
	grid.clear();
	for (int i = 0; i < (int)rocks.size(); i++)
	{
		const Rock &r = rocks[i];
		const int x0 = (int)std::floor((r.body.position.x - r.collideRadius) / cell);
		const int x1 = (int)std::floor((r.body.position.x + r.collideRadius) / cell);
		const int y0 = (int)std::floor((r.body.position.y - r.collideRadius) / cell);
		const int y1 = (int)std::floor((r.body.position.y + r.collideRadius) / cell);
		for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) { grid[key(x, y)].push_back(i); }
	}
	for (int i = 0; i < (int)rocks.size(); i++)
	{
		Rock &a = rocks[i];
		if (!a.awake) { continue; }
		const int x0 = (int)std::floor((a.body.position.x - a.collideRadius) / cell);
		const int x1 = (int)std::floor((a.body.position.x + a.collideRadius) / cell);
		const int y0 = (int)std::floor((a.body.position.y - a.collideRadius) / cell);
		const int y1 = (int)std::floor((a.body.position.y + a.collideRadius) / cell);
		for (int y = y0; y <= y1; y++)
		{
			for (int x = x0; x <= x1; x++)
			{
				const auto found = grid.find(key(x, y));
				if (found == grid.end()) { continue; }
				for (int j : found->second)
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
		}
	}

	// Place the frames the draw and the hit tests use, and let the settled
	// ones sleep: still, and a field rock home again.
	for (Rock &r : rocks)
	{
		if (!r.awake) { continue; }
		place(r);
		awakeCount++;
		const bool still = glm::length(r.body.velocity) < 2.f && std::abs(r.body.spin) < 0.02f;
		const bool home = !r.inField || glm::distance(r.body.position, r.home) < 2.f;
		if (still && home) { r.awake = false; }
	}
}

const CoreRules &coreRules() { return rules; }

glm::vec2 fieldCore(const level::AsteroidField &f) { return f.coreMoved ? f.core : autoCore(f); }

bool coreContact(glm::vec2 centre, float radius, CoreContact &out)
{
	for (const Rock &r : rocks)
	{
		if (!r.core) { continue; }
		if (glm::distance(centre, r.placement.position) > r.bound + radius) { continue; }
		if (!polygon::overlapsCircle(r.outline, polygon::toLocal(r.placement, centre), radius)) { continue; }

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
	return false;
}

namespace
{
	// One layer of rocks through the asteroid shader, then any outlines.
	void drawLayer(wgpu2d::Renderer2D &renderer, bool fieldLayer)
	{
		// Off screen, skip it: the batch would draw it anyway.
		const glm::vec4 view = renderer.getViewRect();
		beginRocks(renderer);
		for (const Rock &r : rocks)
		{
			if (r.inField != fieldLayer || !onScreen(view, r)) { continue; }
			drawRock(renderer, r);
		}
		endRocks(renderer);
		if (showOutlines)
		{
			for (const Rock &r : rocks)
			{
				if (r.inField == fieldLayer && onScreen(view, r)) { drawOutline(renderer, r); }
			}
		}
	}
}

void draw(wgpu2d::Renderer2D &renderer) { drawLayer(renderer, false); }

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
	float darkest = 0.f;
	for (const Rock &r : rocks)
	{
		if (!r.inField) { continue; } // single rocks lie under the ships
		polygon::Placement cast = r.placement;
		cast.position -= away;
		if (glm::distance(centre, cast.position) > r.bound + radius) { continue; }
		int covered = 0;
		for (const glm::vec2 &s : spots)
		{
			if (polygon::contains(r.outline, polygon::toLocal(cast, centre + s * (radius * 0.66f)))) { covered++; }
		}
		darkest = std::max(darkest, covered / 9.f);
		if (darkest >= 1.f) { break; }
	}
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
		drawFan(renderer, d.outline, {at, d.angle}, d.uvOffset, debrisShade, 0.f, {}, 1.f + debrisParallax);
	}
	endRocks(renderer);
}

void drawPlacements(wgpu2d::Renderer2D &renderer, const std::vector<level::Asteroid> &placed,
	const std::vector<level::AsteroidField> &fields)
{
	for (const level::AsteroidField &f : fields) { drawArea(renderer, f); }

	std::vector<Rock> grown;
	for (const level::Asteroid &a : placed) { grown.push_back(grow(a)); }
	for (int i = 0; i < (int)fields.size(); i++) { growField(fields[i], i, grown); }
	const glm::vec4 view = renderer.getViewRect();
	beginRocks(renderer);
	for (const Rock &r : grown)
	{
		if (onScreen(view, r)) { drawRock(renderer, r); }
	}
	endRocks(renderer);
	for (const Rock &r : grown)
	{
		// A field's rocks are too many to outline.
		if (!r.inField && onScreen(view, r)) { drawOutline(renderer, r); }
	}
}

void debugUi()
{
	ImGui::Text("%d rocks, %d moving", (int)rocks.size(), awakeCount);
	if (ImGui::TreeNode("Physics"))
	{
		ImGui::SliderFloat("Shot push", &shotPush, 0.f, 60000.f, "%.0f per damage");
		ImGui::SliderFloat("Missile push", &missilePush, 0.f, 20.f, "x%.1f");
		ImGui::SliderFloat("Beam push", &beamPush, 0.f, 40000.f, "%.0f");
		ImGui::SliderFloat("Blast push", &blastPush, 0.f, 20000.f, "%.0f");
		ImGui::SliderFloat("Blast reach", &blastReach, 0.f, 6000.f, "%.0f");
		ImGui::SliderFloat("Ram push", &ramPush, 0.f, 100000.f, "%.0f");
		ImGui::SliderFloat("Ram forward", &ramForward, 0.f, 3.f, "%.2f (0: straight out from the prow)");
		ImGui::SliderFloat("Friction", &friction, 0.f, 1.f, "%.2f (rock on rock)");
		ImGui::SliderFloat("Resting speed", &restingSpeed, 0.f, 600.f, "%.0f u/s: slower touches don't bounce");
		ImGui::SliderFloat("Spring delay", &springDelay, 0.f, 10.f, "%.1f s loose after a hit");
		ImGui::SliderFloat("Spring ease", &springEase, 0.01f, 5.f, "%.1f s");
		ImGui::SliderFloat("Return speed", &returnSpeed, 50.f, 3000.f, "%.0f u/s home");
		ImGui::SliderFloat("Drift damping", &linearDamping, 0.f, 3.f, "%.2f /s");
		ImGui::SliderFloat("Spin damping", &angularDamping, 0.f, 3.f, "%.2f /s");
		ImGui::SliderFloat("Max speed", &maxSpeed, 50.f, 6000.f, "%.0f");
		ImGui::SliderFloat("Max spin", &maxSpin, 0.1f, 20.f, "%.1f rad/s");
		ImGui::SliderFloat("Bounce", &restitution, 0.f, 1.f, "%.2f");
		ImGui::SliderFloat("Field spring", &springPeriod, 0.5f, 20.f, "%.1f s");
		ImGui::SliderFloat("Spring damping", &springDamping, 0.1f, 2.f, "%.2f");
		ImGui::TextDisabled("Density applies when rocks are grown (next round)");
		ImGui::SliderFloat("Density", &density, 1e-5f, 1e-3f, "%.5f", ImGuiSliderFlags_Logarithmic);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Look"))
	{
		ImGui::SliderFloat("Light from", &lightAzimuth, 0.f, 360.f, "%.0f deg (0 right, 90 below)");
		ImGui::SliderFloat("Light height", &lightElevation, 5.f, 90.f, "%.0f deg");
		ImGui::SliderFloat("Light strength", &lightStrength, 0.f, 3.f, "%.2f");
		ImGui::SliderFloat("Ambient", &ambient, 0.f, 1.f, "%.2f");
		ImGui::ColorEdit3("Tint", &tint.x, ImGuiColorEditFlags_Float | ImGuiColorEditFlags_HDR);
		ImGui::SliderFloat("Coarsen", &coarsen, 0.f, 1.f, "%.2f");
		ImGui::ColorEdit3("Heat colour", &heatColour.x);
		ImGui::SliderFloat("Heat reach", &heatReach, 0.f, 1.f, "%.2f (0 deepest cracks only)");
		ImGui::SliderFloat("Heat rise", &heatRise, 0.05f, 5.f, "%.2f /s under the beam");
		ImGui::SliderFloat("Heat cool", &heatCool, 0.01f, 3.f, "%.2f /s");
		ImGui::SliderFloat("Heat radius", &heatRadius, 50.f, 2000.f, "%.0f");
		ImGui::SliderFloat("Shadow distance", &shadowDistance, 0.f, 600.f, "%.0f");
		ImGui::SliderFloat("Shadow darkness", &shadowAlpha, 0.f, 1.f, "%.2f on a ship fully in shadow");
		ImGui::SliderFloat("Debris parallax", &debrisParallax, 0.f, 1.5f, "%.2f");
		ImGui::SliderFloat("Debris shade", &debrisShade, 0.f, 1.f, "%.2f");
		if (ImGui::SliderFloat("Debris fill", &debrisFill, 0.f, 1.f, "%.2f")) { reset(placedCopy, fieldsCopy); }
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Cores"))
	{
		if (ImGui::SliderFloat("Core size", &coreScale, 1.f, 5.f, "%.1f x max size")) { reset(placedCopy, fieldsCopy); }
		ImGui::SliderFloat("Hit damage", &rules.damage, 0.f, 1.f, "%.2f");
		ImGui::SliderFloat("Ship bounce", &rules.bounce, 0.f, 1.5f, "%.2f");
		ImGui::SliderFloat("Min bounce speed", &rules.minOutSpeed, 0.f, 3000.f, "%.0f");
		ImGui::SliderFloat("Hit grace", &rules.grace, 0.f, 3.f, "%.2f s");
		ImGui::SliderFloat("Enemy knock", &rules.enemyKnock, 0.f, 4000.f, "%.0f");
		ImGui::SliderFloat("Enemy stun", &rules.enemyStun, 0.f, 2.f, "%.2f s");
		ImGui::TreePop();
	}
	bool regrow = false;
	regrow |= ImGui::SliderFloat("Corner spacing", &cornerSpacing, 40.f, 600.f, "%.0f");
	regrow |= ImGui::SliderFloat("Roughness", &roughness, 0.f, 0.6f, "%.2f");
	regrow |= ImGui::SliderFloat("Angle jitter", &angleJitter, 0.f, 0.95f, "%.2f");
	ImGui::TextDisabled("Fields");
	regrow |= ImGui::SliderFloat("Smallest rock", &fieldMinFraction, 0.02f, 1.f, "%.2f of the largest");
	regrow |= ImGui::SliderInt("Layers", &fieldLayers, 1, 5);
	regrow |= ImGui::SliderFloat("Gap variation", &fieldGapVariation, 0.f, 1.f, "%.2f");
	regrow |= ImGui::SliderFloat("Lean small", &fieldSmallBias, 0.5f, 6.f, "%.1f within a layer");
	regrow |= ImGui::SliderFloat("Fill", &fieldFill, 0.1f, 1.f, "%.2f of cells");
	if (regrow) { reset(placedCopy, fieldsCopy); }
	ImGui::SliderFloat("Area dots", &areaDotSpacingPixels, 4.f, 60.f, "%.0f px apart (editor)");
	ImGui::SliderFloat("Hidden shade", &shadeInField, 0.f, 1.f, "%.2f of the light");
	ImGui::SliderFloat("Texture scale", &textureWorldSize, 200.f, 6000.f, "%.0f world units");
	ImGui::SliderFloat("Rim shade", &rimShade, 0.f, 1.f, "%.2f");
	ImGui::SliderFloat("Brightness", &brightness, 0.2f, 1.5f, "%.2f");
	ImGui::Checkbox("Show outlines", &showOutlines);
}

}
