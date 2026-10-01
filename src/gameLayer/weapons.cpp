#include <weapons.h>

#include "imgui.h"
#include "platformInput.h"

#include <algorithm>
#include <cmath>
#include <glm/geometric.hpp>

namespace weapons
{

namespace
{
	struct Weapon
	{
		const char *name;
		BulletStyle style;
		BulletMotion motion;
		float cooldown;    // seconds before it can fire again, from the first shot
		float damage;      // per shot; an enemy has 1 life
		float size;        // scales the sprite, the glow and the hitbox
		float speed;       // bullet speed, world units per second
		int burstCount;    // shots per trigger
		float burstGap;    // seconds between shots of a burst
		int maxAmmo;       // -1: unlimited
	};

	// Longer than first proposed, at the author's request: a cooldown is a
	// decision the player makes, and under a second it is only a rate of fire.
	Weapon weapons[slotCount] = {
		{"Burst laser", BulletStyle::Standard, BulletMotion::Straight, 0.8f, 0.1f, 1.0f, 3000.f, 2, 0.08f, -1},
		{"Heavy laser", BulletStyle::Heavy,    BulletMotion::Straight, 2.0f, 0.3f, 1.5f, 2600.f, 1, 0.f,   -1},
		{"Missile",     BulletStyle::Missile,  BulletMotion::Missile,  3.0f, 0.5f, 1.2f, 6000.f, 1, 0.f,    5},
		// The laser's damage is per second while the beam touches, not per shot.
		{"Laser",       BulletStyle::Laser,    BulletMotion::Straight, 4.0f, 0.4f, 1.0f, 0.f,    1, 0.f,   -1},
	};
	constexpr int missileSlot = 2;
	constexpr int laserSlot = 3;

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
	float laserCharge = 5.f;
	float laserIdleBeforeRegen = 1.f;   // seconds after firing stops
	float laserRegenSeconds = 8.f;      // to go from empty to full
	float laserIdle = 0.f;              // seconds since the beam was last on
	bool laserWasFiring = false;
	Beam currentBeam;

	// Missile flight (gameplay roadmap C3). The weapon's `speed` above is the
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

	// Alternates, so a salvo leaves from both sides.
	float nextSide = 1.f;

	// The enemy nearest `point`, or 0 if there are none.
	unsigned int nearestTo(glm::vec2 point, const std::vector<Enemy> *enemies)
	{
		if (!enemies) { return 0; }
		unsigned int best = 0;
		float bestDistance = 0.f;
		for (const Enemy &e : *enemies)
		{
			const float d = glm::distance(point, e.body.position);
			if (best == 0 || d < bestDistance) { best = e.id; bestDistance = d; }
		}
		return best;
	}

	const Enemy *findEnemy(unsigned int id, const std::vector<Enemy> &enemies)
	{
		if (id == 0) { return nullptr; }
		for (const Enemy &e : enemies) { if (e.id == id) { return &e; } }
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

	int selected = 0;
	float cooldownLeft[slotCount] = {};
	int ammo[slotCount] = {};

	// The rest of a burst, which finishes even if the trigger is released,
	// from wherever the ship and the aim are by then.
	int pendingShots = 0;
	float pendingTimer = 0.f;
	int pendingSlot = 0;

	// Wheel travel not yet turned into a switch. A mouse sends whole notches;
	// a trackpad sends fractions, which add up to one.
	float wheel = 0.f;

	bool usable(int i)
	{
		return weapons[i].maxAmmo < 0 || ammo[i] > 0;
	}

	void select(int i)
	{
		if (i == selected) { return; }
		selected = i;
		pendingShots = 0; // a burst belongs to the weapon that started it
	}

	Bullet shot(const Weapon &w, const FireContext &context)
	{
		Bullet b;
		b.position = context.origin;
		b.fireDirection = context.aim;
		b.speed = w.speed;
		b.damage = w.damage;
		b.size = w.size;
		b.style = w.style;
		b.motion = w.motion;
		// The weapon's speed is muzzle velocity: how fast it leaves the gun,
		// not how fast it goes through the world. Without the ship's own
		// velocity a forward shot sits still relative to a ship that has
		// caught up, and raising top speed past the weapon lets you outrun it.
		b.drift = context.shipVelocity;

		if (w.motion == BulletMotion::Missile)
		{
			// Facing where the player aimed from the first frame, motor off,
			// sliding sideways: the ship's own velocity, so it keeps pace, plus
			// a push out from the wing it left. Sideways is relative to the aim,
			// so the salvo fans out either side of the line of fire.
			const glm::vec2 side = glm::vec2(-context.aim.y, context.aim.x) * nextSide;
			nextSide = -nextSide;
			b.position += side * (context.shipSize * launchSideFraction);

			b.fireDirection = context.aim;
			b.speed = 0.f;
			b.drift += side * launchPush;

			b.aimDirection = context.aim;
			b.targetId = nearestTo(context.mouseWorld, context.enemies);
		}
		return b;
	}
}

void reset()
{
	for (int i = 0; i < slotCount; i++)
	{
		cooldownLeft[i] = 0.f;
		ammo[i] = weapons[i].maxAmmo;
	}
	pendingShots = 0;
	laserCharge = laserChargeSeconds;
	laserIdle = 0.f;
	laserWasFiring = false;
	currentBeam = {};
}

void handleInput()
{
	const ImGuiIO &io = ImGui::GetIO();

	if (!io.WantCaptureKeyboard)
	{
		for (int i = 0; i < slotCount; i++)
		{
			if (platform::isButtonPressedOn(platform::Button::NR1 + i)) { select(i); }
		}
	}

	if (!io.WantCaptureMouse && !platform::isButtonHeld(platform::Button::Shift))
	{
		wheel += platform::getScrollY();
		// Wheel up goes back a slot, down goes forward, and both wrap.
		while (wheel >= 1.f) { select((selected + slotCount - 1) % slotCount); wheel -= 1.f; }
		while (wheel <= -1.f) { select((selected + 1) % slotCount); wheel += 1.f; }
	}
}

int update(float gameDeltaTime, bool triggerHeld, const FireContext &context,
	std::vector<Bullet> &out)
{
	for (float &left : cooldownLeft) { left = std::max(0.f, left - gameDeltaTime); }

	// An emptied laser comes back full when its cooldown ends.
	if (laserCharge <= 0.f && cooldownLeft[laserSlot] <= 0.f) { laserCharge = laserChargeSeconds; }

	// Otherwise it trickles back once the beam has been off a moment. Not
	// during the cooldown: that is the emptied laser's punishment, and the
	// refill at its end is what ends it.
	laserIdle += gameDeltaTime;
	if (laserIdle >= laserIdleBeforeRegen && cooldownLeft[laserSlot] <= 0.f
		&& laserCharge > 0.f && laserCharge < laserChargeSeconds)
	{
		const float perSecond = laserRegenSeconds > 0.f
			? laserChargeSeconds / laserRegenSeconds : laserChargeSeconds;
		laserCharge = std::min(laserChargeSeconds, laserCharge + perSecond * gameDeltaTime);
	}

	currentBeam = {};
	int fired = 0;

	if (pendingShots > 0)
	{
		pendingTimer -= gameDeltaTime;
		while (pendingShots > 0 && pendingTimer <= 0.f)
		{
			out.push_back(shot(weapons[pendingSlot], context));
			fired++;
			pendingShots--;
			pendingTimer += weapons[pendingSlot].burstGap;
		}
	}

	const Weapon &w = weapons[selected];

	if (selected == laserSlot)
	{
		// Not a bullet: a beam for as long as the trigger is held and there is
		// charge. Released, the charge stays where it is.
		const bool firing = triggerHeld && cooldownLeft[laserSlot] <= 0.f && laserCharge > 0.f;
		if (firing)
		{
			laserIdle = 0.f;
			laserCharge -= gameDeltaTime;
			currentBeam.firing = true;
			currentBeam.started = !laserWasFiring;
			currentBeam.direction = context.aim;
			currentBeam.origin = context.origin + context.aim * (context.shipSize * 0.4f);
			currentBeam.damagePerSecond = w.damage;

			if (laserCharge <= 0.f)
			{
				laserCharge = 0.f;
				cooldownLeft[laserSlot] = w.cooldown;
			}
		}
		laserWasFiring = firing;
		return fired;
	}
	laserWasFiring = false;

	if (triggerHeld && pendingShots == 0 && cooldownLeft[selected] <= 0.f && usable(selected))
	{
		out.push_back(shot(w, context));
		fired++;

		if (w.maxAmmo >= 0) { ammo[selected]--; }
		cooldownLeft[selected] = w.cooldown;

		if (w.burstCount > 1)
		{
			pendingShots = w.burstCount - 1;
			pendingTimer = w.burstGap;
			pendingSlot = selected;
		}
	}

	return fired;
}

void steerMissiles(std::vector<Bullet> &bullets, const std::vector<Enemy> &enemies,
	float gameDeltaTime)
{
	for (Bullet &b : bullets)
	{
		if (b.motion != BulletMotion::Missile) { continue; }

		b.age += gameDeltaTime;
		if (b.age < launchSeconds) { continue; } // still sliding out, motor off

		// Lit: the sideways slide and the ship's velocity fade as the motor
		// takes over. Exact exponential, so it fades the same at any frame rate.
		b.drift *= std::exp(-driftFade * gameDeltaTime);

		// The target, if it is still there. Gone -- killed, despawned, or later
		// cloaked -- the missile keeps the heading it has and homes no more.
		glm::vec2 wanted = b.aimDirection;
		if (b.targetId != 0)
		{
			if (const Enemy *target = findEnemy(b.targetId, enemies))
			{
				const glm::vec2 toTarget = target->body.position - b.position;
				const float distance = glm::length(toTarget);
				if (distance > 0.001f) { wanted = toTarget / distance; }
			}
			else
			{
				b.targetId = 0;
				b.aimDirection = b.fireDirection;
				wanted = b.aimDirection;
			}
		}

		const float chase = b.age - launchSeconds;
		const float turnRate = turnRateStart + turnRateGrowth * chase;
		b.fireDirection = turnToward(b.fireDirection, wanted, turnRate * gameDeltaTime);

		const Weapon &missile = weapons[missileSlot];
		b.speed = std::min(missile.speed, b.speed + missileAccel * gameDeltaTime);
	}
}

float missileThrottle(const Bullet &bullet)
{
	if (bullet.motion != BulletMotion::Missile || bullet.age < launchSeconds) { return 0.f; }
	// A little lit the moment it ignites, full at top speed.
	const float topSpeed = weapons[missileSlot].speed;
	return topSpeed > 0.f ? 0.25f + 0.75f * std::min(1.f, bullet.speed / topSpeed) : 1.f;
}

Beam beam() { return currentBeam; }

SlotView slot(int index)
{
	const Weapon &w = weapons[index];
	SlotView view;
	view.style = w.style;
	view.ready = w.cooldown > 0.f ? 1.f - cooldownLeft[index] / w.cooldown : 1.f;
	if (index == laserSlot && cooldownLeft[index] <= 0.f)
	{
		// Between cooldowns the slot shows the charge left, so the shade
		// grows as the beam drains and stays put when it is released.
		view.ready = laserChargeSeconds > 0.f ? laserCharge / laserChargeSeconds : 1.f;
	}
	view.ammo = w.maxAmmo < 0 ? -1 : ammo[index];
	view.maxAmmo = w.maxAmmo;
	view.selected = index == selected;
	view.usable = usable(index);
	return view;
}

void debugUi()
{
	for (int i = 0; i < slotCount; i++)
	{
		Weapon &w = weapons[i];
		ImGui::PushID(i);
		const bool open = ImGui::TreeNode("weapon", "%d  %s%s", i + 1, w.name,
			i == selected ? "  (selected)" : "");
		if (open)
		{
			ImGui::SliderFloat("Cooldown", &w.cooldown, 0.1f, 10.f, "%.2f s");
			ImGui::SliderFloat("Damage", &w.damage, 0.01f, 2.f, "%.2f");
			if (w.speed > 0.f) { ImGui::SliderFloat("Speed", &w.speed, 500.f, 6000.f, "%.0f"); }
			if (w.burstCount > 1)
			{
				ImGui::SliderFloat("Burst gap", &w.burstGap, 0.01f, std::max(0.02f, w.cooldown), "%.2f s");
			}
			if (w.maxAmmo >= 0)
			{
				ImGui::Text("Ammo %d / %d", ammo[i], w.maxAmmo);
				ImGui::SameLine();
				if (ImGui::SmallButton("Refill")) { ammo[i] = w.maxAmmo; }
			}
			if (i == laserSlot)
			{
				ImGui::SliderFloat("Charge", &laserChargeSeconds, 0.5f, 15.f, "%.1f s");
				ImGui::SliderFloat("Regen wait", &laserIdleBeforeRegen, 0.f, 5.f, "%.1f s");
				ImGui::SliderFloat("Regen full in", &laserRegenSeconds, 0.5f, 30.f, "%.1f s");
				ImGui::Text("Left %.1f s (idle %.1f s)", laserCharge, laserIdle);
			}
			if (w.motion == BulletMotion::Missile)
			{
				ImGui::SliderFloat("Launch", &launchSeconds, 0.f, 1.5f, "%.2f s");
				ImGui::SliderFloat("Side push", &launchPush, 0.f, 3000.f, "%.0f");
				ImGui::SliderFloat("Drift fade", &driftFade, 0.f, 20.f, "%.1f /s");
				ImGui::SliderFloat("Acceleration", &missileAccel, 500.f, 50000.f, "%.0f",
					ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Turn rate", &turnRateStart, 0.5f, 12.f, "%.1f rad/s");
				ImGui::SliderFloat("Turn growth", &turnRateGrowth, 0.f, 30.f, "%.1f rad/s per s");
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

}
