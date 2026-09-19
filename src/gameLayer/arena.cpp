#include <arena.h>

#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>

namespace arena
{

namespace
{
	float arenaRadius = 0.f;

	// Units per second of velocity added, per unit past the edge, per second.
	// At 1 unit past, a small nudge; at 1000 past, enough to beat full thrust.
	float playerPush = 6.f;
	// Fraction of the overshoot an enemy is moved back each second.
	float enemyPull = 2.f;

	bool ringVisible = true;
	float ringWidthPixels = 3.f;

	// How far past the edge a point is, and the way back in.
	float overshoot(glm::vec2 position, glm::vec2 &inward)
	{
		const float d = glm::length(position);
		if (arenaRadius <= 0.f || d <= arenaRadius || d <= 0.f) { return 0.f; }
		inward = -position / d;
		return d - arenaRadius;
	}
}

void setRadius(float r) { arenaRadius = std::max(r, 0.f); }
float radius() { return arenaRadius; }

void pushPlayer(glm::vec2 position, glm::vec2 &velocity, float dt)
{
	glm::vec2 inward;
	const float out = overshoot(position, inward);
	if (out <= 0.f) { return; }
	velocity += inward * (out * playerPush * dt);
}

void pushEnemy(glm::vec2 &position, float dt)
{
	glm::vec2 inward;
	const float out = overshoot(position, inward);
	if (out <= 0.f) { return; }
	position += inward * (out * std::min(1.f, enemyPull * dt));
}

void draw(wgpu2d::Renderer2D &renderer, float zoom)
{
	if (!ringVisible || arenaRadius <= 0.f) { return; }

	// Two passes of the same circle: a wide dim one for a glow, a thin one for
	// the line. Widths in world units, divided by zoom so they hold on screen.
	// 256 segments: at a 20000 radius a chord is off the true circle by under
	// two units, far below a pixel at any zoom the view allows.
	const float px = 1.f / std::max(zoom, 0.01f);
	renderer.renderCircleOutline({0.f, 0.f}, {0.15f, 0.35f, 0.60f, 1.f}, arenaRadius,
		ringWidthPixels * 5.f * px, 256);
	renderer.renderCircleOutline({0.f, 0.f}, {0.35f, 0.65f, 1.00f, 1.f}, arenaRadius,
		ringWidthPixels * px, 256);
}

void debugUi()
{
	ImGui::Text("Arena radius: %.0f", arenaRadius);
	ImGui::SliderFloat("Edge push", &playerPush, 0.f, 30.f);
	ImGui::SliderFloat("Enemy pull", &enemyPull, 0.f, 10.f);
	ImGui::Checkbox("Edge ring", &ringVisible);
	ImGui::SliderFloat("Ring width px", &ringWidthPixels, 0.5f, 10.f);
}

}
