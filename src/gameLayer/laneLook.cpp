#include <laneLook.h>
#include <tuning.h>

#include <level.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace laneLook
{

namespace
{
	wgpu2d::Texture white;

	// Speed: "full" is this many times the ship's own top speed -- a lane's
	// default speed -- and the look scales from its own top speed up to it.
	float fullAt = 2.5f;
	float zoomOut = 0.3f;           // at full: the zoom divided by 1 + this
	float zoomRate = 2.f;           // per second, easing in and out
	float stretchAmount = 0.35f;    // at full: the hull this much longer

	float flashStrength = 0.35f;    // of the CRT's zoom blur
	float flashSeconds = 0.3f;

	bool streaksOn = true;
	glm::vec3 streakColour = {0.45f, 0.9f, 1.f};
	float streakIntensity = 1.4f;   // above 1 on purpose: what FinalGlow blooms
	float ridingBoost = 1.8f;       // the lane the player rides
	float streakSpacing = 900.f;    // world units of lane per streak
	float streakWidth = 70.f;
	float streakLengthMin = 300.f, streakLengthMax = 800.f;
	float streakSpeedMin = 700.f, streakSpeedMax = 1800.f;

	float zoomNow = 1.f;
	float stretchNow = 1.f;
	float flashLeft = 0.f;

	uint32_t hash(uint32_t x)
	{
		x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
		return x;
	}
	float unit(uint32_t &state) { state = hash(state + 0x9e3779b9U); return (state >> 8) * (1.f / 16777216.f); }
}

bool init()
{
	white.create1PxSquare();
	return white.id != 0;
}

void cleanup() { white.cleanup(); }

void reset()
{
	zoomNow = 1.f;
	stretchNow = 1.f;
	flashLeft = 0.f;
}

void entered() { flashLeft = flashSeconds; }

void update(float speed, float ownTopSpeed, bool riding, float realDeltaTime)
{
	float past = 0.f; // 0 at the ship's own top speed .. 1 at full
	if (riding && ownTopSpeed > 0.f && fullAt > 1.f)
	{
		past = std::clamp((speed / ownTopSpeed - 1.f) / (fullAt - 1.f), 0.f, 1.f);
	}
	const float ease = 1.f - std::exp(-zoomRate * std::max(realDeltaTime, 0.f));
	zoomNow += (1.f / (1.f + zoomOut * past) - zoomNow) * ease;
	stretchNow += (1.f + stretchAmount * past - stretchNow) * ease;

	flashLeft = std::max(0.f, flashLeft - realDeltaTime);
}

float zoomFactor() { return zoomNow; }
float stretch() { return stretchNow; }

float flash()
{
	if (flashSeconds <= 0.f) { return 0.f; }
	const float left = flashLeft / flashSeconds;
	return flashStrength * left * left; // a hard start, a quick fall
}

void drawStreaks(wgpu2d::Renderer2D &renderer, const std::vector<level::Lane> &lanes, int riding, float gameTime)
{
	if (!streaksOn || white.id == 0 || lanes.empty()) { return; }
	const glm::vec4 view = renderer.getViewRect();
	static std::vector<glm::vec2> positions, uvs;
	static std::vector<glm::vec4> colours;
	positions.clear(); uvs.clear(); colours.clear();

	for (int i = 0; i < (int)lanes.size(); i++)
	{
		const level::Lane &l = lanes[(size_t)i];
		const float boost = i == riding ? ridingBoost : 1.f;
		for (size_t k = 0; k + 1 < l.points.size(); k++)
		{
			const glm::vec2 a = l.points[k], b = l.points[k + 1];
			const float half = l.width * 0.5f;
			if (std::max(a.x, b.x) + half < view.x || std::min(a.x, b.x) - half > view.x + view.z
				|| std::max(a.y, b.y) + half < view.y || std::min(a.y, b.y) - half > view.y + view.w) { continue; }
			const float length = glm::distance(a, b);
			if (length < 1.f) { continue; }
			const glm::vec2 dir = (b - a) / length;
			const glm::vec2 side = {-dir.y, dir.x};
			const int count = std::max(1, (int)(length / std::max(streakSpacing, 50.f)));
			for (int j = 0; j < count; j++)
			{
				// Each streak its own from the lane, segment and number: where
				// across the lane, which way, how fast, how long.
				uint32_t r = hash((uint32_t)i * 73856093U ^ (uint32_t)k * 19349663U ^ (uint32_t)j * 83492791U);
				const float across = (unit(r) * 2.f - 1.f) * half * 0.8f;
				const float way = unit(r) < 0.5f ? -1.f : 1.f;
				const float speed = streakSpeedMin + (streakSpeedMax - streakSpeedMin) * unit(r);
				const float streak = streakLengthMin + (streakLengthMax - streakLengthMin) * unit(r);
				const float phase = unit(r) * length;
				float s = std::fmod(phase + way * speed * gameTime, length);
				if (s < 0.f) { s += length; }
				const glm::vec2 centre = a + dir * s + side * across;
				if (centre.x + streak < view.x || centre.x - streak > view.x + view.z
					|| centre.y + streak < view.y || centre.y - streak > view.y + view.w) { continue; }
				// Faded in and out near the segment's ends, where it wraps.
				const float edge = std::min(s, length - s);
				const float fade = std::clamp(edge / std::max(streak, 1.f), 0.f, 1.f);
				const glm::vec3 c = streakColour * (streakIntensity * boost);
				const float alpha = fade;
				if (alpha <= 0.f) { continue; }

				// A soft bar: a 3 x 3 grid, bright only at its middle, so its
				// ends and sides fall to nothing across each triangle.
				glm::vec2 p[3][3];
				float w[3][3];
				for (int u = 0; u < 3; u++)
				{
					for (int v = 0; v < 3; v++)
					{
						p[u][v] = centre + dir * (streak * 0.5f * (float)(u - 1)) + side * (streakWidth * 0.5f * (float)(v - 1));
						w[u][v] = (u == 1 && v == 1) ? 1.f : 0.f;
					}
				}
				auto vertex = [&](int u, int v)
				{
					positions.push_back(p[u][v]);
					uvs.push_back({0.5f, 0.5f});
					colours.push_back({c, alpha * w[u][v]});
				};
				for (int u = 0; u < 2; u++)
				{
					for (int v = 0; v < 2; v++)
					{
						vertex(u, v); vertex(u + 1, v); vertex(u + 1, v + 1);
						vertex(u, v); vertex(u + 1, v + 1); vertex(u, v + 1);
					}
				}
			}
		}
	}
	if (positions.empty()) { return; }
	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	renderer.renderTriangles(positions.data(), uvs.data(), colours.data(), positions.size(), white);
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("laneLook", {
	{"fullAt", fullAt},
	{"zoomOut", zoomOut},
	{"zoomRate", zoomRate},
	{"stretch", stretchAmount},
	{"flashStrength", flashStrength},
	{"flashSeconds", flashSeconds},
	{"streaks", streaksOn},
	{"streakColour", streakColour},
	{"streakIntensity", streakIntensity},
	{"ridingBoost", ridingBoost},
	{"streakSpacing", streakSpacing},
	{"streakWidth", streakWidth},
	{"streakLengthMin", streakLengthMin},
	{"streakLengthMax", streakLengthMax},
	{"streakSpeedMin", streakSpeedMin},
	{"streakSpeedMax", streakSpeedMax},
});

void debugUi()
{
	ImGui::Text("now: zoom x%.2f, stretch x%.2f, flash %.2f", zoomNow, stretchNow, flash());
	tune::SliderFloat("Full at", &fullAt, 1.2f, 6.f, "%.1f x the ship's own top speed");
	tune::SliderFloat("Zoom out", &zoomOut, 0.f, 1.f, "%.2f at full");
	tune::SliderFloat("Zoom ease", &zoomRate, 0.2f, 10.f, "%.1f /s");
	tune::SliderFloat("Stretch", &stretchAmount, 0.f, 1.5f, "%.2f longer at full");
	tune::SliderFloat("Entry flash", &flashStrength, 0.f, 1.f, "%.2f of the zoom blur (flight mode)");
	tune::SliderFloat("Flash time", &flashSeconds, 0.05f, 1.f, "%.2f s");
	ImGui::SeparatorText("Streaks");
	tune::Checkbox("Streaks", &streaksOn);
	tune::ColorEdit3("Streak colour", &streakColour.x);
	tune::SliderFloat("Intensity", &streakIntensity, 0.2f, 3.f, "%.2f (above ~1 blooms)");
	tune::SliderFloat("Riding", &ridingBoost, 1.f, 4.f, "%.1f x on the lane you are in");
	tune::SliderFloat("Spacing", &streakSpacing, 200.f, 4000.f, "%.0f units of lane each");
	tune::SliderFloat("Width", &streakWidth, 10.f, 300.f, "%.0f");
	tune::SliderFloat("Length min", &streakLengthMin, 50.f, 3000.f, "%.0f");
	tune::SliderFloat("Length max", &streakLengthMax, 50.f, 3000.f, "%.0f");
	tune::SliderFloat("Speed min", &streakSpeedMin, 0.f, 6000.f, "%.0f");
	tune::SliderFloat("Speed max", &streakSpeedMax, 0.f, 6000.f, "%.0f");
}

}
