#include <worldGrade.h>
#include <tuning.h>

#include "imgui.h"
#include <platformTools.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>

namespace worldGrade
{

namespace
{
	wgpu2d::Effect effect;

	// Created the first time the look is used, like the cloak's.
	wgpu2d::FrameBuffer worldTarget;

	float desaturate = 0.85f; // 1 is fully grey
	float brightness = 0.45f; // what is left of the light

	// Outside the safe zone: greyer than paused, but lit enough to fight in.
	bool outsideOn = true;
	float outsideDesaturate = 0.9f;
	float outsideBrightness = 0.6f;
	float outsideEdgePixels = 24.f; // the fade from colour to grey, on screen

	// The fog (sight roadmap S3): what the player cannot see. Grey and Grey
	// and dim are the same grade with their own two sliders; Black is the
	// classic fog of war, for comparison.
	enum class Fog { Off, Grey, GreyDim, Black };
	enum class Edge { Hard, Soft };
	Fog fog = Fog::Grey;
	float greyDesaturate = 0.85f;
	float greyBrightness = 0.7f;
	float dimDesaturate = 0.85f;
	float dimBrightness = 0.4f;
	Edge edge = Edge::Soft;
	float edgeFadePixels = 60.f;     // the soft edge's width, on screen

	// Below unseen: what the player has never seen (after W6).
	enum class Unvisited { Black, LikeUnseen };
	Unvisited unvisited = Unvisited::Black;

	// The fan and its soft ring, in screen pixels, built each frame.
	std::vector<glm::vec2> fanPositions, fanUvs;
	std::vector<wgpu2d::Color4f> fanColours;

	// Whether any of the view is outside `safe`: its furthest corner is.
	bool viewReaches(glm::vec4 view, const zone::Circle &safe)
	{
		const float dx = std::max(std::abs(view.x - safe.centre.x), std::abs(view.x + view.z - safe.centre.x));
		const float dy = std::max(std::abs(view.y - safe.centre.y), std::abs(view.y + view.w - safe.centre.y));
		return dx * dx + dy * dy > safe.radius * safe.radius;
	}

	bool readFile(const char *path, std::string &out)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file.is_open()) { return false; }
		std::stringstream ss;
		ss << file.rdbuf();
		out = ss.str();
		return true;
	}
}

bool init()
{
	std::string source;
	const char *path = RESOURCES_PATH "shaders/worldGrade.wgsl";
	if (!readFile(path, source))
	{
		std::cerr << "worldGrade: cannot read " << path << "\n";
		return false;
	}

	effect = wgpu2d::createEffect(source.c_str(), "worldGrade");
	if (effect.id == 0)
	{
		std::cerr << "worldGrade: effect did not compile\n";
		return false;
	}
	return true;
}

void cleanup()
{
	worldTarget.cleanup();
}

void apply(wgpu2d::Renderer2D &renderer, float pauseAmount, const zone::Circle *safe,
	int width, int height, const visibility::PolarMap *sight, const std::vector<Reveal> *reveals,
	const Explored *explored)
{
	if (effect.id == 0 || width <= 0 || height <= 0) { return; }

	const glm::vec4 view = renderer.getViewRect();
	const bool outside = outsideOn && safe && view.z != 0.f && view.w != 0.f
		&& viewReaches(view, *safe);
	const bool fogged = sight && fog != Fog::Off && !sight->distance.empty() && view.z != 0.f && view.w != 0.f;
	if (pauseAmount <= 0.f && !outside && !fogged) { return; }

	if (worldTarget.fbo == 0)
	{
		worldTarget.create((unsigned)width, (unsigned)height);
		if (worldTarget.fbo == 0) { return; } // no target: the world stays ungraded
	}
	worldTarget.resize((unsigned)width, (unsigned)height);

	worldTarget.clear();
	renderer.flushFBO(worldTarget);

	// Back into the batch as one quad over the whole view, in screen space.
	// Premultiplied, because a target's contents are (outline 12).
	wgpu2d::EffectParams params;
	params.a = {desaturate * pauseAmount, 1.f - (1.f - brightness) * pauseAmount, 0.f, 0.f};
	if (outside)
	{
		// World to screen pixels: the same mapping the projection does, for
		// one point and one length. The quad's uv times the view's size is the
		// pixel the shader is on, y down, like this.
		const float screenPerWorld = (float)width / view.z;
		params.b = {(safe->centre.x - view.x) * screenPerWorld,
			(safe->centre.y - view.y) / view.w * (float)height,
			safe->radius * screenPerWorld, std::max(outsideEdgePixels, 0.5f)};
		params.c = {outsideDesaturate, outsideBrightness, 1.f, 0.f};
	}

	// Unseen: the whole view, if there is fog; the sight is drawn over it.
	if (fogged)
	{
		const bool dim = fog == Fog::GreyDim;
		params.d = fog == Fog::Black ? glm::vec4(1.f, 0.f, 1.f, 0.f)
			: glm::vec4(dim ? dimDesaturate : greyDesaturate, dim ? dimBrightness : greyBrightness, 1.f, 0.f);
	}

	renderer.pushCamera();
	renderer.setBlendMode(wgpu2d::BlendMode::Premultiplied);
	renderer.setEffect(effect, params);
	renderer.renderRectangle({0.f, 0.f, (float)width, (float)height}, worldTarget.texture);

	// Never seen: the explored map over the unseen view through Mask, so
	// only what it covers survives, and the rest is black. Each screen
	// corner's texture coordinate is where in the map its world point is.
	if (fogged && explored && unvisited == Unvisited::Black && explored->texture.id != 0
		&& explored->worldRect.z > 0.f && explored->worldRect.w > 0.f)
	{
		renderer.clearEffect();
		renderer.setBlendMode(wgpu2d::BlendMode::Mask);
		const glm::vec2 corners[4] = {{0.f, 0.f}, {(float)width, 0.f}, {(float)width, (float)height}, {0.f, (float)height}};
		glm::vec2 positions[6], uvs[6];
		wgpu2d::Color4f colours[6];
		const int order[6] = {0, 1, 2, 0, 2, 3};
		for (int k = 0; k < 6; k++)
		{
			const glm::vec2 screen = corners[order[k]];
			const glm::vec2 world = {view.x + screen.x / (float)width * view.z, view.y + screen.y / (float)height * view.w};
			positions[k] = screen;
			uvs[k] = (world - glm::vec2(explored->worldRect.x, explored->worldRect.y))
				/ glm::vec2(explored->worldRect.z, explored->worldRect.w);
			colours[k] = {1.f, 1.f, 1.f, 1.f};
		}
		renderer.renderTriangles(positions, uvs, colours, 6, explored->texture);
		renderer.setBlendMode(wgpu2d::BlendMode::Premultiplied);
		renderer.setEffect(effect, params);
	}

	if (fogged)
	{
		// The player's sight, from the same target with the unseen grade off:
		// a fan from the ship through every slice's corner, in screen pixels,
		// each corner's texture coordinate its pixel over the view's size.
		params.d.z = 0.f;
		renderer.setEffect(effect, params);
		const glm::vec2 size = {(float)width, (float)height};
		auto toScreen = [&](glm::vec2 world)
		{
			return glm::vec2((world.x - view.x) / view.z, (world.y - view.y) / view.w) * size;
		};
		const int n = (int)sight->distance.size();
		const glm::vec2 centre = toScreen(sight->origin);
		const float fadeWorld = edge == Edge::Soft ? edgeFadePixels * view.z / (float)width : 0.f;
		const wgpu2d::Color4f on = {1.f, 1.f, 1.f, 1.f}, off = {0.f, 0.f, 0.f, 0.f};
		fanPositions.clear();
		fanUvs.clear();
		fanColours.clear();
		auto vertex = [&](glm::vec2 screen, wgpu2d::Color4f colour)
		{
			fanPositions.push_back(screen);
			fanUvs.push_back(screen / size);
			fanColours.push_back(colour);
		};
		for (int i = 0; i < n; i++)
		{
			const glm::vec2 a = toScreen(visibility::corner(*sight, i));
			const glm::vec2 b = toScreen(visibility::corner(*sight, i + 1));
			vertex(centre, on); vertex(a, on); vertex(b, on);
			if (fadeWorld <= 0.f) { continue; }

			// The soft edge: a ring past the corners, fading out along each
			// slice. The target is premultiplied, so fading it scales all four
			// channels -- fading alpha alone would leave the colour behind.
			const glm::vec2 aOut = toScreen(visibility::corner(*sight, i)
				+ visibility::direction(*sight, i) * fadeWorld);
			const glm::vec2 bOut = toScreen(visibility::corner(*sight, i + 1)
				+ visibility::direction(*sight, i + 1) * fadeWorld);
			vertex(a, on); vertex(b, on); vertex(bOut, off);
			vertex(a, on); vertex(bOut, off); vertex(aOut, off);
		}

		// Reveals: circles of sight, drawn the same way -- a fan from the
		// centre, and the soft ring past it.
		if (reveals)
		{
			constexpr int sides = 48;
			for (const Reveal &r : *reveals)
			{
				const glm::vec2 c = toScreen(r.centre);
				for (int k = 0; k < sides; k++)
				{
					const float a0 = 6.2831853f * (float)k / sides, a1 = 6.2831853f * (float)(k + 1) / sides;
					const glm::vec2 d0 = {std::cos(a0), std::sin(a0)}, d1 = {std::cos(a1), std::sin(a1)};
					const glm::vec2 p0 = toScreen(r.centre + d0 * r.radius), p1 = toScreen(r.centre + d1 * r.radius);
					vertex(c, on); vertex(p0, on); vertex(p1, on);
					if (fadeWorld <= 0.f) { continue; }
					const glm::vec2 q0 = toScreen(r.centre + d0 * (r.radius + fadeWorld));
					const glm::vec2 q1 = toScreen(r.centre + d1 * (r.radius + fadeWorld));
					vertex(p0, on); vertex(p1, on); vertex(q1, off);
					vertex(p0, on); vertex(q1, off); vertex(q0, off);
				}
			}
		}
		renderer.renderTriangles(fanPositions.data(), fanUvs.data(), fanColours.data(),
			fanPositions.size(), worldTarget.texture);
	}

	renderer.clearEffect();
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	renderer.popCamera();
}

bool fogOn() { return fog != Fog::Off; }

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("worldGrade", {
	{"desaturate", desaturate},
	{"brightness", brightness},
	{"outsideOn", outsideOn},
	{"outsideDesaturate", outsideDesaturate},
	{"outsideBrightness", outsideBrightness},
	{"outsideEdgePixels", outsideEdgePixels},
	{"fog", fog},
	{"fogGreyDesaturate", greyDesaturate},
	{"fogGreyBrightness", greyBrightness},
	{"fogDimDesaturate", dimDesaturate},
	{"fogDimBrightness", dimBrightness},
	{"fogEdge", edge},
	{"fogEdgePixels", edgeFadePixels},
	{"fogUnvisited", unvisited},
});

void debugUi()
{
	ImGui::TextDisabled("Paused");
	tune::SliderFloat("Desaturate", &desaturate, 0.f, 1.f);
	tune::SliderFloat("Brightness", &brightness, 0.f, 1.f);
	ImGui::TextDisabled("Outside the closing circle");
	tune::Checkbox("Grey outside", &outsideOn);
	tune::SliderFloat("Outside desaturate", &outsideDesaturate, 0.f, 1.f);
	tune::SliderFloat("Outside brightness", &outsideBrightness, 0.f, 1.f);
	tune::SliderFloat("Edge fade px", &outsideEdgePixels, 1.f, 200.f, "%.0f");

	ImGui::SeparatorText("Fog: what the player cannot see");
	{
		int f = (int)fog;
		{
			tune::Highlight h(&fog); // the radios edit a copy
			ImGui::RadioButton("Off", &f, (int)Fog::Off); ImGui::SameLine();
			ImGui::RadioButton("Grey", &f, (int)Fog::Grey); ImGui::SameLine();
			ImGui::RadioButton("Grey and dim", &f, (int)Fog::GreyDim); ImGui::SameLine();
			ImGui::RadioButton("Black", &f, (int)Fog::Black);
		}
		fog = (Fog)f;
	}
	if (fog != Fog::Off)
	{
		int u = (int)unvisited;
		{
			tune::Highlight h(&unvisited); // the radios edit a copy
			ImGui::TextUnformatted("Never visited"); ImGui::SameLine();
			ImGui::RadioButton("Black", &u, (int)Unvisited::Black); ImGui::SameLine();
			ImGui::RadioButton("Like the rest", &u, (int)Unvisited::LikeUnseen);
		}
		unvisited = (Unvisited)u;
		ImGui::TextDisabled("  in sight: colour; seen before: the fog below; never seen: black");
	}
	if (fog == Fog::Grey)
	{
		tune::SliderFloat("Fog desaturate", &greyDesaturate, 0.f, 1.f);
		tune::SliderFloat("Fog brightness", &greyBrightness, 0.f, 1.f);
	}
	else if (fog == Fog::GreyDim)
	{
		tune::SliderFloat("Fog desaturate##dim", &dimDesaturate, 0.f, 1.f);
		tune::SliderFloat("Fog brightness##dim", &dimBrightness, 0.f, 1.f);
	}
	if (fog != Fog::Off)
	{
		int e = (int)edge;
		{
			tune::Highlight h(&edge);
			ImGui::TextUnformatted("Fog edge"); ImGui::SameLine();
			ImGui::RadioButton("Hard", &e, (int)Edge::Hard); ImGui::SameLine();
			ImGui::RadioButton("Soft", &e, (int)Edge::Soft);
		}
		edge = (Edge)e;
		if (edge == Edge::Soft) { tune::SliderFloat("Fog edge px", &edgeFadePixels, 2.f, 300.f, "%.0f"); }
	}
}

}
