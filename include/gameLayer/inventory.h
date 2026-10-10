#pragma once

// What the player owns (inventory roadmap I1): weapons in a stash, four
// equipped, and a hold for everything else carried, ore included.
//
//   Stash     owned, between missions. Not saved yet: it lasts while the
//             game runs (the author's call).
//   Equipped  the four weapon slots, keys 1-4. A slot may be empty.
//   Hold      a 5 x 4 grid (engine/hold). Every weapon carried but not
//             equipped is a shape in it, turned to fit: burst 2x2, missile
//             an L of 5, heavy 3x2, beam 4x2. Ore is in it too, in stacks of
//             up to 64 orbs a square, so ore and spare weapons compete for
//             room. Full, orbs wait in space.
//
// A round takes the equipped weapons out of the stash -- they are at risk
// from then on. Extracting puts everything carried back in the stash and
// banks the ore; dying loses all of it, equipped included. A plain beam is
// free and never lost: a round without one gets one, so a player can always
// mine their way back.
//
// An item is a kind and its modifiers, not a copy of a weapon's numbers: its
// stats come from the kind as tuned, so the debug panel's weapon tuning
// still reaches every one of that kind.
//
// Until the screen before a mission exists (I4), a round equips what was
// equipped last time, as far as the stash still has it.
//
// The default loadout -- the debug panel's pick list per slot, saved with
// the tuning -- is kept through death: a slot it sets that the stash cannot
// fill gets its default back, new, every round. It is a test bench, and a
// death should not cost what is being tested.

#include <weapons.h>
#include <engine/hold.h>
#include <vector>

namespace inventory
{
	// A weapon the player owns: one of weapons::shipWeapon's kinds -- 0 burst,
	// 1 heavy, 2 missile, 3 laser (the beam) -- and its modifiers.
	struct Item
	{
		int kind = 3;
		bool stun = false;
		bool lockdown = false;
		bool spread = false;

		bool operator==(const Item &o) const
		{
			return kind == o.kind && stun == o.stun && lockdown == o.lockdown && spread == o.spread;
		}
	};

	constexpr int stackSize = 64;   // orbs of ore in one square of the hold

	// A new player: the default loadout (tuning, the debug panel's pick
	// lists), owned and equipped. Call once, after the tuning is loaded.
	void newPlayer();

	// A new round: the hold emptied, the equipped weapons taken out of the
	// stash, and the free beam if there is no beam.
	void roundStart();

	// The round ended by extracting: everything carried goes to the stash.
	// Take the ore first (takeOre) -- banking it is resources'.
	void extracted();

	// The round ended in death: everything carried is gone.
	void died();

	// Ore, in orbs: in the hold, the room for more, and adding and taking.
	int ore();
	int oreRoom();
	int addOre(int units);    // returns how many fitted
	int takeOre();            // empties the hold's ore, returns how much

	// Builds the equipped slots into `loadout` from `tuned` (the kinds as the
	// debug panel tunes them) and each item's modifiers. Every frame: a
	// tuning change reaches the slots at once, and a slot's state is kept
	// unless its weapon changed.
	void applyTo(weapons::Loadout &loadout, const weapons::Weapon *tuned);

	// For the hold's screen (I2) and the debug panel.
	const hold::Grid &holdGrid();
	hold::Shape shapeOf(int kind);

	// ---- Moving things (I2) -------------------------------------------------
	//
	// Every move either happens whole or not at all: a refused move changes
	// nothing. A weapon moved into a slot starts on its cooldown (the asked
	// swap cost), and the slots as they now are become what the next round
	// equips.

	// What is in the hold under a piece's id.
	struct Held
	{
		bool isOre = false;
		Item item;       // a weapon
		int ore = 0;     // a stack's orbs
	};
	bool heldAt(int id, Held &out);
	bool equippedAt(int slot, Item &out);   // false: the slot is empty

	// Within the hold: to `at`, turned `turns` from the piece's own shape.
	bool moveInHold(int id, int turns, glm::ivec2 at);
	// A weapon from the hold into a slot. What was in the slot goes to the
	// hold, where the moved weapon was if it fits there, else wherever it
	// fits; with nowhere for it, the move is refused.
	bool holdToSlot(int id, int slot);
	// A slot's weapon into the hold, at `at`, turned `turns`.
	bool slotToHold(int slot, int turns, glm::ivec2 at);
	// Two slots trade weapons (an empty one too).
	bool swapSlots(int a, int b);

	// From outside -- a crate (I3): a weapon into the hold at `at`, turned
	// `turns`; or into a slot, handing back what was there.
	bool addToHold(const Item &item, int turns, glm::ivec2 at);
	bool putInSlot(int slot, const Item &item, Item &displaced, bool &hadOne);

	// Thrown out: gone from the hold or the slot. Returns what it was, for
	// the caller to leave in space -- ore as orbs, a weapon as salvage (I3).
	bool jettisonHeld(int id, Held &out);
	bool jettisonSlot(int slot, Item &out);

	void debugUi();
}
