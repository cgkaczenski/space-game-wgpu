#include <hub.h>
#include <bulletLook.h>
#include <controls.h>
#include <crates.h>
#include <hints.h>
#include <inventory.h>
#include <itemLook.h>
#include <platformInput.h>
#include <resources.h>
#include <textLook.h>
#include <tuning.h>
#include <weapons.h>

#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace hub
{

namespace
{
	bool active = false;
	Tally tally;
	std::vector<std::string> levels;
	int chosenLevel = 0;
	std::vector<inventory::Item> stock;
	int stashScroll = 0;      // rows scrolled
	float wheel = 0.f;

	// Tuning.
	int stockSize = 6;
	int priceKind[weapons::slotCount] = {20, 40, 50, 30};   // burst, heavy, missile, beam
	int priceModifier = 25;
	float sellFraction = 0.5f;
	float cellPerc = 0.06f;

	struct Drag
	{
		bool active = false;
		enum class From { Stash, Slot, Hold } from = From::Stash;
		int index = -1;     // stash index, slot, or hold piece id
		inventory::Item item;
		int turns = 0;
	};
	Drag drag;
	int pressedShop = -1;     // a click on a shop tile buys on release over the same tile
	int pressedLevel = -1;
	int pressedButton = -1;   // 0 launch, 1 quit
	std::string hoverText;
	std::string message;      // the last purchase or sale, or why not
	float messageLeft = 0.f;

	const glm::vec4 squareFill = {0.06f, 0.07f, 0.12f, 1.f};
	const glm::vec4 squareLine = {0.22f, 0.24f, 0.34f, 1.f};
	const glm::vec4 panelColour = {0.02f, 0.03f, 0.06f, 0.85f};
	const glm::vec4 fitColour = {0.35f, 1.f, 0.55f, 0.55f};
	const glm::vec4 noFitColour = {1.f, 0.3f, 0.25f, 0.55f};
	const glm::vec4 title = {1.f, 1.f, 1.f, 1.f};
	const glm::vec4 label = {0.75f, 0.8f, 0.9f, 1.f};
	const glm::vec4 warm = {1.f, 0.92f, 0.65f, 1.f};

	const int stashColumns = 5;
	const int stashRows = 6;
	const int shopColumns = 3;

	int price(const inventory::Item &it)
	{
		return priceKind[std::clamp(it.kind, 0, weapons::slotCount - 1)]
			+ priceModifier * ((it.stun ? 1 : 0) + (it.lockdown ? 1 : 0) + (it.spread ? 1 : 0));
	}
	int sellPrice(const inventory::Item &it) { return (int)std::floor(price(it) * sellFraction); }

	struct Layout
	{
		float cell, gap, u, line;
		wgpu2d::GridView stash, hold, shop;
		glm::vec4 stashPanel, shopPanel, loadoutPanel;
		glm::vec4 slots[weapons::slotCount];
		glm::vec4 levelRects[16];
		int levelCount = 0;
		glm::vec4 launch, quit;
		glm::vec2 titleAt, tallyAt, pointsAt, hoverAt;
	};

	bool inside(glm::vec4 r, glm::vec2 p) { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.z && p.y < r.y + r.w; }

	// A level's file name as a button says it: no extension, capitals.
	std::string levelName(const std::string &file)
	{
		std::string s = file.substr(0, file.rfind('.'));
		for (char &c : s) { c = (char)std::toupper((unsigned char)c); }
		return s;
	}

	Layout layout(int w, int h)
	{
		Layout l;
		l.u = textLook::screenScale(h);
		l.line = textLook::font().lineHeight() * l.u;
		l.cell = std::round(h * cellPerc);
		l.gap = std::max(2.f, std::round(l.cell * 0.08f));
		const float margin = std::round(l.cell * 0.5f);

		l.titleAt = {std::round(w * 0.5f), std::round(h * 0.05f)};
		l.tallyAt = {std::round(w * 0.5f), l.titleAt.y + l.line * 2.4f};
		const float top = l.tallyAt.y + l.line * 3.f;

		auto grid = [&](wgpu2d::GridView &g, int cols, int rows)
		{
			g.cell = l.cell;
			g.gap = l.gap;
			g.columns = cols;
			g.rows = rows;
		};
		grid(l.stash, stashColumns, stashRows);
		grid(l.hold, inventory::holdGrid().width, inventory::holdGrid().height);
		grid(l.shop, shopColumns, (stockSize + shopColumns - 1) / shopColumns);
		l.shop.gap = std::round(l.line * 1.4f);   // room under each tile for its price

		const float slotSize = std::round(l.cell * 1.2f);
		const float slotGap = std::round(l.cell * 0.2f);
		const float slotsWidth = weapons::slotCount * slotSize + (weapons::slotCount - 1) * slotGap;
		const float stashW = l.stash.bounds().z, holdW = std::max(l.hold.bounds().z, slotsWidth), shopW = l.shop.bounds().z;
		const float between = std::round(l.cell * 1.2f);
		const float total = stashW + holdW + shopW + 2.f * between;
		float x = std::round(w * 0.5f - total * 0.5f);
		const float gridTop = top + l.line * 1.6f;

		l.stash.topLeft = {x, gridTop};
		l.stashPanel = {x - margin, top - margin, stashW + 2.f * margin, l.stash.bounds().w + l.line * 1.6f + 2.f * margin};
		x += stashW + between;

		const float midLeft = x;
		for (int i = 0; i < weapons::slotCount; i++)
		{
			l.slots[i] = {std::round(midLeft + holdW * 0.5f - slotsWidth * 0.5f) + i * (slotSize + slotGap), gridTop + l.line, slotSize, slotSize};
		}
		l.hold.topLeft = {std::round(midLeft + holdW * 0.5f - l.hold.bounds().z * 0.5f), gridTop + l.line + slotSize + l.line * 2.2f};
		l.loadoutPanel = {midLeft - margin, top - margin, holdW + 2.f * margin,
			l.hold.topLeft.y + l.hold.bounds().w - top + 2.f * margin};
		x += holdW + between;

		l.shop.topLeft = {x, gridTop};
		l.shopPanel = {x - margin, top - margin, shopW + 2.f * margin, l.shop.bounds().w + l.line * 2.2f + 2.f * margin};
		l.pointsAt = {x + shopW, l.tallyAt.y};

		const float panelsBottom = std::max({l.stashPanel.y + l.stashPanel.w, l.loadoutPanel.y + l.loadoutPanel.w,
			l.shopPanel.y + l.shopPanel.w});
		l.hoverAt = {std::round(w * 0.5f), panelsBottom + l.line * 0.8f};

		// The levels in a row, then LAUNCH and QUIT GAME.
		float ly = l.hoverAt.y + l.line * 2.4f;
		l.levelCount = std::min((int)levels.size(), 16);
		const float pad = l.u * 6.f;
		float rowWidth = 0.f;
		std::vector<float> widths;
		for (int i = 0; i < l.levelCount; i++)
		{
			const float tw = wgpu2d::measureText(textLook::font(), levelName(levels[(size_t)i]).c_str(), l.u).x + 2.f * pad;
			widths.push_back(tw);
			rowWidth += tw + pad;
		}
		float lx = std::round(w * 0.5f - rowWidth * 0.5f);
		for (int i = 0; i < l.levelCount; i++)
		{
			l.levelRects[i] = {lx, ly, widths[(size_t)i], l.line * 1.6f};
			lx += widths[(size_t)i] + pad;
		}
		ly += l.line * 2.6f;
		const float launchW = wgpu2d::measureText(textLook::font(), "LAUNCH", l.u * 2.f).x + 4.f * pad;
		l.launch = {std::round(w * 0.5f - launchW * 0.5f), ly, launchW, l.line * 2.6f};
		const float quitW = wgpu2d::measureText(textLook::font(), "QUIT GAME", l.u).x + 2.f * pad;
		l.quit = {std::round(w - quitW - margin * 2.f), std::round(h - l.line * 2.4f), quitW, l.line * 1.6f};
		return l;
	}

	glm::vec2 pointer(int w, int h)
	{
		const glm::ivec2 window = platform::getWindowSize();
		const glm::vec2 toPixels = {window.x > 0 ? (float)w / window.x : 1.f, window.y > 0 ? (float)h / window.y : 1.f};
		return glm::vec2(platform::getRelMousePosition()) * toPixels;
	}

	int stashIndexAt(const Layout &l, glm::vec2 p)
	{
		glm::ivec2 sq;
		if (!l.stash.squareAt(p, sq)) { return -1; }
		const int i = (sq.y + stashScroll) * stashColumns + sq.x;
		return i < (int)inventory::stashItems().size() ? i : -1;
	}

	int slotAt(const Layout &l, glm::vec2 p)
	{
		for (int k = 0; k < weapons::slotCount; k++) { if (inside(l.slots[k], p)) { return k; } }
		return -1;
	}

	int shopIndexAt(const Layout &l, glm::vec2 p)
	{
		glm::ivec2 sq;
		if (!l.shop.squareAt(p, sq)) { return -1; }
		const int i = sq.y * shopColumns + sq.x;
		return i < (int)stock.size() ? i : -1;
	}

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

	void say(const std::string &s) { message = s; messageLeft = 3.f; }

	// Takes the dragged weapon from where it came from.
	bool takeDragged(inventory::Item &out)
	{
		switch (drag.from)
		{
		case Drag::From::Stash: return inventory::takeFromStash(drag.index, out);
		case Drag::From::Slot: return inventory::jettisonSlot(drag.index, out);
		case Drag::From::Hold:
		{
			inventory::Held h;
			if (!inventory::jettisonHeld(drag.index, h)) { return false; }
			out = h.item;
			return true;
		}
		}
		return false;
	}

	void drop(const Layout &l, glm::vec2 p)
	{
		const hold::Shape shape = hold::turned(inventory::shapeOf(drag.item.kind), drag.turns);
		// Onto the shop: sold.
		if (inside(l.shopPanel, p))
		{
			inventory::Item it;
			if (takeDragged(it))
			{
				resources::earn(sellPrice(it));
				say("SOLD " + itemLook::describe(it) + " FOR " + std::to_string(sellPrice(it)));
			}
			return;
		}
		// Into the hold, turned.
		if (inside(l.hold.bounds(), p))
		{
			const glm::ivec2 target = l.hold.nearestSquare(p) - middleSquare(shape);
			if (drag.from == Drag::From::Hold) { inventory::moveInHold(drag.index, drag.turns, target); return; }
			if (drag.from == Drag::From::Slot) { inventory::slotToHold(drag.index, drag.turns, target); return; }
			inventory::Item it = drag.item;
			if (!hold::fits(inventory::holdGrid(), inventory::shapeOf(it.kind), drag.turns, target)) { return; }
			if (inventory::takeFromStash(drag.index, it)) { inventory::addToHold(it, drag.turns, target); }
			return;
		}
		// Onto a slot: equipped; what was there goes to the stash -- or, from
		// a slot, the two swap; from the hold, it goes where the dragged was.
		const int k = slotAt(l, p);
		if (k >= 0)
		{
			if (drag.from == Drag::From::Slot) { inventory::swapSlots(drag.index, k); return; }
			if (drag.from == Drag::From::Hold) { inventory::holdToSlot(drag.index, k); return; }
			inventory::Item it, displaced;
			bool had = false;
			if (inventory::takeFromStash(drag.index, it))
			{
				inventory::putInSlot(k, it, displaced, had);
				if (had) { inventory::addToStash(displaced); }
			}
			return;
		}
		// Onto the stash: left at home.
		if (inside(l.stashPanel, p) && drag.from != Drag::From::Stash)
		{
			inventory::Item it;
			if (takeDragged(it)) { inventory::addToStash(it); }
		}
	}

	void drawPanel(wgpu2d::Renderer2D &r, glm::vec4 rect) { r.renderRectangle(rect, panelColour); }


	void button(wgpu2d::Renderer2D &r, glm::vec4 rect, const char *text, float scale, bool hot, bool on)
	{
		const float b = std::max(2.f, std::round(scale));
		r.renderRectangle(rect, on ? glm::vec4(textLook::hintColour, 1.f) : hot ? warm : label);
		r.renderRectangle({rect.x + b, rect.y + b, rect.z - 2.f * b, rect.w - 2.f * b},
			on ? glm::vec4(0.05f, 0.2f, 0.15f, 1.f) : glm::vec4(0.04f, 0.05f, 0.09f, 1.f));
		textLook::draw(r, {rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f}, text, hot || on ? warm : label, scale, {0.5f, 0.5f});
	}
}

void open(const Tally &t, const std::vector<std::string> &lv, const std::string &current, bool restock)
{
	active = true;
	tally = t;
	levels = lv;
	chosenLevel = 0;
	for (int i = 0; i < (int)levels.size(); i++) { if (levels[(size_t)i] == current) { chosenLevel = i; } }
	if (restock || stock.empty())
	{
		stock.clear();
		for (int i = 0; i < stockSize; i++) { stock.push_back(crates::roll()); }
	}
	inventory::stageLast();
	drag = {};
	stashScroll = 0;
	message.clear();
}

bool isOpen() { return active; }
void close() { active = false; drag = {}; }

Action update(wgpu2d::Renderer2D &renderer, int w, int h, std::string &levelOut)
{
	if (!active) { return Action::None; }
	const Layout l = layout(w, h);
	const glm::vec2 p = pointer(w, h);
	const bool mouseFree = !ImGui::GetCurrentContext() || !ImGui::GetIO().WantCaptureMouse;
	const bool press = mouseFree && platform::isLMousePressed();
	const bool held = platform::isLMouseHeld();
	Action action = Action::None;
	messageLeft = std::max(0.f, messageLeft - 1.f / 60.f);

	// The stash scrolls with the wheel when the pointer is on it.
	if (mouseFree && inside(l.stashPanel, p))
	{
		wheel += platform::getScrollY();
		const int rows = ((int)inventory::stashItems().size() + stashColumns - 1) / stashColumns;
		while (wheel >= 1.f) { stashScroll--; wheel -= 1.f; }
		while (wheel <= -1.f) { stashScroll++; wheel += 1.f; }
		stashScroll = std::clamp(stashScroll, 0, std::max(0, rows - stashRows));
	}

	// Pressing: a drag from the stash, a slot or the hold; or a shop tile, a
	// level, a button -- each acted on when let go over the same thing.
	if (press && !drag.active)
	{
		inventory::Item it;
		if (const int si = stashIndexAt(l, p); si >= 0) { drag = {true, Drag::From::Stash, si, inventory::stashItems()[(size_t)si], 0}; }
		else if (const int k = slotAt(l, p); k >= 0 && inventory::equippedAt(k, it)) { drag = {true, Drag::From::Slot, k, it, 0}; }
		else
		{
			glm::ivec2 sq;
			inventory::Held hh;
			if (l.hold.squareAt(p, sq))
			{
				const int id = hold::at(inventory::holdGrid(), sq);
				const hold::Piece *piece = hold::find(inventory::holdGrid(), id);
				if (piece && inventory::heldAt(id, hh) && !hh.isOre) { drag = {true, Drag::From::Hold, id, hh.item, piece->turns}; }
			}
		}
		pressedShop = shopIndexAt(l, p);
		pressedLevel = -1;
		for (int i = 0; i < l.levelCount; i++) { if (inside(l.levelRects[i], p)) { pressedLevel = i; } }
		pressedButton = inside(l.launch, p) ? 0 : inside(l.quit, p) ? 1 : -1;
	}
	if (drag.active && controls::pressed(controls::Action::MenuRotate)) { drag.turns = (drag.turns + 1) % 4; }
	if (!held)
	{
		if (drag.active) { drop(l, p); drag = {}; }
		if (pressedShop >= 0 && shopIndexAt(l, p) == pressedShop)
		{
			const inventory::Item it = stock[(size_t)pressedShop];
			if (resources::spend(price(it)))
			{
				inventory::addToStash(it);
				stock.erase(stock.begin() + pressedShop);
				say("BOUGHT " + itemLook::describe(it));
			}
			else { say("NOT ENOUGH POINTS"); }
		}
		for (int i = 0; i < l.levelCount; i++) { if (pressedLevel == i && inside(l.levelRects[i], p)) { chosenLevel = i; } }
		if (pressedButton == 0 && inside(l.launch, p) && chosenLevel < (int)levels.size())
		{
			levelOut = levels[(size_t)chosenLevel];
			action = Action::Launch;
		}
		if (pressedButton == 1 && inside(l.quit, p)) { action = Action::Quit; }
		pressedShop = pressedLevel = pressedButton = -1;
	}

	// What the pointer is over, named -- with a price in the shop.
	hoverText.clear();
	inventory::Item it;
	if (const int si = stashIndexAt(l, p); si >= 0) { hoverText = itemLook::describe(inventory::stashItems()[(size_t)si]) + "   SELLS FOR " + std::to_string(sellPrice(inventory::stashItems()[(size_t)si])); }
	if (const int k = slotAt(l, p); k >= 0) { hoverText = inventory::equippedAt(k, it) ? itemLook::describe(it) : "SLOT " + std::to_string(k + 1) + ": EMPTY"; }
	if (const int s = shopIndexAt(l, p); s >= 0) { hoverText = itemLook::describe(stock[(size_t)s]) + "   " + std::to_string(price(stock[(size_t)s])) + " POINTS"; }
	{
		glm::ivec2 sq;
		inventory::Held hh;
		if (l.hold.squareAt(p, sq) && inventory::heldAt(hold::at(inventory::holdGrid(), sq), hh) && !hh.isOre) { hoverText = itemLook::describe(hh.item); }
	}
	if (drag.active)
	{
		hoverText = inside(l.shopPanel, p) ? "LET GO TO SELL FOR " + std::to_string(sellPrice(drag.item)) : "";
	}

	// ---- Drawing ------------------------------------------------------------
	renderer.pushCamera();
	const float u = l.u;
	textLook::draw(renderer, l.titleAt, "HANGAR", title, u * 3.f, {0.5f, 0.f});
	char line[160];
	if (tally.extracted)
	{
		std::snprintf(line, sizeof(line), "EXTRACTED: %d ORE BANKED, %d WEAPON%s TO THE STASH", tally.ore, tally.weapons, tally.weapons == 1 ? "" : "S");
	}
	else { std::snprintf(line, sizeof(line), "LEFT THE MISSION: NOTHING BANKED, %d WEAPON%s LOST", tally.weapons, tally.weapons == 1 ? "" : "S"); }
	textLook::draw(renderer, l.tallyAt, line, tally.extracted ? warm : glm::vec4(1.f, 0.6f, 0.5f, 1.f), u, {0.5f, 0.f});

	// The stash.
	drawPanel(renderer, l.stashPanel);
	std::snprintf(line, sizeof(line), "STASH  %d", (int)inventory::stashItems().size());
	textLook::draw(renderer, {l.stash.topLeft.x, l.stash.topLeft.y - l.line * 1.4f}, line, label, u);
	l.stash.draw(renderer, squareFill, squareLine, std::max(1.f, std::round(u * 0.5f)));
	const std::vector<inventory::Item> &items = inventory::stashItems();
	for (int r = 0; r < stashRows; r++)
	{
		for (int c = 0; c < stashColumns; c++)
		{
			const int i = (r + stashScroll) * stashColumns + c;
			if (i >= (int)items.size()) { continue; }
			if (drag.active && drag.from == Drag::From::Stash && drag.index == i) { continue; }
			itemLook::drawTile(renderer, l.stash.square({c, r}), items[(size_t)i]);
		}
	}

	// The loadout: slots and hold.
	drawPanel(renderer, l.loadoutPanel);
	textLook::draw(renderer, {l.loadoutPanel.x + l.loadoutPanel.z * 0.5f, l.stash.topLeft.y - l.line * 1.4f}, "TAKING", label, u, {0.5f, 0.f});
	for (int k = 0; k < weapons::slotCount; k++)
	{
		const glm::vec4 r = l.slots[k];
		const float b = std::max(2.f, std::round(u));
		const bool target = drag.active && inside(r, p);
		renderer.renderRectangle({r.x - b, r.y - b, r.z + 2.f * b, r.w + 2.f * b}, target ? glm::vec4(fitColour.r, fitColour.g, fitColour.b, 1.f) : label);
		renderer.renderRectangle(r, squareFill);
		if (inventory::equippedAt(k, it) && !(drag.active && drag.from == Drag::From::Slot && drag.index == k))
		{
			itemLook::drawTile(renderer, {r.x + b, r.y + b, r.z - 2.f * b, r.w - 2.f * b}, it);
		}
		textLook::draw(renderer, {r.x + r.z * 0.5f, r.y - b * 2.f}, std::to_string(k + 1).c_str(), label, u, {0.5f, 1.f});
	}
	textLook::draw(renderer, {l.hold.topLeft.x, l.hold.topLeft.y - l.line * 1.4f}, "HOLD", label, u);
	l.hold.draw(renderer, squareFill, squareLine, std::max(1.f, std::round(u * 0.5f)));
	for (const hold::Piece &piece : inventory::holdGrid().pieces)
	{
		inventory::Held hh;
		if (!inventory::heldAt(piece.id, hh) || hh.isOre) { continue; }
		if (drag.active && drag.from == Drag::From::Hold && drag.index == piece.id) { continue; }
		l.hold.fillCells(renderer, piece.shape.cells.data(), piece.shape.cells.size(), piece.at, itemLook::weaponColour(hh.item.kind));
		glm::vec2 sum = {};
		for (const glm::ivec2 &c : piece.shape.cells) { const glm::vec4 s = l.hold.square(piece.at + c); sum += glm::vec2(s.x + s.z * 0.5f, s.y + s.w * 0.5f); }
		bulletLook::drawIcon(renderer, sum / (float)piece.shape.cells.size(), l.cell * 0.9f, weapons::shipWeapon(hh.item.kind).style);
	}

	// The shop.
	drawPanel(renderer, l.shopPanel);
	textLook::draw(renderer, {l.shop.topLeft.x, l.shop.topLeft.y - l.line * 1.4f}, "SHOP", label, u);
	std::snprintf(line, sizeof(line), "POINTS %d", resources::banked());
	textLook::draw(renderer, l.pointsAt, line, warm, u, {1.f, 0.f});
	l.shop.draw(renderer, squareFill, squareLine, std::max(1.f, std::round(u * 0.5f)));
	for (int i = 0; i < (int)stock.size(); i++)
	{
		const glm::vec4 sq = l.shop.square({i % shopColumns, i / shopColumns});
		itemLook::drawTile(renderer, sq, stock[(size_t)i]);
		const bool afford = resources::banked() >= price(stock[(size_t)i]);
		if (!afford) { renderer.renderRectangle(sq, {0.f, 0.f, 0.f, 0.55f}); }
		textLook::draw(renderer, {sq.x + sq.z * 0.5f, sq.y + sq.w}, std::to_string(price(stock[(size_t)i])).c_str(),
			afford ? warm : glm::vec4(0.6f, 0.6f, 0.65f, 1.f), u, {0.5f, 0.f});
	}
	if (stock.empty()) { textLook::draw(renderer, {l.shop.topLeft.x, l.shop.topLeft.y + l.line}, "SOLD OUT", label, u); }
	if (drag.active && inside(l.shopPanel, p))
	{
		const float b = std::max(2.f, std::round(u));
		const glm::vec4 r = l.shopPanel;
		const glm::vec4 c = {warm.r, warm.g, warm.b, 1.f};
		renderer.renderRectangle({r.x, r.y, r.z, b}, c);
		renderer.renderRectangle({r.x, r.y + r.w - b, r.z, b}, c);
		renderer.renderRectangle({r.x, r.y, b, r.w}, c);
		renderer.renderRectangle({r.x + r.z - b, r.y, b, r.w}, c);
	}

	// What the pointer is over, or the last sale.
	if (!hoverText.empty()) { textLook::draw(renderer, l.hoverAt, hoverText.c_str(), warm, u, {0.5f, 0.f}); }
	else if (messageLeft > 0.f) { textLook::draw(renderer, l.hoverAt, message.c_str(), label, u, {0.5f, 0.f}); }

	// Levels, LAUNCH, QUIT GAME.
	for (int i = 0; i < l.levelCount; i++)
	{
		button(renderer, l.levelRects[i], levelName(levels[(size_t)i]).c_str(), u, inside(l.levelRects[i], p), i == chosenLevel);
	}
	button(renderer, l.launch, "LAUNCH", u * 2.f, inside(l.launch, p), false);
	button(renderer, l.quit, "QUIT GAME", u, inside(l.quit, p), false);

	// The dragged weapon: its shape over the hold, green or red; elsewhere
	// its icon. R turns it.
	if (drag.active)
	{
		const hold::Shape shape = hold::turned(inventory::shapeOf(drag.item.kind), drag.turns);
		if (inside(l.hold.bounds(), p))
		{
			const glm::ivec2 target = l.hold.nearestSquare(p) - middleSquare(shape);
			const bool fits = hold::fits(inventory::holdGrid(), inventory::shapeOf(drag.item.kind), drag.turns, target,
				drag.from == Drag::From::Hold ? drag.index : -1);
			l.hold.fillCells(renderer, shape.cells.data(), shape.cells.size(), target, fits ? fitColour : noFitColour);
			wgpu2d::renderMarkup(renderer, {l.hold.topLeft.x + l.hold.bounds().z, l.hold.topLeft.y - l.line * 1.4f},
				(hints::keys(controls::Action::MenuRotate) + " TO ROTATE").c_str(), hints::style(h), {1.f, 0.f});
		}
		bulletLook::drawIcon(renderer, p, l.cell * 0.9f, weapons::shipWeapon(drag.item.kind).style);
	}
	renderer.popCamera();
	return action;
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("hub", {
	{"stockSize", stockSize},
	{"price.burst", priceKind[0]},
	{"price.heavy", priceKind[1]},
	{"price.missile", priceKind[2]},
	{"price.beam", priceKind[3]},
	{"price.modifier", priceModifier},
	{"sellFraction", sellFraction},
	{"cellPerc", cellPerc},
});

void debugUi()
{
	ImGui::Text(active ? "Open" : "Closed");
	tune::SliderInt("Shop stock", &stockSize, 1, 12);
	tune::SliderInt("Burst price", &priceKind[0], 0, 500);
	tune::SliderInt("Heavy price", &priceKind[1], 0, 500);
	tune::SliderInt("Missile price", &priceKind[2], 0, 500);
	tune::SliderInt("Beam price", &priceKind[3], 0, 500);
	tune::SliderInt("Per modifier", &priceModifier, 0, 500);
	tune::SliderFloat("Sells for", &sellFraction, 0.f, 1.f, "%.2f of the price");
	if (ImGui::SmallButton("Restock the shop"))
	{
		stock.clear();
		for (int i = 0; i < stockSize; i++) { stock.push_back(crates::roll()); }
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("+100 points")) { resources::earn(100); }
}

}
