#define GLM_ENABLE_EXPERIMENTAL
#include <enemyAi.h>
#include <tuning.h>

#include <engine/steering.h>
#include "imgui.h"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace enemyAi
{

namespace
{
	bool spawningEnabled = false;
	bool conesVisible = true;

	constexpr size_t maxEnemies = 15;

	// Sight and search (gameplay roadmap C5).
	float hearingRadius = 400.f;        // noticed at any angle this close
	float searchSeconds = 4.f;          // scanning at the last known position
	float scanRate = 1.2f;              // radians per second, sweeping while scanning
	float arriveDistance = 200.f;       // close enough to the last known position
	float wanderSpeedFraction = 0.45f;  // of its top speed, while unaware

	float randomBetween(float a, float b)
	{
		return a + (b - a) * (rand() / (float)RAND_MAX);
	}

	glm::vec2 rotated(glm::vec2 v, float radians)
	{
		const float c = std::cos(radians), s = std::sin(radians);
		return {v.x * c - v.y * s, v.x * s + v.y * c};
	}

	float cross(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

	// `point` lies in the enemy's sight cone: within range, and within the
	// half-angle of its nose. The apex itself does not count.
	bool inCone(const Enemy &enemy, glm::vec2 point)
	{
		const glm::vec2 to = point - enemy.body.position;
		const float dist2 = glm::dot(to, to);
		const float range = enemy.sightRange;
		if (dist2 <= 1e-8f || dist2 > range * range) { return false; }
		return glm::dot(to, enemy.body.facing) >= std::cos(enemy.sightHalfAngle) * std::sqrt(dist2);
	}

	// The two segments cross, endpoints included. Parallel ones do not.
	bool segmentsCross(glm::vec2 a, glm::vec2 b, glm::vec2 c, glm::vec2 d)
	{
		const glm::vec2 ab = b - a;
		const glm::vec2 cd = d - c;
		const float denominator = cross(ab, cd);
		if (std::abs(denominator) < 1e-8f) { return false; }
		const glm::vec2 ac = c - a;
		const float t = cross(ac, cd) / denominator;
		const float s = cross(ac, ab) / denominator;
		return t >= 0.f && t <= 1.f && s >= 0.f && s <= 1.f;
	}

	// The cone is a closed sector, and a sector is convex, so a segment meets
	// it exactly when an end is inside or the segment meets the boundary: one
	// of the two straight edges, or the arc.
	bool shotCrossesCone(const Enemy &enemy, glm::vec2 from, glm::vec2 to)
	{
		if (inCone(enemy, from) || inCone(enemy, to)) { return true; }

		const glm::vec2 apex = enemy.body.position;
		const glm::vec2 left = apex + rotated(enemy.body.facing, enemy.sightHalfAngle) * enemy.sightRange;
		const glm::vec2 right = apex + rotated(enemy.body.facing, -enemy.sightHalfAngle) * enemy.sightRange;
		if (segmentsCross(from, to, apex, left) || segmentsCross(from, to, apex, right)) { return true; }

		const glm::vec2 d = to - from;
		const glm::vec2 f = from - apex;
		const float a = glm::dot(d, d);
		if (a < 1e-8f) { return false; }
		const float b = 2.f * glm::dot(f, d);
		const float c = glm::dot(f, f) - enemy.sightRange * enemy.sightRange;
		const float discriminant = b * b - 4.f * a * c;
		if (discriminant < 0.f) { return false; }

		const float root = std::sqrt(discriminant);
		const float cosine = std::cos(enemy.sightHalfAngle);
		for (const float sign : {-1.f, 1.f})
		{
			const float t = (-b + sign * root) / (2.f * a);
			if (t < 0.f || t > 1.f) { continue; }
			const glm::vec2 at = from + d * t;
			const glm::vec2 toHit = at - apex;
			const float dist = std::sqrt(glm::dot(toHit, toHit));
			if (dist > 1e-4f && glm::dot(toHit, enemy.body.facing) >= cosine * dist) { return true; }
		}
		return false;
	}

	// How each class flies (gameplay roadmap P1). Every enemy is on Momentum,
	// as the player is -- thrust, a little drag, a top speed -- and each class
	// has its own feel. Rolled at spawn, so a change applies to new spawns
	// (Reset game respawns a level's).
	struct Flight
	{
		float mass;                  // for blows and bumps: the player's is 1
		float thrust;                // units per second squared at full intent
		float drag;                  // per second: coasting speed halves in ln 2 / drag
		float speedMin, speedMax;    // the top speed, rolled in this range
		float turnMin, turnMax;      // radians per second, rolled in this range
	};
	// Light and twitchy: 1.5 times the player's 6000 thrust, the old Instant
	// speeds as its cap.
	Flight rusherFlight = {0.8f, 9000.f, 0.6f, 800.f, 1800.f, 2.2f, 4.2f};
	// Heavy: 0.6 times the player's thrust, and slow to turn -- it used to
	// turn at 3.5 .. 4.5.
	Flight sniperFlight = {1.4f, 3600.f, 0.6f, 1200.f, 1600.f, 1.8f, 2.4f};
	// The boss (B2): the player's own flight -- 6000 thrust, a light 0.3
	// falloff, top speed 2000 -- heavier, and turning briskly.
	Flight bossFlight = {3.f, 6000.f, 0.3f, 2000.f, 2000.f, 3.5f, 3.5f};

	// What each enemy can do besides fly and shoot (B1), each rolled on its
	// own at spawn. Every enemy has energy; these are what a full bar can
	// raise or spend, and the ram.
	float shieldChance = 0.2f;
	float cloakChance = 0.2f;
	float ramChance = 0.2f;

	// An ordinary enemy's weapons (B2): sometimes a second, and each weapon's
	// modifiers, each at its own chance. Low, so most enemies are as they were.
	float secondWeaponChance = 0.1f;
	float stunChance = 0.1f;
	float lockdownChance = 0.1f;
	float spreadChance = 0.1f;
	int spreadShots = 2;               // a spread weapon's extra shots

	bool roll(float chance) { return rand() / (float)RAND_MAX < chance; }

	// When an enemy uses them (B1 step 3).
	// The ram: engaged, off cooldown, lined up within this cone, and within
	// about a surge's reach. Rushers close to 550, so they ram often; snipers
	// rarely come this close.
	float ramReach = 1500.f;
	float ramConeDegrees = 12.f;
	// A ram carries it past and points it away; it knows where the player is
	// for this long after, and turns back to fight rather than losing sight
	// and searching an empty spot.
	float ramMemorySeconds = 1.5f;
	// The cloak: to escape, then ambush -- the player's own play, turned on
	// them. It cloaks when hurt, with a full bar; drifts unseen, turning to
	// face the player; and fires, which uncloaks it, once it is close and
	// behind the player and lined up -- or, after a while with no chance,
	// fires anyway to come back.
	float cloakBelowLife = 0.5f;
	float ambushRange = 900.f;
	float maxCloakSeconds = 6.f;

	// Stunned, a ship tumbles: no thrust, no cap -- the blow is far past any
	// top speed -- and this drag, the old knockback's fade, so the blow is
	// mostly spent in half a second.
	float stunDrag = 4.f;

	// How each class fights, flying on engine/steering (P1, step 2). Both
	// circle the player facing it, holding a range: a rusher close and fast,
	// a sniper far and slow. Off the ring they arrive onto it, braking in
	// time; far off they head for where the player is going.
	struct Tactics
	{
		float range;                 // the radius it circles at
		float orbitSpeed;            // how fast it goes round, units per second
		float lead;                  // the most it leads the player by, seconds
	};
	Tactics rusherTactics = {550.f, 700.f, 1.f};
	Tactics sniperTactics = {1900.f, 350.f, 1.f};
	Tactics bossTactics = {1200.f, 600.f, 1.f};   // a middle range: every weapon has its moment

	// The boss's own numbers (B2).
	float bossLife = 10.f;            // an ordinary enemy's is 1
	float bossSize = 1.6f;            // of an ordinary enemy's
	float phase2At = 2.f / 3.f;       // of its life: below, it rams and cloaks too
	float phase3At = 1.f / 3.f;       // below, enraged
	float enragedSpeed = 1.3f;        // thrust and top speed, enraged
	float enragedCooldown = 0.6f;     // its weapons' cooldowns, enraged
	int bossWeaponsMin = 2;
	int bossWeaponsMax = 4;
	float bossModifierChance = 0.3f;  // each modifier on each rolled weapon
	steering::Params steer;          // the controller's response and braking share

	void rollFlight(Enemy &e, const Flight &f)
	{
		e.body.mass = f.mass;
		e.body.move = movement::momentum(f.thrust, f.drag);
		e.body.move.maxSpeed = randomBetween(f.speedMin, f.speedMax);
		e.body.turnRate = randomBetween(f.turnMin, f.turnMax);
	}

	bool canSee(const Enemy &enemy, glm::vec2 playerPos, bool playerHidden)
	{
		if (playerHidden) { return false; }
		const glm::vec2 toPlayer = playerPos - enemy.body.position;
		const float distance = glm::length(toPlayer);
		if (distance <= hearingRadius) { return true; }
		if (distance > enemy.sightRange) { return false; }
		const float cosine = glm::dot(toPlayer / distance, enemy.body.facing);
		return cosine >= std::cos(enemy.sightHalfAngle);
	}

	// Each policy below only decides an intent -- where to face, where to
	// thrust. `update` steps the body with it, the same step as the player's.

	// Lost the player: fly to where it was last seen, then turn slowly to scan
	// for it. The sweep is visible, because the cone turns with the nose.
	movement::Intent search(Enemy &enemy, float dt)
	{
		movement::Intent intent;
		const glm::vec2 toLast = enemy.lastKnown - enemy.body.position;
		const float distance = glm::length(toLast);
		if (distance > arriveDistance)
		{
			intent.face = toLast;
			intent.thrust = steering::arrive(enemy.body, enemy.lastKnown, {}, steer);
			return intent;
		}

		// There: hold the spot -- arriving brakes to it, rather than coasting
		// past -- and turn slowly to scan.
		const float sweep = (enemy.id % 2u) ? scanRate : -scanRate;
		intent.face = rotated(enemy.body.facing, sweep * dt);
		intent.thrust = steering::arrive(enemy.body, enemy.lastKnown, {}, steer);
		enemy.searchLeft -= dt;
		if (enemy.searchLeft <= 0.f) { enemy.awareness = Enemy::Awareness::Unaware; }
		return intent;
	}

	// Nothing to go on: drift in slow curves at part thrust, changing the curve
	// every couple of seconds. Patrols replace this once levels give places.
	movement::Intent wander(Enemy &enemy, float dt)
	{
		enemy.wanderTimer -= dt;
		if (enemy.wanderTimer <= 0.f)
		{
			enemy.wanderTurn = randomBetween(-0.8f, 0.8f);
			enemy.wanderTimer = randomBetween(1.5f, 3.f);
		}
		// At part of its top speed along its nose: a velocity to hold, not a
		// thrust -- part thrust against a little drag still reaches the cap.
		movement::Intent intent;
		intent.face = rotated(enemy.body.facing, enemy.wanderTurn * dt);
		intent.thrust = steering::matchVelocity(enemy.body,
			intent.face * (wanderSpeedFraction * movement::topSpeed(enemy.body.move)), steer);
		return intent;
	}

	glm::vec2 towardPlayer(glm::vec2 from, glm::vec2 playerPos, float *distance)
	{
		glm::vec2 toPlayer = playerPos - from;
		const float length = glm::length(toPlayer);
		if (distance) { *distance = length; }
		if (length == 0.f) { return {1.f, 0.f}; }
		return toPlayer / length;
	}

	bool alignedTo(const Enemy &enemy, glm::vec2 directionToPlayer)
	{
		return glm::length(directionToPlayer + enemy.body.facing) >= enemy.fireRange;
	}

	// Close in and circle tight, fast, facing the player -- guns on it all
	// the way round. It used to fly where it looked, straight at the player,
	// which with momentum overshoots: it passed through and had to come
	// round again. Which way round is the id's.
	movement::Intent closeIn(const Enemy &enemy, glm::vec2 playerPos, glm::vec2 playerVelocity)
	{
		movement::Intent intent;
		intent.face = towardPlayer(enemy.body.position, playerPos, nullptr);
		intent.thrust = steering::orbit(enemy.body, playerPos, playerVelocity,
			rusherTactics.range, rusherTactics.orbitSpeed, enemy.id % 2u == 0u, rusherTactics.lead, steer);
		return intent;
	}

	// Hang back and circle wide, slowly, facing the player: back off when
	// too close, close in when too far, round in between -- all one orbit,
	// braking onto the ring instead of swinging through it. Which way round is
	// the sheet row's, as before (type.y).
	// The boss: circle at a middle range, facing the player, so each of its
	// weapons has its moment -- missiles as it closes, lasers and the beam
	// on the ring. Which way round is its id's.
	movement::Intent bossFight(const Enemy &enemy, glm::vec2 playerPos, glm::vec2 playerVelocity);

	movement::Intent keepDistance(const Enemy &enemy, glm::vec2 playerPos, glm::vec2 playerVelocity)
	{
		movement::Intent intent;
		intent.face = towardPlayer(enemy.body.position, playerPos, nullptr);
		intent.thrust = steering::orbit(enemy.body, playerPos, playerVelocity,
			sniperTactics.range, sniperTactics.orbitSpeed, enemy.type.y % 2u == 0u, sniperTactics.lead, steer);
		return intent;
	}

	// One of the four shared weapons, at random and as defined -- burst
	// laser, heavy laser, missiles (five, homing on the player) or the beam.
	// Its first shot waits a second after the enemy first engages, as enemy
	// guns always have. Two or more weapons, and modifiers on a base weapon,
	// are B2's.
	// `weapon` is a slot of weapons::shipWeapon -- a level's placement can
	// name one -- or -1 to roll.
	movement::Intent bossFight(const Enemy &enemy, glm::vec2 playerPos, glm::vec2 playerVelocity)
	{
		movement::Intent intent;
		intent.face = towardPlayer(enemy.body.position, playerPos, nullptr);
		intent.thrust = steering::orbit(enemy.body, playerPos, playerVelocity,
			bossTactics.range, bossTactics.orbitSpeed, enemy.id % 2u == 0u, bossTactics.lead, steer);
		return intent;
	}

	// Its weapons' modifiers, each rolled at its chance (B2). Spread is not
	// for a beam: a beam is one line.
	void rollModifiers(weapons::Weapon &w)
	{
		w.stun = roll(stunChance);
		w.lockdown = roll(lockdownChance);
		w.spread = (!w.beam && roll(spreadChance)) ? spreadShots : 0;
	}

	bool decide(AbilityChoice choice, float chance);

	// Its weapons. A placement can choose them, slot by slot (B2): each a kind
	// or rolled, each modifier yes, no or rolled. Without a choice, an
	// ordinary enemy rolls one weapon and sometimes a second of another kind;
	// a boss rolls two to four different ones, its modifiers more often.
	void arm(Enemy &e, const std::vector<GunChoice> &chosen)
	{
		weapons::Weapon guns[weapons::slotCount];
		int count = 0;
		const bool boss = e.behaviour == Enemy::Behaviour::Boss;
		const float stunAt = boss ? bossModifierChance : stunChance;
		const float lockAt = boss ? bossModifierChance : lockdownChance;
		const float spreadAt = boss ? bossModifierChance : spreadChance;

		if (!chosen.empty())
		{
			for (const GunChoice &c : chosen)
			{
				if (count >= weapons::slotCount) { break; }
				weapons::Weapon w = weapons::shipWeapon(c.weapon >= 0 && c.weapon < weapons::slotCount
					? c.weapon : rand() % weapons::slotCount);
				w.stun = decide(c.stun, stunAt);
				w.lockdown = decide(c.lockdown, lockAt);
				w.spread = (!w.beam && decide(c.spread, spreadAt)) ? spreadShots : 0;
				guns[count++] = w;
			}
		}
		else
		{
			// Different kinds, in a shuffled order.
			int kinds[weapons::slotCount];
			for (int i = 0; i < weapons::slotCount; i++) { kinds[i] = i; }
			for (int i = weapons::slotCount - 1; i > 0; i--) { std::swap(kinds[i], kinds[rand() % (i + 1)]); }
			int wanted = 1;
			if (boss)
			{
				const int lo = std::clamp(bossWeaponsMin, 1, weapons::slotCount);
				const int hi = std::clamp(bossWeaponsMax, lo, weapons::slotCount);
				wanted = lo + rand() % (hi - lo + 1);
			}
			else if (roll(secondWeaponChance)) { wanted = 2; }
			for (int i = 0; i < wanted; i++)
			{
				weapons::Weapon w = weapons::shipWeapon(kinds[i]);
				w.stun = roll(stunAt);
				w.lockdown = roll(lockAt);
				w.spread = (!w.beam && roll(spreadAt)) ? spreadShots : 0;
				guns[count++] = w;
			}
		}
		e.loadout = weapons::loadoutOf(guns, count);
		for (int i = 0; i < count; i++) { e.loadout.cooldownLeft[i] = 1.f; }
	}

	// Its abilities, at their chances. An enemy's bubble is in the enemies'
	// colours, so it is never read as the player's.
	bool decide(AbilityChoice choice, float chance)
	{
		return choice == AbilityChoice::Random ? roll(chance) : choice == AbilityChoice::Yes;
	}

	void rollAbilities(Enemy &e, AbilityChoice shield, AbilityChoice cloak, AbilityChoice ram)
	{
		// A boss has every ability; its phases decide when it uses them.
		const bool boss = e.behaviour == Enemy::Behaviour::Boss;
		e.energy.hasShield = boss || decide(shield, shieldChance);
		e.energy.canCloak = boss || decide(cloak, cloakChance);
		e.canRam = boss || decide(ram, ramChance);
		e.energy.bubble.palette = shield::enemyPalette();
		energy::reset(e.energy);
	}

	void rollLoadout(Enemy &e, const std::vector<GunChoice> &weapon)
	{
		if (e.behaviour == Enemy::Behaviour::Boss)
		{
			// Column 1 of the sheet, an enemy hull of its own, drawn larger.
			e.type = {1, 0};
			rollFlight(e, bossFlight);
			e.size = sizeOf(Enemy::Behaviour::Boss);
			e.life = e.lifeFull = bossLife;
			e.fireRange = 1.8f;
			arm(e, weapon);
			e.sightRange = 3500.f;
			e.sightHalfAngle = 0.785f;                    // 45 degrees either side
			return;
		}

		if (e.behaviour == Enemy::Behaviour::KeepDistance)
		{
			// Column 2 of the sheet, so they read as a different ship. Two rows
			// so they do not all orbit the same way (keepDistance uses type.y).
			e.type = (rand() % 2) ? glm::uvec2{2, 0} : glm::uvec2{2, 1};
			rollFlight(e, sniperFlight);
			e.fireRange = 1.7f + (rand() % 1000) / 5000.f; // 1.7 .. 1.9
			arm(e, weapon);
			// Further and narrower than a rusher: it spots the player first,
			// and keeps its distance while it does.
			e.sightRange = 3500.f;
			e.sightHalfAngle = 0.524f;                    // 30 degrees either side
			return;
		}

		e.type = (rand() % 2) ? glm::uvec2{0, 0} : glm::uvec2{0, 1};
		rollFlight(e, rusherFlight);
		e.fireRange = 1.5f + (rand() % 1000) / 2000.f;
		arm(e, weapon);
	}
}

void stun(Enemy &enemy, glm::vec2 impulse, float seconds)
{
	enemy.stunned = seconds;
	ram::stop(enemy.ram); // a ram struck mid-lunge ends there
	movement::push(enemy.body, impulse);
	// A fast tumble either way, so two rammed ships do not turn in step.
	const float turns = 6.f + (rand() % 1000) / 200.f; // 6 .. 11 rad/s
	enemy.spinRate = (rand() % 2) ? turns : -turns;
}

void alert(Enemy &enemy, glm::vec2 playerPos)
{
	enemy.awareness = Enemy::Awareness::Engaged;
	enemy.lastKnown = playerPos;
	const glm::vec2 toPlayer = playerPos - enemy.body.position;
	const float distance = glm::length(toPlayer);
	if (distance > 0.001f) { enemy.body.facing = toPlayer / distance; }
}

void noticeShot(Enemy &enemy, glm::vec2 from, glm::vec2 to, glm::vec2 playerPos)
{
	if (enemy.awareness == Enemy::Awareness::Engaged) { return; }
	if (!shotCrossesCone(enemy, from, to)) { return; }
	alert(enemy, playerPos);
}

bool showCones() { return conesVisible; }

Orders update(Enemy &enemy, float gameDeltaTime, const Player &player, const glm::vec2 *comeBackTo)
{
	const glm::vec2 playerPos = player.position;
	const glm::vec2 playerVelocity = player.velocity;
	const glm::vec2 playerFacing = player.facing;
	const bool playerHidden = player.hidden;

	// A boss's phase follows its life (B2). Entering the third, it is
	// enraged: faster, and its weapons cool down sooner -- for good.
	bool phaseChanged = false;
	if (enemy.behaviour == Enemy::Behaviour::Boss)
	{
		const float share = enemy.lifeFull > 0.f ? enemy.life / enemy.lifeFull : 1.f;
		const int phase = share < phase3At ? 3 : share < phase2At ? 2 : 1;
		if (phase > enemy.phase)
		{
			if (phase == 3)
			{
				enemy.body.move.acceleration *= enragedSpeed;
				enemy.body.move.maxSpeed *= enragedSpeed;
				for (int i = 0; i < enemy.loadout.count; i++) { enemy.loadout.slots[i].cooldown *= enragedCooldown; }
			}
			enemy.phase = phase;
			phaseChanged = true;
		}
	}

	if (enemy.stunned > 0.f)
	{
		// Disabled: tumbling on the blow -- no thrust, no cap, the tumble's
		// own quick drag. Integrated directly rather than stepped: the body
		// keeps its own options for when it recovers, and the spin is not a
		// turn toward anything.
		enemy.stunned -= gameDeltaTime;
		enemy.body.facing = glm::normalize(rotated(enemy.body.facing, enemy.spinRate * gameDeltaTime));
		enemy.body.thrust = {};
		movement::integrate(enemy.body.position, enemy.body.velocity, {},
			movement::momentum(0.f, stunDrag), gameDeltaTime);
		{ Orders none; none.phaseChanged = phaseChanged; return none; }
	}

	// A ram under way runs on its body in place of steering, as the player's
	// does: a moment's dip back, the heading still tracking the player, then
	// the surge straight along it. When it ends the speed cap brings it back.
	const glm::vec2 toPlayerNow = towardPlayer(enemy.body.position, playerPos, nullptr);
	ram::update(enemy.ram, gameDeltaTime, toPlayerNow);
	if (ram::windingUp(enemy.ram) || ram::active(enemy.ram))
	{
		enemy.ramMemory = ramMemorySeconds;
		enemy.awareness = Enemy::Awareness::Engaged;
		enemy.lastKnown = playerPos;
		enemy.lastKnownFacing = playerFacing;
		const glm::vec2 heading = ram::direction(enemy.ram);
		const bool surging = ram::active(enemy.ram);
		enemy.body.velocity = surging ? heading * ram::surgeSpeed() : -heading * ram::windupBackSpeed();
		enemy.body.position += enemy.body.velocity * gameDeltaTime;
		enemy.body.facing = heading;
		enemy.body.thrust = surging ? heading : glm::vec2(0.f);
		{ Orders none; none.phaseChanged = phaseChanged; return none; }
	}

	enemy.ramMemory = std::max(0.f, enemy.ramMemory - gameDeltaTime);
	if (canSee(enemy, playerPos, playerHidden) || (enemy.ramMemory > 0.f && !playerHidden))
	{
		enemy.awareness = Enemy::Awareness::Engaged;
		enemy.lastKnown = playerPos;
		enemy.lastKnownFacing = playerFacing;
	}
	else if (enemy.awareness == Enemy::Awareness::Engaged)
	{
		// Just lost sight -- out of the cone, or cloaked in front of it. Go to
		// where the player was.
		enemy.awareness = Enemy::Awareness::Searching;
		enemy.searchLeft = searchSeconds;
	}

	// Cloaked: drifting on the velocity it had -- a cloaked ship cannot
	// thrust, as the player's cannot -- and turning to face the player. Its
	// gun runs only to ambush: close, behind the player and lined up, or
	// after long enough cloaked that it gives up waiting. Firing uncloaks it
	// (the game does that, as it does for the player).
	if (energy::isCloaked(enemy.energy))
	{
		enemy.cloakedFor += gameDeltaTime;
		enemy.body.facing = movement::turnToward(enemy.body.facing, toPlayerNow, enemy.body.turnRate * gameDeltaTime);
		enemy.body.thrust = {};
		movement::integrate(enemy.body.position, enemy.body.velocity, {}, movement::momentum(0.f, 0.f), gameDeltaTime);

		float distance = 0.f;
		towardPlayer(enemy.body.position, playerPos, &distance);
		const bool behind = glm::dot(playerFacing, enemy.body.position - playerPos) < 0.f;
		enemy.loadout.selected = weapons::choose(enemy.loadout, distance, player.shielded);
		Orders orders;
	orders.phaseChanged = phaseChanged;
		orders.fighting = true;
		orders.trigger = (distance < ambushRange && behind && alignedTo(enemy, toPlayerNow))
			|| enemy.cloakedFor > maxCloakSeconds;
		return orders;
	}

	movement::Intent intent;
	bool fighting = false;
	if (comeBackTo && enemy.awareness != Enemy::Awareness::Engaged)
	{
		// Flying back into the closing circle, straight there at full thrust,
		// still looking.
		enemy.awareness = Enemy::Awareness::Unaware;
		intent.face = *comeBackTo - enemy.body.position;
		intent.forward = 1.f;
	}
	else
	{
		switch (enemy.awareness)
		{
		case Enemy::Awareness::Searching: intent = search(enemy, gameDeltaTime); break;
		case Enemy::Awareness::Unaware: intent = wander(enemy, gameDeltaTime); break;
		case Enemy::Awareness::Engaged:
			fighting = true;
			intent = enemy.behaviour == Enemy::Behaviour::KeepDistance ? keepDistance(enemy, playerPos, playerVelocity)
				: enemy.behaviour == Enemy::Behaviour::Boss ? bossFight(enemy, playerPos, playerVelocity)
				: closeIn(enemy, playerPos, playerVelocity);
			break;
		}
	}

	// One step for every ship, the player's included.
	movement::step(enemy.body, intent, gameDeltaTime);

	if (fighting)
	{
		const glm::vec2 toPlayer = towardPlayer(enemy.body.position, playerPos, nullptr);
		float distance = 0.f;
		towardPlayer(enemy.body.position, playerPos, &distance);

		// A boss rams and cloaks only from its second phase (B2), and cloaks
		// whenever its bar is full then -- it is hurt by definition.
		const bool boss = enemy.behaviour == Enemy::Behaviour::Boss;
		const bool mayRam = enemy.canRam && (!boss || enemy.phase >= 2);
		const bool hurt = boss ? enemy.phase >= 2 : enemy.life < cloakBelowLife;

		// Hurt, with a full bar: cloak, and slip away to come back unseen.
		if (enemy.energy.canCloak && enemy.energy.state == energy::State::Full && hurt)
		{
			energy::cloak(enemy.energy);
			enemy.cloakedFor = 0.f;
			{ Orders none; none.phaseChanged = phaseChanged; return none; }
		}

		// Close and lined up: ram.
		const float cone = std::cos(glm::radians(ramConeDegrees));
		if (mayRam && distance < ramReach && glm::dot(enemy.body.facing, toPlayer) > cone)
		{
			ram::tryStart(enemy.ram, enemy.body.facing);
		}
	}

	// Only a fighting enemy fires, once it has turned far enough to be
	// aligned -- judged on the facing it has after this step's turn.
	// The weapon that suits the moment (B2), then the trigger once lined up.
	if (fighting)
	{
		float distance = 0.f;
		towardPlayer(enemy.body.position, playerPos, &distance);
		enemy.loadout.selected = weapons::choose(enemy.loadout, distance, player.shielded);
	}
	Orders orders;
	orders.phaseChanged = phaseChanged;
	orders.fighting = fighting;
	orders.trigger = fighting && alignedTo(enemy, towardPlayer(enemy.body.position, playerPos, nullptr));
	return orders;
}

Enemy spawnAt(glm::vec2 position, glm::vec2 facing, Enemy::Behaviour behaviour, const std::vector<GunChoice> &weapon,
	AbilityChoice shield, AbilityChoice cloak, AbilityChoice ram)
{
	static unsigned int nextId = 1; // 0 means "no enemy"

	Enemy e;
	e.id = nextId++;
	e.behaviour = behaviour;
	e.body.position = position;
	e.body.facing = facing;
	rollLoadout(e, weapon);
	rollAbilities(e, shield, cloak, ram);
	return e;
}

Enemy spawnNear(glm::vec2 playerPos, Enemy::Behaviour behaviour)
{
	glm::vec2 offset(2000, 0);
	offset = glm::vec2(glm::vec4(offset, 0, 1) * glm::rotate(glm::mat4(1.f),
		glm::radians((float)(rand() % 360)), glm::vec3(0, 0, 1)));

	// Outward: a default +X facing would look at the player from the west
	// half of the ring and engage on spawn, skipping unaware wander (C5).
	return spawnAt(playerPos + offset, glm::normalize(offset), behaviour);
}

Enemy spawnNear(glm::vec2 playerPos)
{
	const auto behaviour = (rand() % 2)
		? Enemy::Behaviour::CloseIn
		: Enemy::Behaviour::KeepDistance;
	return spawnNear(playerPos, behaviour);
}

void updateSpawning(std::vector<Enemy> &enemies, float &timerSeconds,
	glm::vec2 playerPos, float gameDeltaTime)
{
	if (!spawningEnabled || enemies.size() >= maxEnemies) { return; }

	timerSeconds -= gameDeltaTime;

	if (timerSeconds < 0)
	{
		timerSeconds = rand() % 6 + 1;

		enemies.push_back(spawnNear(playerPos));
		if (rand() % 3 == 0)
		{
			enemies.push_back(spawnNear(playerPos));
			enemies.push_back(spawnNear(playerPos));
		}
	}
}

// The tunables this file offers (platform/tuning.h): registered at start-up,
// after everything above, so each one's default is the value it is declared with.
const tuning::Group tunables("enemies", {
	{"boss.mass", bossFlight.mass}, {"boss.thrust", bossFlight.thrust}, {"boss.falloff", bossFlight.drag},
	{"boss.topSpeedMin", bossFlight.speedMin}, {"boss.topSpeedMax", bossFlight.speedMax},
	{"boss.turnMin", bossFlight.turnMin}, {"boss.turnMax", bossFlight.turnMax},
	{"boss.range", bossTactics.range}, {"boss.orbitSpeed", bossTactics.orbitSpeed}, {"boss.lead", bossTactics.lead},
	{"boss.life", bossLife}, {"boss.size", bossSize}, {"boss.phase2At", phase2At}, {"boss.phase3At", phase3At},
	{"boss.enragedSpeed", enragedSpeed}, {"boss.enragedCooldown", enragedCooldown},
	{"boss.weaponsMin", bossWeaponsMin}, {"boss.weaponsMax", bossWeaponsMax}, {"boss.modifierChance", bossModifierChance},
	{"chance.shield", shieldChance},
	{"chance.cloak", cloakChance},
	{"chance.ram", ramChance},
	{"chance.secondWeapon", secondWeaponChance},
	{"chance.stun", stunChance},
	{"chance.lockdown", lockdownChance},
	{"chance.spread", spreadChance},
	{"spreadShots", spreadShots},
	{"ram.reach", ramReach},
	{"ram.coneDegrees", ramConeDegrees},
	{"ram.memory", ramMemorySeconds},
	{"cloak.belowLife", cloakBelowLife},
	{"cloak.ambushRange", ambushRange},
	{"cloak.longest", maxCloakSeconds},
	{"spawningEnabled", spawningEnabled},
	{"conesVisible", conesVisible},
	{"hearingRadius", hearingRadius},
	{"searchSeconds", searchSeconds},
	{"scanRate", scanRate},
	{"wanderSpeedFraction", wanderSpeedFraction},
	{"stunDrag", stunDrag},
	{"rusher.mass", rusherFlight.mass},
	{"rusher.thrust", rusherFlight.thrust},
	{"rusher.falloff", rusherFlight.drag},
	{"rusher.topSpeedMin", rusherFlight.speedMin},
	{"rusher.topSpeedMax", rusherFlight.speedMax},
	{"rusher.turnMin", rusherFlight.turnMin},
	{"rusher.turnMax", rusherFlight.turnMax},
	{"rusher.range", rusherTactics.range},
	{"rusher.orbitSpeed", rusherTactics.orbitSpeed},
	{"rusher.lead", rusherTactics.lead},
	{"sniper.mass", sniperFlight.mass},
	{"sniper.thrust", sniperFlight.thrust},
	{"sniper.falloff", sniperFlight.drag},
	{"sniper.topSpeedMin", sniperFlight.speedMin},
	{"sniper.topSpeedMax", sniperFlight.speedMax},
	{"sniper.turnMin", sniperFlight.turnMin},
	{"sniper.turnMax", sniperFlight.turnMax},
	{"sniper.range", sniperTactics.range},
	{"sniper.orbitSpeed", sniperTactics.orbitSpeed},
	{"sniper.lead", sniperTactics.lead},
	{"steer.response", steer.responseTime},
	{"steer.brakeShare", steer.brakeShare},
});

float sizeOf(Enemy::Behaviour behaviour)
{
	return behaviour == Enemy::Behaviour::Boss ? enemyShipSize * bossSize : enemyShipSize;
}

void classUi(Enemy::Behaviour behaviour)
{
	const bool sniper = behaviour == Enemy::Behaviour::KeepDistance;
	const bool boss = behaviour == Enemy::Behaviour::Boss;
	Flight &f = boss ? bossFlight : sniper ? sniperFlight : rusherFlight;
	Tactics &t = boss ? bossTactics : sniper ? sniperTactics : rusherTactics;
	ImGui::PushID(boss ? "boss" : sniper ? "sniper" : "rusher");
	ImGui::TextDisabled(boss ? "Shared by every boss" : sniper ? "Shared by every sniper" : "Shared by every rusher");

	// B2: what makes a boss.
	if (boss)
	{
		ImGui::SeparatorText("Boss (new spawns)");
		tune::SliderFloat("Life", &bossLife, 1.f, 50.f, "%.1f (an enemy's is 1)");
		tune::SliderFloat("Size", &bossSize, 1.f, 3.f, "x%.2f");
		tune::SliderFloat("Phase 2 below", &phase2At, 0.f, 1.f, "%.2f of its life: rams and cloaks");
		tune::SliderFloat("Phase 3 below", &phase3At, 0.f, 1.f, "%.2f of its life: enraged");
		tune::SliderFloat("Enraged speed", &enragedSpeed, 1.f, 3.f, "x%.2f");
		tune::SliderFloat("Enraged cooldowns", &enragedCooldown, 0.1f, 1.f, "x%.2f");
		tune::SliderInt("Weapons, fewest", &bossWeaponsMin, 1, weapons::slotCount);
		tune::SliderInt("Weapons, most", &bossWeaponsMax, 1, weapons::slotCount);
		tune::SliderFloat("Modifier chance", &bossModifierChance, 0.f, 1.f, "%.2f each, on rolled weapons");
	}

	// P1: how it flies. Rolled at spawn.
	ImGui::SeparatorText("Flight (new spawns; Reset game respawns a level's)");
	tune::SliderFloat("Mass", &f.mass, 0.1f, 5.f, "%.2f (the player is 1)");
	tune::SliderFloat("Thrust", &f.thrust, 500.f, 30000.f, "%.0f", ImGuiSliderFlags_Logarithmic);
	tune::SliderFloat("Falloff", &f.drag, 0.f, 3.f, "%.2f");
	tune::DragFloatRange2("Top speed", &f.speedMin, &f.speedMax, 10.f, 100.f, 6000.f, "%.0f");
	tune::DragFloatRange2("Turn rate", &f.turnMin, &f.turnMax, 0.05f, 0.1f, 15.f, "%.1f rad/s");

	// P1 step 2: how it fights with it. Live, for every enemy of the class.
	ImGui::SeparatorText("Tactics (live)");
	tune::SliderFloat("Range", &t.range, 100.f, 4000.f, "%.0f");
	tune::SliderFloat("Orbit speed", &t.orbitSpeed, 0.f, 2000.f, "%.0f");
	tune::SliderFloat("Lead", &t.lead, 0.f, 3.f, "%.2f s");
	ImGui::PopID();
}

void debugUi()
{
	tune::Checkbox("Spawn waves", &spawningEnabled);
	ImGui::SameLine();
	tune::Checkbox("Vision cones", &conesVisible);

	// Each class's tuning together: the same view as beside a selected enemy
	// in the editor.
	if (ImGui::TreeNode("Rusher")) { classUi(Enemy::Behaviour::CloseIn); ImGui::TreePop(); }
	if (ImGui::TreeNode("Sniper")) { classUi(Enemy::Behaviour::KeepDistance); ImGui::TreePop(); }
	if (ImGui::TreeNode("Boss")) { classUi(Enemy::Behaviour::Boss); ImGui::TreePop(); }

	// B1: what each new enemy can do, rolled on its own. Saved with tuning.
	if (ImGui::TreeNode("Abilities"))
	{
		tune::SliderFloat("Shield chance", &shieldChance, 0.f, 1.f, "%.2f");
		tune::SliderFloat("Cloak chance", &cloakChance, 0.f, 1.f, "%.2f");
		tune::SliderFloat("Ram chance", &ramChance, 0.f, 1.f, "%.2f");
		ImGui::SeparatorText("Weapons (B2)");
		tune::SliderFloat("Second weapon", &secondWeaponChance, 0.f, 1.f, "%.2f chance");
		tune::SliderFloat("Stun", &stunChance, 0.f, 1.f, "%.2f chance per weapon");
		tune::SliderFloat("Lockdown", &lockdownChance, 0.f, 1.f, "%.2f chance per weapon");
		tune::SliderFloat("Spread", &spreadChance, 0.f, 1.f, "%.2f chance per weapon");
		tune::SliderInt("Spread shots", &spreadShots, 1, 6, "%d extra");
		ImGui::SeparatorText("When they use them");
		tune::SliderFloat("Ram reach", &ramReach, 200.f, 4000.f, "%.0f");
		tune::SliderFloat("Ram cone", &ramConeDegrees, 1.f, 45.f, "%.0f deg either side");
		tune::SliderFloat("Ram memory", &ramMemorySeconds, 0.f, 5.f, "%.1f s it keeps track after a ram");
		tune::SliderFloat("Cloak below life", &cloakBelowLife, 0.f, 1.f, "%.2f");
		tune::SliderFloat("Ambush range", &ambushRange, 200.f, 3000.f, "%.0f");
		tune::SliderFloat("Longest cloak", &maxCloakSeconds, 1.f, 30.f, "%.1f s, then it fires anyway");
		ImGui::TextDisabled("Chances: spawned enemies, and placed ones set to Random");
		ImGui::TreePop();
	}

	// C5: what they notice, and what they do with nothing to go on.
	if (ImGui::TreeNode("Awareness"))
	{
		tune::SliderFloat("Hearing", &hearingRadius, 0.f, 1500.f, "%.0f");
		tune::SliderFloat("Search time", &searchSeconds, 0.5f, 15.f, "%.1f s");
		tune::SliderFloat("Scan speed", &scanRate, 0.2f, 5.f, "%.1f rad/s");
		tune::SliderFloat("Wander thrust", &wanderSpeedFraction, 0.f, 1.f, "%.2f");
		ImGui::TreePop();
	}

	// Every class: the steering controller (P1), and tumbling after a ram.
	if (ImGui::TreeNode("Steering and stun"))
	{
		tune::SliderFloat("Response", &steer.responseTime, 0.02f, 2.f, "%.2f s");
		tune::SliderFloat("Brake share", &steer.brakeShare, 0.1f, 1.f, "%.2f of full thrust");
		tune::SliderFloat("Stun drag", &stunDrag, 0.5f, 10.f, "%.1f /s (tumbling after a ram)");
		ImGui::TreePop();
	}
}

}
