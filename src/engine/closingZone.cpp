#include <engine/closingZone.h>

#include <glm/geometric.hpp>
#include <algorithm>
#include <utility>

namespace zone
{

namespace
{
	Circle lerp(Circle a, Circle b, float t)
	{
		return {a.centre + (b.centre - a.centre) * t, a.radius + (b.radius - a.radius) * t};
	}

	// Into the next phase: a hold closes, a close lands and the next stage
	// holds from where it landed.
	void advance(ClosingZone &zone)
	{
		if (zone.phase == Phase::Holding)
		{
			zone.phase = Phase::Closing;
		}
		else if (zone.phase == Phase::Closing)
		{
			zone.from = zone.stages[zone.stage].target;
			zone.stage++;
			zone.phase = zone.stage < (int)zone.stages.size() ? Phase::Holding : Phase::Done;
		}
		zone.elapsed = 0.f;
	}
}

float closeSeconds(Circle from, Circle to, float speed)
{
	// The furthest any point of the edge travels: the side the centre moves
	// away from goes by the shift plus the shrink.
	const float travel = glm::length(to.centre - from.centre) + std::max(from.radius - to.radius, 0.f);
	return travel / std::max(speed, 0.001f);
}

ClosingZone begin(Circle start, std::vector<Stage> stages)
{
	ClosingZone zone;
	zone.stages = std::move(stages);
	zone.from = start;
	zone.phase = zone.stages.empty() ? Phase::Done : Phase::Holding;
	return zone;
}

float phaseSeconds(const ClosingZone &zone)
{
	if (zone.phase == Phase::Done) { return 0.f; }
	const Stage &s = zone.stages[zone.stage];
	return zone.phase == Phase::Holding ? s.holdSeconds : closeSeconds(zone.from, s.target, s.closeSpeed);
}

float phaseProgress(const ClosingZone &zone)
{
	if (zone.phase == Phase::Done) { return 1.f; }
	const float length = phaseSeconds(zone);
	return length > 0.f ? std::clamp(zone.elapsed / length, 0.f, 1.f) : 1.f;
}

void update(ClosingZone &zone, float dt)
{
	zone.elapsed += std::max(dt, 0.f);
	// A loop, not an if: one long delta can finish more than one phase. Each
	// pass either returns or moves the phase on, and Done ends it.
	while (zone.phase != Phase::Done)
	{
		const float length = phaseSeconds(zone);
		if (zone.elapsed < length) { return; }
		const float carry = zone.elapsed - length;
		advance(zone);
		zone.elapsed = carry;
	}
	zone.elapsed = 0.f;
}

void skipPhase(ClosingZone &zone)
{
	if (zone.phase != Phase::Done) { advance(zone); }
}

Circle current(const ClosingZone &zone)
{
	if (zone.phase != Phase::Closing) { return zone.from; }
	return lerp(zone.from, zone.stages[zone.stage].target, phaseProgress(zone));
}

Circle next(const ClosingZone &zone)
{
	if (zone.phase == Phase::Done) { return zone.from; }
	return zone.stages[zone.stage].target;
}

float outside(Circle circle, glm::vec2 point)
{
	return std::max(glm::length(point - circle.centre) - circle.radius, 0.f);
}

}
