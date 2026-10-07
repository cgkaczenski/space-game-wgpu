#pragma once

// Weapons, for any ship (gameplay roadmap C2, B1): what a weapon is, what a
// ship carries, and one fire function that turns a trigger into bullets.
//
// A weapon is data -- cooldown, damage, size, speed, burst, ammo -- not a
// class. Every shot is an ordinary Bullet carrying its damage, size, style and
// shooter, so the collision loop and the drawing need no idea which weapon or
// which ship fired it.
//
// What a ship carries is a Loadout: its weapons, by value, and everything
// that changes as it fires -- cooldowns, ammo, a burst in progress, the
// laser's charge, the beam. Each ship owns one; the functions take the one
// they act on. Both draw from the same four (shipWeapon): the player carries
// all of them, an enemy one, rolled at spawn (enemyAi). The tuning that is
// not a weapon's -- the laser's charge rules, how missiles fly -- is shared,
// here.
//
//   1  Burst laser   two shots in quick succession, then the cooldown
//   2  Heavy laser   one larger shot, three times a burst shot's damage
//   3  Missile       5 rounds, homing on the enemy nearest the mouse (C3a)
//   4  Laser         a held beam (C3b). 5 s of charge that drains while it
//                    fires and is kept when released; empty, it cools down
//                    and comes back full
//
// Cooldowns tick for every slot at once, so switching does not reset them.
// The player selects by keys 1-4 or the wheel (wrapping); an empty weapon can
// be selected but not fired.

#include <bullet.h>
#include <shipId.h>
#include <glm/vec2.hpp>
#include <functional>
#include <vector>

struct Enemy;

namespace weapons
{
	constexpr int slotCount = 4;

	struct Weapon
	{
		const char *name = "";
		BulletStyle style = BulletStyle::Standard;
		BulletMotion motion = BulletMotion::Straight;
		float cooldown = 1.f;    // seconds before it can fire again, from the first shot
		float damage = 0.1f;     // per shot -- an enemy has 1 life -- or per second for a beam
		float size = 1.f;        // scales the sprite, the glow and the hitbox
		float speed = 3000.f;    // bullet speed, world units per second; a missile's top speed
		int burstCount = 1;      // shots per trigger
		float burstGap = 0.f;    // seconds between shots of a burst
		int maxAmmo = -1;        // -1: unlimited
		bool beam = false;       // held, not fired: the laser (C3b)
		// Where it is best used, in world units (B2): a ship with several
		// weapons picks the ready one whose best range is nearest how far
		// away its target is.
		float bestRange = 2000.f;

		// Modifiers (gameplay roadmap B2), on this weapon in this loadout.
		// Stun and lockdown act on a hit that reaches the hull -- a raised
		// shield blocks them as it blocks the damage.
		bool stun = false;       // the hit stuns the target, as a ram does
		bool lockdown = false;   // the hit shuts the target's weapons down, then they cool down
		int spread = 0;          // extra shots, fanned either side; not on a beam
	};

	// The laser this frame, as `update` left it. The beam is not a bullet: the
	// game traces it against the enemies and applies `damagePerSecond` to the
	// first one it touches, times the frame's game time.
	struct Beam
	{
		bool firing = false;
		bool started = false;       // the first frame of a pull, for the sound
		glm::vec2 origin = {};      // the ship's nose
		glm::vec2 direction = {};   // unit, toward the mouse
		float damagePerSecond = 0.f;
		bool stun = false;          // its weapon's modifiers (B2), for what it burns
		bool lockdown = false;
	};

	// What one ship carries, and the state of it.
	struct Loadout
	{
		Weapon slots[slotCount] = {};
		int count = 0;
		int selected = 0;
		float cooldownLeft[slotCount] = {};
		int ammo[slotCount] = {};

		// The rest of a burst, which finishes even if the trigger is
		// released, from wherever the ship and the aim are by then.
		int pendingShots = 0;
		float pendingTimer = 0.f;
		int pendingSlot = 0;

		// The laser's charge, in seconds of beam, and how long since it was
		// last on (for the trickle back). Only a beam weapon uses it.
		float laserCharge = 0.f;
		float laserIdle = 0.f;
		bool laserWasFiring = false;
		Beam beam;

		float nextSide = 1.f;      // which wing the next missile leaves from: a salvo alternates

		// Locked down by a hit (B2): no weapon fires for this long, and then
		// each starts its cooldown.
		float lockedFor = 0.f;
	};

	// The player's four, refilled and ready.
	Loadout playersLoadout();

	// One of the four, as defined -- not as tuned in the panel, which tunes
	// the player's own loadout. The player carries all four; an enemy rolls
	// one (B1). Several weapons per ship, and modifiers on top of a base
	// weapon, are B2's.
	Weapon shipWeapon(int slot);

	// The ready weapon in `loadout` best suited to a target `distance` away
	// (B2): the one whose best range is nearest, a laser against a raised
	// shield, never the beam against one -- a shield holds a beam. Keeps the
	// one already selected unless another is clearly better, so a ship does
	// not flick between two. The selected slot if none is ready.
	int choose(const Loadout &loadout, float distance, bool targetShielded);

	// A lockdown hit (B2): nothing fires for `seconds`, and then every weapon
	// starts its cooldown. A burst in progress stops; the beam goes off.
	void lockdown(Loadout &loadout, float seconds);
	bool lockedDown(const Loadout &loadout);

	// The modifiers' shared numbers (B2): how long a stun lasts on the player
	// and on an enemy, how long a lockdown, and the grace after either, during
	// which another does not take.
	float stunSecondsOnPlayer();
	float stunSecondsOnEnemy();
	float lockdownSeconds();
	float effectGraceSeconds();

	// A short name for each of the four, for files: burst, heavy, missile,
	// laser. A level keeps which weapon its enemies carry by it.
	const char *shipWeaponKey(int slot);
	// The slot a key names, or -1 if it names none.
	int shipWeaponSlot(const char *key);

	// A loadout of these weapons, refilled and ready.
	Loadout loadoutOf(const Weapon *weapons, int count);

	// A new round: ammo refilled, cooldowns cleared, the laser full, a
	// lockdown lifted. The weapons themselves, and which is selected, are
	// kept: tuning and a choice, not something that happened.
	void reset(Loadout &loadout);

	// The player's keys 1-4 and the wheel. The wheel only switches without
	// Ctrl; with Ctrl it zooms (zoomControl).
	void handleInput(Loadout &loadout);

	// Where a shot starts, and who it is from. Every shot leaves with the
	// ship's velocity (muzzle speed is extra). A missile chases
	// `missileTarget`; noShip, it flies straight on its aim.
	struct FireContext
	{
		glm::vec2 origin = {};
		glm::vec2 aim = {};           // unit
		glm::vec2 shipVelocity = {};
		float shipSize = 0.f;
		ShipId shooter = playerShip;
		ShipId missileTarget = noShip;
	};

	// Advances cooldowns and any burst in progress, fires the selected weapon
	// if the trigger is held and it is ready, and appends the shots to `out`.
	// Returns how many shots left the ship this frame. Game time.
	int update(Loadout &loadout, float gameDeltaTime, bool triggerHeld, const FireContext &context,
		std::vector<Bullet> &out);

	// The enemy nearest `point` -- what the player's missiles lock onto, the
	// one nearest the mouse -- or noShip if there are none. `eligible`, when
	// given, rules some out: those the player cannot see (sight roadmap S2).
	ShipId nearestEnemy(glm::vec2 point, const std::vector<Enemy> &enemies,
		const std::function<bool(const Enemy &)> &eligible = nullptr);

	// Who a missile can chase: the enemies, and the player, when it can be
	// found -- not cloaked, and not wreckage or leaving.
	struct Targets
	{
		const std::vector<Enemy> *enemies = nullptr;
		glm::vec2 player = {};
		bool playerTargetable = false;
	};

	// Steers every missile in `bullets` (gameplay roadmap C3). Call before the
	// bullets move. A missile faces where it was aimed from the start and is
	// first pushed sideways from the wing with its motor off, keeping pace with
	// the ship. Then the motor lights from zero, the slide fades, and it
	// accelerates hard toward its target with a turn rate that keeps growing --
	// which is what makes it unable to miss: however it is moving, it
	// eventually turns tighter than any path that keeps missing. If its target
	// is gone -- dead, or the player cloaked -- it stops homing and flies on
	// straight.
	void steerMissiles(std::vector<Bullet> &bullets, const Targets &targets, float gameDeltaTime);

	// How lit a missile's exhaust is: 0 during the launch push, then rising
	// with its speed, so the pick-up is visible.
	float missileThrottle(const Bullet &bullet);

	Beam beam(const Loadout &loadout);

	// What the HUD needs to draw one slot.
	struct SlotView
	{
		BulletStyle style = BulletStyle::Standard;
		float ready = 1.f;   // 0 just fired .. 1 ready
		int ammo = -1;       // -1: unlimited
		int maxAmmo = -1;
		bool selected = false;
		bool usable = true;  // has ammo, and fires in this build
	};
	SlotView slot(const Loadout &loadout, int index);

	// The weapons in `loadout` (the player's), and the shared tuning.
	void debugUi(Loadout &loadout);
}
