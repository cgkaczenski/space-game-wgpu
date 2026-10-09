#include <levelEditor.h>

#include <asteroids.h>
#include <gate.h>
#include <lanes.h>
#include <engine/regionMask.h>
#include <enemyAi.h>
#include <weapons.h>
#include <scenery.h>
#include <shipSprite.h>
#include <hintScript.h>
#include <textLook.h>
#include "platformInput.h"
#include "imgui.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace levelEditor
{

namespace
{
	bool isActive = false;
	bool edited = false;

	// The view: its centre in the world, and its zoom. The editor's own, so
	// it can go past the game's limits -- a 20000 arena is 40000 across.
	glm::vec2 centre = {};
	float zoom = 0.2f;
	constexpr float minZoom = 0.01f;
	constexpr float maxZoom = 1.f;

	enum class Tool { Select, Rusher, Sniper, Gate, Ring, Asteroid, Paint, Scenery, Boss, Lane, Jump, Hint };
	Tool tool = Tool::Select;
	int sceneryArt = 0;

	enum class Kind { None, Start, Enemy, Marker, Ring, Asteroid, Field, Core, Scenery, Lane, LanePoint, Jump };
	struct Pick
	{
		Kind kind = Kind::None;
		int index = -1;
		int core = -1;   // which of field `index`'s cores, for Kind::Core
		int point = -1;  // which of lane `index`'s points, for Kind::LanePoint; which end, for Kind::Jump
	};

	// The Jump gate tool (W5): the first click places one end, the second the
	// other.
	bool jumpPending = false;
	glm::vec2 jumpFirst = {};
	const glm::vec4 jumpColour = {0.75f, 0.45f, 1.f, 0.9f};

	// The Hints tool (hints roadmap H2): which step the panel edits, and
	// its text and arguments being typed, synced when the step changes.
	int hintSelected = -1;
	int hintSynced = -2;
	char hintText[256] = "";
	char hintArgs[128] = "";

	// A lane's points are picked by a handle a fixed size on screen.
	constexpr float lanePointPixels = 12.f;

	float distanceToLane(const level::Lane &l, glm::vec2 p)
	{
		float best = 1e30f;
		for (size_t k = 0; k + 1 < l.points.size(); k++)
		{
			const glm::vec2 a = l.points[k], ab = l.points[k + 1] - a;
			const float len2 = glm::dot(ab, ab);
			const float t = len2 > 0.f ? std::clamp(glm::dot(p - a, ab) / len2, 0.f, 1.f) : 0.f;
			best = std::min(best, glm::distance(p, a + ab * t));
		}
		if (l.points.size() == 1) { best = glm::distance(p, l.points[0]); }
		return best;
	}
	Pick selected;

	// The Paint tool (asteroid fields, A1b). A stroke lays stamps -- circles of
	// the brush's size -- whenever the cursor has moved a third of the brush
	// since the last one, so a stroke is a chain of overlapping circles. With
	// Shift held when it starts, it lays erasers instead.
	float brushRadius = 600.f;
	bool painting = false;
	bool paintErasing = false;
	int paintField = -1;
	bool paintNewField = false; // "Start a new field": the next stroke makes one, even over paint
	glm::vec2 lastStamp = {};
	glm::vec2 cursorWorld = {}; // for drawing the brush

	bool dragging = false;
	glm::vec2 dragOffset = {}; // item position minus the cursor, in the world

	// A right click deletes; a right drag pans. Which one it was is only known
	// once the button moves or comes back up.
	bool rightDown = false;
	bool rightPanning = false;
	glm::vec2 rightStart = {};
	glm::vec2 lastMouse = {};
	constexpr float panThresholdPixels = 4.f;

	float panKeysScreensPerSecond = 0.8f;

	glm::vec2 screenToWorld(glm::vec2 mouse, int width, int height)
	{
		return centre + (mouse - glm::vec2(width, height) * 0.5f) / zoom;
	}

	// Where a piece of scenery is drawn from here: see scenery.cpp. The editor
	// picks and drags what is on screen, so it works in drawn positions.
	glm::vec2 drawnAt(const level::Scenery &s)
	{
		return s.position + (centre - s.position) * s.depth;
	}

	glm::vec2 placedFrom(glm::vec2 drawn, float depth)
	{
		// Inverse of drawnAt, for a fixed camera.
		const float keep = std::max(1.f - depth, 0.001f);
		return (drawn - centre * depth) / keep;
	}

	constexpr float gateRadius = 600.f;

	// A ring is picked by a handle at its centre, a fixed size on screen: its
	// edge can be tens of thousands of units long and would steal every click.
	constexpr float ringHandlePixels = 14.f;

	// A ring has to sit inside the one before it, largest first, starting
	// from the arena -- or the circle would grow for part of a close. Those
	// that do not are drawn red.
	bool contains(glm::vec2 outerCentre, float outerRadius, glm::vec2 centre, float radius)
	{
		return glm::distance(outerCentre, centre) + radius <= outerRadius + 0.5f;
	}

	glm::vec2 positionOf(const level::Level &level, Pick p)
	{
		switch (p.kind)
		{
		case Kind::Start: return level.start;
		case Kind::Enemy: return level.enemies[p.index].position;
		case Kind::Marker: return level.markers[p.index].position;
		case Kind::Ring: return level.rings[p.index].position;
		case Kind::Asteroid: return level.asteroids[p.index].position;
		case Kind::Field:
		{
			// A field has no one position; its first stamp stands in, and
			// moving that moves every stamp with it.
			const auto &stamps = level.fields[p.index].stamps;
			return stamps.empty() ? glm::vec2{} : stamps.front().position;
		}
		case Kind::Core:
		{
			const std::vector<glm::vec2> cores = asteroids::fieldCores(level.fields[p.index]);
			return p.core >= 0 && p.core < (int)cores.size() ? cores[(size_t)p.core] : glm::vec2{};
		}
		case Kind::Scenery: return drawnAt(level.scenery[p.index]);
		case Kind::Lane:
		{
			// Like a field: its first point stands in, and moving it moves all.
			const auto &points = level.lanes[p.index].points;
			return points.empty() ? glm::vec2{} : points.front();
		}
		case Kind::LanePoint: return level.lanes[p.index].points[(size_t)p.point];
		case Kind::Jump: return p.point == 0 ? level.jumps[p.index].a : level.jumps[p.index].b;
		default: return {};
		}
	}

	void moveTo(level::Level &level, Pick p, glm::vec2 to)
	{
		switch (p.kind)
		{
		case Kind::Start: level.start = to; break;
		case Kind::Enemy: level.enemies[p.index].position = to; break;
		case Kind::Marker: level.markers[p.index].position = to; break;
		case Kind::Ring: level.rings[p.index].position = to; break;
		case Kind::Asteroid: level.asteroids[p.index].position = to; break;
		case Kind::Field:
		{
			level::AsteroidField &f = level.fields[p.index];
			if (f.stamps.empty()) { break; }
			const glm::vec2 by = to - f.stamps.front().position;
			for (level::FieldStamp &s : f.stamps) { s.position += by; }
			for (glm::vec2 &c : f.cores) { c += by; } // placed cores come along
			break;
		}
		case Kind::Core:
		{
			// The middle's core, dragged, becomes a placed one.
			level::AsteroidField &f = level.fields[p.index];
			if (f.cores.empty()) { f.cores = asteroids::fieldCores(f); }
			if (p.core >= 0 && p.core < (int)f.cores.size()) { f.cores[(size_t)p.core] = to; }
			break;
		}
		case Kind::Scenery:
		{
			level::Scenery &s = level.scenery[p.index];
			s.position = placedFrom(to, s.depth);
			break;
		}
		case Kind::Lane:
		{
			level::Lane &l = level.lanes[p.index];
			if (l.points.empty()) { break; }
			const glm::vec2 by = to - l.points.front();
			for (glm::vec2 &q : l.points) { q += by; }
			break;
		}
		case Kind::LanePoint: level.lanes[p.index].points[(size_t)p.point] = to; break;
		case Kind::Jump: (p.point == 0 ? level.jumps[p.index].a : level.jumps[p.index].b) = to; break;
		default: break;
		}
	}

	// The nearest thing whose shape contains `at`. Ships and markers before
	// scenery: a planet is big and behind everything, and should not steal a
	// click aimed at a ship in front of it.
	// Fields are picked last among the placements, by being inside the painted
	// area -- or on their core, which is picked before the field around it.
	Pick pickAt(const level::Level &level, glm::vec2 at, float shipSize, float enemySize)
	{
		Pick best;
		float bestDistance = 1e30f;
		auto consider = [&](Kind kind, int index, glm::vec2 position, float radius)
		{
			const float d = glm::distance(at, position);
			if (d <= radius && d < bestDistance) { best = {kind, index}; bestDistance = d; }
		};

		consider(Kind::Start, -1, level.start, shipSize * 0.5f);
		for (int i = 0; i < (int)level.enemies.size(); i++)
		{
			consider(Kind::Enemy, i, level.enemies[i].position,
				enemySize / enemyShipSize * enemyAi::sizeOf(level.enemies[i].behaviour) * 0.5f);
		}
		for (int i = 0; i < (int)level.markers.size(); i++)
		{
			consider(Kind::Marker, i, level.markers[i].position, gateRadius);
		}
		for (int i = 0; i < (int)level.rings.size(); i++)
		{
			consider(Kind::Ring, i, level.rings[i].position, ringHandlePixels / zoom);
		}
		for (int i = 0; i < (int)level.asteroids.size(); i++)
		{
			consider(Kind::Asteroid, i, level.asteroids[i].position, level.asteroids[i].radius);
		}
		// Jump gates, either end of a pair, picked like the exit gate.
		for (int i = 0; i < (int)level.jumps.size(); i++)
		{
			for (int end = 0; end < 2; end++)
			{
				const glm::vec2 gateAt = end == 0 ? level.jumps[i].a : level.jumps[i].b;
				const float d = glm::distance(at, gateAt);
				if (d <= gateRadius && d < bestDistance) { best = {Kind::Jump, i, -1, end}; bestDistance = d; }
			}
		}
		// A lane's points, by their handles.
		for (int i = 0; i < (int)level.lanes.size(); i++)
		{
			const auto &points = level.lanes[i].points;
			for (int k = 0; k < (int)points.size(); k++)
			{
				const float d = glm::distance(at, points[(size_t)k]);
				if (d <= lanePointPixels / zoom && d < bestDistance) { best = {Kind::LanePoint, i, -1, k}; bestDistance = d; }
			}
		}
		if (best.kind != Kind::None) { return best; }

		// A lane, anywhere along it: it cuts through fields, so before them.
		for (int i = (int)level.lanes.size() - 1; i >= 0; i--)
		{
			if (distanceToLane(level.lanes[i], at) <= level.lanes[i].width * 0.5f) { return {Kind::Lane, i}; }
		}

		// A field's cores, before the field they sit in.
		for (int i = (int)level.fields.size() - 1; i >= 0; i--)
		{
			const level::AsteroidField &f = level.fields[i];
			const std::vector<glm::vec2> cores = asteroids::fieldCores(f);
			for (int c = 0; c < (int)cores.size(); c++)
			{
				if (glm::distance(at, cores[(size_t)c]) <= asteroids::coreRadius(f, c) * 0.5f) { return {Kind::Core, i, c}; }
			}
		}
		// The most recently made on top.
		for (int i = (int)level.fields.size() - 1; i >= 0; i--)
		{
			if (level.fields[i].contains(at)) { return {Kind::Field, i}; }
		}

		for (int i = 0; i < (int)level.scenery.size(); i++)
		{
			consider(Kind::Scenery, i, drawnAt(level.scenery[i]), level.scenery[i].size * 0.5f);
		}
		return best;
	}

	void remove(level::Level &level, Pick p)
	{
		switch (p.kind)
		{
		case Kind::Enemy: level.enemies.erase(level.enemies.begin() + p.index); break;
		case Kind::Marker: level.markers.erase(level.markers.begin() + p.index); break;
		case Kind::Ring: level.rings.erase(level.rings.begin() + p.index); break;
		case Kind::Asteroid: level.asteroids.erase(level.asteroids.begin() + p.index); break;
		// Only ever from the panel's buttons -- a right click on a field or a
		// core only selects the field. A placed core goes, and the field is
		// selected; the last one gone, the field has one at its middle again.
		case Kind::Field: level.fields.erase(level.fields.begin() + p.index); break;
		case Kind::Core:
		{
			std::vector<glm::vec2> &cores = level.fields[p.index].cores;
			if (p.core >= 0 && p.core < (int)cores.size()) { cores.erase(cores.begin() + p.core); }
			selected = {Kind::Field, p.index};
			dragging = false;
			edited = true;
			return;
		}
		case Kind::Scenery: level.scenery.erase(level.scenery.begin() + p.index); break;
		case Kind::Lane: level.lanes.erase(level.lanes.begin() + p.index); break;
		case Kind::Jump: level.jumps.erase(level.jumps.begin() + p.index); break; // the pair goes together
		case Kind::LanePoint:
		{
			// A point goes; a lane left with fewer than two goes with it.
			std::vector<glm::vec2> &points = level.lanes[p.index].points;
			points.erase(points.begin() + p.point);
			if (points.size() < 2) { level.lanes.erase(level.lanes.begin() + p.index); }
			else
			{
				selected = {Kind::Lane, p.index};
				dragging = false;
				edited = true;
				return;
			}
			break;
		}
		default: return; // the start stays: a level needs one
		}
		selected = {};
		dragging = false;
		edited = true;
	}

	// A new thing from the current tool, at `at`. Returns what was placed.
	Pick place(level::Level &level, glm::vec2 at)
	{
		switch (tool)
		{
		case Tool::Rusher:
		case Tool::Sniper:
		case Tool::Boss:
		{
			level::EnemyPlacement e;
			e.behaviour = tool == Tool::Sniper ? Enemy::Behaviour::KeepDistance
				: tool == Tool::Boss ? Enemy::Behaviour::Boss : Enemy::Behaviour::CloseIn;
			e.position = at;
			level.enemies.push_back(e);
			return {Kind::Enemy, (int)level.enemies.size() - 1};
		}
		case Tool::Gate:
		{
			level::Marker m;
			m.kind = level::Marker::Kind::Gate;
			m.position = at;
			level.markers.push_back(m);
			return {Kind::Marker, (int)level.markers.size() - 1};
		}
		case Tool::Ring:
		{
			// Half the smallest so far, so a new one starts as the next stage.
			float smallest = level.arenaRadius;
			for (const level::Ring &r : level.rings) { smallest = std::min(smallest, r.radius); }
			level::Ring r;
			r.position = at;
			r.radius = std::max(smallest * 0.5f, 100.f);
			level.rings.push_back(r);
			return {Kind::Ring, (int)level.rings.size() - 1};
		}
		case Tool::Asteroid:
		{
			level::Asteroid a;
			a.position = at;
			a.seed = (uint32_t)std::rand(); // a new shape; reroll in the panel
			level.asteroids.push_back(a);
			return {Kind::Asteroid, (int)level.asteroids.size() - 1};
		}
		case Tool::Scenery:
		{
			level::Scenery s;
			s.art = scenery::artName(sceneryArt);
			s.position = placedFrom(at, s.depth); // drawn where it is placed, at its default depth
			level.scenery.push_back(s);
			return {Kind::Scenery, (int)level.scenery.size() - 1};
		}
		default: return {};
		}
	}

	bool validPick(const level::Level &level, Pick p)
	{
		switch (p.kind)
		{
		case Kind::Start: return true;
		case Kind::Enemy: return p.index >= 0 && p.index < (int)level.enemies.size();
		case Kind::Marker: return p.index >= 0 && p.index < (int)level.markers.size();
		case Kind::Ring: return p.index >= 0 && p.index < (int)level.rings.size();
		case Kind::Asteroid: return p.index >= 0 && p.index < (int)level.asteroids.size();
		case Kind::Field: return p.index >= 0 && p.index < (int)level.fields.size();
		case Kind::Core:
			return p.index >= 0 && p.index < (int)level.fields.size() && p.core >= 0
				&& p.core < std::max((int)level.fields[p.index].cores.size(), 1);
		case Kind::Scenery: return p.index >= 0 && p.index < (int)level.scenery.size();
		case Kind::Lane: return p.index >= 0 && p.index < (int)level.lanes.size();
		case Kind::LanePoint:
			return p.index >= 0 && p.index < (int)level.lanes.size() && p.point >= 0
				&& p.point < (int)level.lanes[p.index].points.size();
		case Kind::Jump: return p.index >= 0 && p.index < (int)level.jumps.size() && (p.point == 0 || p.point == 1);
		default: return false;
		}
	}

	// How much of the arena circle the fields cover as played, gate
	// clearings cut out: 0 .. 1 (sight roadmap W1). On a grid 300 cells
	// across the arena, so about a third of a percent at worst.
	float fieldShare(const level::Level &level)
	{
		const float r = level.arenaRadius;
		if (r <= 0.f || level.fields.empty()) { return 0.f; }
		constexpr int across = 300;
		const float cell = 2.f * r / (float)across;
		std::vector<std::vector<region::Stamp>> layers;
		for (const level::AsteroidField &f : level::fieldsAsPlayed(level, gate::clearingRadius()))
		{
			std::vector<region::Stamp> &layer = layers.emplace_back();
			for (const level::FieldStamp &s : f.stamps) { layer.push_back({s.position, s.radius, s.erase}); }
		}
		const region::Mask mask = region::build(layers, cell);
		int inside = 0, painted = 0;
		for (int y = 0; y < across; y++)
		{
			for (int x = 0; x < across; x++)
			{
				const glm::vec2 p = {-r + (x + 0.5f) * cell, -r + (y + 0.5f) * cell};
				if (glm::dot(p, p) > r * r) { continue; }
				inside++;
				if (region::labelAt(mask, p) >= 0) { painted++; }
			}
		}
		return inside > 0 ? (float)painted / (float)inside : 0.f;
	}

	// The sizes picking uses, kept from the last draw: the game lends them
	// there, and update runs first.
	float pickShipSize = 250.f;
	float pickEnemySize = 250.f;
}

bool active() { return isActive; }

void open(glm::vec2 cameraPosition, float z)
{
	isActive = true;
	centre = cameraPosition;
	zoom = std::clamp(z, minZoom, maxZoom);
	selected = {};
	dragging = false;
	rightDown = false;
}

void close()
{
	isActive = false;
	dragging = false;
	rightDown = false;
}

glm::vec2 cameraCentre() { return centre; }
bool changed() { return edited; }
void clearChanged() { edited = false; }

void update(level::Level &level, wgpu2d::Renderer2D &renderer, glm::vec2 mouse,
	int width, int height, float dt)
{
	if (!validPick(level, selected)) { selected = {}; dragging = false; }

	const ImGuiIO &io = ImGui::GetIO();
	const bool mouseFree = !io.WantCaptureMouse;

	// Zoom about the cursor: the world point under it stays under it.
	if (mouseFree)
	{
		const float scroll = platform::getScrollY();
		if (scroll != 0.f)
		{
			const glm::vec2 before = screenToWorld(mouse, width, height);
			zoom = std::clamp(zoom * std::pow(1.15f, scroll), minZoom, maxZoom);
			centre = before - (mouse - glm::vec2(width, height) * 0.5f) / zoom;
		}
	}

	// WASD pans, by a fraction of the view a second whatever the zoom.
	if (!io.WantCaptureKeyboard)
	{
		glm::vec2 keys = {};
		if (platform::isButtonHeld(platform::Button::A)) { keys.x -= 1.f; }
		if (platform::isButtonHeld(platform::Button::D)) { keys.x += 1.f; }
		if (platform::isButtonHeld(platform::Button::W)) { keys.y -= 1.f; }
		if (platform::isButtonHeld(platform::Button::S)) { keys.y += 1.f; }
		centre += keys * (panKeysScreensPerSecond * (float)std::max(width, height) / zoom * dt);
	}

	const glm::vec2 world = screenToWorld(mouse, width, height);

	// Right button: pan if it moves, delete if it does not.
	if (mouseFree && platform::isRMousePressed())
	{
		rightDown = true;
		rightPanning = false;
		rightStart = mouse;
	}
	if (rightDown)
	{
		if (!rightPanning && glm::distance(mouse, rightStart) > panThresholdPixels) { rightPanning = true; }
		if (rightPanning) { centre -= (mouse - lastMouse) / zoom; }
		if (platform::isRMouseReleased() || !platform::isRMouseHeld())
		{
			if (!rightPanning)
			{
				// One stray click should not wipe a whole painted field, so on a
				// field or its core it selects the field instead, and the panel
				// offers "Delete field". Picked with fields included, so a click
				// on a field never falls through to the planet behind it.
				const Pick p = pickAt(level, screenToWorld(rightStart, width, height),
					pickShipSize, pickEnemySize);
				if (p.kind == Kind::Field || p.kind == Kind::Core)
				{
					selected = {Kind::Field, p.index};
					dragging = false;
				}
				else if (p.kind == Kind::Lane)
				{
					// Likewise a whole lane: selected, deleted from the panel.
					selected = {Kind::Lane, p.index};
					dragging = false;
				}
				else { remove(level, p); }
			}
			rightDown = false;
		}
	}

	cursorWorld = world;

	// The Paint tool has the left button to itself.
	if (tool == Tool::Paint)
	{
		if (!io.WantCaptureKeyboard)
		{
			if (platform::isButtonPressedOn(platform::Button::Minus)) { brushRadius = std::max(brushRadius / 1.25f, 50.f); }
			if (platform::isButtonPressedOn(platform::Button::Equal)) { brushRadius = std::min(brushRadius * 1.25f, 20000.f); }
		}

		auto stamp = [&](glm::vec2 at)
		{
			level.fields[paintField].stamps.push_back({at, brushRadius, paintErasing});
			lastStamp = at;
			edited = true;
		};

		if (mouseFree && platform::isLMousePressed())
		{
			paintErasing = platform::isButtonHeld(platform::Button::Shift);
			// Into the field the brush starts on -- its middle, or half-way out,
			// so a stroke begun at a field's edge still grows it; the most
			// recent field on top. Started on open space, a new field: each
			// patch is a field with its own core. (Painting into the selected
			// field wherever the stroke began made a far-off second patch part
			// of the first, and its core landed between the two.) Erasing on
			// open space erases from the selected field; it needs one.
			paintField = -1;
			if (!paintNewField || paintErasing)
			{
				for (int i = (int)level.fields.size() - 1; i >= 0 && paintField < 0; i--)
				{
					for (int k = 0; k <= 8; k++)
					{
						const float angle = 0.7853982f * (float)k;
						const glm::vec2 probe = world + (k == 0 ? glm::vec2(0.f)
							: glm::vec2(std::cos(angle), std::sin(angle)) * (brushRadius * 0.5f));
						if (level.fields[i].contains(probe)) { paintField = i; break; }
					}
				}
			}
			if (paintField < 0 && paintErasing && selected.kind == Kind::Field) { paintField = selected.index; }
			if (!paintErasing) { paintNewField = false; }
			if (paintField < 0 && !paintErasing)
			{
				level::AsteroidField f;
				f.seed = (uint32_t)std::rand();
				level.fields.push_back(f);
				paintField = (int)level.fields.size() - 1;
			}
			if (paintField >= 0)
			{
				selected = {Kind::Field, paintField};
				painting = true;
				stamp(world);
			}
		}
		if (painting)
		{
			if (!platform::isLMouseHeld() || paintField >= (int)level.fields.size()) { painting = false; }
			else if (glm::distance(world, lastStamp) > brushRadius * 0.35f) { stamp(world); }
		}

		lastMouse = mouse;
		renderer.currentCamera.zoom = zoom;
		renderer.currentCamera.position = centre - glm::vec2(width, height) * 0.5f;
		return;
	}
	painting = false;

	if (tool != Tool::Jump) { jumpPending = false; }
	// The Jump gate tool (W5): on a gate, pick it up; anywhere else, the
	// first click places one end and the second the other.
	if (tool == Tool::Jump && mouseFree && platform::isLMousePressed())
	{
		Pick hit = pickAt(level, world, pickShipSize, pickEnemySize);
		if (hit.kind == Kind::Jump)
		{
			jumpPending = false;
			selected = hit;
			dragging = true;
			dragOffset = positionOf(level, hit) - world;
		}
		else if (!jumpPending)
		{
			jumpPending = true;
			jumpFirst = world;
		}
		else
		{
			level.jumps.push_back({jumpFirst, world});
			jumpPending = false;
			selected = {Kind::Jump, (int)level.jumps.size() - 1, -1, 1};
			edited = true;
		}
	}
	// The Lane tool (W4): on a lane's point, pick it up; anywhere else, a
	// point added to the end of the selected lane, or a new lane begun.
	else if (tool == Tool::Lane && mouseFree && platform::isLMousePressed())
	{
		Pick hit = pickAt(level, world, pickShipSize, pickEnemySize);
		if (hit.kind != Kind::LanePoint)
		{
			const bool onLane = selected.kind == Kind::Lane || selected.kind == Kind::LanePoint;
			int lane = onLane ? selected.index : -1;
			if (lane < 0)
			{
				level.lanes.push_back({});
				lane = (int)level.lanes.size() - 1;
			}
			level.lanes[(size_t)lane].points.push_back(world);
			hit = {Kind::LanePoint, lane, -1, (int)level.lanes[(size_t)lane].points.size() - 1};
			edited = true;
		}
		selected = hit;
		dragging = true;
		dragOffset = positionOf(level, hit) - world;
	}
	// The Hints tool (H2): a click puts the selected step's point there.
	else if (tool == Tool::Hint && mouseFree && platform::isLMousePressed())
	{
		if (hintSelected >= 0 && hintSelected < (int)level.hints.size())
		{
			level::HintStep &h = level.hints[(size_t)hintSelected];
			h.where = level::HintStep::Where::World;
			h.at = world;
			edited = true;
		}
	}
	// Left button: pick and drag, or place.
	else if (mouseFree && platform::isLMousePressed())
	{
		Pick hit = pickAt(level, world, pickShipSize, pickEnemySize);
		if (hit.kind == Kind::None && tool != Tool::Select)
		{
			hit = place(level, world);
			edited = true;
		}
		selected = hit;
		dragging = hit.kind != Kind::None;
		if (dragging) { dragOffset = positionOf(level, hit) - world; }
	}
	if (dragging)
	{
		if (!platform::isLMouseHeld()) { dragging = false; }
		else
		{
			const glm::vec2 to = world + dragOffset;
			if (glm::distance(to, positionOf(level, selected)) > 0.001f)
			{
				moveTo(level, selected, to);
				edited = true;
			}
		}
	}

	lastMouse = mouse;

	renderer.currentCamera.zoom = zoom;
	renderer.currentCamera.position = centre - glm::vec2(width, height) * 0.5f;
}

void drawMarkers(const level::Level &level, wgpu2d::Renderer2D &renderer, float z)
{
	const float px = 1.f / std::max(z, 0.001f);
	for (const level::Marker &m : level.markers)
	{
		renderer.renderCircleOutline(m.position, {0.3f, 0.9f, 1.f, 0.9f}, gateRadius, 3.f * px, 48);
		renderer.renderCircleOutline(m.position, {0.3f, 0.9f, 1.f, 0.5f}, gateRadius * 0.75f, 2.f * px, 48);
	}
}

void draw(const level::Level &level, wgpu2d::Renderer2D &renderer, const Look &look)
{
	pickShipSize = look.shipSize;
	pickEnemySize = look.enemySize;
	const float px = 1.f / std::max(zoom, 0.001f);

	drawMarkers(level, renderer, zoom);

	// Rocks as they will be in play, textured, with their outline: the shape
	// is what a shot hits, so it is shown. Fields show their painted area as
	// dots under their scattered rocks.
	asteroids::drawPlacements(renderer, level.asteroids, level::fieldsAsPlayed(level, gate::clearingRadius()));

	// The brush, where it would stamp: green painting, red with Shift.
	if (tool == Tool::Paint)
	{
		const bool erasing = painting ? paintErasing : platform::isButtonHeld(platform::Button::Shift);
		renderer.renderCircleOutline(cursorWorld, erasing ? glm::vec4(1.f, 0.35f, 0.3f, 0.9f)
			: glm::vec4(0.4f, 1.f, 0.6f, 0.9f), brushRadius, 2.f * px, 64);
	}

	// Jump gate pairs (W5): a violet ring at each end and a faint line between,
	// and the end of one being placed.
	for (int i = 0; i < (int)level.jumps.size(); i++)
	{
		const level::JumpPair &j = level.jumps[(size_t)i];
		const bool picked = selected.kind == Kind::Jump && selected.index == i;
		renderer.renderLine(j.a, j.b, {jumpColour.r, jumpColour.g, jumpColour.b, picked ? 0.6f : 0.25f}, 2.f * px);
		for (int end = 0; end < 2; end++)
		{
			const glm::vec2 at = end == 0 ? j.a : j.b;
			renderer.renderCircleOutline(at, jumpColour, gateRadius, 3.f * px, 48);
			if (picked && selected.point == end) { renderer.renderCircleOutline(at, {1.f, 1.f, 1.f, 1.f}, gateRadius * 1.15f, 3.f * px, 48); }
		}
	}
	if (jumpPending && tool == Tool::Jump)
	{
		renderer.renderCircleOutline(jumpFirst, jumpColour, gateRadius, 3.f * px, 48);
		renderer.renderLine(jumpFirst, cursorWorld, {jumpColour.r, jumpColour.g, jumpColour.b, 0.4f}, 2.f * px);
	}

	// The lanes (W4), the selected one brighter, with handles on its points.
	{
		const int lane = (selected.kind == Kind::Lane || selected.kind == Kind::LanePoint)
			&& validPick(level, selected) ? selected.index : -1;
		lanes::drawLanes(renderer, level.lanes, lane);
		if (lane >= 0)
		{
			const auto &points = level.lanes[(size_t)lane].points;
			for (int k = 0; k < (int)points.size(); k++)
			{
				const bool picked = selected.kind == Kind::LanePoint && selected.point == k;
				renderer.renderCircleOutline(points[(size_t)k], picked ? glm::vec4(1.f) : glm::vec4(0.35f, 0.85f, 1.f, 0.9f),
					lanePointPixels * px, 2.f * px, 16);
			}
		}
	}

	// Hint steps that point at a place (H2): a ring in the hints' colour, the
	// selected one brighter, and the ring the step will pulse if it has one.
	for (int i = 0; i < (int)level.hints.size(); i++)
	{
		const level::HintStep &h = level.hints[(size_t)i];
		if (h.where != level::HintStep::Where::World) { continue; }
		const bool picked = tool == Tool::Hint && i == hintSelected;
		const glm::vec4 colour = {textLook::hintColour, picked ? 1.f : 0.45f};
		renderer.renderCircleOutline(h.at, colour, 14.f * px, 3.f * px, 16);
		if (h.ring > 0.f) { renderer.renderCircleOutline(h.at, colour, h.ring, 2.f * px, 64); }
	}

	// The closing circle's rings, in the order they close: largest first. Each
	// a little whiter than the last, red if it is not inside the one before,
	// with a handle at its centre.
	{
		std::vector<int> order((size_t)level.rings.size());
		for (int i = 0; i < (int)order.size(); i++) { order[i] = i; }
		std::sort(order.begin(), order.end(),
			[&](int a, int b) { return level.rings[a].radius > level.rings[b].radius; });
		glm::vec2 outerCentre = {};
		float outerRadius = level.arenaRadius;
		for (int n = 0; n < (int)order.size(); n++)
		{
			const level::Ring &r = level.rings[order[n]];
			const bool fits = contains(outerCentre, outerRadius, r.position, r.radius);
			const float k = (n + 1.f) / (float)order.size();
			const glm::vec4 colour = fits ? glm::vec4(1.f, 0.55f + 0.45f * k, 0.3f + 0.7f * k, 0.9f)
				: glm::vec4(1.f, 0.15f, 0.15f, 1.f);
			renderer.renderCircleOutline(r.position, colour, r.radius, 3.f * px, 256);
			renderer.renderCircleOutline(r.position, colour, ringHandlePixels * px, 2.f * px, 24);
			outerCentre = r.position;
			outerRadius = r.radius;
		}
	}


	for (const level::EnemyPlacement &e : level.enemies)
	{
		const bool sniper = e.behaviour == Enemy::Behaviour::KeepDistance;
		const bool boss = e.behaviour == Enemy::Behaviour::Boss;
		renderSpaceShip(renderer, e.position, look.enemySize / enemyShipSize * enemyAi::sizeOf(e.behaviour),
			look.shipSheet, boss ? look.bossCell : sniper ? look.sniperCell : look.rusherCell,
			level::direction(e.facingDegrees));
	}

	renderSpaceShip(renderer, level.start, look.shipSize, look.shipSheet, look.playerCell,
		level::direction(level.startFacingDegrees));
	renderer.renderCircleOutline(level.start, {0.3f, 1.f, 0.4f, 0.8f}, look.shipSize * 0.6f, 2.f * px, 32);

	// A selected field's cores: a handle on each, so they can be seen to be
	// draggable. The selected one white.
	if ((selected.kind == Kind::Field || selected.kind == Kind::Core) && validPick(level, selected))
	{
		const level::AsteroidField &f = level.fields[selected.index];
		const std::vector<glm::vec2> cores = asteroids::fieldCores(f);
		for (int c = 0; c < (int)cores.size(); c++)
		{
			const bool picked = selected.kind == Kind::Core && selected.core == c;
			renderer.renderCircleOutline(cores[(size_t)c], picked ? glm::vec4(1.f) : glm::vec4(1.f, 0.8f, 0.3f, 0.9f),
				asteroids::coreRadius(f, c) * 0.5f, 3.f * px, 48);
		}
	}

	if (selected.kind != Kind::None && selected.kind != Kind::Field && selected.kind != Kind::Core
		&& validPick(level, selected))
	{
		float radius = look.enemySize * 0.6f;
		if (selected.kind == Kind::Start) { radius = look.shipSize * 0.7f; }
		if (selected.kind == Kind::Marker) { radius = gateRadius * 1.15f; }
		if (selected.kind == Kind::Ring) { radius = ringHandlePixels * 1.5f * px; }
		if (selected.kind == Kind::Asteroid) { radius = level.asteroids[selected.index].radius * 1.4f; }
		if (selected.kind == Kind::Scenery) { radius = level.scenery[selected.index].size * 0.55f; }
		renderer.renderCircleOutline(positionOf(level, selected), {1.f, 1.f, 1.f, 1.f}, radius, 3.f * px, 48);
	}
}

namespace
{
	// The Hints tool's panel (H2): the level's script, step by step.
	void hintsPanel(level::Level &level)
	{
		using level::HintStep;
		std::vector<HintStep> &steps = level.hints;
		ImGui::TextDisabled("Steps run in order every round. {action} in the text becomes its keys, e.g. {weapon4}");
		ImGui::TextDisabled("L: put the selected step's point where you click");

		if (ImGui::BeginListBox("##hintSteps", ImVec2(-1.f, 6.f * ImGui::GetTextLineHeightWithSpacing())))
		{
			for (int i = 0; i < (int)steps.size(); i++)
			{
				const std::string label = std::to_string(i + 1) + ". " + steps[(size_t)i].text + "##" + std::to_string(i);
				if (ImGui::Selectable(label.c_str(), i == hintSelected)) { hintSelected = i; }
			}
			ImGui::EndListBox();
		}
		if (ImGui::Button("Add step"))
		{
			HintStep h;
			h.text = "NEW STEP";
			steps.insert(steps.begin() + (hintSelected >= 0 ? hintSelected + 1 : (int)steps.size()), h);
			hintSelected = hintSelected >= 0 ? hintSelected + 1 : (int)steps.size() - 1;
			edited = true;
		}
		const bool valid = hintSelected >= 0 && hintSelected < (int)steps.size();
		ImGui::BeginDisabled(!valid);
		ImGui::SameLine();
		if (ImGui::Button("Delete") && valid)
		{
			steps.erase(steps.begin() + hintSelected);
			hintSelected = std::min(hintSelected, (int)steps.size() - 1);
			edited = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Up") && valid && hintSelected > 0)
		{
			std::swap(steps[(size_t)hintSelected], steps[(size_t)hintSelected - 1]);
			hintSelected--;
			edited = true;
		}
		ImGui::SameLine();
		if (ImGui::Button("Down") && valid && hintSelected + 1 < (int)steps.size())
		{
			std::swap(steps[(size_t)hintSelected], steps[(size_t)hintSelected + 1]);
			hintSelected++;
			edited = true;
		}
		ImGui::EndDisabled();
		if (!valid) { hintSynced = -2; return; }

		HintStep &h = steps[(size_t)hintSelected];
		if (hintSynced != hintSelected)
		{
			hintSynced = hintSelected;
			std::snprintf(hintText, sizeof(hintText), "%s", h.text.c_str());
			std::string joined;
			for (const std::string &a : h.args) { joined += (joined.empty() ? "" : " ") + a; }
			std::snprintf(hintArgs, sizeof(hintArgs), "%s", joined.c_str());
		}

		if (ImGui::InputText("Text", hintText, sizeof(hintText)))
		{
			// No '#': the level file would read the rest of the line as a comment.
			for (char *c = hintText; *c; c++) { if (*c == '#') { *c = ' '; } }
			h.text = hintText;
			edited = true;
		}

		int where = (int)h.where;
		ImGui::RadioButton("Top of screen", &where, (int)HintStep::Where::Screen); ImGui::SameLine();
		ImGui::RadioButton("A place", &where, (int)HintStep::Where::World); ImGui::SameLine();
		ImGui::RadioButton("The HUD", &where, (int)HintStep::Where::Hud);
		if (where != (int)h.where) { h.where = (HintStep::Where)where; edited = true; }
		if (h.where == HintStep::Where::World)
		{
			if (ImGui::DragFloat2("Point", &h.at.x, 10.f, -1e6f, 1e6f, "%.0f")) { edited = true; }
			if (ImGui::DragFloat("Ring", &h.ring, 10.f, 0.f, 50000.f, "%.0f")) { edited = true; }
		}
		if (h.where == HintStep::Where::Hud)
		{
			const std::vector<std::string> &names = hintScript::hudElements();
			if (ImGui::BeginCombo("Element", h.hud.empty() ? "(choose)" : h.hud.c_str()))
			{
				for (const std::string &n : names)
				{
					if (ImGui::Selectable(n.c_str(), n == h.hud)) { h.hud = n; edited = true; }
				}
				ImGui::EndCombo();
			}
		}

		const std::vector<std::string> &words = hintScript::conditions();
		if (ImGui::BeginCombo("Until", h.until.empty() ? "(skip only)" : h.until.c_str()))
		{
			for (const std::string &w : words)
			{
				if (ImGui::Selectable(w.empty() ? "(skip only)" : w.c_str(), w == h.until)) { h.until = w; edited = true; }
			}
			ImGui::EndCombo();
		}
		if (ImGui::InputText("Arguments", hintArgs, sizeof(hintArgs)))
		{
			h.args.clear();
			std::istringstream in(hintArgs);
			std::string a;
			while (in >> a) { h.args.push_back(a); }
			edited = true;
		}
		ImGui::TextDisabled("%s", hintScript::conditionArgs(h.until));
		if (h.until == "near" && h.where == HintStep::Where::World && ImGui::SmallButton("Near the step's point"))
		{
			const float r = h.ring > 0.f ? h.ring : 1500.f;
			std::snprintf(hintArgs, sizeof(hintArgs), "%.0f %.0f %.0f", h.at.x, h.at.y, r);
			h.args = {std::to_string((int)h.at.x), std::to_string((int)h.at.y), std::to_string((int)r)};
			edited = true;
		}
	}
}

Request debugUi(level::Level &level, bool unsaved)
{
	Request request = Request::None;

	if (ImGui::Button("Save")) { request = Request::Save; }
	ImGui::SameLine();
	if (ImGui::Button("Reload")) { request = Request::Reload; }
	ImGui::SameLine();
	if (ImGui::Button("Test from here")) { request = Request::TestHere; }
	ImGui::SameLine();
	if (ImGui::Button("Exit")) { request = Request::Exit; }
	if (unsaved) { ImGui::TextColored({1.f, 0.7f, 0.2f, 1.f}, "Unsaved changes"); }
	ImGui::TextDisabled("L: select/drag/place  R: delete (a field: select)  R-drag/WASD: pan  wheel: zoom");

	if (ImGui::DragFloat("Arena radius", &level.arenaRadius, 50.f, 1000.f, 100000.f, "%.0f")) { edited = true; }
	ImGui::Text("Fields cover %.0f%% of the arena", fieldShare(level) * 100.f);
	ImGui::SameLine();
	ImGui::TextDisabled("(gate clearings cut out)");

	int t = (int)tool;
	ImGui::RadioButton("Select", &t, (int)Tool::Select); ImGui::SameLine();
	ImGui::RadioButton("Rusher", &t, (int)Tool::Rusher); ImGui::SameLine();
	ImGui::RadioButton("Sniper", &t, (int)Tool::Sniper); ImGui::SameLine();
	ImGui::RadioButton("Boss", &t, (int)Tool::Boss);
	ImGui::RadioButton("Gate", &t, (int)Tool::Gate); ImGui::SameLine();
	ImGui::RadioButton("Ring", &t, (int)Tool::Ring); ImGui::SameLine();
	ImGui::RadioButton("Asteroid", &t, (int)Tool::Asteroid); ImGui::SameLine();
	ImGui::RadioButton("Paint field", &t, (int)Tool::Paint); ImGui::SameLine();
	ImGui::RadioButton("Scenery", &t, (int)Tool::Scenery); ImGui::SameLine();
	ImGui::RadioButton("Lane", &t, (int)Tool::Lane); ImGui::SameLine();
	ImGui::RadioButton("Jump gates", &t, (int)Tool::Jump); ImGui::SameLine();
	ImGui::RadioButton("Hints", &t, (int)Tool::Hint);
	tool = (Tool)t;
	if (tool == Tool::Hint) { hintsPanel(level); }
	if (tool == Tool::Jump)
	{
		ImGui::TextDisabled(jumpPending ? "L: place the other end" : "L: place one end of a pair, then the other");
		ImGui::TextDisabled("L-drag a gate to move it  R on a gate: delete the pair");
	}
	if (tool == Tool::Lane)
	{
		ImGui::TextDisabled("L: add a point to the selected lane (none selected: a new lane)");
		ImGui::TextDisabled("L-drag a point to move it  R on a point: delete it");
		if (ImGui::Button("Start a new lane")) { selected = {}; }
	}
	if (tool == Tool::Paint)
	{
		ImGui::TextDisabled("L-drag: paint  Shift+L-drag: erase  -/=: brush size");
		ImGui::TextDisabled("Started on a field, a stroke grows it; on open space, it starts a new one");
		ImGui::SliderFloat("Brush", &brushRadius, 50.f, 20000.f, "%.0f", ImGuiSliderFlags_Logarithmic);
		if (paintNewField) { ImGui::TextDisabled("The next stroke starts a new field"); }
		else if (ImGui::Button("Start a new field")) { paintNewField = true; selected = {}; }
		ImGui::SameLine();
		ImGui::TextDisabled("(even over another's paint)");

		// W1: paint the clearings, not the field. One stamp the arena's size,
		// first in the field's list, so what has been erased stays erased.
		if (ImGui::Button("Fill the arena"))
		{
			int target = (selected.kind == Kind::Field || selected.kind == Kind::Core) ? selected.index : -1;
			if (target < 0)
			{
				level::AsteroidField f;
				f.seed = (uint32_t)std::rand();
				level.fields.push_back(f);
				target = (int)level.fields.size() - 1;
			}
			std::vector<level::FieldStamp> &stamps = level.fields[(size_t)target].stamps;
			stamps.insert(stamps.begin(), {{0.f, 0.f}, level.arenaRadius, false});
			selected = {Kind::Field, target};
			paintNewField = false;
			edited = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("into the selected field, or a new one; then Shift+drag carves");
	}
	if (tool == Tool::Scenery)
	{
		if (ImGui::BeginCombo("Art", scenery::artName(sceneryArt)))
		{
			for (int i = 0; i < scenery::artCount(); i++)
			{
				if (ImGui::Selectable(scenery::artName(i), i == sceneryArt)) { sceneryArt = i; }
			}
			ImGui::EndCombo();
		}
	}

	ImGui::Separator();
	if (!validPick(level, selected))
	{
		ImGui::TextDisabled("Nothing selected");
		return request;
	}

	switch (selected.kind)
	{
	case Kind::Start:
		ImGui::Text("Player start");
		if (ImGui::DragFloat2("Position", &level.start.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		if (ImGui::SliderFloat("Facing", &level.startFacingDegrees, -180.f, 180.f, "%.0f deg")) { edited = true; }
		break;
	case Kind::Enemy:
	{
		level::EnemyPlacement &e = level.enemies[selected.index];
		int kind = (int)e.behaviour; // CloseIn, KeepDistance, Boss
		if (ImGui::Combo("Kind", &kind, "Rusher\0Sniper\0Boss\0"))
		{
			e.behaviour = (Enemy::Behaviour)kind;
			edited = true;
		}
		if (ImGui::DragFloat2("Position", &e.position.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		if (ImGui::SliderFloat("Facing", &e.facingDegrees, -180.f, 180.f, "%.0f deg")) { edited = true; }

		// A choice of no, yes, or rolled each round at its chance.
		auto choiceCombo = [&](const char *label, AbilityChoice &choice)
		{
			int c = (int)choice;
			if (ImGui::Combo(label, &c, "No\0Yes\0Random\0"))
			{
				choice = (AbilityChoice)c;
				edited = true;
			}
		};

		// Its weapons (B2), slot by slot: each one of the shared four or
		// rolled, each modifier no, yes or rolled. Kept on the placement, so
		// they are there when the enemy is selected again, and written with
		// the level on Save. With none, they are rolled -- one weapon for an
		// ordinary enemy, sometimes two; two to four for a boss.
		ImGui::SeparatorText("Weapons");
		if (e.guns.empty()) { ImGui::TextDisabled("Rolled. Add one to choose them."); }
		int removeAt = -1;
		for (int g = 0; g < (int)e.guns.size(); g++)
		{
			GunChoice &gun = e.guns[g];
			ImGui::PushID(g);
			ImGui::SetNextItemWidth(150.f);
			if (ImGui::BeginCombo("##kind", gun.weapon < 0 ? "Random" : weapons::shipWeapon(gun.weapon).name))
			{
				if (ImGui::Selectable("Random", gun.weapon < 0)) { gun.weapon = -1; edited = true; }
				for (int i = 0; i < weapons::slotCount; i++)
				{
					if (ImGui::Selectable(weapons::shipWeapon(i).name, gun.weapon == i)) { gun.weapon = i; edited = true; }
				}
				ImGui::EndCombo();
			}
			ImGui::SameLine();
			if (ImGui::SmallButton("remove")) { removeAt = g; }
			ImGui::Indent();
			choiceCombo("Stun", gun.stun);
			choiceCombo("Lockdown", gun.lockdown);
			choiceCombo("Spread", gun.spread);
			ImGui::Unindent();
			ImGui::PopID();
		}
		if (removeAt >= 0) { e.guns.erase(e.guns.begin() + removeAt); edited = true; }
		ImGui::BeginDisabled((int)e.guns.size() >= weapons::slotCount);
		if (ImGui::SmallButton("Add weapon")) { e.guns.push_back({}); edited = true; }
		ImGui::EndDisabled();

		// Its abilities (B1): on the placement and saved with the level, as
		// the weapons are. A boss has them all; its phases decide when.
		ImGui::SeparatorText("Abilities");
		if (e.behaviour == Enemy::Behaviour::Boss)
		{
			ImGui::TextDisabled("A boss has the shield, cloak and ram; it rams and cloaks from phase 2");
		}
		else
		{
			choiceCombo("Shield", e.shield);
			choiceCombo("Cloak", e.cloak);
			choiceCombo("Ram", e.ram);
		}

		// And its class's tuning, here beside it -- shared by every enemy of
		// the class, and not saved with the level: it is the game's tuning.
		if (ImGui::TreeNode(e.behaviour == Enemy::Behaviour::Boss ? "Boss tuning"
			: e.behaviour == Enemy::Behaviour::KeepDistance ? "Sniper tuning" : "Rusher tuning"))
		{
			enemyAi::classUi(e.behaviour);
			ImGui::TreePop();
		}
		break;
	}
	case Kind::Marker:
	{
		level::Marker &m = level.markers[selected.index];
		ImGui::Text("Extraction gate");
		if (ImGui::DragFloat2("Position", &m.position.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		break;
	}
	case Kind::Ring:
	{
		level::Ring &r = level.rings[selected.index];
		ImGui::Text("Closing circle ring");
		ImGui::TextDisabled("Closes largest first; red if not inside the one before");
		if (ImGui::DragFloat2("Centre", &r.position.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		if (ImGui::DragFloat("Radius", &r.radius, 10.f, 100.f, 100000.f, "%.0f")) { edited = true; }
		break;
	}
	case Kind::Asteroid:
	{
		level::Asteroid &a = level.asteroids[selected.index];
		ImGui::Text("Asteroid");
		if (ImGui::DragFloat2("Position", &a.position.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		if (ImGui::DragFloat("Radius", &a.radius, 10.f, 100.f, 20000.f, "%.0f")) { edited = true; }
		if (ImGui::InputScalar("Seed", ImGuiDataType_U32, &a.seed)) { edited = true; }
		ImGui::SameLine();
		if (ImGui::Button("Reroll")) { a.seed = (uint32_t)std::rand(); edited = true; }
		break;
	}
	case Kind::Field:
	{
		level::AsteroidField &f = level.fields[selected.index];
		ImGui::Text("Asteroid field %d: %d brush stamps", selected.index, (int)f.stamps.size());
		if (ImGui::InputScalar("Seed", ImGuiDataType_U32, &f.seed)) { edited = true; }
		ImGui::SameLine();
		if (ImGui::Button("Reroll")) { f.seed = (uint32_t)std::rand(); edited = true; }
		if (ImGui::SliderFloat("Max size", &f.maxSize, 40.f, 3000.f, "%.0f", ImGuiSliderFlags_Logarithmic)) { edited = true; }
		// Logarithmic, so the small end -- where the look changes most -- gets
		// most of the slider's travel.
		if (ImGui::SliderFloat("Max gap", &f.maxGap, 0.f, 1500.f, "%.0f", ImGuiSliderFlags_Logarithmic)) { edited = true; }
		ImGui::TextDisabled("Select tool: drag inside it to move the whole field");
		ImGui::TextDisabled("Its cores (the rings) drag on their own");
		ImGui::TextDisabled("A core fits inside the paint: paint wider for a bigger one");

		ImGui::SeparatorText("Cores");
		if (f.cores.empty()) { ImGui::TextDisabled("One, at the painted area's middle"); }
		else { ImGui::Text("%d placed by hand", (int)f.cores.size()); }
		// A new core at the middle of the view: the middle's own, if there was
		// only that, is kept where it was.
		if (ImGui::Button("Add a core here"))
		{
			if (f.cores.empty()) { f.cores = asteroids::fieldCores(f); }
			f.cores.push_back(centre);
			selected = {Kind::Core, selected.index, (int)f.cores.size() - 1};
			edited = true;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(at the middle of the view)");
		if (!f.cores.empty() && ImGui::Button("Back to one at the middle")) { f.cores.clear(); edited = true; }
		break;
	}
	case Kind::Core:
	{
		level::AsteroidField &f = level.fields[selected.index];
		ImGui::Text("Core %d of field %d", selected.core + 1, selected.index);
		if (f.cores.empty())
		{
			ImGui::TextDisabled("At the painted area's middle; drag it to place it by hand");
			break;
		}
		ImGui::TextDisabled("Placed by hand; %d in this field", (int)f.cores.size());
		if (ImGui::Button("Remove this core")) { remove(level, selected); break; }
		ImGui::SameLine();
		if (ImGui::Button("Back to one at the middle")) { f.cores.clear(); selected = {Kind::Field, selected.index}; edited = true; }
		break;
	}
	case Kind::Jump:
	{
		level::JumpPair &j = level.jumps[selected.index];
		ImGui::Text("Jump gate pair %d", selected.index);
		if (ImGui::DragFloat2("End A", &j.a.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		if (ImGui::DragFloat2("End B", &j.b.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		ImGui::TextDisabled("%.0f apart; each end sits in a clearing", glm::distance(j.a, j.b));
		break;
	}
	case Kind::Lane:
	case Kind::LanePoint:
	{
		level::Lane &l = level.lanes[selected.index];
		ImGui::Text("Lane %d: %d points", selected.index, (int)l.points.size());
		if (ImGui::DragFloat("Width", &l.width, 10.f, 200.f, 10000.f, "%.0f")) { edited = true; }
		if (ImGui::SliderFloat("Speed", &l.speed, 1.f, 6.f, "%.2f x a ship's own")) { edited = true; }
		if (selected.kind == Kind::LanePoint)
		{
			if (ImGui::DragFloat2("Point", &l.points[(size_t)selected.point].x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
			if (ImGui::Button("Delete this point")) { remove(level, selected); break; }
		}
		ImGui::TextDisabled("It cuts through any field; ships ride it either way");
		ImGui::TextDisabled("Select tool: drag along it to move the whole lane");
		if (ImGui::Button("Delete lane")) { remove(level, {Kind::Lane, selected.index}); }
		break;
	}
	case Kind::Scenery:
	{
		level::Scenery &s = level.scenery[selected.index];
		if (ImGui::BeginCombo("Art", s.art.c_str()))
		{
			for (int i = 0; i < scenery::artCount(); i++)
			{
				if (ImGui::Selectable(scenery::artName(i), s.art == scenery::artName(i)))
				{
					s.art = scenery::artName(i);
					edited = true;
				}
			}
			ImGui::EndCombo();
		}
		if (ImGui::DragFloat2("Position", &s.position.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		if (ImGui::DragFloat("Size", &s.size, 10.f, 50.f, 50000.f, "%.0f")) { edited = true; }
		if (ImGui::SliderFloat("Depth", &s.depth, 0.f, 0.95f, "%.2f")) { edited = true; }
		break;
	}
	default: break;
	}

	if (validPick(level, selected) && selected.kind != Kind::Start && selected.kind != Kind::Core
		&& selected.kind != Kind::Lane && selected.kind != Kind::LanePoint
		&& ImGui::Button(selected.kind == Kind::Field ? "Delete field" : "Delete"))
	{
		remove(level, selected);
	}

	return request;
}

}
