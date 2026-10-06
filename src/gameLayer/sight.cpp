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
	Beam beamRule = Beam::MiningTool;
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

	// ---- What the fog hides (S3) ----
	enum class UnseenEnemies { Hidden, Greyed };
	enum class UnseenExplosions { Greyed, Hidden };
	UnseenEnemies unseenEnemies = UnseenEnemies::Hidden;
	UnseenExplosions unseenExplosions = UnseenExplosions::Greyed;

	visibility::PolarMap playerView;
	float buildMillis = 0.f;        // the last rebuild, for the panel
	int segmentCount = 0;

	// ---- Looking out of a field ----
	//
	// From inside a field, within the viewer's cone, a line that leaves the
	// paint sees on past the edge -- less far than in the open. By default out
	// to a fixed radius from the viewer, so the cone ends in a clean arc
	// rather than in the field's own edge pushed outward. Behind that,
	// anything blocks as ever: another field's edge, a rock. Outside the cone,
	// and looking in from outside, the edge stays a wall. Only with vision
	// Both ways: Into fields only already sees out everywhere.
	enum class LookOut
	{
		Off,          // the edge is a wall from inside too
		PastEdge,     // a fixed distance past where the line leaves
		Peeking,      // that distance near the edge, less deeper in, none at the peek depth
		Fraction,     // the range cut to a fraction, measured from the viewer
		Arc,          // out to a fixed radius from the viewer: the cone ends in an arc
	};
	// What can block a line in the cone, before its reach runs out.
	enum class InCone
	{
		Nothing,      // the whole cone is seen, out to the reach: rocks, cores, more paint
		Blockers,     // rocks, cores and the edge of paint it comes back into still block
	};
	LookOut lookOut = LookOut::Arc;
	InCone inConeBlocks = InCone::Nothing;
	float lookOutDistance = 1500.f;
	float peekDepth = 1500.f;
	float lookOutFraction = 0.5f;
	float lookOutRadius = 2500.f;   // Arc: from the viewer, whatever shape the field is
	float playerConeDegrees = 90.f; // the player's cone, its full width, round the aim

	bool looksOut() { return lookOut != LookOut::Off && vision == Edges::BothWays; }

	// How far past the edge a line that crosses it `exit` from the viewer
	// sees, for this look: by the Looking out selection, or -- the scope's --
	// all the way to the look's range.
	float reachPast(float exit, const Look &look)
	{
		const float range = look.range;
		if (look.reachToRange) { return std::max(0.f, range - exit); }
		switch (lookOut)
		{
		case LookOut::PastEdge: return lookOutDistance;
		case LookOut::Peeking: return lookOutDistance * std::max(0.f, 1.f - exit / std::max(peekDepth, 1.f));
		case LookOut::Fraction: return std::max(0.f, range * lookOutFraction - exit);
		case LookOut::Arc: return std::max(0.f, lookOutRadius - exit);
		default: return 0.f;
		}
	}

	// ---- Seeing into a field, smoothing, and light (after the S3b playtests) ----

	// Looking into a field from outside, the player's cone (not an enemy's)
	// sees past the edge to the same reach, but the field's own rocks cast
	// shadows there: destroying them opens sight deeper in.
	enum class LookIn { Off, RocksBlock };
	LookIn lookIn = LookIn::RocksBlock;

	// The cone's sides fade over this many degrees: a slice near a side gets
	// part of the reach rather than all or none, so the cone does not flicker
	// as the aim moves a hair. And the cone turns toward the aim over this
	// long, rather than snapping to it.
	float coneSoftDegrees = 8.f;
	float coneEaseSeconds = 0.08f;
	glm::vec2 coneFacing = {1.f, 0.f};

	// The fog is drawn from an eased copy of the player's map: each slice
	// eases to where it now is over about this long, which hides the jumps of
	// rays crossing the grid's stair steps. The rules keep the exact map.
	float fogEaseSeconds = 0.06f;
	visibility::PolarMap drawnView;

	// Circles of sight this frame, apart from the player's own: what the
	// player's beam is burning (Beam lights).
	std::vector<Reveal> revealed;
	float beamLightRadius = 300.f;  // 0: the beam lights nothing

	// How much of the reach past an edge a line in this direction gets: 1
	// well inside the cone, 0 outside it, fading over coneSoftDegrees at its
	// sides.
	float coneWeight(glm::vec2 direction, const Look &look)
	{
		const float angle = std::acos(std::clamp(glm::dot(direction, look.facing), -1.f, 1.f));
		const float soft = glm::radians(std::max(coneSoftDegrees, 0.f));
		if (soft <= 0.f) { return angle <= look.halfAngle ? 1.f : 0.f; }
		return std::clamp((look.halfAngle + soft * 0.5f - angle) / soft, 0.f, 1.f);
	}

	bool seesIn(const Look *look) { return look && look->seesIntoFields && lookIn != LookIn::Off && vision != Edges::Rocks; }

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

	// edgeAlong for a line looking out of a field: the first crossing, if it
	// leaves the paint, does not block -- the line sees on past it for
	// reachPast -- and any crossing after it does.
	float edgeLookingOut(glm::vec2 from, glm::vec2 to, const Look &look, float weight)
	{
		float at = -1.f, exit = -1.f;
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
			if (side == previous) { return true; }
			if (exit < 0.f && previous >= 0 && side < 0)
			{
				exit = distance; // out of the field: seen past, for a while
				previous = side;
				return true;
			}
			at = distance;
			return false;
		});
		if (exit >= 0.f)
		{
			const float limit = exit + weight * reachPast(exit, look);
			if (limit < glm::distance(from, to)) { at = nearer(at, limit); }
		}
		return at;
	}

	// The first two edges a line from `from` to `to` crosses, and whether the
	// first leaves the paint (the viewer is inside, this is the way out) or
	// enters it (the viewer is outside, this is the way in).
	struct Crossings
	{
		float first = -1.f, second = -1.f;
		bool leaving = false, entering = false;
	};
	Crossings crossings(glm::vec2 from, glm::vec2 to)
	{
		Crossings c;
		bool start = true;
		int previous = -1;
		region::march(asteroids::paintMask(), from, to, [&](float distance, int label)
		{
			const int side = fields == Fields::Continuous ? (label >= 0 ? 0 : -1) : label;
			if (start)
			{
				start = false;
				previous = side;
				return true;
			}
			if (side == previous) { return true; }
			if (c.first < 0.f)
			{
				c.first = distance;
				c.leaving = previous >= 0 && side < 0;
				c.entering = previous < 0 && side >= 0;
				previous = side;
				return true;
			}
			c.second = distance;
			return false;
		});
		return c;
	}

	// The field rock a shot crossing an edge at `point` strikes, or -1.
	int edgeRock(glm::vec2 point, float radius)
	{
		if (shotStop != ShotStop::EdgeRock) { return -1; }
		return asteroids::hitCircle(point, radius + edgeRockReach, asteroids::Which::InPaint);
	}
}

float blockedAt(glm::vec2 from, glm::vec2 to, const Look *look)
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
	const glm::vec2 direction = line / length;
	const float weight = look ? coneWeight(direction, *look) : 0.f;
	if (weight > 0.f && (looksOut() || seesIn(look)))
	{
		// Measured along the viewer's whole sight, not only to the target,
		// so the map and this agree.
		const Crossings c = crossings(from, from + direction * look->range);
		if (looksOut() && c.leaving && inConeBlocks == InCone::Nothing)
		{
			// Looking out: the whole cone is seen out to the reach past where
			// its line leaves the field.
			const float limit = std::min(look->range, c.first + weight * reachPast(c.first, *look));
			return length > limit ? limit : -1.f;
		}
		if (seesIn(look) && c.entering)
		{
			// Looking in (the player): out to the reach past where it enters,
			// the field's far edge, and the field's own rocks on the way.
			float limit = std::min(look->range, c.first + weight * reachPast(c.first, *look));
			if (c.second >= 0.f) { limit = std::min(limit, c.second); }
			// A field rock poking out past the paint, under the viewer, is
			// looked out of, as a solid one is (S2).
			const int underField = onRock == OnRock::SeeOut ? asteroids::hitCircle(from, 0.f, asteroids::Which::InPaint) : -1;
			const float fieldRock = asteroids::raycast(from, direction, length, nullptr, asteroids::Which::InPaint, underField);
			return nearer(nearer(rock, fieldRock), length > limit ? limit : -1.f);
		}
	}
	const bool lookingOut = weight > 0.f && looksOut();
	return nearer(rock, lookingOut ? edgeLookingOut(from, to, *look, weight) : edgeAlong(from, to, vision));
}

bool clear(glm::vec2 from, glm::vec2 to, const Look *look) { return blockedAt(from, to, look) < 0.f; }

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
	//
	// `exits`, when given, takes the sides a line leaves a field by instead,
	// for looking out: the caller decides, slice by slice, how far past them
	// the line sees.
	void addEdges(visibility::PolarMap &map, glm::vec2 viewer, glm::vec2 boxMin, glm::vec2 boxMax,
		visibility::PolarMap *exits)
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
		const int x0 = std::max(-1, (int)std::floor((boxMin.x - mask.origin.x) / mask.cell));
		const int y0 = std::max(-1, (int)std::floor((boxMin.y - mask.origin.y) / mask.cell));
		const int x1 = std::min(mask.width - 1, (int)std::floor((boxMax.x - mask.origin.x) / mask.cell));
		const int y1 = std::min(mask.height - 1, (int)std::floor((boxMax.y - mask.origin.y) / mask.cell));
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
					const int near = viewerLeft ? here : right, far = viewerLeft ? right : here;
					if (blocks(near, far))
					{
						const bool leaving = exits && near >= 0 && far < 0;
						visibility::addSegment(leaving ? *exits : map, {line, top - seal}, {line, top + mask.cell + seal});
						segmentCount++;
					}
				}

				// The side below: a horizontal line at y + 1.
				const int below = side(x, y + 1);
				if (here != below)
				{
					const float line = top + mask.cell;
					const bool viewerAbove = viewer.y < line;
					const int near = viewerAbove ? here : below, far = viewerAbove ? below : here;
					if (blocks(near, far))
					{
						const bool leaving = exits && near >= 0 && far < 0;
						visibility::addSegment(leaving ? *exits : map, {left - seal, line}, {left + mask.cell + seal, line});
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
	// Over the box from `boxMin` to `boxMax`: the square round the viewer for
	// all-round sight, or just a cone's bounds for the scope's.
	void build(visibility::PolarMap &view, glm::vec2 position, float range, const Look *look,
		glm::vec2 boxMin, glm::vec2 boxMax)
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
			asteroids::outlinesIn(boxMin, boxMax, which,
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
			if (vision != Edges::Rocks && !(look && (looksOut() || seesIn(look)))) { addEdges(view, position, boxMin, boxMax, nullptr); }
			else if (vision != Edges::Rocks)
			{
				// Looking out: the edges go to maps of their own -- the sides a
				// line leaves a field by, and every other side that blocks --
				// so each slice can tell whether its first crossing is the way
				// out. In the cone, if it is, the line sees past it for the
				// reach: through everything, or still stopped by rocks and by
				// paint it comes back into (In the cone). Otherwise the edges
				// block as ever.
				static visibility::PolarMap exits, others, fieldRocks;
				visibility::begin(exits, position, range, (int)view.distance.size());
				visibility::begin(others, position, range, (int)view.distance.size());
				addEdges(others, position, boxMin, boxMax, &exits);

				// Seeing in (the player, from outside the paint): the field's
				// own rocks, which only cast shadows past the way in.
				const region::Mask &mask = asteroids::paintMask();
				const bool outside = region::labelAt(mask, position) < 0;
				const bool lookingIn = outside && seesIn(look);
				if (lookingIn)
				{
					visibility::begin(fieldRocks, position, range, (int)view.distance.size());
					const int underField = onRock == OnRock::SeeOut ? asteroids::hitCircle(position, 0.f, asteroids::Which::InPaint) : -1;
					asteroids::outlinesIn(boxMin, boxMax, asteroids::Which::InPaint,
						[&](int rock, const std::vector<glm::vec2> &outline, glm::vec2, float)
					{
						if (rock == underField) { return; } // looked out of (S2)
						for (size_t k = 0; k < outline.size(); k++)
						{
							visibility::addSegment(fieldRocks, outline[k], outline[(k + 1) % outline.size()]);
						}
						segmentCount += (int)outline.size();
					});
				}

				for (int i = 0; i < (int)view.distance.size(); i++)
				{
					float &slice = view.distance[(size_t)i];
					const float exit = exits.distance[(size_t)i];
					const float other = others.distance[(size_t)i];
					const float weight = coneWeight(visibility::direction(view, i), *look);
					if (weight > 0.f && looksOut() && exit < range && exit < other)
					{
						const float limit = std::min(range, exit + weight * reachPast(exit, *look));
						slice = inConeBlocks == InCone::Nothing ? limit : std::min(slice, std::min(other, limit));
					}
					else if (weight > 0.f && lookingIn && other < range && other < exit)
					{
						// In through the edge (`other`, the way in), to the
						// reach, the far edge (`exit`), and the field's rocks.
						const float limit = std::min(range, other + weight * reachPast(other, *look));
						slice = std::min(std::min(slice, fieldRocks.distance[(size_t)i]), std::min(limit, exit));
					}
					else
					{
						slice = std::min(slice, std::min(exit, other));
					}
				}
			}
		}
	}
}

void updatePlayer(glm::vec2 position, bool cloaked, glm::vec2 aim, float realDeltaTime, const Scope *scope)
{
	const auto started = std::chrono::steady_clock::now();
	segmentCount = 0;
	revealed.clear();
	float range = sightRange * (cloaked && cloakedSight == CloakedSight::Shorter ? cloakedRange : 1.f);
	// Scoped, the all-round sight shrinks toward a small circle (S4b).
	if (scope) { range += (std::min(scope->aroundRadius, range) - range) * scope->amount; }

	// The cone turns toward the aim rather than snapping to it.
	// By angle: blending the two directions and normalising would barely
	// move a cone facing away from the aim, and not at all one facing exactly
	// away; turning by a share of the angle between them eases any turn alike.
	const glm::vec2 want = glm::length(aim) > 0.f ? glm::normalize(aim) : coneFacing;
	const float follow = coneEaseSeconds > 0.f ? 1.f - std::exp(-realDeltaTime / coneEaseSeconds) : 1.f;
	const float between = std::atan2(coneFacing.x * want.y - coneFacing.y * want.x, glm::dot(coneFacing, want));
	const float turn = between * follow, c = std::cos(turn), sn = std::sin(turn);
	coneFacing = glm::normalize(glm::vec2(coneFacing.x * c - coneFacing.y * sn, coneFacing.x * sn + coneFacing.y * c));

	Look look;
	look.facing = coneFacing;
	look.halfAngle = glm::radians(playerConeDegrees * 0.5f);
	look.range = range;
	look.seesIntoFields = true;
	build(playerView, position, range, &look, position - glm::vec2(range), position + glm::vec2(range));

	// The scope's cone (S4b): a map of its own over the cone's bounds alone,
	// merged in slice by slice -- the further of the two, blended at the
	// cone's soft sides and by how far in the scope is. Two shapes seen from
	// one point make one that is still all visible from it.
	if (scope && scope->amount > 0.f)
	{
		const Look &cone = scope->cone;
		const float facing = std::atan2(cone.facing.y, cone.facing.x);
		const float reach = cone.halfAngle + glm::radians(coneSoftDegrees * 0.5f);
		glm::vec2 lo = position, hi = position;
		auto include = [&](float angle)
		{
			const glm::vec2 p = position + glm::vec2(std::cos(angle), std::sin(angle)) * cone.range;
			lo = glm::min(lo, p);
			hi = glm::max(hi, p);
		};
		include(facing - reach);
		include(facing + reach);
		for (int k = 0; k < 4; k++)
		{
			// The arc's extremes, where the cone takes in an axis.
			const float axis = 1.5707963f * (float)k;
			if (std::fabs(std::remainder(axis - facing, 6.2831853f)) <= reach) { include(axis); }
		}
		static visibility::PolarMap scoped;
		build(scoped, position, cone.range, &cone, lo, hi);
		for (int i = 0; i < (int)playerView.distance.size(); i++)
		{
			const float w = coneWeight(visibility::direction(playerView, i), cone) * scope->amount;
			float &slice = playerView.distance[(size_t)i];
			if (w > 0.f && scoped.distance[(size_t)i] > slice) { slice += (scoped.distance[(size_t)i] - slice) * w; }
		}
	}

	// The drawn copy eases toward it -- or is it outright, after a jump.
	if (drawnView.distance.size() != playerView.distance.size()
		|| glm::distance(drawnView.origin, position) > 400.f || fogEaseSeconds <= 0.f)
	{
		drawnView = playerView;
	}
	else
	{
		const float ease = 1.f - std::exp(-realDeltaTime / fogEaseSeconds);
		drawnView.origin = position;
		drawnView.range = range;
		for (size_t i = 0; i < drawnView.distance.size(); i++)
		{
			drawnView.distance[i] += (playerView.distance[i] - drawnView.distance[i]) * ease;
		}
	}
	buildMillis = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - started).count();
}

bool playerSees(glm::vec2 point)
{
	if (visibility::sees(playerView, point)) { return true; }
	for (const Reveal &r : revealed)
	{
		if (glm::distance(point, r.centre) <= r.radius) { return true; }
	}
	return false;
}
const visibility::PolarMap &playerMap() { return playerView; }
const visibility::PolarMap &playerDrawnMap() { return drawnView; }

void reveal(glm::vec2 centre, float radius)
{
	if (radius > 0.f) { revealed.push_back({centre, radius}); }
}
const std::vector<Reveal> &reveals() { return revealed; }
float beamLight() { return beamLightRadius; }
bool locksNeedSight() { return locks == Locks::SeenOnly; }

bool playerSeesShip(glm::vec2 centre, float radius)
{
	if (playerSees(centre)) { return true; }
	for (int k = 0; k < 8; k++)
	{
		const float a = 0.7853982f * (float)k;
		if (playerSees(centre + glm::vec2(std::cos(a), std::sin(a)) * radius)) { return true; }
	}
	return false;
}

bool hidesUnseenEnemies() { return unseenEnemies == UnseenEnemies::Hidden; }

bool explosionShown(glm::vec2 point)
{
	return unseenExplosions == UnseenExplosions::Greyed || playerSees(point);
}

float beamSeenFrom(glm::vec2 start, glm::vec2 end)
{
	// Sampled along it: the sight is a fan, so a line can pass into it and
	// out again, and the first point in is what matters.
	constexpr int steps = 64;
	const float length = glm::distance(start, end);
	for (int k = 0; k <= steps; k++)
	{
		const float t = (float)k / steps;
		if (playerSees(start + (end - start) * t)) { return t * length; }
	}
	return -1.f;
}

bool warnsWhenSeen() { return outline == Outline::MintAmber; }
glm::vec3 seenColour() { return warningColour; }

void drawDebug(wgpu2d::Renderer2D &renderer, const std::vector<Viewer> &viewers, glm::vec2 target)
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
		for (const Viewer &viewer : viewers)
		{
			const glm::vec2 from = viewer.position;
			const float blocked = blockedAt(from, target, &viewer.look);
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
	{"unseenEnemies", unseenEnemies},
	{"lookOut", lookOut},
	{"inCone", inConeBlocks},
	{"lookIn", lookIn},
	{"coneSoftDegrees", coneSoftDegrees},
	{"coneEaseSeconds", coneEaseSeconds},
	{"fogEaseSeconds", fogEaseSeconds},
	{"beamLightRadius", beamLightRadius},
	{"lookOutDistance", lookOutDistance},
	{"peekDepth", peekDepth},
	{"lookOutFraction", lookOutFraction},
	{"lookOutRadius", lookOutRadius},
	{"playerConeDegrees", playerConeDegrees},
	{"unseenExplosions", unseenExplosions},
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
	choose("Looking out of a field", lookOut, {
		{"Off", LookOut::Off}, {"Arc round the ship", LookOut::Arc}, {"Distance past edge", LookOut::PastEdge},
		{"Peeking", LookOut::Peeking}, {"Fraction of range", LookOut::Fraction}});
	if (lookOut != LookOut::Off)
	{
		if (vision != Edges::BothWays) { ImGui::TextDisabled("  only with vision Both ways"); }
		if (lookOut == LookOut::Arc)
		{
			tune::SliderFloat("Cone radius", &lookOutRadius, 200.f, 8000.f, "%.0f units from the ship");
		}
		if (lookOut == LookOut::PastEdge || lookOut == LookOut::Peeking)
		{
			tune::SliderFloat("Past the edge", &lookOutDistance, 0.f, 6000.f, "%.0f units");
		}
		if (lookOut == LookOut::Peeking)
		{
			tune::SliderFloat("Peek depth", &peekDepth, 100.f, 6000.f, "%.0f deep: nothing past the edge");
		}
		if (lookOut == LookOut::Fraction)
		{
			tune::SliderFloat("Looking-out range", &lookOutFraction, 0.05f, 1.f, "%.2f of the range");
		}
		choose("In the cone", inConeBlocks, {
			{"Nothing blocks", InCone::Nothing}, {"Rocks and edges block", InCone::Blockers}});
		tune::SliderFloat("Player's cone", &playerConeDegrees, 10.f, 360.f, "%.0f degrees round the aim");
		tune::SliderFloat("Cone softness", &coneSoftDegrees, 0.f, 45.f, "%.0f degrees at each side");
		tune::SliderFloat("Cone turn", &coneEaseSeconds, 0.f, 0.5f, "%.2f s behind the aim");
		ImGui::TextDisabled("  an enemy's cone is its own sight cone");
	}
	choose("Looking into a field", lookIn, {
		{"Off", LookIn::Off}, {"Rocks block, to the reach", LookIn::RocksBlock}});
	ImGui::TextDisabled("  the player's cone only; enemies see the edge as a wall");
	tune::SliderFloat("Fog smoothing", &fogEaseSeconds, 0.f, 0.3f, "%.2f s (0: exact)");
	tune::SliderFloat("Beam lights", &beamLightRadius, 0.f, 1500.f, "%.0f units round what it burns");
	choose("Unseen enemies", unseenEnemies, {
		{"Hidden", UnseenEnemies::Hidden}, {"Greyed (debug)", UnseenEnemies::Greyed}});
	choose("Unseen explosions", unseenExplosions, {
		{"Greyed", UnseenExplosions::Greyed}, {"Hidden", UnseenExplosions::Hidden}});
	ImGui::TextDisabled("The fog's look is under World grade.");
	tune::Checkbox("Show polar map", &showPolarMap);
	ImGui::TextDisabled("  built in %.2f ms from %d segments", buildMillis, segmentCount);

	ImGui::Separator();
	tune::Checkbox("Show mask", &showMask);
	ImGui::SameLine();
	tune::Checkbox("Show sight lines", &showLines);
	ImGui::TextDisabled("The mask's cell size is with the field sliders, under Asteroids.");
}

}
