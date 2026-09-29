#include <levelEditor.h>

#include <resources.h>
#include <asteroids.h>
#include <scenery.h>
#include <shipSprite.h>
#include "platformInput.h"
#include "imgui.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
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

	enum class Tool { Select, Rusher, Sniper, Resource, Gate, Ring, Asteroid, Paint, Scenery };
	// A deposit's ring in the editor, matching how big one is in play.
	constexpr float depositRadius = 320.f;
	Tool tool = Tool::Select;
	int sceneryArt = 0;

	enum class Kind { None, Start, Enemy, Resource, Marker, Ring, Asteroid, Field, Core, Scenery };
	struct Pick
	{
		Kind kind = Kind::None;
		int index = -1;
	};
	Pick selected;

	// The Paint tool (asteroid fields, A1b). A stroke lays stamps -- circles of
	// the brush's size -- whenever the cursor has moved a third of the brush
	// since the last one, so a stroke is a chain of overlapping circles. With
	// Shift held when it starts, it lays erasers instead.
	float brushRadius = 600.f;
	bool painting = false;
	bool paintErasing = false;
	int paintField = -1;
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
		case Kind::Resource: return level.resources[p.index].position;
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
		case Kind::Core: return asteroids::fieldCore(level.fields[p.index]);
		case Kind::Scenery: return drawnAt(level.scenery[p.index]);
		default: return {};
		}
	}

	void moveTo(level::Level &level, Pick p, glm::vec2 to)
	{
		switch (p.kind)
		{
		case Kind::Start: level.start = to; break;
		case Kind::Enemy: level.enemies[p.index].position = to; break;
		case Kind::Resource: level.resources[p.index].position = to; break;
		case Kind::Marker: level.markers[p.index].position = to; break;
		case Kind::Ring: level.rings[p.index].position = to; break;
		case Kind::Asteroid: level.asteroids[p.index].position = to; break;
		case Kind::Field:
		{
			level::AsteroidField &f = level.fields[p.index];
			if (f.stamps.empty()) { break; }
			const glm::vec2 by = to - f.stamps.front().position;
			for (level::FieldStamp &s : f.stamps) { s.position += by; }
			if (f.coreMoved) { f.core += by; } // a dragged core comes along
			break;
		}
		case Kind::Core:
		{
			level::AsteroidField &f = level.fields[p.index];
			f.core = to;
			f.coreMoved = true;
			break;
		}
		case Kind::Scenery:
		{
			level::Scenery &s = level.scenery[p.index];
			s.position = placedFrom(to, s.depth);
			break;
		}
		default: break;
		}
	}

	// The nearest thing whose shape contains `at`. Ships and markers before
	// scenery: a planet is big and behind everything, and should not steal a
	// click aimed at a ship in front of it.
	// Fields are picked last among the placements, by being inside the painted
	// area, and only when asked: a right click deletes what it picks, and one
	// stray click should not wipe a whole painted field.
	Pick pickAt(const level::Level &level, glm::vec2 at, float shipSize, float enemySize,
		bool includeFields = true)
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
			consider(Kind::Enemy, i, level.enemies[i].position, enemySize * 0.5f);
		}
		for (int i = 0; i < (int)level.resources.size(); i++)
		{
			consider(Kind::Resource, i, level.resources[i].position, depositRadius);
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
		if (best.kind != Kind::None) { return best; }

		if (includeFields)
		{
			// A field's core, before the field it sits in.
			for (int i = (int)level.fields.size() - 1; i >= 0; i--)
			{
				const level::AsteroidField &f = level.fields[i];
				if (glm::distance(at, asteroids::fieldCore(f)) <= f.maxSize * 1.5f) { return {Kind::Core, i}; }
			}
			// The most recently made on top.
			for (int i = (int)level.fields.size() - 1; i >= 0; i--)
			{
				if (level.fields[i].contains(at)) { return {Kind::Field, i}; }
			}
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
		case Kind::Resource: level.resources.erase(level.resources.begin() + p.index); break;
		case Kind::Marker: level.markers.erase(level.markers.begin() + p.index); break;
		case Kind::Ring: level.rings.erase(level.rings.begin() + p.index); break;
		case Kind::Asteroid: level.asteroids.erase(level.asteroids.begin() + p.index); break;
		case Kind::Field: level.fields.erase(level.fields.begin() + p.index); break;
		case Kind::Core:
			// A core is not removed, only sent back to the middle.
			level.fields[p.index].coreMoved = false;
			edited = true;
			return;
		case Kind::Scenery: level.scenery.erase(level.scenery.begin() + p.index); break;
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
		{
			level::EnemyPlacement e;
			e.behaviour = tool == Tool::Sniper ? Enemy::Behaviour::KeepDistance : Enemy::Behaviour::CloseIn;
			e.position = at;
			level.enemies.push_back(e);
			return {Kind::Enemy, (int)level.enemies.size() - 1};
		}
		case Tool::Resource:
		{
			level::Resource r;
			r.position = at;
			r.amount = resources::defaultAmount();
			level.resources.push_back(r);
			return {Kind::Resource, (int)level.resources.size() - 1};
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
		case Kind::Resource: return p.index >= 0 && p.index < (int)level.resources.size();
		case Kind::Marker: return p.index >= 0 && p.index < (int)level.markers.size();
		case Kind::Ring: return p.index >= 0 && p.index < (int)level.rings.size();
		case Kind::Asteroid: return p.index >= 0 && p.index < (int)level.asteroids.size();
		case Kind::Field:
		case Kind::Core: return p.index >= 0 && p.index < (int)level.fields.size();
		case Kind::Scenery: return p.index >= 0 && p.index < (int)level.scenery.size();
		default: return false;
		}
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
				remove(level, pickAt(level, screenToWorld(rightStart, width, height),
					pickShipSize, pickEnemySize, false));
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
			// Into the selected field; else the one under the brush; else,
			// painting, a new one. Erasing needs a field to erase from.
			paintField = selected.kind == Kind::Field ? selected.index : -1;
			if (paintField < 0)
			{
				for (int i = (int)level.fields.size() - 1; i >= 0; i--)
				{
					if (level.fields[i].contains(world)) { paintField = i; break; }
				}
			}
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

	// Left button: pick and drag, or place.
	if (mouseFree && platform::isLMousePressed())
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
	asteroids::drawPlacements(renderer, level.asteroids, level.fields);

	// The brush, where it would stamp: green painting, red with Shift.
	if (tool == Tool::Paint)
	{
		const bool erasing = painting ? paintErasing : platform::isButtonHeld(platform::Button::Shift);
		renderer.renderCircleOutline(cursorWorld, erasing ? glm::vec4(1.f, 0.35f, 0.3f, 0.9f)
			: glm::vec4(0.4f, 1.f, 0.6f, 0.9f), brushRadius, 2.f * px, 64);
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

	// Deposits: a gold ring, with a second one showing how much is in it.
	for (const level::Resource &r : level.resources)
	{
		renderer.renderCircleOutline(r.position, {1.f, 0.8f, 0.2f, 0.9f}, depositRadius, 3.f * px, 32);
		const float fill = std::clamp(r.amount / 20.f, 0.05f, 1.f);
		renderer.renderCircleOutline(r.position, {1.f, 0.85f, 0.35f, 0.6f},
			depositRadius * fill, 2.f * px, 24);
	}

	for (const level::EnemyPlacement &e : level.enemies)
	{
		const bool sniper = e.behaviour == Enemy::Behaviour::KeepDistance;
		renderSpaceShip(renderer, e.position, look.enemySize, look.shipSheet,
			sniper ? look.sniperCell : look.rusherCell, level::direction(e.facingDegrees));
	}

	renderSpaceShip(renderer, level.start, look.shipSize, look.shipSheet, look.playerCell,
		level::direction(level.startFacingDegrees));
	renderer.renderCircleOutline(level.start, {0.3f, 1.f, 0.4f, 0.8f}, look.shipSize * 0.6f, 2.f * px, 32);

	// A selected field's core: a handle, so it can be seen to be draggable.
	if ((selected.kind == Kind::Field || selected.kind == Kind::Core) && validPick(level, selected))
	{
		const level::AsteroidField &f = level.fields[selected.index];
		renderer.renderCircleOutline(asteroids::fieldCore(f),
			selected.kind == Kind::Core ? glm::vec4(1.f) : glm::vec4(1.f, 0.8f, 0.3f, 0.9f),
			f.maxSize * 1.5f, 3.f * px, 48);
	}

	if (selected.kind != Kind::None && selected.kind != Kind::Field && selected.kind != Kind::Core
		&& validPick(level, selected))
	{
		float radius = look.enemySize * 0.6f;
		if (selected.kind == Kind::Start) { radius = look.shipSize * 0.7f; }
		if (selected.kind == Kind::Resource) { radius = depositRadius * 1.2f; }
		if (selected.kind == Kind::Marker) { radius = gateRadius * 1.15f; }
		if (selected.kind == Kind::Ring) { radius = ringHandlePixels * 1.5f * px; }
		if (selected.kind == Kind::Asteroid) { radius = level.asteroids[selected.index].radius * 1.4f; }
		if (selected.kind == Kind::Scenery) { radius = level.scenery[selected.index].size * 0.55f; }
		renderer.renderCircleOutline(positionOf(level, selected), {1.f, 1.f, 1.f, 1.f}, radius, 3.f * px, 48);
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
	ImGui::TextDisabled("L: select/drag/place  R: delete  R-drag/WASD: pan  wheel: zoom");

	if (ImGui::DragFloat("Arena radius", &level.arenaRadius, 50.f, 1000.f, 100000.f, "%.0f")) { edited = true; }

	int t = (int)tool;
	ImGui::RadioButton("Select", &t, (int)Tool::Select); ImGui::SameLine();
	ImGui::RadioButton("Rusher", &t, (int)Tool::Rusher); ImGui::SameLine();
	ImGui::RadioButton("Sniper", &t, (int)Tool::Sniper);
	ImGui::RadioButton("Resource", &t, (int)Tool::Resource); ImGui::SameLine();
	ImGui::RadioButton("Gate", &t, (int)Tool::Gate); ImGui::SameLine();
	ImGui::RadioButton("Ring", &t, (int)Tool::Ring); ImGui::SameLine();
	ImGui::RadioButton("Asteroid", &t, (int)Tool::Asteroid); ImGui::SameLine();
	ImGui::RadioButton("Paint field", &t, (int)Tool::Paint); ImGui::SameLine();
	ImGui::RadioButton("Scenery", &t, (int)Tool::Scenery);
	tool = (Tool)t;
	if (tool == Tool::Paint)
	{
		ImGui::TextDisabled("L-drag: paint  Shift+L-drag: erase  -/=: brush size");
		ImGui::TextDisabled("Paints the selected field, the one under the brush, or a new one");
		ImGui::SliderFloat("Brush", &brushRadius, 50.f, 20000.f, "%.0f", ImGuiSliderFlags_Logarithmic);
		if (selected.kind == Kind::Field && ImGui::Button("Start a new field")) { selected = {}; }
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
		int kind = e.behaviour == Enemy::Behaviour::KeepDistance ? 1 : 0;
		if (ImGui::Combo("Kind", &kind, "Rusher\0Sniper\0"))
		{
			e.behaviour = kind ? Enemy::Behaviour::KeepDistance : Enemy::Behaviour::CloseIn;
			edited = true;
		}
		if (ImGui::DragFloat2("Position", &e.position.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		if (ImGui::SliderFloat("Facing", &e.facingDegrees, -180.f, 180.f, "%.0f deg")) { edited = true; }
		break;
	}
	case Kind::Resource:
	{
		level::Resource &r = level.resources[selected.index];
		ImGui::Text("Deposit");
		if (ImGui::DragFloat2("Position", &r.position.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
		if (ImGui::DragFloat("Amount", &r.amount, 0.25f, 0.5f, 100.f, "%.1f")) { edited = true; }
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
		ImGui::TextDisabled("Its core (the ring) drags on its own");
		break;
	}
	case Kind::Core:
	{
		level::AsteroidField &f = level.fields[selected.index];
		ImGui::Text("Core of field %d", selected.index);
		ImGui::TextDisabled(f.coreMoved ? "Placed by hand" : "At the painted area's middle");
		if (f.coreMoved && ImGui::Button("Back to the middle")) { f.coreMoved = false; edited = true; }
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

	if (selected.kind != Kind::Start && ImGui::Button("Delete"))
	{
		remove(level, selected);
	}

	return request;
}

}
