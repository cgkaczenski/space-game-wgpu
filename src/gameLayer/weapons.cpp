#include <weapons.h>
#include <tuning.h>

#include <enemy.h>
#include "imgui.h"
#include "platformInput.h"
#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>
#include <glm/trigonometric.hpp>
#include <string>

namespace weapons
{

namespace
{
	// The four, shared: the player carries all of them, an enemy rolls one.
	// Longer cooldowns than first proposed, at the author's request: a
	// cooldown is a decision the player makes, and under a second it is only
	// a rate of fire.
	const Weapon shipWeapons[slotCount] = {
		// The last number is each one's best range (B2): the burst laser up
		// close, the heavy laser further, missiles from afar, the beam within
		// an enemy's reach of it.
		{"Burst laser", BulletStyle::Standard, BulletMotion::Straight, 0.8f, 0.1f, 1.0f, 3000.f, 2, 0.08f, -1, false, 1600.f},
		{"Heavy laser", BulletStyle::Heavy,    BulletMotion::Straight, 2.0f, 0.3f, 1.5f, 2600.f, 1, 0.f,   -1, false, 2200.f},
		{"Missile",     BulletStyle::Missile,  BulletMotion::Missile,  3.0f, 0.5f, 1.2f, 6000.f, 1, 0.f,    5, false, 3500.f},
		// The laser's damage is per second while the beam touches, not per shot.
		{"Laser",       BulletStyle::Laser,    BulletMotion::Straight, 4.0f, 0.4f, 1.0f, 0.f,    1, 0.f,   -1, true,  2500.f},
	};

	// The laser's charge, in seconds of beam (gameplay roadmap C3b). It drains
	// while the beam is on; only an empty charge starts the cooldown, and the
	// cooldown's end refills it.
	//
	// It also trickles back on its own after a moment's rest (L3). Keeping
	// what was left and never refilling it was the original rule, and it only
	// worked while the beam was a weapon: once it also mines, a run of short
	// burns leaves a laser that can only be restored by wasting the rest of
	// it. The idle wait is what keeps the rule honest -- holding the trigger
	// never regains anything, so emptying it is still the mistake.
	float laserChargeSeconds = 5.f;
	float laserIdleBeforeRegen = 1.f;   // seconds after firing stops
	float laserRegenSeconds = 8.f;      // to go from empty to full

	// Missile flight (gameplay roadmap C3). The weapon's `speed` is the
	// motor's top speed. The launch is a sideways push with the motor off; the
	// motor then lights from zero and accelerates hard, which is what makes
	// the missile visibly pick up steam rather than just fly.
	float launchSeconds = 0.3f;      // pushed out from the wing, motor off
	float launchPush = 700.f;        // sideways speed of that push, world units/s
	float driftFade = 4.f;           // per second: how fast the push and the
	                                 // ship's inherited velocity fade once lit
	float missileAccel = 14000.f;    // world units per second squared, once lit
	float turnRateStart = 3.f;       // radians per second as the chase begins
	float turnRateGrowth = 6.f;      // added per second of chase: the no-miss rule
	float launchSideFraction = 0.45f; // of the ship's size, out to the wing

	// Modifiers (B2).
	float spreadDegrees = 15.f;       // between a spread weapon's shots
	float modifierStunPlayer = 0.5f;  // seconds: the player, as an enemy's ram stuns it
	float modifierStunEnemy = 2.f;    // seconds: an enemy, as the player's ram stuns it
	float lockdownFor = 2.f;          // seconds with no weapon, before the cooldowns
	float effectGrace = 1.f;          // seconds after a stun or lockdown when another does not take

	// Wheel travel not yet turned into a switch. A mouse sends whole notches;
	// a trackpad sends fractions, which add up to one. The player's alone:
	// only the player has a wheel.
	float wheel = 0.f;

	const Enemy *findEnemy(ShipId id, const std::vector<Enemy> &enemies)
	{
		if (id == noShip || id == playerShip) { return nullptr; }
		// A cloaked enemy cannot be found: the lock is lost, as on the player.
		for (const Enemy &e : enemies) { if (e.id == id) { return energy::isCloaked(e.energy) ? nullptr : &e; } }
		return nullptr;
	}

	// Turns unit vector `from` toward unit vector `to` by at most `maxRadians`.
	glm::vec2 turnToward(glm::vec2 from, glm::vec2 to, float maxRadians)
	{
		const float cross = from.x * to.y - from.y * to.x;
		const float dot = from.x * to.x + from.y * to.y;
		const float angle = std::atan2(cross, dot); // signed, -pi..pi
		const float step = std::clamp(angle, -maxRadians, maxRadians);
		const float c = std::cos(step);
		const float s = std::sin(step);
		return glm::normalize(glm::vec2(from.x * c - from.y * s, from.x * s + from.y * c));
	}

	// Which slot holds the beam, or -1.
	int beamSlot(const Loadout &l)
	{
		for (int i = 0; i < l.count; i++) { if (l.slots[i].beam) { return i; } }
		return -1;
	}

	bool usable(const Loadout &l, int i)
	{
		return l.slots[i].maxAmmo < 0 || l.ammo[i] > 0;
	}

	void select(Loadout &l, int i)
	{
		if (i == l.selected || i < 0 || i >= l.count) { return; }
		l.selected = i;
		l.pendingShots = 0; // a burst belongs to the weapon that started it
	}

	Bullet shot(Loadout &l, const Weapon &w, const FireContext &context)
	{
		Bullet b;
		b.position = context.origin;
		b.fireDirection = context.aim;
		b.speed = w.speed;
		b.damage = w.damage;
		b.size = w.size;
		b.style = w.style;
		b.motion = w.motion;
		b.shooter = context.shooter;
		// The weapon's speed is muzzle velocity: how fast it leaves the gun,
		// not how fast it goes through the world. Without the ship's own
		// velocity a forward shot sits still relative to a ship that has
		// caught up, and raising top speed past the weapon lets you outrun it.
		b.drift = context.shipVelocity;

		if (w.motion == BulletMotion::Missile)
		{
			// Facing where its ship aimed from the first frame, motor off,
			// sliding sideways: the ship's own velocity, so it keeps pace, plus
			// a push out from the wing it left. Sideways is relative to the aim,
			// so the salvo fans out either side of the line of fire.
			const glm::vec2 side = glm::vec2(-context.aim.y, context.aim.x) * l.nextSide;
			l.nextSide = -l.nextSide;
			b.position += side * (context.shipSize * launchSideFraction);

			b.fireDirection = context.aim;
			b.speed = 0.f;
			b.topSpeed = w.speed;
			b.drift += side * launchPush;

			b.aimDirection = context.aim;
			b.target = context.missileTarget;
		}
		b.stun = w.stun;
		b.lockdown = w.lockdown;
		return b;
	}

	// A weapon's shot, and its spread (B2): the extra shots fanned either
	// side of the aim, one more each side per pair, `spreadDegrees` apart.
	int fire(Loadout &l, const Weapon &w, const FireContext &context, std::vector<Bullet> &out)
	{
		out.push_back(shot(l, w, context));
		int fired = 1;
		for (int k = 1; k <= w.spread; k++)
		{
			const float side = (k % 2) ? 1.f : -1.f;
			const float turn = glm::radians(spreadDegrees) * (float)((k + 1) / 2) * side;
			FireContext turned = context;
			const float c = std::cos(turn), s = std::sin(turn);
			turned.aim = {context.aim.x * c - context.aim.y * s, context.aim.x * s + context.aim.y * c};
			out.push_back(shot(l, w, turned));
			fired++;
		}
		return fired;
	}

	// How well a weapon suits a target `distance` away: lower is better.
	float unsuited(const Weapon &w, float distance, bool targetShielded)
	{
		float score = std::fabs(distance - w.bestRange) / std::max(w.bestRange, 1.f);
		if (targetShielded && w.beam) { score += 10.f; }               // a shield holds a beam
		if (targetShielded && !w.beam && w.motion == BulletMotion::Straight) { score *= 0.5f; } // lasers break shields
		if (w.motion == BulletMotion::Missile && distance < 800.f) { score += 1.f; } // too close to turn
		return score;
	}
}

Loadout loadoutOf(const Weapon *weapons, int count)
{
	Loadout l;
	l.count = std::clamp(count, 0, slotCount);
	for (int i = 0; i < l.count; i++) { l.slots[i] = weapons[i]; }
	reset(l);
	return l;
}

Loadout playersLoadout() { return loadoutOf(shipWeapons, slotCount); }

int choose(const Loadout &l, float distance, bool targetShielded)
{
	auto ready = [&](int i)
	{
		const Weapon &w = l.slots[i];
		return l.cooldownLeft[i] <= 0.f && usable(l, i) && (!w.beam || l.laserCharge > 0.f);
	};
	int best = -1;
	float bestScore = 0.f;
	for (int i = 0; i < l.count; i++)
	{
		if (!ready(i)) { continue; }
		const float score = unsuited(l.slots[i], distance, targetShielded);
		if (best < 0 || score < bestScore) { best = i; bestScore = score; }
	}
	if (best < 0) { return l.selected; }
	// Keep the current one if it is ready and nearly as good.
	if (best != l.selected && l.selected < l.count && ready(l.selected)
		&& unsuited(l.slots[l.selected], distance, targetShielded) <= bestScore + 0.2f)
	{
		return l.selected;
	}
	return best;
}

void lockdown(Loadout &l, float seconds)
{
	l.lockedFor = std::max(l.lockedFor, seconds);
	l.pendingShots = 0;
	l.beam = {};
}

bool lockedDown(const Loadout &l) { return l.lockedFor > 0.f; }

float stunSecondsOnPlayer() { return modifierStunPlayer; }
float stunSecondsOnEnemy() { return modifierStunEnemy; }
float lockdownSeconds() { return lockdownFor; }
float effectGraceSeconds() { return effectGrace; }

Weapon shipWeapon(int slot)
{
	return shipWeapons[std::clamp(slot, 0, slotCount - 1)];
}

namespace
{
	// In slot order, beside the table above.
	const char *const shipWeaponKeys[slotCount] = {"burst", "heavy", "missile", "laser"};
}

const char *shipWeaponKey(int slot)
{
	return shipWeaponKeys[std::clamp(slot, 0, slotCount - 1)];
}

int shipWeaponSlot(const char *key)
{
	for (int i = 0; i < slotCount; i++) { if (std::string(key) == shipWeaponKeys[i]) { return i; } }
	return -1;
}

void reset(Loadout &l)
{
	for (int i = 0; i < slotCount; i++)
	{
		l.cooldownLeft[i] = 0.f;
		l.ammo[i] = l.slots[i].maxAmmo;
	}
	l.pendingShots = 0;
	l.laserCharge = laserChargeSeconds;
	l.laserIdle = 0.f;
	l.laserWasFiring = false;
	l.beam = {};
}

void handleInput(Loadout &l)
{
	const ImGuiIO &io = ImGui::GetIO();

	if (!io.WantCaptureKeyboard)
	{
		for (int i = 0; i < l.count; i++)
		{
			if (platform::isButtonPressedOn(platform::Button::NR1 + i)) { select(l, i); }
		}
	}

	if (!io.WantCaptureMouse && !platform::isButtonHeld(platform::Button::Shift) && l.count > 0)
	{
		wheel += platform::getScrollY();
		// Wheel up goes back a slot, down goes forward, and both wrap.
		while (wheel >= 1.f) { select(l, (l.selected + l.count - 1) % l.count); wheel -= 1.f; }
		while (wheel <= -1.f) { select(l, (l.selected + 1) % l.count); wheel += 1.f; }
	}
}

int update(Loadout &l, float gameDeltaTime, bool triggerHeld, const FireContext &context,
	std::vector<Bullet> &out)
{
	for (float &left : l.cooldownLeft) { left = std::max(0.f, left - gameDeltaTime); }

	const int laser = beamSlot(l);
	if (laser >= 0)
	{
		// An emptied laser comes back full when its cooldown ends.
		if (l.laserCharge <= 0.f && l.cooldownLeft[laser] <= 0.f) { l.laserCharge = laserChargeSeconds; }

		// Otherwise it trickles back once the beam has been off a moment. Not
		// during the cooldown: that is the emptied laser's punishment, and the
		// refill at its end is what ends it.
		l.laserIdle += gameDeltaTime;
		if (l.laserIdle >= laserIdleBeforeRegen && l.cooldownLeft[laser] <= 0.f
			&& l.laserCharge > 0.f && l.laserCharge < laserChargeSeconds)
		{
			const float perSecond = laserRegenSeconds > 0.f
				? laserChargeSeconds / laserRegenSeconds : laserChargeSeconds;
			l.laserCharge = std::min(laserChargeSeconds, l.laserCharge + perSecond * gameDeltaTime);
		}
	}

	l.beam = {};
	int fired = 0;
	if (l.count <= 0) { return 0; }

	// Locked down (B2): nothing fires; when it ends, every weapon starts its
	// cooldown, so the lockdown costs a full cycle and not only its 2 s.
	if (l.lockedFor > 0.f)
	{
		l.lockedFor -= gameDeltaTime;
		if (l.lockedFor <= 0.f)
		{
			l.lockedFor = 0.f;
			for (int i = 0; i < l.count; i++) { l.cooldownLeft[i] = l.slots[i].cooldown; }
		}
		l.laserWasFiring = false;
		return 0;
	}

	if (l.pendingShots > 0)
	{
		l.pendingTimer -= gameDeltaTime;
		while (l.pendingShots > 0 && l.pendingTimer <= 0.f)
		{
			fired += fire(l, l.slots[l.pendingSlot], context, out);
			l.pendingShots--;
			l.pendingTimer += l.slots[l.pendingSlot].burstGap;
		}
	}

	const Weapon &w = l.slots[l.selected];

	if (w.beam)
	{
		// Not a bullet: a beam for as long as the trigger is held and there is
		// charge. Released, the charge stays where it is.
		const bool firing = triggerHeld && l.cooldownLeft[l.selected] <= 0.f && l.laserCharge > 0.f;
		if (firing)
		{
			l.laserIdle = 0.f;
			l.laserCharge -= gameDeltaTime;
			l.beam.firing = true;
			l.beam.started = !l.laserWasFiring;
			l.beam.direction = context.aim;
			l.beam.origin = context.origin + context.aim * (context.shipSize * 0.4f);
			l.beam.damagePerSecond = w.damage;
			l.beam.stun = w.stun;
			l.beam.lockdown = w.lockdown;

			if (l.laserCharge <= 0.f)
			{
				l.laserCharge = 0.f;
				l.cooldownLeft[l.selected] = w.cooldown;
			}
		}
		l.laserWasFiring = firing;
		return fired;
	}
	l.laserWasFiring = false;

	if (triggerHeld && l.pendingShots == 0 && l.cooldownLeft[l.selected] <= 0.f && usable(l, l.selected))
	{
		fired += fire(l, w, context, out);

		if (w.maxAmmo >= 0) { l.ammo[l.selected]--; }
		l.cooldownLeft[l.selected] = w.cooldown;

		if (w.burstCount > 1)
		{
			l.pendingShots = w.burstCount - 1;
			l.pendingTimer = w.burstGap;
			l.pendingSlot = l.selected;
		}
	}

	return fired;
}

ShipId nearestEnemy(glm::vec2 point, const std::vector<Enemy> &enemies)
{
	ShipId best = noShip;
	float bestDistance = 0.f;
	for (const Enemy &e : enemies)
	{
		if (energy::isCloaked(e.energy)) { continue; } // nothing to lock onto
		const float d = glm::distance(point, e.body.position);
		if (best == noShip || d < bestDistance) { best = e.id; bestDistance = d; }
	}
	return best;
}

void steerMissiles(std::vector<Bullet> &bullets, const Targets &targets, float gameDeltaTime)
{
	for (Bullet &b : bullets)
	{
		if (b.motion != BulletMotion::Missile) { continue; }

		b.age += gameDeltaTime;
		if (b.age < launchSeconds) { continue; } // still sliding out, motor off

		// Lit: the sideways slide and the ship's velocity fade as the motor
		// takes over. Exact exponential, so it fades the same at any frame rate.
		b.drift *= std::exp(-driftFade * gameDeltaTime);

		// The target, if it can still be found: an enemy still alive, or the
		// player, neither cloaked nor gone. Lost, the missile keeps the
		// heading it has and homes no more -- the lock does not come back.
		glm::vec2 wanted = b.aimDirection;
		if (b.target != noShip)
		{
			bool found = false;
			glm::vec2 at = {};
			if (b.target == playerShip)
			{
				found = targets.playerTargetable;
				at = targets.player;
			}
			else if (targets.enemies)
			{
				if (const Enemy *e = findEnemy(b.target, *targets.enemies)) { found = true; at = e->body.position; }
			}

			if (found)
			{
				const glm::vec2 toTarget = at - b.position;
				const float distance = glm::length(toTarget);
				if (distance > 0.001f) { wanted = toTarget / distance; }
			}
			else
			{
				b.target = noShip;
				b.aimDirection = b.fireDirection;
				wanted = b.aimDirection;
			}
		}

		const float chase = b.age - launchSeconds;
		const float turnRate = turnRateStart + turnRateGrowth * chase;
		b.fireDirection = turnToward(b.fireDirection, wanted, turnRate * gameDeltaTime);
		b.speed = std::min(b.topSpeed, b.speed + missileAccel * gameDeltaTime);
	}
}

float missileThrottle(const Bullet &bullet)
{
	if (bullet.motion != BulletMotion::Missile || bullet.age < launchSeconds) { return 0.f; }
	// A little lit the moment it ignites, full at top speed.
	return bullet.topSpeed > 0.f ? 0.25f + 0.75f * std::min(1.f, bullet.speed / bullet.topSpeed) : 1.f;
}

Beam beam(const Loadout &l) { return l.beam; }

SlotView slot(const Loadout &l, int index)
{
	SlotView view;
	if (index < 0 || index >= l.count) { view.usable = false; return view; }
	const Weapon &w = l.slots[index];
	view.style = w.style;
	view.ready = l.lockedFor > 0.f ? 0.f : w.cooldown > 0.f ? 1.f - l.cooldownLeft[index] / w.cooldown : 1.f;
	view.ammo = w.maxAmmo < 0 ? -1 : l.ammo[index];
	view.maxAmmo = w.maxAmmo;
	view.selected = index == l.selected;
	view.usable = usable(l, index) && l.lockedFor <= 0.f; // locked down: dimmed
	return view;
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("weapons", {
	{"modifier.spreadDegrees", spreadDegrees},
	{"modifier.stunOnPlayer", modifierStunPlayer},
	{"modifier.stunOnEnemy", modifierStunEnemy},
	{"modifier.lockdown", lockdownFor},
	{"modifier.grace", effectGrace},
	{"laserChargeSeconds", laserChargeSeconds},
	{"laserIdleBeforeRegen", laserIdleBeforeRegen},
	{"laserRegenSeconds", laserRegenSeconds},
	{"launchSeconds", launchSeconds},
	{"launchPush", launchPush},
	{"driftFade", driftFade},
	{"missileAccel", missileAccel},
	{"turnRateStart", turnRateStart},
	{"turnRateGrowth", turnRateGrowth},
});

void debugUi(Loadout &l)
{
	if (l.lockedFor > 0.f) { ImGui::TextColored({1.f, 0.5f, 0.9f, 1.f}, "Locked down: %.1f s", l.lockedFor); }
	if (ImGui::TreeNode("Modifiers (B2)"))
	{
		tune::SliderFloat("Spread angle", &spreadDegrees, 1.f, 45.f, "%.0f deg");
		tune::SliderFloat("Stun on player", &modifierStunPlayer, 0.f, 3.f, "%.2f s");
		tune::SliderFloat("Stun on enemy", &modifierStunEnemy, 0.f, 6.f, "%.2f s");
		tune::SliderFloat("Lockdown", &lockdownFor, 0.f, 6.f, "%.1f s, then the cooldowns");
		tune::SliderFloat("Grace", &effectGrace, 0.f, 5.f, "%.1f s before another takes");
		ImGui::TreePop();
	}
	for (int i = 0; i < l.count; i++)
	{
		Weapon &w = l.slots[i];
		ImGui::PushID(i);
		const bool open = ImGui::TreeNode("weapon", "%d  %s%s", i + 1, w.name,
			i == l.selected ? "  (selected)" : "");
		if (open)
		{
			tune::SliderFloat("Cooldown", &w.cooldown, 0.1f, 10.f, "%.2f s");
			tune::SliderFloat("Damage", &w.damage, 0.01f, 2.f, "%.2f");
			if (w.speed > 0.f) { tune::SliderFloat("Speed", &w.speed, 500.f, 6000.f, "%.0f"); }
			if (w.burstCount > 1)
			{
				tune::SliderFloat("Burst gap", &w.burstGap, 0.01f, std::max(0.02f, w.cooldown), "%.2f s");
			}
			if (w.maxAmmo >= 0)
			{
				ImGui::Text("Ammo %d / %d", l.ammo[i], w.maxAmmo);
				ImGui::SameLine();
				if (ImGui::SmallButton("Refill")) { l.ammo[i] = w.maxAmmo; }
			}
			if (w.beam)
			{
				tune::SliderFloat("Charge", &laserChargeSeconds, 0.5f, 15.f, "%.1f s");
				tune::SliderFloat("Regen wait", &laserIdleBeforeRegen, 0.f, 5.f, "%.1f s");
				tune::SliderFloat("Regen full in", &laserRegenSeconds, 0.5f, 30.f, "%.1f s");
				ImGui::Text("Left %.1f s (idle %.1f s)", l.laserCharge, l.laserIdle);
			}
			if (w.motion == BulletMotion::Missile)
			{
				tune::SliderFloat("Launch", &launchSeconds, 0.f, 1.5f, "%.2f s");
				tune::SliderFloat("Side push", &launchPush, 0.f, 3000.f, "%.0f");
				tune::SliderFloat("Drift fade", &driftFade, 0.f, 20.f, "%.1f /s");
				tune::SliderFloat("Acceleration", &missileAccel, 500.f, 50000.f, "%.0f",
					ImGuiSliderFlags_Logarithmic);
				tune::SliderFloat("Turn rate", &turnRateStart, 0.5f, 12.f, "%.1f rad/s");
				tune::SliderFloat("Turn growth", &turnRateGrowth, 0.f, 30.f, "%.1f rad/s per s");
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

}
