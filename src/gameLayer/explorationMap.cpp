#include <explorationMap.h>
#include <tuning.h>

#include <arena.h>
#include <asteroids.h>
#include <gate.h>
#include <lanes.h>
#include <lastKnown.h>
#include <level.h>
#include <sight.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace explorationMap
{

namespace
{
	constexpr int exploredSize = 1024;   // the explored map's target, pixels square
	constexpr int baseSize = 512;        // the base map's texture

	wgpu2d::FrameBuffer explored;
	wgpu2d::Texture base;
	wgpu2d::Texture white;
	float radius = 0.f;                  // the map covers -radius .. radius both ways
	bool needsClear = false;

	// Which gates the player has seen this round: the exit, then each jump
	// gate in jumpGates' order.
	bool exitSeen = false;
	std::vector<bool> jumpSeen;
	std::vector<level::JumpPair> jumps;
	bool hasExit = false;
	glm::vec2 exitAt = {};

	bool shown = true;
	float cornerSize = 0.26f;            // of the window's height
	float cornerSpan = 40000.f;          // world units across the corner map
	float fullSize = 0.86f;              // of the window's height
	float exploredDim = 0.55f;           // how bright what was seen, but is not now
	glm::vec3 fieldColour = {0.42f, 0.36f, 0.30f};
	glm::vec3 openColour = {0.07f, 0.09f, 0.17f};
	glm::vec3 laneColour = {0.25f, 0.6f, 0.75f};

	glm::vec2 uvOf(glm::vec2 world) { return (world + glm::vec2(radius)) / (2.f * radius); }

	// A screen rectangle showing the world square `centre` +- `span` / 2.
	struct Frame
	{
		glm::vec2 corner;   // screen, top left
		float size;         // screen pixels square
		glm::vec2 centre;   // world
		float span;         // world units across
		glm::vec2 toScreen(glm::vec2 world) const { return corner + ((world - centre) / span + 0.5f) * size; }
		bool inside(glm::vec2 s, float margin = 0.f) const
		{
			return s.x >= corner.x - margin && s.y >= corner.y - margin
				&& s.x <= corner.x + size + margin && s.y <= corner.y + size + margin;
		}
	};

	// The part of the map texture under `f`, as two triangles: what is outside
	// the map (past its square) samples the clamped edge, which is
	// transparent -- the arena circle lies inside the square.
	void drawTexture(wgpu2d::Renderer2D &renderer, const Frame &f, wgpu2d::Texture texture, glm::vec4 tint)
	{
		const glm::vec2 p[4] = {f.corner, f.corner + glm::vec2(f.size, 0.f), f.corner + glm::vec2(f.size), f.corner + glm::vec2(0.f, f.size)};
		glm::vec2 positions[6], uvs[6];
		glm::vec4 colours[6];
		const int order[6] = {0, 1, 2, 0, 2, 3};
		for (int k = 0; k < 6; k++)
		{
			positions[k] = p[order[k]];
			const glm::vec2 world = f.centre + ((p[order[k]] - f.corner) / f.size - 0.5f) * f.span;
			uvs[k] = uvOf(world);
			colours[k] = tint;
		}
		renderer.renderTriangles(positions, uvs, colours, 6, texture);
	}

	// The fan of `seen` in the frame, textured with the base map.
	void drawSeen(wgpu2d::Renderer2D &renderer, const Frame &f, const visibility::PolarMap &seen)
	{
		static std::vector<glm::vec2> positions, uvs;
		static std::vector<glm::vec4> colours;
		positions.clear(); uvs.clear(); colours.clear();
		const int n = (int)seen.distance.size();
		auto vertex = [&](glm::vec2 world)
		{
			positions.push_back(f.toScreen(world));
			uvs.push_back(uvOf(world));
			colours.push_back({1.f, 1.f, 1.f, 1.f});
		};
		for (int i = 0; i < n; i++)
		{
			vertex(seen.origin); vertex(visibility::corner(seen, i)); vertex(visibility::corner(seen, i + 1));
		}
		if (!positions.empty()) { renderer.renderTriangles(positions.data(), uvs.data(), colours.data(), positions.size(), base); }
	}
}

bool init()
{
	explored.create(exploredSize, exploredSize);
	white.create1PxSquare();
	return explored.texture.id != 0 && white.id != 0;
}

void cleanup()
{
	explored.cleanup();
	base.cleanup();
	white.cleanup();
}

void start(const level::Level &level, float arenaRadius)
{
	radius = arenaRadius;
	needsClear = true;
	exitSeen = false;
	jumps = level.jumps;
	jumpSeen.assign(jumps.size() * 2, false);
	hasExit = false;
	for (const level::Marker &m : level.markers)
	{
		if (m.kind == level::Marker::Kind::Gate) { hasExit = true; exitAt = m.position; }
	}
	if (radius <= 0.f) { return; }

	// The base map: field paint, open space and lanes, opaque inside the arena
	// and transparent past it, rows top first like the world.
	std::vector<unsigned char> pixels((size_t)baseSize * baseSize * 4, 0);
	const float cell = 2.f * radius / (float)baseSize;
	for (int y = 0; y < baseSize; y++)
	{
		for (int x = 0; x < baseSize; x++)
		{
			const glm::vec2 p = {-radius + (x + 0.5f) * cell, -radius + (y + 0.5f) * cell};
			if (glm::dot(p, p) > radius * radius) { continue; }
			glm::vec3 c = openColour;
			if (lanes::laneAt(p) >= 0) { c = laneColour; }
			else if (asteroids::inField(p)) { c = fieldColour; }
			unsigned char *px = &pixels[((size_t)y * baseSize + x) * 4];
			px[0] = (unsigned char)std::clamp(c.r * 255.f, 0.f, 255.f);
			px[1] = (unsigned char)std::clamp(c.g * 255.f, 0.f, 255.f);
			px[2] = (unsigned char)std::clamp(c.b * 255.f, 0.f, 255.f);
			px[3] = 255;
		}
	}
	base.cleanup();
	base.createFromBuffer((const char *)pixels.data(), baseSize, baseSize, false, false);
}

void reveal(wgpu2d::Renderer2D &renderer, const visibility::PolarMap &seen)
{
	if (radius <= 0.f || base.id == 0 || explored.texture.id == 0) { return; }
	if (needsClear) { explored.clear(); needsClear = false; }
	if (seen.distance.empty()) { return; }

	// The target's own camera: the world square -radius .. radius onto its
	// pixels. A camera's position is the world point at the target's top
	// left, scaled about the target's middle.
	wgpu2d::Camera camera;
	camera.zoom = (float)exploredSize / (2.f * radius);
	camera.position = glm::vec2(-(float)exploredSize * 0.5f);
	renderer.pushCamera(camera);
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	static std::vector<glm::vec2> positions, uvs;
	static std::vector<glm::vec4> colours;
	positions.clear(); uvs.clear(); colours.clear();
	const int n = (int)seen.distance.size();
	auto vertex = [&](glm::vec2 world)
	{
		positions.push_back(world);
		uvs.push_back(uvOf(world));
		colours.push_back({1.f, 1.f, 1.f, 1.f});
	};
	for (int i = 0; i < n; i++)
	{
		vertex(seen.origin); vertex(visibility::corner(seen, i)); vertex(visibility::corner(seen, i + 1));
	}
	renderer.renderTriangles(positions.data(), uvs.data(), colours.data(), positions.size(), base);
	renderer.flushFBO(explored);
	renderer.popCamera();
}

void draw(wgpu2d::Renderer2D &renderer, int width, int height, const Marks &marks, bool full)
{
	if (radius <= 0.f) { return; }

	// The gates seen now are known from now on.
	auto seesGate = [](glm::vec2 at)
	{
		const float r = gate::radius();
		return sight::playerSees(at) || sight::playerSees(at + glm::vec2(r, 0.f)) || sight::playerSees(at - glm::vec2(r, 0.f))
			|| sight::playerSees(at + glm::vec2(0.f, r)) || sight::playerSees(at - glm::vec2(0.f, r));
	};
	if (hasExit && !exitSeen && seesGate(exitAt)) { exitSeen = true; }
	for (size_t g = 0; g < jumpSeen.size(); g++)
	{
		const glm::vec2 at = g % 2 == 0 ? jumps[g / 2].a : jumps[g / 2].b;
		if (!jumpSeen[g] && seesGate(at)) { jumpSeen[g] = true; }
	}

	if (!shown && !full) { return; }
	renderer.pushCamera();
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	const float h = (float)height, w = (float)width;
	Frame f;
	if (full)
	{
		renderer.renderRectangle({0.f, 0.f, w, h}, {0.f, 0.f, 0.f, 0.6f});
		f.size = h * fullSize;
		f.corner = {(w - f.size) * 0.5f, (h - f.size) * 0.5f};
		f.centre = {};
		f.span = 2.f * radius;
	}
	else
	{
		f.size = h * cornerSize;
		const float margin = h * 0.03f;
		f.corner = {w - f.size - margin, h - f.size - margin};
		f.centre = marks.player;
		f.span = cornerSpan;
	}
	const float px = std::max(1.6f, f.size / 220.f);  // a line a size that reads at either scale

	// The frame, the explored map dimmed, and what is seen now in full.
	renderer.renderRectangle({f.corner.x - 2.f * px, f.corner.y - 2.f * px, f.size + 4.f * px, f.size + 4.f * px},
		{0.35f, 0.35f, 0.48f, 0.9f});
	renderer.renderRectangle({f.corner.x, f.corner.y, f.size, f.size}, {0.01f, 0.01f, 0.03f, 0.92f});
	drawTexture(renderer, f, explored.texture, {exploredDim, exploredDim, exploredDim, 1.f});
	if (marks.seen) { drawSeen(renderer, f, *marks.seen); }

	// The closing circle, while it is closing.
	if (arena::closes())
	{
		const zone::Circle safe = arena::safeZone();
		const glm::vec2 c = f.toScreen(safe.centre);
		const float r = safe.radius / f.span * f.size;
		if (full || r < f.size * 2.f) { renderer.renderCircleOutline(c, {1.f, 1.f, 1.f, 0.6f}, r, 1.5f * px, 96); }
	}

	// The gates seen.
	const float gateR = std::max(4.f * px, gate::radius() / f.span * f.size);
	if (hasExit && exitSeen)
	{
		const glm::vec2 s = f.toScreen(exitAt);
		if (f.inside(s)) { renderer.renderCircleOutline(s, {1.f, 0.8f, 0.3f, 1.f}, gateR * 1.4f, 2.f * px, 20); }
	}
	for (size_t p = 0; p < jumps.size(); p++)
	{
		const glm::vec2 a = f.toScreen(jumps[p].a), b = f.toScreen(jumps[p].b);
		const glm::vec4 violet = {0.75f, 0.45f, 1.f, 1.f};
		if (full && jumpSeen[p * 2] && jumpSeen[p * 2 + 1])
		{
			renderer.renderLine(a, b, {violet.r, violet.g, violet.b, 0.35f}, 1.5f * px);
		}
		if (jumpSeen[p * 2] && f.inside(a)) { renderer.renderCircleOutline(a, violet, gateR, 2.f * px, 16); }
		if (jumpSeen[p * 2 + 1] && f.inside(b)) { renderer.renderCircleOutline(b, violet, gateR, 2.f * px, 16); }
	}

	// S4's ghosts, and the enemies in sight.
	lastKnown::forEachGhost([&](glm::vec2 at, float alpha)
	{
		const glm::vec2 s = f.toScreen(at);
		if (f.inside(s)) { renderer.renderCircleOutline(s, {1.f, 0.35f, 0.3f, 0.8f * alpha}, 3.f * px, 1.5f * px, 10); }
	});
	for (const glm::vec2 &e : marks.enemies)
	{
		const glm::vec2 s = f.toScreen(e);
		if (f.inside(s)) { renderer.renderRectangle({s.x - 2.5f * px, s.y - 2.5f * px, 5.f * px, 5.f * px}, {1.f, 0.3f, 0.25f, 1.f}); }
	}

	// The player: an arrow the way it faces.
	{
		const glm::vec2 s = f.toScreen(marks.player);
		const glm::vec2 d = glm::length(marks.facing) > 0.f ? glm::normalize(marks.facing) : glm::vec2(1.f, 0.f);
		const glm::vec2 side = {-d.y, d.x};
		const float len = 7.f * px;
		const glm::vec2 tip = s + d * len, l = s - d * len * 0.6f + side * len * 0.6f, r = s - d * len * 0.6f - side * len * 0.6f;
		const glm::vec2 positions[3] = {tip, l, r};
		const glm::vec2 uvs[3] = {{0.5f, 0.5f}, {0.5f, 0.5f}, {0.5f, 0.5f}};
		const glm::vec4 colours[3] = {{1.f, 1.f, 1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}, {1.f, 1.f, 1.f, 1.f}};
		if (f.inside(s)) { renderer.renderTriangles(positions, uvs, colours, 3, white); }
	}
	renderer.popCamera();
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("explorationMap", {
	{"shown", shown},
	{"cornerSize", cornerSize},
	{"cornerSpan", cornerSpan},
	{"fullSize", fullSize},
	{"exploredDim", exploredDim},
	{"fieldColour", fieldColour},
	{"openColour", openColour},
	{"laneColour", laneColour},
});

void debugUi()
{
	tune::Checkbox("Corner map", &shown);
	ImGui::SameLine();
	ImGui::TextDisabled("(hold M for the whole level)");
	tune::SliderFloat("Corner size", &cornerSize, 0.1f, 0.5f, "%.2f of the height");
	tune::SliderFloat("Corner shows", &cornerSpan, 5000.f, 200000.f, "%.0f units across", ImGuiSliderFlags_Logarithmic);
	tune::SliderFloat("Full size", &fullSize, 0.3f, 1.f, "%.2f of the height");
	tune::SliderFloat("Seen before", &exploredDim, 0.1f, 1.f, "%.2f bright");
	ImGui::TextDisabled("Colours take effect when the round restarts:");
	tune::ColorEdit3("Field", &fieldColour.x);
	tune::ColorEdit3("Open", &openColour.x);
	tune::ColorEdit3("Lane", &laneColour.x);
	if (ImGui::Button("Forget what was seen")) { needsClear = true; exitSeen = false; jumpSeen.assign(jumpSeen.size(), false); }
}

}
