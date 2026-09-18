#include <weapons.h>

#include "imgui.h"
#include "platformInput.h"

#include <algorithm>

namespace weapons
{

namespace
{
	struct Weapon
	{
		const char *name;
		BulletStyle style;
		float cooldown;    // seconds before it can fire again, from the first shot
		float damage;      // per shot; an enemy has 1 life
		float size;        // scales the sprite, the glow and the hitbox
		float speed;       // bullet speed, world units per second
		int burstCount;    // shots per trigger
		float burstGap;    // seconds between shots of a burst
		int maxAmmo;       // -1: unlimited
		bool firesYet;     // the laser's beam is C3
	};

	// Longer than first proposed, at the author's request: a cooldown is a
	// decision the player makes, and under a second it is only a rate of fire.
	Weapon weapons[slotCount] = {
		{"Burst laser", BulletStyle::Standard, 0.8f, 0.1f, 1.0f, 3000.f, 2, 0.08f, -1, true},
		{"Heavy laser", BulletStyle::Heavy,    2.0f, 0.3f, 1.5f, 2600.f, 1, 0.f,   -1, true},
		{"Missile",     BulletStyle::Missile,  3.0f, 0.5f, 1.2f, 2000.f, 1, 0.f,    5, true},
		{"Laser",       BulletStyle::Laser,    4.0f, 0.4f, 1.0f, 0.f,    1, 0.f,   -1, false},
	};

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
		return weapons[i].firesYet && (weapons[i].maxAmmo < 0 || ammo[i] > 0);
	}

	void select(int i)
	{
		if (i == selected) { return; }
		selected = i;
		pendingShots = 0; // a burst belongs to the weapon that started it
	}

	Bullet shot(const Weapon &w, glm::vec2 origin, glm::vec2 aim)
	{
		Bullet b;
		b.position = origin;
		b.fireDirection = aim;
		b.speed = w.speed;
		b.damage = w.damage;
		b.size = w.size;
		b.style = w.style;
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

int update(float gameDeltaTime, bool triggerHeld, glm::vec2 origin, glm::vec2 aim,
	std::vector<Bullet> &out)
{
	for (float &left : cooldownLeft) { left = std::max(0.f, left - gameDeltaTime); }

	int fired = 0;

	if (pendingShots > 0)
	{
		pendingTimer -= gameDeltaTime;
		while (pendingShots > 0 && pendingTimer <= 0.f)
		{
			out.push_back(shot(weapons[pendingSlot], origin, aim));
			fired++;
			pendingShots--;
			pendingTimer += weapons[pendingSlot].burstGap;
		}
	}

	const Weapon &w = weapons[selected];
	if (triggerHeld && pendingShots == 0 && cooldownLeft[selected] <= 0.f && usable(selected))
	{
		out.push_back(shot(w, origin, aim));
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

SlotView slot(int index)
{
	const Weapon &w = weapons[index];
	SlotView view;
	view.style = w.style;
	view.ready = w.cooldown > 0.f ? 1.f - cooldownLeft[index] / w.cooldown : 1.f;
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
		const bool open = ImGui::TreeNode("weapon", "%d  %s%s%s", i + 1, w.name,
			i == selected ? "  (selected)" : "", w.firesYet ? "" : "  -- beam is C3");
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
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
}

}
