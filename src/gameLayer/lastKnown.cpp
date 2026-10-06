#include <lastKnown.h>
#include <tuning.h>

#include <engine/contactMemory.h>
#include <outline.h>
#include <shipSprite.h>
#include <sight.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace lastKnown
{

namespace
{
	// How long a ghost lasts: until its spot is looked at again and found
	// empty, that or a number of seconds, or until its enemy is seen again.
	enum class Lasts { UntilChecked, Fades, Forever };
	// How a ghost shows its age: not at all, or fading toward faint.
	enum class Ageing { None, Fade };

	Lasts lasts = Lasts::UntilChecked;
	float fadeSeconds = 20.f;      // Fades: gone after this long
	Ageing ageing = Ageing::Fade;
	float ageSeconds = 20.f;       // Fade: this long to reach its faintest
	float faintest = 0.3f;
	bool headingLine = true;
	bool offScreenArrows = true;   // a HUD chevron toward each ghost off screen
	float headingSeconds = 0.6f;   // the line is where the velocity would take it in this long
	glm::vec3 colour = {0.85f, 0.35f, 0.32f};

	contacts::Memory memory;
	float now = 0.f;               // game time, this round

	// A ghost's look, kept after its enemy is gone.
	struct Look
	{
		glm::vec4 cell = {};
		float size = 0.f;
	};
	std::unordered_map<unsigned, Look> looks;
}

void reset()
{
	contacts::clear(memory);
	looks.clear();
	now = 0.f;
}

void update(const std::vector<Enemy> &enemies, const std::function<bool(const Enemy &)> &seen,
	const std::function<glm::vec4(const Enemy &)> &cellOf, float gameDeltaTime)
{
	now += gameDeltaTime;
	for (const Enemy &e : enemies)
	{
		const bool sees = seen(e);
		if (sees) { looks[e.id] = {cellOf(e), e.size}; }
		contacts::observe(memory, e.id, sees, {e.body.position, e.body.facing, e.body.velocity}, now);
	}
	contacts::check(memory, [](glm::vec2 p) { return sight::playerSees(p); }, now,
		lasts != Lasts::Forever, lasts == Lasts::Fades ? fadeSeconds : 0.f);
}

namespace
{
	// Older is fainter: toward `faintest` over the age, or toward nothing over
	// the time a ghost lasts.
	float alphaOf(const contacts::Ghost &g)
	{
		const float age = now - g.lostAt;
		if (lasts == Lasts::Fades) { return std::clamp(1.f - age / std::max(fadeSeconds, 0.01f), 0.f, 1.f); }
		if (ageing == Ageing::Fade)
		{
			return 1.f - (1.f - faintest) * std::clamp(age / std::max(ageSeconds, 0.01f), 0.f, 1.f);
		}
		return 1.f;
	}
}

void forEachGhost(const std::function<void(glm::vec2 position, float alpha)> &visit)
{
	for (const contacts::Ghost &g : memory.ghosts) { visit(g.last.position, alphaOf(g)); }
}

glm::vec3 ghostColour() { return colour; }
bool arrowsShown() { return offScreenArrows; }

void draw(wgpu2d::Renderer2D &renderer, wgpu2d::Texture shipSheet)
{
	if (memory.ghosts.empty()) { return; }
	const float px = 1.f / std::max(renderer.currentCamera.zoom, 1e-4f);
	for (const contacts::Ghost &g : memory.ghosts)
	{
		const auto look = looks.find(g.id);
		if (look == looks.end()) { continue; }

		const float alpha = alphaOf(g);

		outline::begin(renderer, 0.f, colour);
		renderSpaceShip(renderer, g.last.position, look->second.size, shipSheet, look->second.cell,
			g.last.facing, {1.f, 1.f, 1.f, alpha});
		outline::end(renderer);

		const float speed = glm::length(g.last.velocity);
		if (headingLine && speed > 20.f)
		{
			const float length = std::min(speed * headingSeconds, 900.f);
			renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
			renderer.renderLine(g.last.position, g.last.position + g.last.velocity / speed * length,
				{colour, 0.6f * alpha}, 2.f * px);
		}
	}
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("lastKnown", {
	{"lasts", lasts},
	{"fadeSeconds", fadeSeconds},
	{"ageing", ageing},
	{"ageSeconds", ageSeconds},
	{"faintest", faintest},
	{"headingLine", headingLine},
	{"offScreenArrows", offScreenArrows},
	{"headingSeconds", headingSeconds},
	{"colour", colour},
});

void debugUi()
{
	ImGui::Text("%d ghosts", (int)memory.ghosts.size());
	{
		int l = (int)lasts;
		{
			tune::Highlight h(&lasts); // the radios edit a copy
			ImGui::TextUnformatted("Ghost lasts");
			ImGui::RadioButton("Until checked", &l, (int)Lasts::UntilChecked); ImGui::SameLine();
			ImGui::RadioButton("Fades", &l, (int)Lasts::Fades); ImGui::SameLine();
			ImGui::RadioButton("Forever", &l, (int)Lasts::Forever);
		}
		lasts = (Lasts)l;
	}
	if (lasts == Lasts::Fades)
	{
		tune::SliderFloat("Gone after", &fadeSeconds, 1.f, 120.f, "%.0f s");
	}
	else
	{
		int a = (int)ageing;
		{
			tune::Highlight h(&ageing);
			ImGui::TextUnformatted("Ageing"); ImGui::SameLine();
			ImGui::RadioButton("None", &a, (int)Ageing::None); ImGui::SameLine();
			ImGui::RadioButton("Fade", &a, (int)Ageing::Fade);
		}
		ageing = (Ageing)a;
		if (ageing == Ageing::Fade)
		{
			tune::SliderFloat("Fades over", &ageSeconds, 1.f, 120.f, "%.0f s");
			tune::SliderFloat("Faintest", &faintest, 0.f, 1.f, "%.2f");
		}
	}
	tune::Checkbox("Heading line", &headingLine);
	if (headingLine) { tune::SliderFloat("Heading reach", &headingSeconds, 0.1f, 3.f, "%.1f s of its speed"); }
	tune::ColorEdit3("Ghost colour", &colour.x);
	tune::Checkbox("Off-screen arrows", &offScreenArrows);
}

}
