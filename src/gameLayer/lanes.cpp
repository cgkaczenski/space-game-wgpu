#include <lanes.h>
#include <tuning.h>

#include <interior.h>
#include <level.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace lanes
{

namespace
{
	enum class Rule
	{
		Off,       // lanes are only open space
		Current,   // pushed along, steering as ever
		Rail,      // carried along the line, steering only to leave
	};

	Rule rule = Rule::Current;
	float push = 4000.f;            // Current: units per second squared along the lane
	float hold = 1.5f;              // Current: per second, speed across the lane damped
	float railPush = 20000.f;
	float railHold = 10.f;
	float exitHalfLife = 0.6f;      // seconds for the speed carried out to halve its excess

	// Flight mode in a lane.
	float boost = 1500.f;           // speed added along the lane on entering it
	float trailSeconds = 0.3f;      // the ram's afterimages after, a shorter run than a ram's
	float reentryGrace = 1.f;       // seconds out of every lane before entering earns the kick again
	float slideTurn = 4.f;          // radians per second the velocity swings toward the nose, sliding

	glm::vec4 colour = {0.35f, 0.85f, 1.f, 0.35f};
	float edgePixels = 2.f;
	float dashPixels = 3.f;

	std::vector<level::Lane> current;

	// The nearest point of a lane's line within half its width, if any.
	struct Hit
	{
		int lane = -1;
		glm::vec2 direction = {};   // unit, along the segment, as the points run
		float distance = 1e30f;     // from the line
	};

	Hit find(const std::vector<level::Lane> &lanes, glm::vec2 p)
	{
		Hit best;
		for (int i = 0; i < (int)lanes.size(); i++)
		{
			const level::Lane &l = lanes[(size_t)i];
			const float half = l.width * 0.5f;
			for (size_t k = 0; k + 1 < l.points.size(); k++)
			{
				const glm::vec2 a = l.points[k], b = l.points[k + 1];
				const glm::vec2 ab = b - a;
				const float len2 = glm::dot(ab, ab);
				if (len2 < 1e-6f) { continue; }
				// A cheap reject before the exact distance.
				if (p.x < std::min(a.x, b.x) - half || p.x > std::max(a.x, b.x) + half
					|| p.y < std::min(a.y, b.y) - half || p.y > std::max(a.y, b.y) + half) { continue; }
				const float t = std::clamp(glm::dot(p - a, ab) / len2, 0.f, 1.f);
				const float d = glm::distance(p, a + ab * t);
				if (d <= half && d < best.distance) { best = {i, ab / std::sqrt(len2), d}; }
			}
		}
		return best;
	}
}

void start(const std::vector<level::Lane> &lanes) { current = lanes; }
const std::vector<level::Lane> &all() { return current; }

int laneAt(glm::vec2 point) { return find(current, point).lane; }

movement::Medium mediumFor(Rider &rider, const movement::Body &body, float gameDeltaTime)
{
	movement::Medium m = interior::at(body.position);
	const float speed = glm::length(body.velocity);
	const Hit hit = rule == Rule::Off ? Hit{} : find(current, body.position);
	rider.entered = false;
	if (hit.lane < 0)
	{
		rider.lane = -1;
		rider.outFor += gameDeltaTime;
	}
	if (hit.lane >= 0)
	{
		const level::Lane &lane = current[(size_t)hit.lane];
		// Either way: whichever way along the lane the ship is going, or, all
		// but still, facing.
		glm::vec2 dir = hit.direction;
		float along = glm::dot(body.velocity, dir);
		if (std::abs(along) < 50.f) { along = glm::dot(body.facing, dir); }
		if (along < 0.f) { dir = -dir; }
		const glm::vec2 across = body.velocity - dir * glm::dot(body.velocity, dir);
		const bool railed = rule == Rule::Rail;
		// Along the lane, its speed; across it, the ship's own.
		const float align = speed > 1.f ? std::abs(glm::dot(body.velocity / speed, dir)) : 1.f;
		m = {};
		m.topSpeed = railed ? lane.speed : 1.f + (std::max(lane.speed, 1.f) - 1.f) * align * align;
		m.settleHalfLife = exitHalfLife; // turning across it, the extra speed bleeds rather than snaps
		m.push = dir * (railed ? railPush : push) - across * (railed ? railHold : hold);
		rider.coasting = true;
		// In from outside every lane for a while: an entry. Hopping in and
		// out at a lane's edge is not.
		if (rider.lane < 0 && rider.outFor >= reentryGrace) { rider.entered = true; }
		rider.lane = hit.lane;
		rider.direction = dir;
		rider.outFor = 0.f;
		return m;
	}

	// Out of a lane, still faster than this place allows: thrown out at
	// speed, the excess bleeding off. In a field the field's own bleed does
	// that already (W2).
	if (rider.coasting)
	{
		const float open = movement::topSpeed(body.move);
		if (open > 0.f && speed > open * m.topSpeed + 1.f)
		{
			if (m.settleHalfLife <= 0.f) { m.settleHalfLife = exitHalfLife; }
			m.carriesExcess = true; // from the lane's speed, not first down to the ship's own
		}
		else { rider.coasting = false; }
	}
	return m;
}

float entryBoost() { return boost; }
float entryTrailSeconds() { return trailSeconds; }

void slide(movement::Body &body, float gameDeltaTime)
{
	const float speed = glm::length(body.velocity);
	if (speed < 100.f) { return; }
	const glm::vec2 heading = body.velocity / speed;
	body.velocity = movement::turnToward(heading, body.facing, slideTurn * gameDeltaTime) * speed;
}

namespace
{
	void drawLane(wgpu2d::Renderer2D &renderer, const level::Lane &l, glm::vec4 c, const glm::vec4 &view, float px)
	{
		const float half = l.width * 0.5f;
		const float dash = 260.f, gapLength = 420.f;
		for (size_t k = 0; k + 1 < l.points.size(); k++)
		{
			const glm::vec2 a = l.points[k], b = l.points[k + 1];
			if (std::max(a.x, b.x) + half < view.x || std::min(a.x, b.x) - half > view.x + view.z
				|| std::max(a.y, b.y) + half < view.y || std::min(a.y, b.y) - half > view.y + view.w) { continue; }
			const float length = glm::distance(a, b);
			if (length < 1.f) { continue; }
			const glm::vec2 dir = (b - a) / length;
			const glm::vec2 side = glm::vec2(-dir.y, dir.x) * half;
			renderer.renderLine(a + side, b + side, c, edgePixels * px);
			renderer.renderLine(a - side, b - side, c, edgePixels * px);
			for (float t = 0.f; t < length; t += dash + gapLength)
			{
				renderer.renderLine(a + dir * t, a + dir * std::min(t + dash, length), c * glm::vec4(1, 1, 1, 0.8f),
					dashPixels * px);
			}
		}
	}
}

void drawLanes(wgpu2d::Renderer2D &renderer, const std::vector<level::Lane> &lanes, int selected)
{
	if (lanes.empty()) { return; }
	const float px = 1.f / std::max(renderer.currentCamera.zoom, 0.001f);
	const glm::vec4 view = renderer.getViewRect();
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	for (int i = 0; i < (int)lanes.size(); i++)
	{
		glm::vec4 c = colour;
		if (i == selected) { c.a = std::min(1.f, c.a * 2.5f); }
		drawLane(renderer, lanes[(size_t)i], c, view, px);
	}
}

void draw(wgpu2d::Renderer2D &renderer, int riding) { drawLanes(renderer, current, riding); }

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("lanes", {
	{"rule", rule},
	{"push", push},
	{"hold", hold},
	{"railPush", railPush},
	{"railHold", railHold},
	{"exitHalfLife", exitHalfLife},
	{"boost", boost},
	{"trailSeconds", trailSeconds},
	{"reentryGrace", reentryGrace},
	{"slideTurn", slideTurn},
	{"colour", colour},
	{"edgePixels", edgePixels},
	{"dashPixels", dashPixels},
});

void debugUi()
{
	ImGui::Text("%d lanes this round", (int)current.size());
	int r = (int)rule;
	{
		tune::Highlight h(&rule); // the radios edit a copy
		ImGui::RadioButton("Off", &r, (int)Rule::Off);
		ImGui::SameLine();
		ImGui::RadioButton("Current", &r, (int)Rule::Current);
		ImGui::SameLine();
		ImGui::RadioButton("Rail", &r, (int)Rule::Rail);
	}
	rule = (Rule)r;
	if (rule == Rule::Current)
	{
		tune::SliderFloat("Push", &push, 0.f, 20000.f, "%.0f along the lane");
		tune::SliderFloat("Hold", &hold, 0.f, 10.f, "%.1f /s across it");
	}
	if (rule == Rule::Rail)
	{
		tune::SliderFloat("Rail push", &railPush, 0.f, 60000.f, "%.0f along the lane");
		tune::SliderFloat("Rail hold", &railHold, 0.f, 40.f, "%.1f /s across it");
	}
	tune::SliderFloat("Thrown out", &exitHalfLife, 0.05f, 3.f, "%.2f s for the extra speed to halve");
	ImGui::SeparatorText("Flight mode, in a lane");
	tune::SliderFloat("Entry boost", &boost, 0.f, 6000.f, "%.0f added along it");
	tune::SliderFloat("Entry trail", &trailSeconds, 0.f, 1.f, "%.2f s of the ram's afterimages");
	tune::SliderFloat("Re-entry grace", &reentryGrace, 0.f, 5.f, "%.1f s out of every lane first");
	tune::SliderFloat("Slide turn", &slideTurn, 0.5f, 15.f, "%.1f rad/s toward the nose (Shift)");
	ImGui::TextDisabled("  a lane's speed (times a ship's own) is the lane's, set in the editor");
	tune::ColorEdit4("Colour", &colour.x);
	tune::SliderFloat("Edges", &edgePixels, 0.f, 8.f, "%.1f px");
	tune::SliderFloat("Dashes", &dashPixels, 0.f, 8.f, "%.1f px");
}

}
