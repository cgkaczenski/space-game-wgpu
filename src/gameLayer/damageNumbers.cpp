#include <damageNumbers.h>
#include <textLook.h>
#include <tuning.h>

#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace damageNumbers
{

namespace
{
	struct Number
	{
		unsigned int id = 0;
		glm::vec2 at = {};      // world: the latest hit's point
		float amount = 0.f;     // life taken, summed while hits keep coming
		float age = 0.f;        // since the latest hit
	};

	std::vector<Number> numbers;

	// Tuning.
	bool on = true;
	float mergeSeconds = 0.35f;   // a hit this soon after the last adds to it
	float lifeSeconds = 0.9f;     // from the latest hit to gone
	float fadeFrom = 0.6f;        // of the life: fading after this
	float riseScreens = 0.04f;    // of the screen height, over the life
	float lift = 0.03f;           // of the screen height: starts this far above the hit
	int sizeStep = 1;             // textLook::screenScale's size
	glm::vec3 colour = {1.f, 0.92f, 0.65f};
	int limit = 64;
}

void reset()
{
	numbers.clear();
}

void hit(unsigned int id, glm::vec2 at, float amount)
{
	if (!on || amount <= 0.f) { return; }

	for (Number &n : numbers)
	{
		if (n.id == id && n.age < mergeSeconds)
		{
			n.amount += amount;
			n.at = at;     // follows a moving target while the hits keep coming
			n.age = 0.f;
			return;
		}
	}
	if ((int)numbers.size() >= limit) { numbers.erase(numbers.begin()); }
	numbers.push_back({id, at, amount, 0.f});
}

void update(float gameDeltaTime)
{
	for (Number &n : numbers) { n.age += gameDeltaTime; }
	numbers.erase(std::remove_if(numbers.begin(), numbers.end(),
		[](const Number &n) { return n.age >= lifeSeconds; }), numbers.end());
}

void draw(wgpu2d::Renderer2D &renderer, glm::vec4 view, int width, int height, const Shown &shown)
{
	if (numbers.empty() || view.z == 0.f || view.w == 0.f) { return; }

	const float scale = textLook::screenScale(height, sizeStep);
	char text[16];

	renderer.pushCamera();
	for (const Number &n : numbers)
	{
		if (shown && !shown(n.at)) { continue; }

		const float t = std::min(n.age / lifeSeconds, 1.f);
		const float rise = 1.f - (1.f - t) * (1.f - t); // fast, then settling
		const float alpha = t < fadeFrom ? 1.f : 1.f - (t - fadeFrom) / std::max(1.f - fadeFrom, 1e-3f);

		const glm::vec2 onScreen = {(n.at.x - view.x) / view.z * (float)width,
			(n.at.y - view.y) / view.w * (float)height - (lift + riseScreens * rise) * (float)height};
		if (onScreen.x < -100.f || onScreen.x > width + 100.f || onScreen.y < -100.f || onScreen.y > height + 100.f)
		{
			continue;
		}

		std::snprintf(text, sizeof(text), "%d", std::max(1, (int)std::lround(n.amount * 100.f)));
		textLook::draw(renderer, onScreen, text, {colour, alpha}, scale, {0.5f, 1.f});
	}
	renderer.popCamera();
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("damageNumbers", {
	{"on", on},
	{"mergeSeconds", mergeSeconds},
	{"lifeSeconds", lifeSeconds},
	{"fadeFrom", fadeFrom},
	{"riseScreens", riseScreens},
	{"lift", lift},
	{"sizeStep", sizeStep},
	{"colour", colour},
});

void debugUi()
{
	tune::Checkbox("Damage numbers", &on);
	ImGui::SameLine();
	ImGui::TextDisabled("%d showing", (int)numbers.size());
	tune::SliderFloat("Merge window", &mergeSeconds, 0.f, 1.5f, "%.2f s");
	tune::SliderFloat("Life", &lifeSeconds, 0.2f, 3.f, "%.2f s");
	tune::SliderFloat("Fade from", &fadeFrom, 0.f, 1.f, "%.2f of the life");
	tune::SliderFloat("Rise", &riseScreens, 0.f, 0.2f, "%.3f screens");
	tune::SliderFloat("Lift", &lift, 0.f, 0.1f, "%.3f screens");
	tune::SliderInt("Size", &sizeStep, 1, 4);
	tune::ColorEdit3("Colour", &colour.x);
}

}
