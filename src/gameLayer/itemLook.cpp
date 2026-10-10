#include <itemLook.h>
#include <bulletLook.h>
#include <weapons.h>

#include <cctype>

namespace itemLook
{

namespace
{
	const glm::vec4 colours[weapons::slotCount] = {
		{0.20f, 0.30f, 0.55f, 1.f},   // burst
		{0.50f, 0.25f, 0.45f, 1.f},   // heavy
		{0.55f, 0.35f, 0.15f, 1.f},   // missile
		{0.15f, 0.45f, 0.45f, 1.f},   // beam
	};
}

const glm::vec4 oreColour = {0.85f, 0.55f, 0.18f, 1.f};
const glm::vec4 unknownColour = {0.32f, 0.33f, 0.36f, 1.f};

glm::vec4 weaponColour(int kind)
{
	return kind >= 0 && kind < weapons::slotCount ? colours[kind] : unknownColour;
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

void drawTile(wgpu2d::Renderer2D &r, glm::vec4 rect, const inventory::Item &it)
{
	r.renderRectangle(rect, weaponColour(it.kind));
	bulletLook::drawIcon(r, {rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f}, rect.w * 0.85f,
		weapons::shipWeapon(it.kind).style);
}

}
