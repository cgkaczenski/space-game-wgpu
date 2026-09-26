#include <levelEditor.h>

#include <scenery.h>
#include <shipSprite.h>
#include "platformInput.h"
#include "imgui.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

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

	enum class Tool { Select, Rusher, Sniper, Resource, Gate, Scenery };
	Tool tool = Tool::Select;
	int sceneryArt = 0;

	enum class Kind { None, Start, Enemy, Marker, Scenery };
	struct Pick
	{
		Kind kind = Kind::None;
		int index = -1;
	};
	Pick selected;

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

	float markerRadius(level::Marker::Kind k)
	{
		return k == level::Marker::Kind::Gate ? 600.f : 250.f;
	}

	glm::vec2 positionOf(const level::Level &level, Pick p)
	{
		switch (p.kind)
		{
		case Kind::Start: return level.start;
		case Kind::Enemy: return level.enemies[p.index].position;
		case Kind::Marker: return level.markers[p.index].position;
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
		case Kind::Marker: level.markers[p.index].position = to; break;
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
			consider(Kind::Enemy, i, level.enemies[i].position, enemySize * 0.5f);
		}
		for (int i = 0; i < (int)level.markers.size(); i++)
		{
			consider(Kind::Marker, i, level.markers[i].position, markerRadius(level.markers[i].kind));
		}
		if (best.kind != Kind::None) { return best; }

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
		case Tool::Gate:
		{
			level::Marker m;
			m.kind = tool == Tool::Gate ? level::Marker::Kind::Gate : level::Marker::Kind::Resource;
			m.position = at;
			level.markers.push_back(m);
			return {Kind::Marker, (int)level.markers.size() - 1};
		}
		case Tool::Scenery:
		{
			level::Scenery s;
			s.art = scenery::artName(sceneryArt);
			s.position = at; // depth 0: drawn where it is placed
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
					pickShipSize, pickEnemySize));
			}
			rightDown = false;
		}
	}

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
		if (m.kind == level::Marker::Kind::Resource)
		{
			renderer.renderCircleOutline(m.position, {1.f, 0.8f, 0.2f, 0.9f}, 250.f, 3.f * px, 32);
		}
		else
		{
			renderer.renderCircleOutline(m.position, {0.3f, 0.9f, 1.f, 0.9f}, 600.f, 3.f * px, 48);
			renderer.renderCircleOutline(m.position, {0.3f, 0.9f, 1.f, 0.5f}, 450.f, 2.f * px, 48);
		}
	}
}

void draw(const level::Level &level, wgpu2d::Renderer2D &renderer, const Look &look)
{
	pickShipSize = look.shipSize;
	pickEnemySize = look.enemySize;
	const float px = 1.f / std::max(zoom, 0.001f);

	drawMarkers(level, renderer, zoom);

	for (const level::EnemyPlacement &e : level.enemies)
	{
		const bool sniper = e.behaviour == Enemy::Behaviour::KeepDistance;
		renderSpaceShip(renderer, e.position, look.enemySize, look.shipSheet,
			sniper ? look.sniperCell : look.rusherCell, level::direction(e.facingDegrees));
	}

	renderSpaceShip(renderer, level.start, look.shipSize, look.shipSheet, look.playerCell,
		level::direction(level.startFacingDegrees));
	renderer.renderCircleOutline(level.start, {0.3f, 1.f, 0.4f, 0.8f}, look.shipSize * 0.6f, 2.f * px, 32);

	if (selected.kind != Kind::None && validPick(level, selected))
	{
		float radius = look.enemySize * 0.6f;
		if (selected.kind == Kind::Start) { radius = look.shipSize * 0.7f; }
		if (selected.kind == Kind::Marker) { radius = markerRadius(level.markers[selected.index].kind) * 1.15f; }
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
	ImGui::RadioButton("Scenery", &t, (int)Tool::Scenery);
	tool = (Tool)t;
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
	case Kind::Marker:
	{
		level::Marker &m = level.markers[selected.index];
		int kind = m.kind == level::Marker::Kind::Gate ? 1 : 0;
		if (ImGui::Combo("Kind", &kind, "Resource\0Gate\0"))
		{
			m.kind = kind ? level::Marker::Kind::Gate : level::Marker::Kind::Resource;
			edited = true;
		}
		if (ImGui::DragFloat2("Position", &m.position.x, 10.f, 0.f, 0.f, "%.0f")) { edited = true; }
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
