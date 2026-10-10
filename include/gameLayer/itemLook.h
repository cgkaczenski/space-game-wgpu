#pragma once

// How the player's things look wherever they are shown (inventory roadmap
// I2-I4): the loadout menu, the hub's stash, the shop. One place for each
// kind's colour and name, so a heavy laser is the same purple everywhere.

#include <inventory.h>
#include <render/wgpu2d.h>
#include <string>

namespace itemLook
{
	glm::vec4 weaponColour(int kind);
	extern const glm::vec4 oreColour;
	extern const glm::vec4 unknownColour;   // a crate's weapon not yet looked at

	// "HEAVY LASER +STUN", in capitals for the pixel font.
	std::string describe(const inventory::Item &item);

	// A weapon in one square: its colour and its bullet's icon.
	void drawTile(wgpu2d::Renderer2D &renderer, glm::vec4 rect, const inventory::Item &item);
}
