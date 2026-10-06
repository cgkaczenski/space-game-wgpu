#include <sight.h>
#include <tuning.h>

#include <asteroids.h>
#include <engine/regionMask.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <utility>

namespace sight
{

namespace
{
	// What makes one cell a different side of a wall from the next.
	enum class Fields
	{
		Continuous,   // in paint or not: fields that touch are one
		EachField,    // which field: two that touch have a wall between them
	};

	// What an edge does to a line crossing it.
	enum class Edges
	{
		BothWays,     // a wall from either side: rooms
		IntoFields,   // only going in: from inside you see and shoot out
		Rocks,        // no edges: every rock blocks on its own outline (A1, A1b)
	};

	// Where a shot that crosses an edge ends.
	enum class ShotStop
	{
		EdgeRock,     // on the field rock at the crossing, pushed and hurt, if there is one
		Edge,         // at the edge, always, touching nothing
	};

	enum class Beam
	{
		MiningTool,   // the edges as a shot has them, and every rock it touches
		LikeShot,     // exactly a shot's rule: inside a field it passes the rocks
		Rocks,        // every rock, and no edges
	};

	enum class Outline
	{
		Mint,         // the hidden outline as it always was
		MintAmber,    // and amber while an enemy can see you in a field
	};

	Fields fields = Fields::Continuous;
	Edges vision = Edges::BothWays;
	Edges shots = Edges::IntoFields;
	ShotStop shotStop = ShotStop::EdgeRock;
	Beam beamRule = Beam::LikeShot;
	Outline outline = Outline::MintAmber;
	glm::vec3 warningColour = {1.f, 0.66f, 0.22f};

	// How far round a shot's crossing it looks for the edge rock to strike,
	// beyond its own radius. The crossing is on a cell's side, and the rocks
	// along the edge sit within about a cell of it.
	float edgeRockReach = 60.f;

	bool showMask = false;
	bool showLines = false;

	float nearer(float a, float b)
	{
		if (a < 0.f) { return b; }
		if (b < 0.f) { return a; }
		return std::min(a, b);
	}

	// How far along `from` -> `to` the first edge that `rule` blocks on is,
	// or -1. Walks the painted area's grid (engine/regionMask) and compares
	// each cell with the one before.
	float edgeAlong(glm::vec2 from, glm::vec2 to, Edges rule)
	{
		if (rule == Edges::Rocks) { return -1.f; }
		float at = -1.f;
		bool first = true;
		int previous = -1;
		region::march(asteroids::paintMask(), from, to, [&](float distance, int label)
		{
			const int side = fields == Fields::Continuous ? (label >= 0 ? 0 : -1) : label;
			if (first)
			{
				first = false;
				previous = side;
				return true;
			}
			if (side != previous && (rule == Edges::BothWays || side >= 0))
			{
				at = distance;
				return false;
			}
			previous = side;
			return true;
		});
		return at;
	}

	// The field rock a shot crossing an edge at `point` strikes, or -1.
	int edgeRock(glm::vec2 point, float radius)
	{
		if (shotStop != ShotStop::EdgeRock) { return -1; }
		return asteroids::hitCircle(point, radius + edgeRockReach, asteroids::Which::InPaint);
	}
}

float blockedAt(glm::vec2 from, glm::vec2 to)
{
	const glm::vec2 line = to - from;
	const float length = glm::length(line);
	if (vision == Edges::Rocks)
	{
		// The rule before S1: every rock blocks, and anyone in a field's
		// paint is hidden outright, gaps and all (A1, A1b).
		if (asteroids::inField(to)) { return length; }
		if (length <= 0.f) { return asteroids::hitCircle(from, 0.f) >= 0 ? 0.f : -1.f; }
		return asteroids::raycast(from, line / length, length);
	}
	if (length <= 0.f) { return -1.f; }
	const float rock = asteroids::raycast(from, line / length, length, nullptr, asteroids::Which::Solid);
	return nearer(rock, edgeAlong(from, to, vision));
}

bool clear(glm::vec2 from, glm::vec2 to) { return blockedAt(from, to) < 0.f; }

Stop shot(glm::vec2 from, glm::vec2 to, float radius)
{
	Stop stop;
	if (shots == Edges::Rocks)
	{
		// The rule before S1: any rock it touches now.
		const int rock = asteroids::hitCircle(to, radius);
		if (rock >= 0) { stop = {true, to, rock}; }
		return stop;
	}

	// The edge first: it is somewhere along the way, before where it is now.
	const float edge = edgeAlong(from, to, shots);
	if (edge >= 0.f)
	{
		stop.stopped = true;
		stop.point = from + glm::normalize(to - from) * edge;
		stop.rock = edgeRock(stop.point, radius);
		return stop;
	}
	const int rock = asteroids::hitCircle(to, radius, asteroids::Which::Solid);
	if (rock >= 0) { stop = {true, to, rock}; }
	return stop;
}

float beam(glm::vec2 origin, glm::vec2 direction, float reach, int *rock)
{
	*rock = -1;
	if (beamRule == Beam::Rocks) { return asteroids::raycast(origin, direction, reach, rock); }

	const asteroids::Which which = beamRule == Beam::MiningTool ? asteroids::Which::All : asteroids::Which::Solid;
	float t = asteroids::raycast(origin, direction, reach, rock, which);
	const float edge = edgeAlong(origin, origin + direction * reach, shots);
	if (edge >= 0.f && (t < 0.f || edge < t))
	{
		t = edge;
		*rock = edgeRock(origin + direction * edge, 0.f);
	}
	return t;
}

bool warnsWhenSeen() { return outline == Outline::MintAmber; }
glm::vec3 seenColour() { return warningColour; }

void drawDebug(wgpu2d::Renderer2D &renderer, const std::vector<glm::vec2> &viewers, glm::vec2 target)
{
	const float zoom = std::max(renderer.currentCamera.zoom, 1e-4f);

	if (showMask)
	{
		// A square in each painted cell in view, coloured by field so Each
		// field's walls show where two meet. Far out, every nth cell, so a
		// zoomed-out view stays a few tens of thousands of quads.
		const region::Mask &mask = asteroids::paintMask();
		const glm::vec4 view = renderer.getViewRect();
		const int x0 = std::max(0, (int)std::floor((view.x - mask.origin.x) / mask.cell));
		const int y0 = std::max(0, (int)std::floor((view.y - mask.origin.y) / mask.cell));
		const int x1 = std::min(mask.width - 1, (int)std::floor((view.x + view.z - mask.origin.x) / mask.cell));
		const int y1 = std::min(mask.height - 1, (int)std::floor((view.y + view.w - mask.origin.y) / mask.cell));
		const float cells = (float)std::max(0, x1 - x0 + 1) * (float)std::max(0, y1 - y0 + 1);
		const int stride = std::max(1, (int)std::ceil(std::sqrt(cells / 40000.f)));
		const float half = std::max(mask.cell * 0.18f, 1.5f / zoom) * (float)stride;
		static const glm::vec3 palette[] = {
			{0.35f, 0.9f, 0.8f}, {0.95f, 0.55f, 0.85f}, {0.6f, 0.75f, 1.f},
			{0.95f, 0.85f, 0.4f}, {0.6f, 1.f, 0.5f}, {1.f, 0.6f, 0.45f}};
		for (int y = y0; y <= y1; y += stride)
		{
			for (int x = x0; x <= x1; x += stride)
			{
				const int label = region::labelOf(mask, x, y);
				if (label < 0) { continue; }
				const glm::vec3 c = palette[label % 6];
				const glm::vec2 centre = mask.origin + (glm::vec2((float)x, (float)y) + 0.5f) * mask.cell;
				renderer.renderRectangle({centre.x - half, centre.y - half, half * 2.f, half * 2.f},
					wgpu2d::Color4f{c, 0.45f});
			}
		}
	}

	if (showLines)
	{
		const float width = 2.5f / zoom;
		for (const glm::vec2 &from : viewers)
		{
			const float blocked = blockedAt(from, target);
			if (blocked < 0.f)
			{
				renderer.renderLine(from, target, {0.3f, 1.f, 0.4f, 0.8f}, width);
				continue;
			}
			const glm::vec2 stop = from + glm::normalize(target - from) * blocked;
			renderer.renderLine(from, stop, {0.3f, 1.f, 0.4f, 0.8f}, width);
			renderer.renderLine(stop, target, {1.f, 0.25f, 0.2f, 0.8f}, width);
			const float s = 6.f / zoom;
			renderer.renderRectangle({stop.x - s, stop.y - s, s * 2.f, s * 2.f}, {1.f, 0.25f, 0.2f, 1.f});
		}
	}
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("sight", {
	{"fields", fields},
	{"vision", vision},
	{"shots", shots},
	{"shotStop", shotStop},
	{"beam", beamRule},
	{"outline", outline},
	{"seenColour", warningColour},
	{"edgeRockReach", edgeRockReach},
	{"showMask", showMask},
	{"showLines", showLines},
});

namespace
{
	// A row of radio buttons over an enum. The radios edit a copy, so the
	// highlight is for the real one.
	template <class E>
	void choose(const char *label, E &value, std::initializer_list<std::pair<const char *, E>> options)
	{
		int v = (int)value;
		{
			tune::Highlight h(&value);
			ImGui::PushID(label);
			ImGui::TextUnformatted(label);
			bool firstOption = true;
			for (const auto &[name, option] : options)
			{
				if (!firstOption) { ImGui::SameLine(); }
				firstOption = false;
				ImGui::RadioButton(name, &v, (int)option);
			}
			ImGui::PopID();
		}
		value = (E)v;
	}
}

void debugUi()
{
	choose("Fields are", fields, {
		{"Continuous paint", Fields::Continuous}, {"Each field", Fields::EachField}});
	choose("Vision at edges", vision, {
		{"Both ways", Edges::BothWays}, {"Into fields only", Edges::IntoFields}, {"Rocks", Edges::Rocks}});
	choose("Shots at edges", shots, {
		{"Into fields only", Edges::IntoFields}, {"Both ways", Edges::BothWays}, {"Rocks", Edges::Rocks}});
	if (shots != Edges::Rocks)
	{
		choose("Shot stops", shotStop, {
			{"On an edge rock", ShotStop::EdgeRock}, {"At the edge", ShotStop::Edge}});
		if (shotStop == ShotStop::EdgeRock)
		{
			tune::SliderFloat("Edge rock reach", &edgeRockReach, 0.f, 300.f, "%.0f units past the shot");
		}
	}
	choose("Beam", beamRule, {
		{"Like a shot", Beam::LikeShot}, {"Mining tool", Beam::MiningTool}, {"Rocks", Beam::Rocks}});
	choose("Hidden outline", outline, {
		{"Mint", Outline::Mint}, {"Mint, amber when seen", Outline::MintAmber}});
	if (outline == Outline::MintAmber) { tune::ColorEdit3("Seen colour", &warningColour.x); }

	ImGui::Separator();
	tune::Checkbox("Show mask", &showMask);
	ImGui::SameLine();
	tune::Checkbox("Show sight lines", &showLines);
	ImGui::TextDisabled("The mask's cell size is with the field sliders, under Asteroids.");
}

}
