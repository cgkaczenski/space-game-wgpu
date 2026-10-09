#include <hintScript.h>
#include <controls.h>
#include <hints.h>
#include <hud.h>
#include <platformInput.h>
#include <playerSettings.h>

#include <engine/sequence.h>
#include "imgui.h"
#include <cstdlib>

namespace hintScript
{

namespace
{
	std::vector<level::HintStep> steps;
	std::string levelName;
	sequence::Sequence seq;
	int killsAtEntry = 0;

	// The script's bubble, tagged so hints keeps where its caps were drawn:
	// the last two are its buttons, SKIP and then the DISABLE HINTS box.
	const int bubbleTag = 1;

	// A finished step stays up this long, ticked, before the next.
	const float finishSeconds = 0.7f;

	float number(const level::HintStep &h, size_t i, float otherwise = 0.f)
	{
		return i < h.args.size() ? (float)std::atof(h.args[i].c_str()) : otherwise;
	}

	bool met(const level::HintStep &h, const Context &c)
	{
		const std::string &u = h.until;
		if (u == "pressed")
		{
			const controls::Action a = h.args.empty() ? controls::Action::Count : controls::fromId(h.args[0]);
			return a != controls::Action::Count && controls::pressed(a);
		}
		if (u == "selected") { return c.selectedWeapon == (int)number(h, 0) - 1; }
		if (u == "hold") { return c.held >= number(h, 0); }
		if (u == "near") { return glm::distance(c.ship, {number(h, 0), number(h, 1)}) <= number(h, 2); }
		if (u == "mode") { return !h.args.empty() && (h.args[0] == "flight") == c.flight; }
		if (u == "cloaked") { return c.cloaked; }
		if (u == "kills") { return c.kills - killsAtEntry >= (int)number(h, 0, 1.f); }
		if (u == "seconds") { return seq.inStep >= number(h, 0); }
		return false; // nothing, or a word this build does not know: only a skip ends it
	}

	// {action} becomes that action's key-caps, lit while held, and `\n` a
	// line break. An unknown name is left as written, so a typo shows rather
	// than vanishing.
	std::string expand(const std::string &text)
	{
		std::string out;
		for (size_t i = 0; i < text.size(); i++)
		{
			if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n')
			{
				out += '\n';
				i++;
				continue;
			}
			if (text[i] == '{')
			{
				const size_t close = text.find('}', i);
				if (close != std::string::npos)
				{
					const controls::Action a = controls::fromId(text.substr(i + 1, close - i - 1));
					if (a != controls::Action::Count)
					{
						out += hints::keys(a);
						i = close;
						continue;
					}
				}
			}
			out += text[i];
		}
		return out;
	}

	hud::Element element(const std::string &name)
	{
		const std::vector<std::string> &names = hudElements();
		for (size_t i = 0; i < names.size(); i++) { if (names[i] == name) { return (hud::Element)i; } }
		return hud::Element::Count;
	}

	bool enabled() { return playerSettings::hintsOn(levelName); }

	// The cursor in framebuffer pixels, as the menu reads it.
	glm::vec2 pointer(int width, int height)
	{
		const glm::ivec2 window = platform::getWindowSize();
		const glm::vec2 toPixels = {window.x > 0 ? (float)width / window.x : 1.f,
			window.y > 0 ? (float)height / window.y : 1.f};
		return glm::vec2(platform::getRelMousePosition()) * toPixels;
	}

	bool over(glm::vec4 r, glm::vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.z && p.y < r.y + r.w; }

	// The buttons, where they were drawn last frame: the bubble's last two
	// caps. A frame late, which a click never notices.
	bool buttons(glm::vec4 &skip, glm::vec4 &disable)
	{
		const std::vector<glm::vec4> &caps = hints::capsDrawn(bubbleTag);
		if (caps.size() < 2) { return false; }
		skip = caps[caps.size() - 2];
		disable = caps.back();
		return true;
	}

	// The bubble's small print: a SKIP button in the bottom-left corner, the
	// box in the bottom-right, each lit under the pointer. The skip key still
	// works; the button is what the bubble shows.
	hints::Corners corners(bool overSkip, bool overDisable)
	{
		return {overSkip ? "[!SKIP]" : "[SKIP]", std::string(overDisable ? "[! ]" : "[ ]") + " DISABLE HINTS"};
	}
}

void start(const std::vector<level::HintStep> &s, const std::string &level)
{
	steps = s;
	levelName = level;
	sequence::start(seq, (int)steps.size());
	killsAtEntry = 0;
}

bool update(const Context &c, int width, int height)
{
	if (!enabled() || !sequence::running(seq)) { return false; }

	// The buttons first: a click on one is the button's, not the trigger's.
	// The box switches this level's hints off; SKIP moves on, as the key does.
	glm::vec4 skipRect, boxRect;
	const bool haveButtons = buttons(skipRect, boxRect);
	const bool mouseFree = !ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureMouse;
	const glm::vec2 at = pointer(width, height);
	const bool overSkip = haveButtons && mouseFree && over(skipRect, at);
	const bool overDisable = haveButtons && mouseFree && over(boxRect, at);
	const bool click = platform::isLMousePressed();
	if (overDisable && click)
	{
		playerSettings::hintsOffFor(levelName);
		return true;
	}
	const bool skipClicked = overSkip && click;

	if (c.live)
	{
		const level::HintStep &h = steps[(size_t)seq.current];
		const bool skip = skipClicked || controls::pressed(controls::Action::SkipHint);
		const sequence::Event e = sequence::update(seq, c.gameDeltaTime, met(h, c), skip, finishSeconds);
		if (e == sequence::Event::Entered) { killsAtEntry = c.kills; }
		if (!sequence::running(seq)) { return skipClicked; }
	}

	const level::HintStep &h = steps[(size_t)seq.current];
	std::string text = expand(h.text);
	if (sequence::finishing(seq)) { text += "  [!DONE]"; }
	const hints::Corners small = corners(overSkip, overDisable);
	switch (h.where)
	{
	case level::HintStep::Where::World: hints::atWorld(h.at, text, h.ring, bubbleTag, small); break;
	case level::HintStep::Where::Hud:
	{
		const hud::Element e = element(h.hud);
		if (e != hud::Element::Count) { hints::atHud(e, text, bubbleTag, small); }
		else { hints::atScreen(text, bubbleTag, small); }
		break;
	}
	case level::HintStep::Where::Screen: hints::atScreen(text, bubbleTag, small); break;
	}
	return skipClicked;
}

const std::vector<std::string> &conditions()
{
	static const std::vector<std::string> words = {
		"", "pressed", "selected", "hold", "near", "mode", "cloaked", "kills", "seconds"};
	return words;
}

const char *conditionArgs(const std::string &c)
{
	if (c == "pressed") { return "an action: cloak, weapon4, mode..."; }
	if (c == "selected") { return "a weapon slot, 1-4"; }
	if (c == "hold") { return "orbs of ore in the hold, e.g. 1"; }
	if (c == "near") { return "x y radius"; }
	if (c == "mode") { return "flight or fight"; }
	if (c == "kills") { return "how many, since the step began"; }
	if (c == "seconds") { return "game seconds, since the step began"; }
	if (c == "cloaked") { return "(nothing)"; }
	return "(only the skip key ends it)";
}

const std::vector<std::string> &hudElements()
{
	// In the order of hud::Element.
	static const std::vector<std::string> names = {
		"weapon1", "weapon2", "weapon3", "weapon4", "ram", "mode", "health", "energy", "haul"};
	return names;
}

void debugUi()
{
	if (steps.empty()) { ImGui::TextDisabled("This level has no hint script"); }
	else if (!sequence::running(seq)) { ImGui::Text("Script done (%d steps)", (int)steps.size()); }
	else
	{
		const level::HintStep &h = steps[(size_t)seq.current];
		ImGui::Text("Step %d of %d, %.1f s in%s", seq.current + 1, (int)steps.size(), seq.inStep,
			sequence::finishing(seq) ? ", done" : "");
		ImGui::TextWrapped("%s", h.text.c_str());
		ImGui::TextDisabled("until %s", h.until.empty() ? "(skip)" : h.until.c_str());
	}
	if (!playerSettings::get().hints) { ImGui::TextColored({1.f, 0.7f, 0.3f, 1.f}, "Hints are off (Settings > Hints)"); }
	else if (!enabled()) { ImGui::TextColored({1.f, 0.7f, 0.3f, 1.f}, "Hints are off for %s (Settings > Hints, off and on, brings them back)", levelName.c_str()); }
	if (ImGui::SmallButton("Restart the script")) { start(steps, levelName); }
}

}
