#include <loadoutMenu.h>
#include <bulletLook.h>
#include <controls.h>
#include <hints.h>
#include <hud.h>
#include <inventory.h>
#include <platformInput.h>
#include <resources.h>
#include <textLook.h>
#include <tuning.h>
#include <weapons.h>

#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace loadoutMenu
{

namespace
{
	bool open = false;
	float lookNow = 0.f;
	std::chrono::steady_clock::time_point lastTick;
	bool ticking = false;

	// What is being dragged, and from where.
	struct Drag
	{
		bool active = false;
		bool fromSlot = false;
		int slot = -1;       // fromSlot
		int id = -1;         // from the hold
		bool isOre = false;
		int kind = 0;
		int ore = 0;
		int turns = 0;
		bool hotbarOnly = false;  // started on the HUD with the menu closed: slot to slot, nothing else
	};
	Drag drag;

	// What the pointer is over, for the line of text under the grid -- plain
	// text, or markup with key-caps.
	std::string hoverText;
	std::string hoverMarkup;

	// The look. Sizes are fractions of the screen's height.
	float cellPerc = 0.07f;
	float topPerc = 0.2f;
	float lookSeconds = 0.15f;
	float greyAmount = 0.45f;     // of the paused grade, at full look

	const glm::vec4 panelColour = {0.02f, 0.03f, 0.06f, 0.88f};
	const glm::vec4 squareFill = {0.06f, 0.07f, 0.12f, 1.f};
	const glm::vec4 squareLine = {0.22f, 0.24f, 0.34f, 1.f};
	const glm::vec4 slotLine = {0.45f, 0.45f, 0.6f, 1.f};
	const glm::vec4 oreColour = {0.85f, 0.55f, 0.18f, 1.f};
	const glm::vec4 fitColour = {0.35f, 1.f, 0.55f, 0.55f};
	const glm::vec4 noFitColour = {1.f, 0.3f, 0.25f, 0.55f};
	const glm::vec4 weaponColours[weapons::slotCount] = {
		{0.20f, 0.30f, 0.55f, 1.f},   // burst
		{0.50f, 0.25f, 0.45f, 1.f},   // heavy
		{0.55f, 0.35f, 0.15f, 1.f},   // missile
		{0.15f, 0.45f, 0.45f, 1.f},   // beam
	};

	struct Layout
	{
		float cell;
		wgpu2d::GridView hold;
		glm::vec4 slots[weapons::slotCount];
		glm::vec4 hudSlots[weapons::slotCount];   // the HUD's own row: slots too, while the menu is open
		glm::vec4 panel;
		glm::vec2 titleAt;
		glm::vec2 holdLabelAt;
		glm::vec2 hoverAt;
		float textScale;
	};

	Layout layout(int w, int h)
	{
		Layout l;
		l.textScale = textLook::screenScale(h);
		l.cell = std::round(h * cellPerc);
		const float line = textLook::font().lineHeight() * l.textScale;
		const float gap = std::max(2.f, std::round(l.cell * 0.06f));

		l.hold.cell = l.cell;
		l.hold.gap = gap;
		l.hold.columns = inventory::holdGrid().width;
		l.hold.rows = inventory::holdGrid().height;
		const glm::vec4 hb = l.hold.bounds();

		const float slotSize = std::round(l.cell * 1.25f);
		const float slotGap = std::round(l.cell * 0.2f);
		const float slotsWidth = weapons::slotCount * slotSize + (weapons::slotCount - 1) * slotGap;
		const float width = std::max(hb.z, slotsWidth);
		const float left = std::round(w * 0.5f - width * 0.5f);

		float y = std::round(h * topPerc);
		l.titleAt = {std::round(w * 0.5f), y};
		y += line * 2.8f;   // the title, then the slots' numbers above them
		for (int i = 0; i < weapons::slotCount; i++)
		{
			l.slots[i] = {std::round(w * 0.5f - slotsWidth * 0.5f) + i * (slotSize + slotGap), y, slotSize, slotSize};
		}
		y += slotSize + line * 0.8f;
		l.holdLabelAt = {left, y};
		y += line * 1.3f;
		l.hold.topLeft = {std::round(w * 0.5f - hb.z * 0.5f), y};
		y += hb.w + line * 0.6f;
		l.hoverAt = {std::round(w * 0.5f), y};
		y += line * 1.4f;

		const float margin = l.cell * 0.4f;
		l.panel = {left - margin, l.titleAt.y - margin, width + 2.f * margin, y - l.titleAt.y + margin};
		for (int i = 0; i < weapons::slotCount; i++)
		{
			l.hudSlots[i] = hud::elementRect((hud::Element)((int)hud::Element::Weapon1 + i), w, h);
		}
		return l;
	}

	bool inside(glm::vec4 r, glm::vec2 p);

	// The slot under `p`, the panel's or the HUD's: they are the same four
	// slots, drawn twice. -1 for none.
	int slotUnder(const Layout &l, glm::vec2 p)
	{
		for (int k = 0; k < weapons::slotCount; k++)
		{
			if (inside(l.slots[k], p) || inside(l.hudSlots[k], p)) { return k; }
		}
		return -1;
	}

	// The HUD's row alone: what is there with the menu closed.
	int hudSlotUnder(const Layout &l, glm::vec2 p)
	{
		for (int k = 0; k < weapons::slotCount; k++) { if (inside(l.hudSlots[k], p)) { return k; } }
		return -1;
	}

	bool inside(glm::vec4 r, glm::vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.z && p.y < r.y + r.w; }

	glm::vec2 pointer(int w, int h)
	{
		const glm::ivec2 window = platform::getWindowSize();
		const glm::vec2 toPixels = {window.x > 0 ? (float)w / window.x : 1.f, window.y > 0 ? (float)h / window.y : 1.f};
		return glm::vec2(platform::getRelMousePosition()) * toPixels;
	}

	hold::Shape dragShape()
	{
		hold::Shape s;
		if (drag.isOre) { s.cells = {{0, 0}}; }
		else { s = inventory::shapeOf(drag.kind); }
		return hold::turned(s, drag.turns);
	}

	// The square of a (turned) shape nearest its middle: what is under the
	// pointer while it is dragged.
	glm::ivec2 middleSquare(const hold::Shape &s)
	{
		glm::vec2 centre = {};
		for (const glm::ivec2 &c : s.cells) { centre += glm::vec2(c) + 0.5f; }
		centre /= (float)std::max<size_t>(s.cells.size(), 1);
		glm::ivec2 best = s.cells.empty() ? glm::ivec2(0) : s.cells[0];
		float bestD = 1e9f;
		for (const glm::ivec2 &c : s.cells)
		{
			const float d = glm::distance(glm::vec2(c) + 0.5f, centre);
			if (d < bestD) { bestD = d; best = c; }
		}
		return best;
	}

	glm::vec2 centroid(const wgpu2d::GridView &g, const hold::Shape &s, glm::ivec2 at)
	{
		glm::vec2 sum = {};
		for (const glm::ivec2 &c : s.cells)
		{
			const glm::vec4 r = g.square(at + c);
			sum += glm::vec2(r.x + r.z * 0.5f, r.y + r.w * 0.5f);
		}
		return sum / (float)std::max<size_t>(s.cells.size(), 1);
	}

	std::string describe(const inventory::Item &it)
	{
		std::string s = weapons::shipWeapon(it.kind).name;
		if (it.stun) { s += " +STUN"; }
		if (it.lockdown) { s += " +LOCKDOWN"; }
		if (it.spread) { s += " +SPREAD"; }
		for (char &c : s) { c = (char)std::toupper((unsigned char)c); }
		return s;
	}

	void drop(const Layout &l, glm::vec2 p, const Frame &f)
	{
		const hold::Shape shape = dragShape();
		const glm::ivec2 target = l.hold.nearestSquare(p) - middleSquare(shape);

		if (inside(l.hold.bounds(), p))
		{
			if (drag.fromSlot) { inventory::slotToHold(drag.slot, drag.turns, target); }
			else { inventory::moveInHold(drag.id, drag.turns, target); }
			return;
		}
		const int k = slotUnder(l, p);
		if (k >= 0)
		{
			if (drag.fromSlot) { inventory::swapSlots(drag.slot, k); }
			else if (!drag.isOre) { inventory::holdToSlot(drag.id, k); }
			return;
		}
		if (inside(l.panel, p)) { return; } // on the panel but nowhere: put back

		// Outside the panel: thrown out behind the ship.
		const glm::vec2 back = -f.facing;
		if (drag.fromSlot)
		{
			inventory::Item gone;
			inventory::jettisonSlot(drag.slot, gone);
		}
		else
		{
			inventory::Held gone;
			if (inventory::jettisonHeld(drag.id, gone) && gone.isOre)
			{
				resources::jettison(f.ship + back * 220.f, back, gone.ore);
			}
		}
	}
}

void update(const Frame &f, bool escape, bool &escapeTaken)
{
	escapeTaken = false;

	// Real time, so a pause does not hold the grey half-way.
	const auto now = std::chrono::steady_clock::now();
	const float dt = ticking ? std::min(0.1f, std::chrono::duration<float>(now - lastTick).count()) : 0.f;
	lastTick = now;
	ticking = true;

	if (!f.live && !f.paused) { close(); }
	if (f.live && controls::pressed(controls::Action::Loadout))
	{
		open = !open;
		drag = {};
	}
	if (open && escape)
	{
		close();
		escapeTaken = true;
	}

	const float target = open ? 1.f : 0.f;
	const float step = lookSeconds > 0.f ? dt / lookSeconds : 1.f;
	lookNow = target > lookNow ? std::min(target, lookNow + step) : std::max(target, lookNow - step);

	hoverText.clear();
	hoverMarkup.clear();
	if (!f.live) { drag = {}; return; }

	const Layout l = layout(f.width, f.height);
	const glm::vec2 p = pointer(f.width, f.height);
	const bool mouseFree = !ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureMouse;

	// Closed, the HUD's row can still be rearranged: press on a weapon, let
	// go on another slot, and they swap. Let go anywhere else and nothing
	// happens -- there is no hold to drop into and nothing is thrown out.
	if (!open)
	{
		if (!drag.active && mouseFree && platform::isLMousePressed())
		{
			const int k = hudSlotUnder(l, p);
			inventory::Item it;
			if (k >= 0 && inventory::equippedAt(k, it))
			{
				drag = {true, true, k, -1, false, it.kind, 0, 0, true};
			}
		}
		if (drag.active && !platform::isLMouseHeld())
		{
			const int k = hudSlotUnder(l, p);
			if (k >= 0 && k != drag.slot) { inventory::swapSlots(drag.slot, k); }
			drag = {};
		}
		return;
	}

	// Picking up.
	if (!drag.active && mouseFree && platform::isLMousePressed())
	{
		glm::ivec2 sq;
		if (l.hold.squareAt(p, sq))
		{
			const int id = hold::at(inventory::holdGrid(), sq);
			inventory::Held held;
			const hold::Piece *piece = hold::find(inventory::holdGrid(), id);
			if (piece && inventory::heldAt(id, held))
			{
				drag = {true, false, -1, id, held.isOre, held.item.kind, held.ore, piece->turns};
			}
		}
		const int k = slotUnder(l, p);
		inventory::Item it;
		if (k >= 0 && inventory::equippedAt(k, it))
		{
			drag = {true, true, k, -1, false, it.kind, 0, 0};
		}
	}

	if (drag.active)
	{
		if (controls::pressed(controls::Action::MenuRotate)) { drag.turns = (drag.turns + 1) % 4; }
		if (!platform::isLMouseHeld())
		{
			drop(l, p, f);
			drag = {};
		}
	}

	// What is under the pointer, named.
	glm::ivec2 sq;
	if (l.hold.squareAt(p, sq))
	{
		inventory::Held held;
		if (inventory::heldAt(hold::at(inventory::holdGrid(), sq), held))
		{
			hoverText = held.isOre ? "ORE  " + std::to_string(held.ore) + " / " + std::to_string(inventory::stackSize)
				: describe(held.item);
		}
	}
	if (const int k = slotUnder(l, p); k >= 0)
	{
		inventory::Item it;
		hoverText = inventory::equippedAt(k, it) ? describe(it) : "SLOT " + std::to_string(k + 1) + ": EMPTY";
	}
	// Dragging a weapon, the line says how to turn it. (What letting go will
	// do travels with the item, under the pointer.)
	if (drag.active)
	{
		hoverText.clear();
		if (!drag.isOre) { hoverMarkup = hints::keys(controls::Action::MenuRotate) + " TO ROTATE"; }
	}
}

bool claimsMouse() { return open || drag.active; }

bool isOpen() { return open; }

void close()
{
	open = false;
	drag = {};
}

float look() { return lookNow * greyAmount; }

void draw(wgpu2d::Renderer2D &renderer, int width, int height)
{
	if (!open && !drag.active) { return; }
	const Layout l = layout(width, height);
	const glm::vec2 p = pointer(width, height);
	const float u = l.textScale;
	const hold::Grid &grid = inventory::holdGrid();

	renderer.pushCamera();

	// Closed, only a drag along the HUD's row: the slot it came from
	// covered, the one under it framed, its icon following the pointer.
	if (!open)
	{
		const float b = std::max(2.f, std::round(u));
		renderer.renderRectangle(l.hudSlots[drag.slot], {0.f, 0.f, 0.f, 0.7f});
		const int k = hudSlotUnder(l, p);
		if (k >= 0 && k != drag.slot)
		{
			const glm::vec4 r = l.hudSlots[k];
			glm::vec4 c = fitColour;
			c.a = 1.f;
			renderer.renderRectangle({r.x - 2.f * b, r.y - 2.f * b, r.z + 4.f * b, b}, c);
			renderer.renderRectangle({r.x - 2.f * b, r.y + r.w + b, r.z + 4.f * b, b}, c);
			renderer.renderRectangle({r.x - 2.f * b, r.y - b, b, r.w + 2.f * b}, c);
			renderer.renderRectangle({r.x + r.z + b, r.y - b, b, r.w + 2.f * b}, c);
		}
		bulletLook::drawIcon(renderer, p, l.cell * 0.9f, weapons::shipWeapon(drag.kind).style);
		renderer.popCamera();
		return;
	}

	renderer.renderRectangle(l.panel, panelColour);
	textLook::draw(renderer, l.titleAt, "LOADOUT", {1.f, 1.f, 1.f, 1.f}, u, {0.5f, 0.f});

	// The slots: a frame each, the weapon's icon, its key above it.
	for (int k = 0; k < weapons::slotCount; k++)
	{
		const glm::vec4 r = l.slots[k];
		const bool from = drag.active && drag.fromSlot && drag.slot == k;
		glm::vec4 line = slotLine;
		if (drag.active && inside(r, p) && !(drag.fromSlot && drag.slot == k))
		{
			line = drag.isOre ? noFitColour : fitColour;
			line.a = 1.f;
		}
		const float b = std::max(2.f, std::round(u));
		renderer.renderRectangle({r.x - b, r.y - b, r.z + 2.f * b, r.w + 2.f * b}, line);
		renderer.renderRectangle(r, squareFill);
		inventory::Item it;
		if (inventory::equippedAt(k, it) && !from)
		{
			renderer.renderRectangle({r.x + b, r.y + b, r.z - 2.f * b, r.w - 2.f * b}, weaponColours[it.kind]);
			bulletLook::drawIcon(renderer, {r.x + r.z * 0.5f, r.y + r.w * 0.5f}, r.w * 0.75f, weapons::shipWeapon(it.kind).style);
		}
		textLook::draw(renderer, {r.x + r.z * 0.5f, r.y - b * 2.f}, std::to_string(k + 1).c_str(),
			{0.7f, 0.75f, 0.85f, 1.f}, u, {0.5f, 1.f});
	}

	// The hold.
	char label[64];
	std::snprintf(label, sizeof(label), "HOLD  %d ORE  %d / %d FREE", inventory::ore(), hold::freeSquares(grid),
		grid.width * grid.height);
	textLook::draw(renderer, {l.hold.topLeft.x, l.holdLabelAt.y}, label, {0.75f, 0.8f, 0.9f, 1.f}, u);
	l.hold.draw(renderer, squareFill, squareLine, std::max(1.f, std::round(u * 0.5f)));
	for (const hold::Piece &piece : grid.pieces)
	{
		inventory::Held held;
		if (!inventory::heldAt(piece.id, held)) { continue; }
		const bool from = drag.active && !drag.fromSlot && drag.id == piece.id;
		glm::vec4 colour = held.isOre ? oreColour : weaponColours[held.item.kind];
		if (from) { colour.a = 0.3f; }
		l.hold.fillCells(renderer, piece.shape.cells.data(), piece.shape.cells.size(), piece.at, colour);
		const glm::vec2 c = centroid(l.hold, piece.shape, piece.at);
		if (held.isOre)
		{
			textLook::draw(renderer, c, std::to_string(held.ore).c_str(), {1.f, 0.95f, 0.85f, from ? 0.4f : 1.f}, u, {0.5f, 0.5f});
		}
		else if (!from)
		{
			bulletLook::drawIcon(renderer, c, l.cell * 0.9f, weapons::shipWeapon(held.item.kind).style);
		}
	}

	// The HUD's slots are slots too while the menu is open: one a weapon was
	// lifted from is covered, and one under a drag is framed green or red.
	{
		const float b = std::max(2.f, std::round(u));
		for (int k = 0; k < weapons::slotCount; k++)
		{
			const glm::vec4 r = l.hudSlots[k];
			if (drag.active && drag.fromSlot && drag.slot == k) { renderer.renderRectangle(r, {0.f, 0.f, 0.f, 0.7f}); }
			if (drag.active && inside(r, p) && !(drag.fromSlot && drag.slot == k))
			{
				glm::vec4 c = drag.isOre ? noFitColour : fitColour;
				c.a = 1.f;
				renderer.renderRectangle({r.x - 2.f * b, r.y - 2.f * b, r.z + 4.f * b, b}, c);
				renderer.renderRectangle({r.x - 2.f * b, r.y + r.w + b, r.z + 4.f * b, b}, c);
				renderer.renderRectangle({r.x - 2.f * b, r.y - b, b, r.w + 2.f * b}, c);
				renderer.renderRectangle({r.x + r.z + b, r.y - b, b, r.w + 2.f * b}, c);
			}
		}
	}

	// What is being dragged. Over the hold, its shape where it would go --
	// green where it fits, red where not. Anywhere else, only its icon
	// follows the pointer: the shape matters only where it has to fit.
	if (drag.active)
	{
		if (inside(l.hold.bounds(), p))
		{
			const hold::Shape shape = dragShape();
			const glm::ivec2 target = l.hold.nearestSquare(p) - middleSquare(shape);
			const int ignoring = drag.fromSlot ? -1 : drag.id;
			hold::Shape base;
			if (drag.isOre) { base.cells = {{0, 0}}; } else { base = inventory::shapeOf(drag.kind); }
			const bool fits = hold::fits(grid, base, drag.turns, target, ignoring);
			l.hold.fillCells(renderer, shape.cells.data(), shape.cells.size(), target, fits ? fitColour : noFitColour);
		}
		const bool jettisoning = !inside(l.panel, p) && slotUnder(l, p) < 0;
		if (drag.isOre)
		{
			// A stack is its count on an amber square, the size of one in the hold.
			const float s = l.cell * 0.8f;
			glm::vec4 c = oreColour;
			if (jettisoning) { c = noFitColour; c.a = 0.85f; }
			renderer.renderRectangle({p.x - s * 0.5f, p.y - s * 0.5f, s, s}, c);
			textLook::draw(renderer, p, std::to_string(drag.ore).c_str(), {1.f, 0.95f, 0.85f, 1.f}, u, {0.5f, 0.5f});
		}
		else
		{
			bulletLook::drawIcon(renderer, p, l.cell * 0.9f, weapons::shipWeapon(drag.kind).style);
		}
		// About to be dropped: said under the item, where the eye already is.
		if (jettisoning)
		{
			textLook::draw(renderer, p + glm::vec2(0.f, l.cell * 0.65f), "LET GO TO DROP", {1.f, 0.6f, 0.5f, 1.f}, u, {0.5f, 0.f});
		}
	}

	if (!hoverMarkup.empty())
	{
		wgpu2d::renderMarkup(renderer, l.hoverAt, hoverMarkup.c_str(), hints::style(height), {0.5f, 0.f});
	}
	else if (!hoverText.empty())
	{
		textLook::draw(renderer, l.hoverAt, hoverText.c_str(), {1.f, 0.92f, 0.65f, 1.f}, u, {0.5f, 0.f});
	}
	renderer.popCamera();
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("loadoutMenu", {
	{"cellPerc", cellPerc},
	{"topPerc", topPerc},
	{"lookSeconds", lookSeconds},
	{"greyAmount", greyAmount},
});

void debugUi()
{
	ImGui::Text("%s%s", open ? "Open" : "Closed", drag.active ? ", dragging" : "");
	tune::SliderFloat("Square size", &cellPerc, 0.03f, 0.12f, "%.3f of the height");
	tune::SliderFloat("Top", &topPerc, 0.f, 0.5f, "%.2f of the height");
	tune::SliderFloat("Grey", &greyAmount, 0.f, 1.f, "%.2f of the paused grade");
	tune::SliderFloat("Grey in", &lookSeconds, 0.f, 1.f, "%.2f s");
}

}
