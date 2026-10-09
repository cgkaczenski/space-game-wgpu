#include <hints.h>
#include <textLook.h>
#include <platformInput.h>

#include "imgui.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace hints
{

namespace
{
	struct Pending
	{
		bool world = false;
		glm::vec2 at = {};            // world, or a HUD element's rect centre below
		hud::Element element = hud::Element::Count;
		std::string markup;
		float ringRadius = 0.f;
	};
	std::vector<Pending> pending;

	// The gap between what a bubble points at and the bubble's nearest edge,
	// as a fraction of the screen's height: short enough that the tail reads
	// as a pointer.
	const float offsetPerc = 0.06f;
	// Up and to the right of a world point, by default: away from the HUD's
	// weapon row along the bottom.
	const glm::vec2 worldOffsetDir = glm::normalize(glm::vec2(0.6f, -1.f));

	// The test hints (debug panel).
	bool testWorld = false;
	bool testHud = false;
	bool testOffScreen = false;
	float testAhead = 1500.f;

	float pulse()
	{
		static const auto start = std::chrono::steady_clock::now();
		const float t = std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count();
		return 0.5f - 0.5f * std::cos(6.2831853f * 1.6f * t);
	}

	wgpu2d::CalloutStyle style(int height)
	{
		const glm::vec3 c = textLook::hintColour;
		wgpu2d::CalloutStyle s;
		s.font = &textLook::font();
		s.scale = textLook::screenScale(height);
		s.text = {0.92f, 1.f, 0.96f, 1.f};
		s.frame = {c, 1.f};
		s.fill = {0.02f, 0.07f, 0.06f, 1.f};
		s.capFrame = {c, 1.f};
		s.capFill = {0.05f, 0.16f, 0.13f, 1.f};
		s.capText = {0.92f, 1.f, 0.96f, 1.f};
		s.capLit = {c, 1.f};
		return s;
	}

	std::string cap(const actions::Binding &b, bool lit)
	{
		const std::string bang = lit ? "!" : "";
		std::string out;
		if (b.modifier >= 0)
		{
			const char *m = platform::buttonName(b.modifier);
			out += "[" + bang + (m ? m : "?") + "] + ";
		}
		switch (b.device)
		{
		case actions::Device::Key:
		{
			const char *k = platform::buttonName(b.code);
			out += "[" + bang + (k ? k : "?") + "]";
			break;
		}
		case actions::Device::Mouse:
			out += "[" + bang + (b.code == 0 ? "mouse:left" : "mouse:right") + "]";
			break;
		case actions::Device::Wheel:
			out += "[" + bang + "mouse:wheel]";
			break;
		}
		return out;
	}
}

std::string keys(controls::Action action)
{
	const bool lit = controls::held(action);
	std::string out;
	for (int slot = 0; slot < actions::maxBindings; slot++)
	{
		const actions::Binding b = controls::binding(action, slot);
		if (!b.bound()) { continue; }
		const std::string c = cap(b, lit);
		if (out.find(c) != std::string::npos) { continue; } // a sideways wheel reads as the wheel again
		if (!out.empty()) { out += " OR "; }
		out += c;
	}
	return out.empty() ? std::string("(UNBOUND)") : out;
}

void atWorld(glm::vec2 world, const std::string &markup, float ringRadius)
{
	Pending p;
	p.world = true;
	p.at = world;
	p.markup = markup;
	p.ringRadius = ringRadius;
	pending.push_back(p);
}

void atHud(hud::Element element, const std::string &markup)
{
	Pending p;
	p.element = element;
	p.markup = markup;
	pending.push_back(p);
	hud::highlight(element);
}

void debugFrame(glm::vec2 ship, glm::vec2 facing)
{
	using controls::Action;
	if (testWorld)
	{
		atWorld(ship + facing * testAhead,
			"PRESS " + keys(Action::Weapon4) + " FOR THE BEAM\nHOLD " + keys(Action::Fire) + " ON A ROCK TO MINE", 220.f);
	}
	if (testHud)
	{
		atHud(hud::Element::Weapon4, "THE BEAM: " + keys(Action::Weapon4));
		atHud(hud::Element::Ram, "HOLD " + keys(Action::Ram) + " TO AIM\nLET GO TO RAM");
	}
	if (testOffScreen)
	{
		atWorld(ship + glm::vec2(60000.f, 20000.f), "THE GATE IS THIS WAY", 0.f);
	}
}

void draw(wgpu2d::Renderer2D &renderer, glm::vec4 view, int width, int height)
{
	if (pending.empty()) { return; }
	const wgpu2d::CalloutStyle s = style(height);
	const float offset = height * offsetPerc;
	const glm::vec2 screenCentre = {width * 0.5f, height * 0.5f};

	renderer.pushCamera();
	for (const Pending &p : pending)
	{
		glm::vec2 target;
		glm::vec2 dir;
		if (p.world)
		{
			if (view.z == 0.f || view.w == 0.f) { continue; }
			target = {(p.at.x - view.x) / view.z * (float)width, (p.at.y - view.y) / view.w * (float)height};
			dir = worldOffsetDir;
			if (p.ringRadius > 0.f)
			{
				const float r = p.ringRadius / view.z * (float)width * (1.f + 0.12f * pulse());
				renderer.renderCircleOutline(target, {textLook::hintColour, 0.5f + 0.5f * pulse()}, r,
					std::max(2.f, s.scale), 48);
			}
		}
		else
		{
			// The element's edge nearest the screen's centre, and the bubble
			// out from there toward the middle.
			const glm::vec4 r = hud::elementRect(p.element, width, height);
			const glm::vec2 centre = {r.x + r.z * 0.5f, r.y + r.w * 0.5f};
			const glm::vec2 toward = screenCentre - centre;
			dir = glm::length(toward) > 0.f ? glm::normalize(toward) : glm::vec2(0.f, -1.f);
			const float sx = dir.x != 0.f ? r.z * 0.5f / std::abs(dir.x) : 1e9f;
			const float sy = dir.y != 0.f ? r.w * 0.5f / std::abs(dir.y) : 1e9f;
			target = centre + dir * (std::min(sx, sy) + height * 0.012f);
		}
		wgpu2d::drawCallout(renderer, target, dir * offset, p.markup.c_str(), s);
	}
	renderer.popCamera();
	pending.clear();
}

void debugUi()
{
	ImGui::TextDisabled("Test hints, until H2's scripts exist");
	ImGui::Checkbox("In the level: the beam, ahead of the ship", &testWorld);
	ImGui::SliderFloat("How far ahead", &testAhead, 200.f, 6000.f, "%.0f");
	ImGui::Checkbox("On the HUD: weapon 4 and the ram", &testHud);
	ImGui::Checkbox("Off screen: a point far away", &testOffScreen);
}

}
