#include <sight.h>
#include <tuning.h>

#include <asteroids.h>
#include <engine/regionMask.h>
#include <engine/visibility.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <chrono>
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

	// ---- What the player sees (S2) ----

	// A rock's shape in the polar map: its outline, or the circle round it
	// (cheaper and cruder, to measure the difference).
	enum class Silhouette { Exact, Circle };
	enum class CloakedSight { Unchanged, Shorter };
	// A ship over a single rock: the rock under it does not block its own
	// view, or it sees nothing at all.
	enum class OnRock { SeeOut, Blind };
	// What a missile may lock onto: anything, or only what its shooter sees.
	enum class Locks { AnyTarget, SeenOnly };
	// What stops a missile: only a field's core, or the shot rule.
	enum class Missiles { CoresOnly, LikeShot };

	float sightRange = 4000.f;
	int slices = 720;
	Silhouette silhouette = Silhouette::Exact;
	CloakedSight cloakedSight = CloakedSight::Unchanged;
	float cloakedRange = 0.6f;      // of the range, while cloaked and Shorter
	OnRock onRock = OnRock::SeeOut;
	Locks locks = Locks::SeenOnly;
	Missiles missiles = Missiles::CoresOnly;
	bool showPolarMap = false;

	visibility::PolarMap playerView;
	float buildMillis = 0.f;        // the last rebuild, for the panel
	int segmentCount = 0;

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
	// The rule before S1: every rock blocks, and anyone in a field's paint is
	// hidden outright, gaps and all (A1, A1b).
	if (vision == Edges::Rocks && asteroids::inField(to)) { return length; }
	const asteroids::Which which = vision == Edges::Rocks ? asteroids::Which::All : asteroids::Which::Solid;

	// The rock `from` is over, if any: looked out of, or blinding (S2).
	const int under = asteroids::hitCircle(from, 0.f, which);
	if (under >= 0 && onRock == OnRock::Blind) { return 0.f; }
	if (length <= 0.f) { return -1.f; }
	const float rock = asteroids::raycast(from, line / length, length, nullptr, which, under);
	if (vision == Edges::Rocks) { return rock; }
	return nearer(rock, edgeAlong(from, to, vision));
}

bool clear(glm::vec2 from, glm::vec2 to) { return blockedAt(from, to) < 0.f; }

Stop shot(glm::vec2 from, glm::vec2 to, float radius, bool missile)
{
	Stop stop;
	if (missile && missiles == Missiles::CoresOnly)
	{
		// Through every rock and edge to what it chases; a core is the one
		// thing in its way (S2).
		const int core = asteroids::hitCircle(to, radius, asteroids::Which::Cores);
		if (core >= 0) { stop = {true, to, core}; }
		return stop;
	}
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

namespace
{
	// The sides of the grid's cells, within `range` of `viewer`, where the
	// vision rule says an edge stands, as segments into the polar map. A line
	// from the viewer crosses a side from the cell on the viewer's side of it,
	// so whether it blocks -- any change of side, or only one into a field --
	// is decided by which of the two cells that is. This is the same test
	// edgeAlong makes walking a line, made once for every side at once.
	void addEdges(visibility::PolarMap &map, glm::vec2 viewer, float range)
	{
		const region::Mask &mask = asteroids::paintMask();
		if (mask.width <= 0) { return; }
		auto side = [&](int x, int y)
		{
			const int label = region::labelOf(mask, x, y);
			return fields == Fields::Continuous ? (label >= 0 ? 0 : -1) : label;
		};
		auto blocks = [&](int near, int far) { return near != far && (vision == Edges::BothWays || far >= 0); };
		// Each side reaches a hair past its corners, so where two meet there
		// is no crack: a line exactly through a corner hits one of them
		// rather than slipping between, as floating point otherwise lets it.
		const float seal = mask.cell * 1e-5f;

		// One cell past the grid on each side: outside it is unpainted, so the
		// grid's own border can be an edge.
		const int x0 = std::max(-1, (int)std::floor((viewer.x - range - mask.origin.x) / mask.cell));
		const int y0 = std::max(-1, (int)std::floor((viewer.y - range - mask.origin.y) / mask.cell));
		const int x1 = std::min(mask.width - 1, (int)std::floor((viewer.x + range - mask.origin.x) / mask.cell));
		const int y1 = std::min(mask.height - 1, (int)std::floor((viewer.y + range - mask.origin.y) / mask.cell));
		for (int y = y0; y <= y1; y++)
		{
			const float top = mask.origin.y + (float)y * mask.cell;
			for (int x = x0; x <= x1; x++)
			{
				const float left = mask.origin.x + (float)x * mask.cell;
				const int here = side(x, y);

				// The side to the right: a vertical line at x + 1.
				const int right = side(x + 1, y);
				if (here != right)
				{
					const float line = left + mask.cell;
					const bool viewerLeft = viewer.x < line;
					if (blocks(viewerLeft ? here : right, viewerLeft ? right : here))
					{
						visibility::addSegment(map, {line, top - seal}, {line, top + mask.cell + seal});
						segmentCount++;
					}
				}

				// The side below: a horizontal line at y + 1.
				const int below = side(x, y + 1);
				if (here != below)
				{
					const float line = top + mask.cell;
					const bool viewerAbove = viewer.y < line;
					if (blocks(viewerAbove ? here : below, viewerAbove ? below : here))
					{
						visibility::addSegment(map, {left - seal, line}, {left + mask.cell + seal, line});
						segmentCount++;
					}
				}
			}
		}
	}
}

namespace
{
	// Everything the rule says blocks, seen from `position`, into `view`.
	void build(visibility::PolarMap &view, glm::vec2 position, float range)
	{
		visibility::begin(view, position, range, slices);

		const asteroids::Which which = vision == Edges::Rocks ? asteroids::Which::All : asteroids::Which::Solid;
		const int under = asteroids::hitCircle(position, 0.f, which);
		if (under >= 0 && onRock == OnRock::Blind)
		{
			visibility::blockAll(view);
		}
		else
		{
			asteroids::outlinesNear(position, range, which,
				[&](int rock, const std::vector<glm::vec2> &outline, glm::vec2 boundCentre, float bound)
			{
				if (rock == under) { return; } // looked out of (S2)
				if (silhouette == Silhouette::Circle)
				{
					constexpr int sides = 16;
					for (int k = 0; k < sides; k++)
					{
						const float a0 = 6.2831853f * (float)k / sides, a1 = 6.2831853f * (float)(k + 1) / sides;
						visibility::addSegment(view, boundCentre + glm::vec2(std::cos(a0), std::sin(a0)) * bound,
							boundCentre + glm::vec2(std::cos(a1), std::sin(a1)) * bound);
					}
					segmentCount += sides;
					return;
				}
				for (size_t k = 0; k < outline.size(); k++)
				{
					visibility::addSegment(view, outline[k], outline[(k + 1) % outline.size()]);
				}
				segmentCount += (int)outline.size();
			});
			if (vision != Edges::Rocks) { addEdges(view, position, range); }
		}
	}
}

void updatePlayer(glm::vec2 position, bool cloaked)
{
	const auto started = std::chrono::steady_clock::now();
	segmentCount = 0;
	const float range = sightRange * (cloaked && cloakedSight == CloakedSight::Shorter ? cloakedRange : 1.f);
	build(playerView, position, range);
	buildMillis = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
}

bool playerSees(glm::vec2 point) { return visibility::sees(playerView, point); }
const visibility::PolarMap &playerMap() { return playerView; }
bool locksNeedSight() { return locks == Locks::SeenOnly; }

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

	if (showPolarMap && !playerView.distance.empty())
	{
		// The fan's rim, corner to corner, and every 16th slice's line, so
		// the slicing shows.
		const float width = 2.f / zoom;
		const int n = (int)playerView.distance.size();
		for (int i = 0; i < n; i++)
		{
			renderer.renderLine(visibility::corner(playerView, i), visibility::corner(playerView, i + 1),
				{0.4f, 0.85f, 1.f, 0.9f}, width);
			if (i % 16 == 0)
			{
				renderer.renderLine(playerView.origin, visibility::corner(playerView, i), {0.4f, 0.85f, 1.f, 0.25f}, width);
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
	{"sightRange", sightRange},
	{"slices", slices},
	{"silhouette", silhouette},
	{"cloakedSight", cloakedSight},
	{"cloakedRange", cloakedRange},
	{"onRock", onRock},
	{"locks", locks},
	{"missiles", missiles},
	{"showPolarMap", showPolarMap},
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
	choose("Missiles", missiles, {
		{"Cores only", Missiles::CoresOnly}, {"Like a shot", Missiles::LikeShot}});
	choose("Missile locks", locks, {
		{"Seen only", Locks::SeenOnly}, {"Any target", Locks::AnyTarget}});
	choose("Hidden outline", outline, {
		{"Mint", Outline::Mint}, {"Mint, amber when seen", Outline::MintAmber}});
	if (outline == Outline::MintAmber) { tune::ColorEdit3("Seen colour", &warningColour.x); }

	ImGui::SeparatorText("What the player sees");
	tune::SliderFloat("Sight range", &sightRange, 500.f, 20000.f, "%.0f units", ImGuiSliderFlags_Logarithmic);
	tune::SliderInt("Slices", &slices, 180, 2048);
	choose("Rock silhouettes", silhouette, {
		{"Exact outline", Silhouette::Exact}, {"Bounding circle", Silhouette::Circle}});
	choose("Cloaked sight", cloakedSight, {
		{"Unchanged", CloakedSight::Unchanged}, {"Shorter", CloakedSight::Shorter}});
	if (cloakedSight == CloakedSight::Shorter)
	{
		tune::SliderFloat("Cloaked range", &cloakedRange, 0.1f, 1.f, "%.2f of the range");
	}
	choose("On a rock", onRock, {{"See out", OnRock::SeeOut}, {"Blind", OnRock::Blind}});
	tune::Checkbox("Show polar map", &showPolarMap);
	ImGui::TextDisabled("  built in %.2f ms from %d segments", buildMillis, segmentCount);

	ImGui::Separator();
	tune::Checkbox("Show mask", &showMask);
	ImGui::SameLine();
	tune::Checkbox("Show sight lines", &showLines);
	ImGui::TextDisabled("The mask's cell size is with the field sliders, under Asteroids.");
}

}
