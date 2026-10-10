#include <inventory.h>
#include <tuning.h>

#include "imgui.h"
#include <algorithm>
#include <string>

namespace inventory
{

namespace
{
	struct Slot
	{
		bool filled = false;
		Item item;
	};

	// What the hold keeps under each piece id.
	struct Content
	{
		bool isOre = false;
		Item item;       // a weapon
		int ore = 0;     // orbs, up to stackSize
	};

	std::vector<Item> stash;
	Slot equipped[weapons::slotCount];
	bool swappedIn[weapons::slotCount] = {};   // put in this frame: applyTo starts its cooldown
	bool launching = false;                    // the next round keeps the hub's slots and hold
	Slot lastLoadout[weapons::slotCount];   // what the next round equips (until I4)

	const int holdWidth = 5;
	const int holdHeight = 4;
	hold::Grid grid = hold::make(holdWidth, holdHeight);
	std::vector<std::pair<int, Content>> contents;  // by piece id
	int nextId = 1;

	const int freeKind = 3;          // the beam: free and never lost
	int spreadShots = 2;             // a spread item's extra shots, as enemies' (B2)

	// The kinds' shapes, as 4 x 4 bit masks: bit y * 4 + x is square (x, y).
	// Tunable, and drawn as tick boxes in the panel.
	int shapeMask[weapons::slotCount] = {
		0x0033,   // burst: 2 x 2
		0x0077,   // heavy: 3 x 2
		0x0711,   // missile: an L of 5
		0x00FF,   // beam: 4 x 2
	};

	// The default loadout: a new player's, and what the panel's pick lists
	// set. -1 is an empty slot. A beam alone, in slot 4.
	int defaultKind[weapons::slotCount] = {-1, -1, -1, 3};
	bool defaultStun[weapons::slotCount] = {};
	bool defaultLockdown[weapons::slotCount] = {};
	bool defaultSpread[weapons::slotCount] = {};

	const char *const kindNames[] = {"Empty", "Burst laser", "Heavy laser", "Missile", "Laser (beam)"};

	Content *contentOf(int id)
	{
		for (auto &c : contents) { if (c.first == id) { return &c.second; } }
		return nullptr;
	}

	void forget(int id)
	{
		contents.erase(std::remove_if(contents.begin(), contents.end(),
			[&](const std::pair<int, Content> &c) { return c.first == id; }), contents.end());
		hold::remove(grid, id);
	}

	void emptyHold()
	{
		grid = hold::make(holdWidth, holdHeight);
		contents.clear();
	}

	Slot defaultSlot(int i)
	{
		Slot s;
		if (defaultKind[i] < 0 || defaultKind[i] >= weapons::slotCount) { return s; }
		s.filled = true;
		s.item = {defaultKind[i], defaultStun[i], defaultLockdown[i], defaultSpread[i]};
		return s;
	}

	// Out of the stash, if it has one like it.
	bool takeFromStash(const Item &want)
	{
		const auto it = std::find(stash.begin(), stash.end(), want);
		if (it == stash.end()) { return false; }
		stash.erase(it);
		return true;
	}

	bool hasBeam()
	{
		for (const Slot &s : equipped) { if (s.filled && s.item.kind == freeKind) { return true; } }
		for (const auto &c : contents) { if (!c.second.isOre && c.second.item.kind == freeKind) { return true; } }
		return false;
	}

	std::string describe(const Item &it)
	{
		std::string s = kindNames[it.kind + 1];
		if (it.stun) { s += " +stun"; }
		if (it.lockdown) { s += " +lockdown"; }
		if (it.spread) { s += " +spread"; }
		return s;
	}

	// A weapon into the hold, wherever it first fits. False if it does not.
	bool stow(const Item &it)
	{
		glm::ivec2 at;
		int turns = 0;
		const hold::Shape shape = shapeOf(it.kind);
		if (!hold::findSpot(grid, shape, at, turns)) { return false; }
		const int id = nextId++;
		hold::place(grid, id, shape, turns, at);
		Content c;
		c.item = it;
		contents.push_back({id, c});
		return true;
	}

	// The panel's item maker: a kind and its modifiers, kept between frames.
	int makeKind = 0;
	bool makeStun = false, makeLockdown = false, makeSpread = false;
}

hold::Shape shapeOf(int kind)
{
	hold::Shape s;
	if (kind < 0 || kind >= weapons::slotCount) { return s; }
	for (int b = 0; b < 16; b++)
	{
		if (shapeMask[kind] & (1 << b)) { s.cells.push_back({b % 4, b / 4}); }
	}
	return s;
}

const hold::Grid &holdGrid() { return grid; }

void newPlayer()
{
	stash.clear();
	for (int i = 0; i < weapons::slotCount; i++)
	{
		lastLoadout[i] = defaultSlot(i);
		if (lastLoadout[i].filled) { stash.push_back(lastLoadout[i].item); }
	}
}

void roundStart()
{
	// Launched from the hub (I4): the slots and hold are as the player packed
	// them, already out of the stash.
	if (launching) { launching = false; }
	else
	{
		emptyHold();
		for (int i = 0; i < weapons::slotCount; i++)
		{
			equipped[i] = {};
			if (lastLoadout[i].filled && takeFromStash(lastLoadout[i].item)) { equipped[i] = lastLoadout[i]; }
		}
	}

	// The default loadout's slots are kept through death: one the stash could
	// not fill gets its default back, new. The pick lists are a test bench,
	// and a death should not cost what is being tested. (The shipped default
	// is the beam alone, which is free anyway.)
	for (int i = 0; i < weapons::slotCount; i++)
	{
		if (!equipped[i].filled) { equipped[i] = defaultSlot(i); }
	}

	// The free beam: in slot 4 if it is empty, else the first empty slot,
	// else the hold.
	if (!hasBeam())
	{
		Slot beam;
		beam.filled = true;
		beam.item = {freeKind};
		int at = equipped[weapons::slotCount - 1].filled ? -1 : weapons::slotCount - 1;
		for (int i = 0; at < 0 && i < weapons::slotCount; i++) { if (!equipped[i].filled) { at = i; } }
		if (at >= 0) { equipped[at] = beam; }
		else { stow(beam.item); }
	}
}

void extracted()
{
	for (int i = 0; i < weapons::slotCount; i++)
	{
		lastLoadout[i] = equipped[i];
		if (equipped[i].filled) { stash.push_back(equipped[i].item); }
		equipped[i] = {};
	}
	for (const auto &c : contents) { if (!c.second.isOre) { stash.push_back(c.second.item); } }
	emptyHold();
}

void died()
{
	// The loadout to try for next time stays what it was; the stash may no
	// longer have it, and a slot it cannot fill stays empty.
	for (Slot &s : equipped) { s = {}; }
	emptyHold();
}

int ore()
{
	int n = 0;
	for (const auto &c : contents) { if (c.second.isOre) { n += c.second.ore; } }
	return n;
}

int oreRoom()
{
	int room = hold::freeSquares(grid) * stackSize;
	for (const auto &c : contents) { if (c.second.isOre) { room += stackSize - c.second.ore; } }
	return room;
}

int addOre(int units)
{
	int left = std::max(units, 0);
	// Topping up the stacks there are, first: a square holds 64.
	for (auto &c : contents)
	{
		if (left <= 0) { break; }
		if (!c.second.isOre) { continue; }
		const int put = std::min(left, stackSize - c.second.ore);
		c.second.ore += put;
		left -= put;
	}
	// Then a new stack in each free square, as long as there is ore.
	hold::Shape square;
	square.cells = {{0, 0}};
	glm::ivec2 at;
	int turns = 0;
	while (left > 0 && hold::findSpot(grid, square, at, turns))
	{
		const int id = nextId++;
		hold::place(grid, id, square, 0, at);
		Content c;
		c.isOre = true;
		c.ore = std::min(left, stackSize);
		left -= c.ore;
		contents.push_back({id, c});
	}
	return std::max(units, 0) - left;
}

int takeOre()
{
	const int n = ore();
	std::vector<int> stacks;
	for (const auto &c : contents) { if (c.second.isOre) { stacks.push_back(c.first); } }
	for (int id : stacks) { forget(id); }
	return n;
}

namespace
{
	// The slots as they now are are what the next round equips.
	void remember()
	{
		for (int i = 0; i < weapons::slotCount; i++) { lastLoadout[i] = equipped[i]; }
	}

	bool validSlot(int s) { return s >= 0 && s < weapons::slotCount; }

	// A weapon into the hold at a place, or anywhere: false if neither.
	bool stowAt(const Item &it, int turns, glm::ivec2 at, bool anywhere)
	{
		const hold::Shape shape = shapeOf(it.kind);
		int id = nextId;
		if (!hold::place(grid, id, shape, turns, at))
		{
			if (!anywhere || !hold::findSpot(grid, shape, at, turns)) { return false; }
			hold::place(grid, id, shape, turns, at);
		}
		nextId++;
		Content c;
		c.item = it;
		contents.push_back({id, c});
		return true;
	}
}

bool heldAt(int id, Held &out)
{
	const Content *c = contentOf(id);
	if (!c) { return false; }
	out = {c->isOre, c->item, c->ore};
	return true;
}

bool equippedAt(int slot, Item &out)
{
	if (!validSlot(slot) || !equipped[slot].filled) { return false; }
	out = equipped[slot].item;
	return true;
}

bool moveInHold(int id, int turns, glm::ivec2 at)
{
	const Content *c = contentOf(id);
	if (!c) { return false; }
	hold::Shape shape;
	if (c->isOre) { shape.cells = {{0, 0}}; }
	else { shape = shapeOf(c->item.kind); }
	return hold::place(grid, id, shape, turns, at);
}

bool holdToSlot(int id, int slot)
{
	Content *c = contentOf(id);
	const hold::Piece *piece = hold::find(grid, id);
	if (!c || c->isOre || !piece || !validSlot(slot)) { return false; }
	const Item moving = c->item;
	const glm::ivec2 wasAt = piece->at;
	const int wasTurns = piece->turns;

	// Out of the hold first, so the slot's weapon can take its place.
	forget(id);
	if (equipped[slot].filled && !stowAt(equipped[slot].item, wasTurns, wasAt, true))
	{
		// Nowhere for it: put the moved weapon back as it was, and refuse.
		stowAt(moving, wasTurns, wasAt, true);
		return false;
	}
	equipped[slot] = {true, moving};
	swappedIn[slot] = true;
	remember();
	return true;
}

bool slotToHold(int slot, int turns, glm::ivec2 at)
{
	if (!validSlot(slot) || !equipped[slot].filled) { return false; }
	if (!stowAt(equipped[slot].item, turns, at, false)) { return false; }
	equipped[slot] = {};
	remember();
	return true;
}

bool swapSlots(int a, int b)
{
	if (!validSlot(a) || !validSlot(b) || a == b) { return false; }
	std::swap(equipped[a], equipped[b]);
	swappedIn[a] = equipped[a].filled;
	swappedIn[b] = equipped[b].filled;
	remember();
	return true;
}

const std::vector<Item> &stashItems() { return stash; }

bool takeFromStash(int index, Item &out)
{
	if (index < 0 || index >= (int)stash.size()) { return false; }
	out = stash[(size_t)index];
	stash.erase(stash.begin() + index);
	return true;
}

void addToStash(const Item &item) { stash.push_back(item); }

void stageLast()
{
	// Whatever is out goes home first, so nothing is lost or doubled.
	for (Slot &s : equipped) { if (s.filled) { stash.push_back(s.item); } s = {}; }
	for (const auto &c : contents) { if (!c.second.isOre) { stash.push_back(c.second.item); } }
	emptyHold();
	for (int i = 0; i < weapons::slotCount; i++)
	{
		if (lastLoadout[i].filled && takeFromStash(lastLoadout[i].item)) { equipped[i] = lastLoadout[i]; }
	}
}

int carriedWeapons()
{
	int n = 0;
	for (const Slot &s : equipped) { n += s.filled ? 1 : 0; }
	for (const auto &c : contents) { n += c.second.isOre ? 0 : 1; }
	return n;
}

void launch()
{
	remember();
	launching = true;
}

void abandoned() { died(); }

bool addToHold(const Item &item, int turns, glm::ivec2 at)
{
	return stowAt(item, turns, at, false);
}

bool putInSlot(int slot, const Item &item, Item &displaced, bool &hadOne)
{
	if (!validSlot(slot)) { return false; }
	hadOne = equipped[slot].filled;
	if (hadOne) { displaced = equipped[slot].item; }
	equipped[slot] = {true, item};
	swappedIn[slot] = true;
	remember();
	return true;
}

bool jettisonHeld(int id, Held &out)
{
	if (!heldAt(id, out)) { return false; }
	forget(id);
	return true;
}

bool jettisonSlot(int slot, Item &out)
{
	if (!equippedAt(slot, out)) { return false; }
	equipped[slot] = {};
	remember();
	return true;
}

void applyTo(weapons::Loadout &loadout, const weapons::Weapon *tuned)
{
	for (int i = 0; i < weapons::slotCount; i++)
	{
		weapons::Weapon w;
		if (equipped[i].filled)
		{
			const Item &it = equipped[i].item;
			w = tuned[it.kind];
			w.stun = it.stun;
			w.lockdown = it.lockdown;
			w.spread = it.spread && !w.beam ? spreadShots : 0;
		}
		else
		{
			w.name = "";
			w.empty = true;
		}
		weapons::refit(loadout, i, w);
		// Put in by hand: on its cooldown, even if it is the same kind as
		// what it replaced -- a swap always costs one.
		if (swappedIn[i] && !w.empty) { loadout.cooldownLeft[i] = w.cooldown; }
		swappedIn[i] = false;
	}
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("inventory", {
	{"slot1.kind", defaultKind[0]}, {"slot1.stun", defaultStun[0]}, {"slot1.lockdown", defaultLockdown[0]}, {"slot1.spread", defaultSpread[0]},
	{"slot2.kind", defaultKind[1]}, {"slot2.stun", defaultStun[1]}, {"slot2.lockdown", defaultLockdown[1]}, {"slot2.spread", defaultSpread[1]},
	{"slot3.kind", defaultKind[2]}, {"slot3.stun", defaultStun[2]}, {"slot3.lockdown", defaultLockdown[2]}, {"slot3.spread", defaultSpread[2]},
	{"slot4.kind", defaultKind[3]}, {"slot4.stun", defaultStun[3]}, {"slot4.lockdown", defaultLockdown[3]}, {"slot4.spread", defaultSpread[3]},
	{"shape.burst", shapeMask[0]}, {"shape.heavy", shapeMask[1]}, {"shape.missile", shapeMask[2]}, {"shape.beam", shapeMask[3]},
	{"spreadShots", spreadShots},
});

void debugUi()
{
	// The default loadout: a pick list and modifiers per slot. Saved with the
	// tuning; a change here also equips it now, so it can be tried at once.
	ImGui::SeparatorText("Slots (the default loadout, saved with the tuning)");
	ImGui::TextDisabled("Kept through death: a slot set here is refilled every round");
	for (int i = 0; i < weapons::slotCount; i++)
	{
		ImGui::PushID(i);
		int pick = defaultKind[i] + 1;
		bool changed = false;
		{
			tune::Highlight h(&defaultKind[i]);
			ImGui::SetNextItemWidth(140.f);
			if (ImGui::Combo(("Slot " + std::to_string(i + 1)).c_str(), &pick, kindNames, IM_ARRAYSIZE(kindNames)))
			{
				defaultKind[i] = pick - 1;
				changed = true;
			}
		}
		if (defaultKind[i] >= 0)
		{
			ImGui::SameLine(); changed |= tune::Checkbox("stun", &defaultStun[i]);
			ImGui::SameLine(); changed |= tune::Checkbox("lockdown", &defaultLockdown[i]);
			ImGui::SameLine(); changed |= tune::Checkbox("spread", &defaultSpread[i]);
		}
		if (changed)
		{
			equipped[i] = defaultSlot(i);
			lastLoadout[i] = equipped[i];
		}
		ImGui::PopID();
	}
	if (ImGui::SmallButton("New player")) { newPlayer(); roundStart(); }
	ImGui::SameLine();
	ImGui::TextDisabled("(the stash becomes the default loadout, equipped)");

	// The hold: a square per cell, a letter per weapon, a count per stack.
	ImGui::SeparatorText("Hold");
	ImGui::Text("%d orbs of ore, room for %d more; %d of %d squares free", ore(), oreRoom(),
		hold::freeSquares(grid), holdWidth * holdHeight);
	if (ImGui::BeginTable("##hold", holdWidth, ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit))
	{
		for (int y = 0; y < holdHeight; y++)
		{
			ImGui::TableNextRow();
			for (int x = 0; x < holdWidth; x++)
			{
				ImGui::TableNextColumn();
				const int id = hold::at(grid, {x, y});
				const Content *c = id >= 0 ? contentOf(id) : nullptr;
				if (!c) { ImGui::TextDisabled(" .  "); }
				else if (c->isOre) { ImGui::Text("%3d ", c->ore); }
				else { ImGui::Text(" %c%-2d", "BHML"[c->item.kind], id); }
			}
		}
		ImGui::EndTable();
	}
	for (const auto &c : contents)
	{
		if (c.second.isOre) { continue; }
		ImGui::PushID(c.first);
		ImGui::BulletText("%d: %s", c.first, describe(c.second.item).c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("Drop")) { forget(c.first); ImGui::PopID(); break; }
		ImGui::PopID();
	}

	ImGui::SeparatorText("Equipped and stash");
	for (int i = 0; i < weapons::slotCount; i++)
	{
		ImGui::Text("Slot %d: %s", i + 1, equipped[i].filled ? describe(equipped[i].item).c_str() : "(empty)");
	}
	ImGui::Text("Stash: %d", (int)stash.size());
	for (const Item &it : stash) { ImGui::BulletText("%s", describe(it).c_str()); }

	// Making items, until salvage, pickups and the shop (I3).
	ImGui::SeparatorText("Make an item (test)");
	{
		// The kinds without "Empty", so the combo's index is the kind.
		ImGui::SetNextItemWidth(140.f);
		ImGui::Combo("Kind", &makeKind, kindNames + 1, IM_ARRAYSIZE(kindNames) - 1);
		ImGui::SameLine(); ImGui::Checkbox("stun##make", &makeStun);
		ImGui::SameLine(); ImGui::Checkbox("lockdown##make", &makeLockdown);
		ImGui::SameLine(); ImGui::Checkbox("spread##make", &makeSpread);
		const Item it = {makeKind, makeStun, makeLockdown, makeSpread};
		if (ImGui::Button("Into the hold") && !stow(it)) { ImGui::OpenPopup("full"); }
		ImGui::SameLine();
		if (ImGui::Button("Into the stash")) { stash.push_back(it); }
		ImGui::SameLine();
		if (ImGui::Button("+16 ore")) { addOre(16); }
		if (ImGui::BeginPopup("full")) { ImGui::Text("It does not fit in the hold"); ImGui::EndPopup(); }
	}

	// The shapes, as tick boxes on a 4 x 4: tuned, and saved with the tuning.
	if (ImGui::TreeNode("Shapes"))
	{
		for (int k = 0; k < weapons::slotCount; k++)
		{
			ImGui::PushID(k);
			int cells = 0;
			for (int b = 0; b < 16; b++) { if (shapeMask[k] & (1 << b)) { cells++; } }
			ImGui::Text("%s: %d squares", kindNames[k + 1], cells);
			tune::Highlight h(&shapeMask[k]);
			for (int y = 0; y < 4; y++)
			{
				for (int x = 0; x < 4; x++)
				{
					if (x > 0) { ImGui::SameLine(); }
					const int bit = 1 << (y * 4 + x);
					bool on = (shapeMask[k] & bit) != 0;
					ImGui::PushID(y * 4 + x);
					if (ImGui::Checkbox("##c", &on)) { shapeMask[k] = on ? (shapeMask[k] | bit) : (shapeMask[k] & ~bit); }
					ImGui::PopID();
				}
			}
			ImGui::PopID();
		}
		ImGui::TextDisabled("New pieces take the new shape; ones already in the hold keep theirs");
		ImGui::TreePop();
	}
}

}
