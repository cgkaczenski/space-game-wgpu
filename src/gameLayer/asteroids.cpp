#include <asteroids.h>

#include <engine/polygon.h>
#include <engine/scatter.h>
#include "imgui.h"
#include <platformTools.h>
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace asteroids
{

namespace
{
	wgpu2d::Texture rockTexture;

	struct Rock
	{
		polygon::Placement placement;
		std::vector<glm::vec2> outline; // around the origin, in the rock's frame
		float bound = 0.f;              // bounding radius: the broad phase
		glm::vec2 uvOffset = {};        // which part of the texture it wears
		bool inField = false;           // drawn over the ships, not under
	};

	std::vector<Rock> rocks;
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
		return r;
	}

	// The fan, as renderTriangles takes it: three corners per triangle, the
	// rock's centre first. Texture coordinates come from where each corner
	// sits *on the rock*, so the texture is fixed to it and would turn with
	// it (A2); positions are those corners placed in the world.
	void drawRock(wgpu2d::Renderer2D &renderer, const Rock &r)
	{
		const size_t n = r.outline.size();
		if (n < 3 || rockTexture.id == 0) { return; }

		static std::vector<glm::vec2> positions, uvs;
		static std::vector<glm::vec4> colours;
		positions.clear(); uvs.clear(); colours.clear();

		const glm::vec4 centreColour = {glm::vec3(brightness), 1.f};
		const glm::vec4 rimColour = {glm::vec3(brightness * rimShade), 1.f};
		const float scale = 1.f / std::max(textureWorldSize, 1.f);
		auto uvOf = [&](glm::vec2 local) { return local * scale + r.uvOffset; };

		for (size_t i = 0; i < n; i++)
		{
			const glm::vec2 a = r.outline[i];
			const glm::vec2 b = r.outline[(i + 1) % n];
			positions.push_back(r.placement.position);
			positions.push_back(polygon::toWorld(r.placement, a));
			positions.push_back(polygon::toWorld(r.placement, b));
			uvs.push_back(uvOf({0.f, 0.f}));
			uvs.push_back(uvOf(a));
			uvs.push_back(uvOf(b));
			colours.push_back(centreColour);
			colours.push_back(rimColour);
			colours.push_back(rimColour);
		}
		renderer.renderTriangles(positions.data(), uvs.data(), colours.data(), positions.size(), rockTexture);
	}

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

	// A field's rocks: layers of grids, one rock per cell, the big rocks first
	// and smaller ones filling between them, never closer than the field's gap
	// (engine/scatter). Each cell's rock is decided by the field's seed and
	// that cell, so painting more area adds rocks and leaves the old ones
	// where they were.
	void growField(const level::AsteroidField &f, std::vector<Rock> &out)
	{
		glm::vec2 lo, hi;
		if (!fieldBounds(f, lo, hi)) { return; }
		scatter::Params params;
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
	// on one at a fraction of its size shimmers as the camera moves. Mipmaps
	// for the same reason when zoomed out. Repeating, for big rocks.
	const char *path = RESOURCES_PATH "asteroid/rock_colour.png";
	rockTexture.loadFromFile(path, false, true, true);
	if (rockTexture.id == 0)
	{
		std::cerr << "asteroids: failed to load " << path << "\n";
		return false;
	}
	return true;
}

void cleanup() { rockTexture.cleanup(); }

void reset(const std::vector<level::Asteroid> &placed, const std::vector<level::AsteroidField> &fields)
{
	placedCopy = placed;
	fieldsCopy = fields;
	rocks.clear();
	for (const level::Asteroid &a : placed) { rocks.push_back(grow(a)); }
	for (const level::AsteroidField &f : fields) { growField(f, rocks); }
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

bool hitsCircle(glm::vec2 centre, float radius)
{
	for (const Rock &r : rocks)
	{
		// Broad phase: two circles that do not touch rule out the triangles.
		if (glm::distance(centre, r.placement.position) > r.bound + radius) { continue; }
		if (polygon::overlapsCircle(r.outline, polygon::toLocal(r.placement, centre), radius)) { return true; }
	}
	return false;
}

float raycast(glm::vec2 origin, glm::vec2 direction, float maxDistance)
{
	float nearest = -1.f;
	for (const Rock &r : rocks)
	{
		// Broad phase: how close the ray's line passes the rock's centre.
		const glm::vec2 toCentre = r.placement.position - origin;
		const float along = glm::dot(toCentre, direction);
		const float miss = glm::length(toCentre - direction * along);
		if (miss > r.bound || along < -r.bound || along > maxDistance + r.bound) { continue; }

		const float reach = nearest >= 0.f ? nearest : maxDistance;
		const float t = polygon::raycast(r.outline, polygon::toLocal(r.placement, origin),
			polygon::directionToLocal(r.placement, direction), reach);
		if (t >= 0.f) { nearest = t; }
	}
	return nearest;
}

bool blocksSight(glm::vec2 from, glm::vec2 to)
{
	const glm::vec2 line = to - from;
	const float length = glm::length(line);
	if (length <= 0.f) { return hitsCircle(from, 0.f); }
	return raycast(from, line / length, length) >= 0.f;
}

void draw(wgpu2d::Renderer2D &renderer)
{
	// Off screen, skip it: the batch would draw it anyway.
	const glm::vec4 view = renderer.getViewRect();
	for (const Rock &r : rocks)
	{
		if (r.inField || !onScreen(view, r)) { continue; }
		drawRock(renderer, r);
		if (showOutlines) { drawOutline(renderer, r); }
	}
}

void drawFields(wgpu2d::Renderer2D &renderer)
{
	const glm::vec4 view = renderer.getViewRect();
	for (const Rock &r : rocks)
	{
		if (!r.inField || !onScreen(view, r)) { continue; }
		drawRock(renderer, r);
		if (showOutlines) { drawOutline(renderer, r); }
	}
}

void drawPlacements(wgpu2d::Renderer2D &renderer, const std::vector<level::Asteroid> &placed,
	const std::vector<level::AsteroidField> &fields)
{
	for (const level::AsteroidField &f : fields) { drawArea(renderer, f); }

	std::vector<Rock> grown;
	for (const level::Asteroid &a : placed) { grown.push_back(grow(a)); }
	for (const level::AsteroidField &f : fields) { growField(f, grown); }
	const glm::vec4 view = renderer.getViewRect();
	for (const Rock &r : grown)
	{
		if (!onScreen(view, r)) { continue; }
		drawRock(renderer, r);
		if (!r.inField) { drawOutline(renderer, r); } // a field's rocks are too many to outline
	}
}

void debugUi()
{
	ImGui::Text("%d rocks", (int)rocks.size());
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
