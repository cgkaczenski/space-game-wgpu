#include <theirGhost.h>
#include <tuning.h>

#include <outline.h>
#include <shipSprite.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <algorithm>

namespace theirGhost
{

namespace
{
	// Whose beliefs are drawn: none; every searching enemy's, as Splinter
	// Cell: Conviction draws them -- the player learns they were seen even by
	// an enemy they never saw; or only those of enemies the player knows of.
	enum class Show { Off, AllSearching, Known };
	Show show = Show::AllSearching;

	float mergeDistance = 300.f;   // beliefs closer than this are one ghost
	float alpha = 0.45f;           // one enemy's ghost
	float perExtra = 0.15f;        // stronger for each enemy more behind it
	glm::vec3 colour = {0.78f, 0.86f, 1.f};

	struct Belief
	{
		glm::vec2 position;
		glm::vec2 facing;
		int count;
	};
}

void draw(wgpu2d::Renderer2D &renderer, const std::vector<Enemy> &enemies,
	const std::function<bool(const Enemy &)> &known,
	wgpu2d::Texture shipSheet, glm::vec4 playerCell, float shipSize)
{
	if (show == Show::Off) { return; }

	// Each searching enemy's belief, merged with any already close to it.
	static std::vector<Belief> beliefs;
	beliefs.clear();
	for (const Enemy &e : enemies)
	{
		if (e.awareness != Enemy::Awareness::Searching) { continue; }
		if (show == Show::Known && !known(e)) { continue; }
		auto near = std::find_if(beliefs.begin(), beliefs.end(), [&](const Belief &b)
		{
			return glm::distance(b.position, e.lastKnown) <= mergeDistance;
		});
		if (near != beliefs.end()) { near->count++; continue; }
		beliefs.push_back({e.lastKnown, e.lastKnownFacing, 1});
	}
	if (beliefs.empty()) { return; }

	for (const Belief &b : beliefs)
	{
		const float a = std::min(1.f, alpha + perExtra * (float)(b.count - 1));
		outline::begin(renderer, 0.f, colour);
		renderSpaceShip(renderer, b.position, shipSize, shipSheet, playerCell, b.facing, {1.f, 1.f, 1.f, a});
		outline::end(renderer);
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("theirGhost", {
	{"show", show},
	{"mergeDistance", mergeDistance},
	{"alpha", alpha},
	{"perExtra", perExtra},
	{"colour", colour},
});

void debugUi()
{
	int s = (int)show;
	{
		tune::Highlight h(&show); // the radios edit a copy
		ImGui::TextUnformatted("Show where enemies think you are");
		ImGui::RadioButton("Off", &s, (int)Show::Off); ImGui::SameLine();
		ImGui::RadioButton("Every searching enemy", &s, (int)Show::AllSearching); ImGui::SameLine();
		ImGui::RadioButton("Enemies you know of", &s, (int)Show::Known);
	}
	show = (Show)s;
	if (show == Show::Off) { return; }
	tune::SliderFloat("Merge within", &mergeDistance, 0.f, 2000.f, "%.0f units");
	tune::SliderFloat("Strength", &alpha, 0.05f, 1.f, "%.2f");
	tune::SliderFloat("Per extra enemy", &perExtra, 0.f, 0.5f, "%.2f");
	tune::ColorEdit3("Their ghost colour", &colour.x);
}

}
