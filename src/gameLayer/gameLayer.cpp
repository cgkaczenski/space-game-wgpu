#define GLM_ENABLE_EXPERIMENTAL
#include "gameLayer.h"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include "platformInput.h"
#include "imgui.h"
#include <iostream>
#include <sstream>
#include "imfilebrowser.h"
#include <render/wgpu2d.h>
#include <hud.h>
#include <shipThruster.h>
#include <shipShield.h>
#include <bulletLook.h>
#include <cloak.h>
#include <crt.h>
#include <hitboxDebug.h>
#include <debugPanel.h>
#include <platformTools.h>
#include <background.h>
#include <sfx.h>
#include <shipSprite.h>
#include <bullet.h>
#include <vector>
#include <enemy.h>
#include <enemyAi.h>
#include <zoomControl.h>
#include <gameClock.h>
#include <playerMove.h>
#include <energy.h>
#include <weapons.h>
#include <effects.h>
#include <ram.h>
#include <cstdio>
#include <engine/collisionSystem.h>
#include <shipHitbox.h>
#include <engine/cameraFollow.h>

// Everything in this block has internal linkage: no other file can name it.
// Before roadmap R10 these were ordinary globals, reachable from anywhere with
// one `extern` -- and a feature cannot be self-contained while its state can be
// reached into from another translation unit.
namespace
{

// ---- What happened this round ---------------------------------------------
//
// Restart throws all of this away. What was *chosen* -- control scheme, zoom,
// toggles, tuning -- lives in the feature that owns it and survives.
struct Session
{
	glm::vec2 playerPos = {100,100};
	glm::vec2 playerVelocity = {};
	glm::vec2 playerFacing = {1,0};

	std::vector<Bullet> bullets;

	std::vector<Enemy> enemies;

	float health = 1.f;

	float spawnEnemyTimerSecconds = 3;
};

Session session;

// ---- What the game owns -----------------------------------------------------

wgpu2d::Renderer2D renderer;

collision::BasicCollisionSystem collisionSystem;

// Who owns an asset, by the rule R6 gave the device: whoever creates a thing
// releases it, and holding it is only borrowing. An asset with one consumer
// belongs to that consumer -- the bullet sheet to bulletLook, the backgrounds
// to background, the shot to sfx. The ship sheet has several, the player and
// every enemy, so the game loads it, lends it by reference, and releases it.
wgpu2d::Texture shipSheet;
wgpu2d::TextureAtlasPadding shipAtlas;

constexpr float shipSize = 250.f;

// Enemies further than this from the player are removed. Named because the
// zoom-out limit depends on it: past that zoom, the player would see it happen.
constexpr float enemyDespawnDistance = 4000.f;

// ---- Lifecycle --------------------------------------------------------------
//
// One row per feature with GPU or audio resources or per-round state. Init runs
// down the table and cleanup runs back up it, so a feature is torn down before
// anything it was started after. A feature is added once, here, and gets all
// three -- three hand-kept lists had already drifted: crt was started and never
// cleaned up, because there was no crt::cleanup to forget.
struct Feature
{
	const char *name;
	bool (*init)();     // null: nothing to load
	void (*reset)();    // null: nothing of this round to forget
	void (*cleanup)();  // null: nothing to release
};

const Feature features[] = {
	{"hud",        hud::init,        hud::reset,      hud::cleanup},
	{"thruster",   thruster::init,   thruster::reset, thruster::cleanup},
	{"shield",     shield::init,     shield::reset,   shield::cleanup},
	{"bulletLook", bulletLook::init, nullptr,         bulletLook::cleanup},
	{"cloak",      cloak::init,      nullptr,         cloak::cleanup},
	{"crt",        crt::init,        nullptr,         crt::cleanup},
	{"background", background::init, nullptr,         background::cleanup},
	{"sfx",        sfx::init,        nullptr,         sfx::cleanup},
	{"effects",    effects::init,    effects::reset,  effects::cleanup},
	{"ram",        nullptr,          ram::reset,      nullptr},
	// After shield and cloak: its reset raises one and lowers the other.
	{"energy",     nullptr,          energy::reset,   nullptr},
	{"weapons",    nullptr,          weapons::reset,  nullptr},
};

// A setting, so it survives restart.
bool healthRegenEnabled = true;

// Where the camera is before the world shake. Follow chases from here, and the
// shake is added on top each frame, so the shake never becomes where the
// camera thinks it is.
glm::vec2 cameraBase = {};

// How far down the table init got, so cleanup after a failed start releases
// exactly the successes. The row that failed is cleaned in initGame itself
// before this count moves, because it never became a success.
int startedFeatures = 0;

void restartGame()
{
	session = {};

	for (const Feature &feature : features)
	{
		if (feature.reset) { feature.reset(); }
	}

	// Zero dead zone and zero leash: snap straight onto the player.
	cameraBase = camera::follow(
		cameraBase, session.playerPos,
		{(float)renderer.windowW, (float)renderer.windowH},
		{550.f, 0.f, 0.f});
	renderer.currentCamera.position = cameraBase;
}

// Every kill goes through here, whatever did it, so every death explodes the
// same way (gameplay roadmap C4). Leaving the despawn ring is not a death.
void killEnemy(int index)
{
	const Enemy &e = session.enemies[index];
	effects::enemyKilled(e, shipAtlas.get(e.type.x, e.type.y));
	session.enemies.erase(session.enemies.begin() + index);
}

// How far a ray from inside the view travels before leaving it. The laser
// reaches "all the way across the screen": to the edge of what is shown.
float distanceToViewEdge(glm::vec2 origin, glm::vec2 direction, glm::vec4 view)
{
	float reach = 1e9f;
	if (direction.x > 0.f) { reach = std::min(reach, (view.x + view.z - origin.x) / direction.x); }
	if (direction.x < 0.f) { reach = std::min(reach, (view.x - origin.x) / direction.x); }
	if (direction.y > 0.f) { reach = std::min(reach, (view.y + view.w - origin.y) / direction.y); }
	if (direction.y < 0.f) { reach = std::min(reach, (view.y - origin.y) / direction.y); }
	return std::max(reach, 0.f);
}

// The session's own controls. The game owns the session, so this is where they
// belong -- no longer a waiting room for controls with nowhere else to go.
void sessionDebugUi()
{
	ImGui::Text("Bullets count: %d", (int)session.bullets.size());
	ImGui::Text("Enemies count: %d", (int)session.enemies.size());

	if (ImGui::Button("Spawn rusher"))
	{
		session.enemies.push_back(enemyAi::spawnNear(session.playerPos, Enemy::Behaviour::CloseIn));
	}
	ImGui::SameLine();
	if (ImGui::Button("Spawn sniper"))
	{
		session.enemies.push_back(enemyAi::spawnNear(session.playerPos, Enemy::Behaviour::KeepDistance));
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset game"))
	{
		restartGame();
	}

	ImGui::SliderFloat("Player Health", &session.health, 0, 1);
	ImGui::Checkbox("Health regen", &healthRegenEnabled);
}

}

bool initGame()
{
	std::srand(std::time(0));

	//initializing stuff for the renderer
	wgpu2d::init();
	renderer.create();

	shipSheet.loadFromFileWithPixelPadding
	(RESOURCES_PATH "spaceShip/stitchedFiles/spaceships.png", 128, true);
	if (shipSheet.id == 0)
	{
		std::cerr << "initGame: could not load the ship sheet\n";
		return false;
	}
	shipAtlas = wgpu2d::TextureAtlasPadding(5, 2, shipSheet.GetSize().x, shipSheet.GetSize().y);

	for (const Feature &feature : features)
	{
		if (feature.init && !feature.init())
		{
			std::cerr << "initGame: " << feature.name << " failed to start\n";
			// The table only walks successes. This row created something or it
			// did not; cleanup is safe either way (a zero texture is a no-op).
			if (feature.cleanup) { feature.cleanup(); }
			return false;
		}
		startedFeatures++;
	}

	restartGame();

	return true;
}

bool gameLogic(float deltaTime)
{

#pragma region init stuff
	int w = 0; int h = 0;
	w = platform::getFrameBufferSizeX(); //window w
	h = platform::getFrameBufferSizeY(); //window h
	
	renderer.clearScreen({0, 0, 0, 1}); //clear screen (the pass's load op, applied at flush)

	renderer.updateWindowMetrics(w, h);

	// Before anything is drawn: setting this is what routes the frame through
	// a target, and the target has to exist before the first quad lands.
	crt::apply();

	// The two clocks, named once (roadmap R8). The simulation takes
	// `time.game`; camera, zoom and the panel take `time.real`.
	const FrameTime time = gameClock::tick(deltaTime);
#pragma endregion


#pragma region mouse pos

	// Before movement, because the mouse can steer the ship.
	glm::vec2 mousePos = platform::getRelMousePosition();
	glm::vec2 screenCenter(w / 2.f, h / 2.f);

	glm::vec2 mouseDirection = mousePos - screenCenter;

	if (glm::length(mouseDirection) == 0.f)
	{
		mouseDirection = {1,0};
	}
	else
	{
		mouseDirection = normalize(mouseDirection);
	}

#pragma endregion


#pragma region energy

	// Before movement, because a cloaked ship drifts instead of flying.
	if (platform::isButtonPressedOn(platform::Button::E)) { energy::cloak(); }
	energy::update(time.game);

#pragma endregion

#pragma region movement

	// Facing is the hull; aim is the gun. They are the same vector unless the
	// ship is turned with A/D, when the mouse aims independently.
	// The ram, before flying: Space starts it toward the mouse, and ramming
	// uncloaks, as firing does (gameplay roadmap C4b).
	ram::update(time.game);
	if (!ImGui::GetIO().WantCaptureKeyboard && platform::isButtonPressedOn(platform::Button::Space)
		&& ram::tryStart(mouseDirection))
	{
		energy::uncloak();
	}
	shield::setRam(ram::barrierLevel(), ram::direction());

	playerMove::Result player;
	if (ram::windingUp())
	{
		// The wind-up: a moment's dip back, facing the ram, while the prow
		// brightens. The anticipation is what makes the lunge read as heavy.
		session.playerVelocity = -ram::direction() * ram::windupBackSpeed();
		session.playerPos += session.playerVelocity * time.game;
		session.playerFacing = ram::direction();
		player.facing = ram::direction();
		player.aim = mouseDirection;
		player.throttle = 0.f;
	}
	else if (ram::active())
	{
		// The surge overrides flying: straight along the ram, past the normal
		// top speed. When it ends the ship still has this velocity, and the
		// momentum settings take it from there -- the speed cap brings it back.
		session.playerVelocity = ram::direction() * ram::surgeSpeed();
		session.playerPos += session.playerVelocity * time.game;
		session.playerFacing = ram::direction();
		player.facing = ram::direction();
		player.aim = mouseDirection;
		player.throttle = 1.f;

		effects::ramTrail(session.playerPos, ram::direction(), shipSize,
			shipAtlas.get(3, 0), time.game);
	}
	else
	{
		player = playerMove::update(session.playerPos, session.playerVelocity,
			session.playerFacing, mouseDirection, time.game, energy::isCloaked());
	}

	// What the ram strikes: the arc's reach, a little ahead of the hull. Each
	// enemy once per ram; the player takes nothing.
	if (ram::active())
	{
		const collision::Circle front = {session.playerPos + ram::direction() * (shipSize * 0.3f),
			shipSize * 0.65f};
		for (int e = 0; e < (int)session.enemies.size(); e++)
		{
			Enemy &enemy = session.enemies[e];
			if (!collisionSystem.overlaps(front, enemy.getHitbox()) || !ram::firstHit(enemy.id)) { continue; }

			// The instant of contact: the game stops dead for a moment, the prow
			// flares, the world shakes -- then the enemy goes.
			gameClock::hitStop(ram::hitStopSeconds());
			shield::ramImpact();
			effects::shake(1.f);
			if (!hitboxDebug::isDamageFrozen()) { enemy.life -= ram::hitDamage(); }
			if (enemy.life <= 0.f)
			{
				killEnemy(e);
				e--;
				continue;
			}
			// Knocked aside, not ahead: 45 degrees off the ram toward whichever
			// side of the ship it was on, faster than the ram itself. Straight
			// ahead, the ship -- still surging -- caught the enemy it had just
			// struck and ran through it; aside, the ship passes it.
			const glm::vec2 side = {-ram::direction().y, ram::direction().x};
			const float which = glm::dot(enemy.position - session.playerPos, side) >= 0.f ? 1.f : -1.f;
			const glm::vec2 away = glm::normalize(ram::direction() + side * which);
			enemyAi::stun(enemy, away * (ram::surgeSpeed() + ram::knockbackSpeed()),
				ram::stunSeconds());
		}
	}

#pragma endregion

#pragma region follow

	// Real time: the camera is presentation, and should keep settling while
	// the game is slowed.
	cameraBase = camera::follow(
		cameraBase, session.playerPos, {(float)w, (float)h},
		{time.real * 550.f, 1.f, 150.f});

	// The world shake rides on top: the whole world moves, background and all,
	// and the HUD, drawn with its own screen camera, stays still.
	renderer.currentCamera.position = cameraBase + effects::shakeOffset(time.real)
		+ ram::cameraLean(); // and leans ahead while ramming

#pragma endregion

#pragma region render background

	// Wall time, not game time: see zoomControl.h.
	renderer.currentCamera.zoom = zoomControl::update(time.real,
		{(float)w, (float)h}, enemyDespawnDistance);

	background::draw(renderer);
#pragma endregion

#pragma region handle bulets


	weapons::handleInput();

	// Held, not clicked: the selected weapon fires whenever it is ready.
	// Clicks on the debug panel are the panel's.
	const bool trigger = platform::isLMouseHeld() && !ImGui::GetIO().WantCaptureMouse;
	// The mouse in the world, for a missile's target. The view rect is the
	// world area on screen, so the pointer's fraction of the window is its
	// fraction of that.
	const glm::vec4 view = renderer.getViewRect();
	const glm::vec2 mouseWorld = glm::vec2(view.x, view.y)
		+ mousePos / glm::vec2((float)w, (float)h) * glm::vec2(view.z, view.w);

	weapons::FireContext fire;
	fire.origin = session.playerPos;
	fire.aim = player.aim; // the mouse, not necessarily the hull
	fire.shipVelocity = session.playerVelocity;
	fire.shipSize = shipSize;
	fire.mouseWorld = mouseWorld;
	fire.enemies = &session.enemies;
	const int shots = weapons::update(time.game, trigger, fire, session.bullets);
	if (shots > 0)
	{
		// Firing is how the player leaves the cloak, and the shot still goes out.
		energy::uncloak();
		for (int s = 0; s < shots; s++) { sfx::playerShot(); }
	}

	// The laser: traced, not flown. It reaches the edge of the view unless an
	// enemy is in the way, and burns the first one it touches for as long as
	// it touches it (gameplay roadmap C3b). Enemies have no shields yet; when
	// they do, a shielded enemy is where the beam stops without its damage.
	static float effectClock = 0.f; // drives the beam's scroll and flicker
	effectClock += time.game;

	const weapons::Beam beam = weapons::beam();
	glm::vec2 beamEnd = {};
	bool beamHit = false;
	if (beam.firing)
	{
		energy::uncloak();
		if (beam.started) { sfx::playerShot(); }

		float reach = distanceToViewEdge(beam.origin, beam.direction, view);
		int target = -1;
		for (int e = 0; e < (int)session.enemies.size(); e++)
		{
			const float t = collision::rayToCircle(beam.origin, beam.direction,
				session.enemies[e].getHitbox());
			if (t >= 0.f && t < reach) { reach = t; target = e; }
		}
		beamEnd = beam.origin + beam.direction * reach;
		beamHit = target >= 0;

		if (beamHit && !hitboxDebug::isDamageFrozen())
		{
			session.enemies[target].life -= beam.damagePerSecond * time.game;
			if (session.enemies[target].life <= 0.f)
			{
				killEnemy(target);
			}
		}
	}


	// Before anything moves: missiles turn and speed up, then fly with the rest.
	weapons::steerMissiles(session.bullets, session.enemies, time.game);

	for (int i = 0; i < session.bullets.size(); i++)
	{
		
		if (glm::distance(session.bullets[i].position, session.playerPos) > 5'000)
		{
			session.bullets.erase(session.bullets.begin() + i);
			i--;
			continue;
		}

		if (!hitboxDebug::isDamageFrozen())
		{
			if (!session.bullets[i].isEnemy)
			{
				bool breakBothLoops = false;
				for (int e = 0; e < session.enemies.size(); e++)
				{

					if (collisionSystem.overlaps(session.bullets[i].getHitbox(),
						session.enemies[e].getHitbox()))
					{
						session.enemies[e].life -= session.bullets[i].damage;

						if (session.enemies[e].life <= 0)
						{
							killEnemy(e);
						}

						session.bullets.erase(session.bullets.begin() + i);
						i--;
						breakBothLoops = true;
						continue;
					}

				}

				if (breakBothLoops)
				{
					continue;
				}
			}
			else
			{
				// A cloaked ship cannot be hit: the shot passes through and
				// carries on, rather than vanishing on something that isn't there.
				if (!energy::isCloaked() &&
					collisionSystem.overlaps(session.bullets[i].getHitbox(),
					game::shipHitbox(session.playerPos, shipSize)))
				{
					// The ram's prow takes shots from the front while it is out.
					if (ram::barrierUp() && glm::dot(session.bullets[i].position - session.playerPos,
						ram::direction()) > 0.f)
					{
						session.bullets.erase(session.bullets.begin() + i);
						i--;
						continue;
					}

					// Relative to the ship, because the shield moves with it and
					// the ripple has to stay anchored to the bubble.
					const energy::HitResult hit =
						energy::onHit(session.bullets[i].position - session.playerPos);

					if (hit == energy::HitResult::Damaged)
					{
						session.health -= 0.1;
						hud::onDamage();  // shake the HUD when the hull is hit
					}

					session.bullets.erase(session.bullets.begin() + i);
					i--;
					continue;
				}

			}
		}

		session.bullets[i].update(time.game);

	}

	if (session.health <= 0)
	{
		//kill player
		restartGame();
	}
	else
	{
		// Game time. This was the frame's own delta, so at 1% speed the ship
		// healed at full rate while everything shooting at it crawled.
		if (healthRegenEnabled) { session.health += time.game * 0.05; }
		session.health = glm::clamp(session.health, 0.f, 1.f);
	}

#pragma endregion

#pragma region handle enemies

	enemyAi::updateSpawning(session.enemies, session.spawnEnemyTimerSecconds,
		session.playerPos, time.game);


	for (int i = 0; i < session.enemies.size(); i++)
	{

		if (glm::distance(session.playerPos, session.enemies[i].position) > enemyDespawnDistance)
		{
			//dispawn enemy
			session.enemies.erase(session.enemies.begin() + i);
			i--;
			continue;
		}

		// Ship-ship (player vs enemy, enemy vs enemy) will use
		// collisionSystem.overlaps(hitboxA, hitboxB) and
		// collisionSystem.separation(circleA, circleB) to push them apart.

		if (enemyAi::update(session.enemies[i], time.game, session.playerPos))
		{
			Bullet b;
			b.position = session.enemies[i].position;
			b.fireDirection = session.enemies[i].viewDirection;
			// The gun's, copied onto the shot. Flight reads Bullet::speed.
			b.speed = session.enemies[i].bulletSpeed;

			b.isEnemy = true;
			session.bullets.push_back(b);

			sfx::enemyShot();

		}
	}

#pragma endregion

	effects::update(time.game);

#pragma region render enemies

	for (auto &e : session.enemies)
	{
		renderSpaceShip(renderer, e.position, enemyShipSize,
			shipSheet, shipAtlas.get(e.type.x, e.type.y), e.viewDirection);
	}

	// Wrecks sit where ships sit: after them, under everything else.
	effects::drawDebris(renderer, shipSheet);

	// A missile's lock on its target: a dashed red box, until impact.
	for (const auto &b : session.bullets)
	{
		if (b.motion != BulletMotion::Missile || b.targetId == 0) { continue; }
		for (const auto &e : session.enemies)
		{
			if (e.id != b.targetId) { continue; }
			effects::drawTargetBox(renderer, e.position, enemyShipSize * 1.3f, b.age);
		}
	}

#pragma endregion

#pragma region render ship

	// The ram's afterimages, under everything of the ship's own.
	effects::drawAfterimages(renderer, shipSheet);

	// Before the hull, so the hull covers the end of the plume inside it.
	thruster::draw(renderer, session.playerPos, shipSize, player.facing,
		player.throttle, time.game);

	// Faded by the cloak. The hull going nearly transparent is half the
	// effect; the other half is the world bending around it, which happens
	// below when the world goes through the cloak's shader.
	renderSpaceShip(renderer, session.playerPos, shipSize,
		shipSheet, shipAtlas.get(3, 0), player.facing,
		{1.f, 1.f, 1.f, cloak::shipAlpha()});

	// After the hull, so the rim reads as being in front of it.
	shield::draw(renderer, session.playerPos, shipSize, time.game);

#pragma endregion

#pragma region render bullets

	// Two passes, glows then sprites, rather than both per bullet. The batch
	// breaks a run wherever the blend mode changes, so doing it this way costs
	// two run breaks a frame instead of two per bullet -- and it is the right
	// layering anyway, since every glow belongs under every sprite.
	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	for (auto &b : session.bullets)
	{
		if (b.motion == BulletMotion::Missile)
		{
			// The ship's plume, small and green: out while the missile is
			// pushed clear, then brightening as it picks up speed.
			thruster::drawPlume(renderer, b.position, 130.f * b.size, b.fireDirection,
				weapons::missileThrottle(b), b.age, glm::vec4(0.30f, 0.85f, 0.35f, 1.f));
		}
		bulletLook::drawGlow(renderer, b.position, b.fireDirection, b.isEnemy, b.style, b.size);
	}
	if (beam.firing) { bulletLook::drawBeamGlow(renderer, beam.origin, beamEnd, beamHit, effectClock); }
	effects::drawGlow(renderer);
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	for (auto &b : session.bullets)
	{
		bulletLook::drawSprite(renderer, b.position, b.fireDirection, b.isEnemy, b.style, b.size);
	}
	if (beam.firing) { bulletLook::drawBeamCore(renderer, beam.origin, beamEnd, effectClock); }

#pragma endregion

#pragma region debug hitboxes

	hitboxDebug::draw(renderer, collisionSystem,
		game::shipHitbox(session.playerPos, shipSize), session.enemies, session.bullets);

#pragma endregion


#pragma region ui

	// The world's flush, routed through the cloak. Down, this is exactly
	// renderer.flush(); up, the world goes into a target and comes back
	// through the shader. hud::draw flushes again straight after, which is a
	// no-op on an empty batch.
	cloak::flushWorld(renderer, session.playerPos, shipSize, w, h, time.game);

	hud::WeaponSlot slots[weapons::slotCount];
	for (int s = 0; s < weapons::slotCount; s++)
	{
		const weapons::SlotView v = weapons::slot(s);
		slots[s] = {v.style, v.ready, v.ammo, v.maxAmmo, v.selected, v.usable};
	}

	// Flushes the world, then the HUD.
	hud::draw(renderer, session.health, energy::level(), slots, weapons::slotCount,
		ram::ready(), w, h);

#pragma endregion




	renderer.flush();
	

	//ImGui::ShowDemoWindow();

	// The panel holds nothing (roadmap R11): each feature draws its own
	// controls, and the panel only decides the order and the headings.
	ImGui::Begin("debug");

	debugPanel::renderStats();
	debugPanel::section("Session", sessionDebugUi);
	debugPanel::section("Sound", sfx::debugUi);
	debugPanel::section("Clock", gameClock::debugUi);
	debugPanel::section("Player", playerMove::debugUi);
	debugPanel::section("Energy", energy::debugUi);
	debugPanel::section("Weapons", weapons::debugUi);
	debugPanel::section("Explosions", effects::debugUi);
	debugPanel::section("Ram", ram::debugUi);
	debugPanel::section("Camera", zoomControl::debugUi);
	debugPanel::section("Enemies", enemyAi::debugUi);
	debugPanel::section("Hitboxes", hitboxDebug::debugUi);
	debugPanel::section("Shield", shield::debugUi);
	debugPanel::section("CRT", crt::debugUi);

	ImGui::End();


	return true;
#pragma endregion

}

//This function might not be be called if the program is forced closed
void closeGame()
{
	// Back up the table from wherever init got to. Safe to call twice.
	for (int i = startedFeatures - 1; i >= 0; i--)
	{
		if (features[i].cleanup) { features[i].cleanup(); }
	}
	startedFeatures = 0;

	// Loaded before the features, so released after them.
	shipSheet.cleanup();
	renderer.cleanup();
}
