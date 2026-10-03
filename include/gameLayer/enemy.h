#pragma once

// What an enemy is: plain data. What it does is enemyAi, and how it looks is
// renderSpaceShip in shipSprite.h (roadmap R9). Nothing here includes the
// renderer, so a behaviour can be read, swapped or tested without it.

#include <glm/vec2.hpp>
#include <engine/movement.h>
#include <shipHitbox.h>
#include <weapons.h>
#include <energy.h>
#include <ram.h>

constexpr float enemyShipSize = 250.f;

// Whether an enemy has an ability (B1): a level's placement can say yes or no,
// or leave it to the ability's chance.
enum class AbilityChoice { No, Yes, Random };

// One weapon slot as a placement chooses it (B2): which of the four -- or -1,
// rolled -- and each modifier.
struct GunChoice
{
	int weapon = -1;
	AbilityChoice stun = AbilityChoice::No;
	AbilityChoice lockdown = AbilityChoice::No;
	AbilityChoice spread = AbilityChoice::No;
};

struct Enemy
{
	// Stable for the enemy's life, never reused. The enemy list is erased from
	// as enemies die or leave, so an index is not an identity; a missile keeps
	// its target by this.
	unsigned int id = 0;

	glm::uvec2 type = {}; //used to index into the texture atlas

	// Which function enemyAi calls. Same data, same draw; the policy is not a
	// subclass. CloseIn flies at the player. KeepDistance faces them and holds
	// a range -- movement and facing are not the same vector. Boss (B2) holds
	// a middle range with the player's flight, every ability and several
	// weapons, and fights in phases as its life falls.
	enum class Behaviour { CloseIn, KeepDistance, Boss };
	Behaviour behaviour = Behaviour::CloseIn;

	// How big it is, in world units: its art, its hitbox, its plume, its
	// shield. A boss is larger (B2).
	float size = enemyShipSize;
	// A boss's phase (B2): 1 above two thirds of its life, 2 below, 3 below a
	// third. Ordinary enemies stay 1.
	int phase = 1;
	float lifeFull = 1.f;         // what `life` starts at: a boss has more

	// Its body: position, velocity, facing (where it looks, and where its
	// sight cone points), how it moves and how fast it turns -- the same kind
	// of body as the player's, moved by the same movement::step (gameplay
	// roadmap P1). enemyAi only decides the intent. Its numbers are rolled per
	// class at spawn.
	movement::Body body;
	float plume = 0.f;            // the engine's glow, eased toward its thrust

	// Its weapons: the same kind of loadout as the player's, fired through the
	// same weapons::update (gameplay roadmap B1) -- one gun, rolled per class
	// at spawn. enemyAi only decides the trigger. `fireRange` is how lined up
	// it must be first: the AI's, not the gun's -- |toPlayer + facing|, 2
	// dead ahead, 2 cos(angle / 2) off it, so 1.5 is about 83 degrees either side.
	weapons::Loadout loadout;
	float fireRange = 1.5;

	// Its energy (B1): the same rules as the player's. Every enemy has a bar;
	// whether a full one raises a shield, or can be spent on a cloak, is a
	// placement's choice or rolled at spawn, and so is whether it can ram.
	energy::Energy energy;
	bool canRam = false;
	ram::Ram ram;                 // the same ram as the player's, when it can
	float cloakLevel = 0.f;       // its cloak, eased: how faint the hull, how strong the bending
	float cloakedFor = 0.f;       // seconds cloaked: it ambushes, or gives up, in time
	float ramMemory = 0.f;        // seconds it still knows where the player is, after a ram
	float effectImmune = 0.f;     // seconds before another stun or lockdown takes (B2)

	float life = 1.f;

	// Disabled after being rammed (gameplay roadmap C4b): no steering, no
	// firing, just carried along by the blow and spinning, until `stunned`
	// runs out. The blow is on the body's velocity (P1). See enemyAi::stun.
	float stunned = 0.f;          // seconds left
	float spinRate = 0.f;         // radians per second

	// What it knows (gameplay roadmap C5). It sees along a cone from its nose,
	// and hears anything very close. Engaged is the old behaviour -- it knows
	// where the player is. Searching flies to where it last saw the player and
	// scans. Unaware wanders.
	enum class Awareness { Unaware, Engaged, Searching };
	Awareness awareness = Awareness::Unaware;
	glm::vec2 lastKnown = {};     // where it last saw the player
	float searchLeft = 0.f;       // seconds of scanning left at lastKnown
	float wanderTurn = 0.f;       // radians per second, while unaware
	float wanderTimer = 0.f;      // until the next change of wander turn
	float sightRange = 2500.f;
	float sightHalfAngle = 0.785f; // radians: 45 degrees either side

	// Burning outside the closing circle (gameplay roadmap L4): time toward
	// the next tick, and the red flash of the last one, 1 fading to 0.
	float burnTimer = 0.f;
	float burnFlash = 0.f;

	// Seconds before touching an asteroid field's core hurts it again (A2).
	float coreGrace = 0.f;
	// Seconds before bumping the player hurts again (P1): one bump, one hit,
	// however many frames the two stay in contact.
	float bumpGrace = 0.f;

	collision::Circle getHitbox() const
	{
		return game::shipHitbox(body.position, size);
	}
};
