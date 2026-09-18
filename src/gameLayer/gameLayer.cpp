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
	// After shield and cloak: its reset raises one and lowers the other.
	{"energy",     nullptr,          energy::reset,   nullptr},
};

// A setting, so it survives restart.
bool healthRegenEnabled = true;

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
	renderer.currentCamera.position = camera::follow(
		renderer.currentCamera.position, session.playerPos,
		{(float)renderer.windowW, (float)renderer.windowH},
		{550.f, 0.f, 0.f});
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
	const playerMove::Result player = playerMove::update(session.playerPos,
		session.playerVelocity, session.playerFacing, mouseDirection, time.game,
		energy::isCloaked());

#pragma endregion

#pragma region follow

	// Real time: the camera is presentation, and should keep settling while
	// the game is slowed.
	renderer.currentCamera.position = camera::follow(
		renderer.currentCamera.position, session.playerPos, {(float)w, (float)h},
		{time.real * 550.f, 1.f, 150.f});

#pragma endregion

#pragma region render background

	// Wall time, not game time: see zoomControl.h.
	renderer.currentCamera.zoom = zoomControl::update(time.real,
		{(float)w, (float)h}, enemyDespawnDistance);

	background::draw(renderer);
#pragma endregion

#pragma region handle bulets


	if (platform::isLMousePressed())
	{
		// Firing is how the player leaves the cloak, and the shot still goes out.
		energy::uncloak();

		Bullet b;

		b.position = session.playerPos;
		b.fireDirection = player.aim; // the mouse, not necessarily the hull

		session.bullets.push_back(b);

		sfx::playerShot();

	}


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
						session.enemies[e].life -= 0.1;

						if (session.enemies[e].life <= 0)
						{
							//kill enemy
							session.enemies.erase(session.enemies.begin() + e);
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

#pragma region render enemies

	for (auto &e : session.enemies)
	{
		renderSpaceShip(renderer, e.position, enemyShipSize,
			shipSheet, shipAtlas.get(e.type.x, e.type.y), e.viewDirection);
	}

#pragma endregion

#pragma region render ship

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
		bulletLook::drawGlow(renderer, b.position, b.fireDirection, b.isEnemy);
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	for (auto &b : session.bullets)
	{
		bulletLook::drawSprite(renderer, b.position, b.fireDirection, b.isEnemy);
	}

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

	hud::draw(renderer, session.health, energy::level(), w, h); // flushes the world, then the HUD

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
