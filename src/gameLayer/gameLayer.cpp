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
#include <tiledRenderer.h>
#include <shipSprite.h>
#include <bullet.h>
#include <vector>
#include <enemy.h>
#include <enemyAi.h>
#include <zoomControl.h>
#include <cstdio>
#include <raudio.h>
#include <engine/collisionSystem.h>
#include <shipHitbox.h>
#include <engine/cameraFollow.h>

struct GameplayData
{
	glm::vec2 playerPos = {100,100};

	std::vector<Bullet> bullets;

	std::vector<Enemy> enemies;

	float health = 1.f;

	float spawnEnemyTimerSecconds = 3;
};


GameplayData data;

collision::BasicCollisionSystem collisionSystem;

wgpu2d::Renderer2D renderer;

constexpr int BACKGROUNDS = 4;

wgpu2d::Texture spaceShipsTexture;
wgpu2d::TextureAtlasPadding spaceShipsAtlas;

wgpu2d::Texture bulletsTexture;
wgpu2d::TextureAtlasPadding bulletsAtlas;

wgpu2d::Texture backgroundTexture[BACKGROUNDS];
TiledRenderer tiledRenderer[BACKGROUNDS];


Sound shootSound;
bool soundEffectsEnabled = false;
float gameSpeedScale = 50.f;
constexpr float playerMoveSpeed = 2000.f;

float gameSpeedMultiplier()
{
	// 50 stays 1x. 100 is 1.5x. Below 50 uses an exponential curve so 0 is ~1% speed.
	if (gameSpeedScale <= 50.f)
	{
		float t = gameSpeedScale / 50.f;
		return 0.01f * glm::pow(100.f, t);
	}

	float t = (gameSpeedScale - 50.f) / 50.f;
	return 1.f + t * 0.5f;
}

void restartGame()
{
	data = {};
	// Zero dead zone and zero leash: snap straight onto the player.
	renderer.currentCamera.position = camera::follow(
		renderer.currentCamera.position, data.playerPos,
		{(float)renderer.windowW, (float)renderer.windowH},
		{550.f, 0.f, 0.f});
}

bool initGame()
{
	std::srand(std::time(0));

	//initializing stuff for the renderer
	wgpu2d::init();
	renderer.create();

	spaceShipsTexture.loadFromFileWithPixelPadding
	(RESOURCES_PATH "spaceShip/stitchedFiles/spaceships.png", 128, true);
	spaceShipsAtlas = wgpu2d::TextureAtlasPadding(5, 2, spaceShipsTexture.GetSize().x, spaceShipsTexture.GetSize().y);

	bulletsTexture.loadFromFileWithPixelPadding
	(RESOURCES_PATH "spaceShip/stitchedFiles/projectiles.png", 500, true);
	bulletsAtlas = wgpu2d::TextureAtlasPadding(3, 2, bulletsTexture.GetSize().x, bulletsTexture.GetSize().y);

	if (!hud::init()) { return false; }
	if (!thruster::init()) { return false; }
	if (!shield::init()) { return false; }
	if (!bulletLook::init()) { return false; }
	if (!cloak::init()) { return false; }
	if (!crt::init()) { return false; }

	shootSound = LoadSound(RESOURCES_PATH "shoot.flac");
	if (shootSound.stream.buffer == nullptr)
	{
		std::cerr << "AUDIO: failed to load " << RESOURCES_PATH "shoot.flac\n";
	}
	SetSoundVolume(shootSound, 1.0);

	backgroundTexture[0].loadFromFile(RESOURCES_PATH "background1.png", true);
	backgroundTexture[1].loadFromFile(RESOURCES_PATH "background2.png", true);
	backgroundTexture[2].loadFromFile(RESOURCES_PATH "background3.png", true);
	backgroundTexture[3].loadFromFile(RESOURCES_PATH "background4.png", true);

	tiledRenderer[0].texture = backgroundTexture[0];
	tiledRenderer[1].texture = backgroundTexture[1];
	tiledRenderer[2].texture = backgroundTexture[2];
	tiledRenderer[3].texture = backgroundTexture[3];

	tiledRenderer[0].paralaxStrength = 0;
	tiledRenderer[1].paralaxStrength = 0.2;
	tiledRenderer[2].paralaxStrength = 0.4;
	tiledRenderer[3].paralaxStrength = 0.7;

	restartGame();

	return true;
}


constexpr float shipSize = 250.f;

// Enemies further than this from the player are removed. Named because the
// zoom-out limit depends on it: past that zoom, the player would see it happen.
constexpr float enemyDespawnDistance = 4000.f;

// The controls that no feature owns yet, because the state they touch has no
// home yet either. Kept together and named so the panel holds nothing; each
// leaves when its milestone gives it somewhere to go:
//   game speed          -> R8, where the two clock conventions become one
//   health, reset, sound,
//   counts, spawn buttons -> R10, which decides who owns `data`, the assets,
//                          and what restart means
// The spawn-waves toggle went to enemyAi in R9. The button and the counts
// could not follow it: they need the enemy list, which is still `data`'s.
static void gameplayDebugUi()
{
	ImGui::Text("Bullets count: %d", (int)data.bullets.size());
	ImGui::Text("Enemies count: %d", (int)data.enemies.size());

	if (ImGui::Button("Spawn rusher"))
	{
		data.enemies.push_back(enemyAi::spawnNear(data.playerPos, Enemy::Behaviour::CloseIn));
	}
	ImGui::SameLine();
	if (ImGui::Button("Spawn sniper"))
	{
		data.enemies.push_back(enemyAi::spawnNear(data.playerPos, Enemy::Behaviour::KeepDistance));
	}
	ImGui::SameLine();
	if (ImGui::Button("Reset game"))
	{
		restartGame();
	}

	ImGui::SliderFloat("Player Health", &data.health, 0, 1);
	ImGui::SliderFloat("Game speed", &gameSpeedScale, 0, 100);

	if (ImGui::Checkbox("Sound effects", &soundEffectsEnabled))
	{
		if (!soundEffectsEnabled)
		{
			StopSound(shootSound);
		}
	}
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
#pragma endregion



#pragma region movement

	glm::vec2 move = {};

	if (
		platform::isButtonHeld(platform::Button::W) ||
		platform::isButtonHeld(platform::Button::Up)
		)
	{
		move.y = -1;
	}
	if (
		platform::isButtonHeld(platform::Button::S) ||
		platform::isButtonHeld(platform::Button::Down)
		)
	{
		move.y = 1;
	}
	if (
		platform::isButtonHeld(platform::Button::A) ||
		platform::isButtonHeld(platform::Button::Left)
		)
	{
		move.x = -1;
	}
	if (
		platform::isButtonHeld(platform::Button::D) ||
		platform::isButtonHeld(platform::Button::Right)
		)
	{
		move.x = 1;
	}

	const float playerThrottle = (move.x != 0 || move.y != 0) ? 1.f : 0.f;

	if (move.x != 0 || move.y != 0)
	{
		move = glm::normalize(move);
		move *= deltaTime * playerMoveSpeed * gameSpeedMultiplier();
		data.playerPos += move;
	}

#pragma endregion

#pragma region follow

	renderer.currentCamera.position = camera::follow(
		renderer.currentCamera.position, data.playerPos, {(float)w, (float)h},
		{deltaTime * 550.f, 1.f, 150.f});

#pragma endregion

#pragma region render background

	// Wall time, not game time: see zoomControl.h.
	renderer.currentCamera.zoom = zoomControl::update(deltaTime,
		{(float)w, (float)h}, enemyDespawnDistance);

	for (int i = 0; i < BACKGROUNDS; i++)
	{
		tiledRenderer[i].render(renderer);
	}
	//tiledRenderer[0].render(renderer);
#pragma endregion


#pragma region mouse pos

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

	float spaceShipAngle = atan2(mouseDirection.y, -mouseDirection.x);

#pragma endregion

#pragma region handle bulets


	if (platform::isLMousePressed())
	{
		Bullet b;

		b.position = data.playerPos;
		b.fireDirection = mouseDirection;

		data.bullets.push_back(b);

		if (soundEffectsEnabled)
		{
			PlaySound(shootSound);
		}

	}


	for (int i = 0; i < data.bullets.size(); i++)
	{
		
		if (glm::distance(data.bullets[i].position, data.playerPos) > 5'000)
		{
			data.bullets.erase(data.bullets.begin() + i);
			i--;
			continue;
		}

		if (!hitboxDebug::isDamageFrozen())
		{
			if (!data.bullets[i].isEnemy)
			{
				bool breakBothLoops = false;
				for (int e = 0; e < data.enemies.size(); e++)
				{

					if (collisionSystem.overlaps(data.bullets[i].getHitbox(),
						data.enemies[e].getHitbox()))
					{
						data.enemies[e].life -= 0.1;

						if (data.enemies[e].life <= 0)
						{
							//kill enemy
							data.enemies.erase(data.enemies.begin() + e);
						}

						data.bullets.erase(data.bullets.begin() + i);
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
				if (collisionSystem.overlaps(data.bullets[i].getHitbox(),
					game::shipHitbox(data.playerPos, shipSize)))
				{
					data.health -= 0.1;
					hud::onDamage();  // shake the HUD on the hit
					// Relative to the ship, because the shield moves with it and
					// the ripple has to stay anchored to the bubble.
					shield::hit(data.bullets[i].position - data.playerPos);

					data.bullets.erase(data.bullets.begin() + i);
					i--;
					continue;
				}

			}
		}

		data.bullets[i].update(deltaTime, gameSpeedMultiplier());

	}

	if (data.health <= 0)
	{
		//kill player
		restartGame();
	}
	else
	{
		data.health += deltaTime * 0.05;
		data.health = glm::clamp(data.health, 0.f, 1.f);
	}

#pragma endregion

#pragma region handle enemies

	// Game time, like the enemies themselves: at low game speed the waves slow
	// down with everything else instead of piling up at full rate.
	enemyAi::updateSpawning(data.enemies, data.spawnEnemyTimerSecconds,
		data.playerPos, deltaTime * gameSpeedMultiplier());


	for (int i = 0; i < data.enemies.size(); i++)
	{

		if (glm::distance(data.playerPos, data.enemies[i].position) > enemyDespawnDistance)
		{
			//dispawn enemy
			data.enemies.erase(data.enemies.begin() + i);
			i--;
			continue;
		}

		// Ship-ship (player vs enemy, enemy vs enemy) will use
		// collisionSystem.overlaps(hitboxA, hitboxB) and
		// collisionSystem.separation(circleA, circleB) to push them apart.

		if (enemyAi::update(data.enemies[i], deltaTime, data.playerPos, gameSpeedMultiplier()))
		{
			Bullet b;
			b.position = data.enemies[i].position;
			b.fireDirection = data.enemies[i].viewDirection;
			// The gun's, copied onto the shot. Flight reads Bullet::speed.
			b.speed = data.enemies[i].bulletSpeed;

			b.isEnemy = true;
			data.bullets.push_back(b);

			if (soundEffectsEnabled && !IsSoundPlaying(shootSound)) PlaySound(shootSound);

		}
	}

#pragma endregion

#pragma region render enemies

	for (auto &e : data.enemies)
	{
		renderSpaceShip(renderer, e.position, enemyShipSize,
			spaceShipsTexture, spaceShipsAtlas.get(e.type.x, e.type.y), e.viewDirection);
	}

#pragma endregion

#pragma region render ship

	// Before the hull, so the hull covers the end of the plume inside it.
	thruster::draw(renderer, data.playerPos, shipSize, mouseDirection,
		playerThrottle, deltaTime * gameSpeedMultiplier());

	// Faded by the cloak. The hull going nearly transparent is half the
	// effect; the other half is the world bending around it, which happens
	// below when the world goes through the cloak's shader.
	renderSpaceShip(renderer, data.playerPos, shipSize,
		spaceShipsTexture, spaceShipsAtlas.get(3, 0), mouseDirection,
		{1.f, 1.f, 1.f, cloak::shipAlpha()});

	// After the hull, so the rim reads as being in front of it.
	shield::draw(renderer, data.playerPos, shipSize, deltaTime * gameSpeedMultiplier());

#pragma endregion

#pragma region render bullets

	// Two passes, glows then sprites, rather than both per bullet. The batch
	// breaks a run wherever the blend mode changes, so doing it this way costs
	// two run breaks a frame instead of two per bullet -- and it is the right
	// layering anyway, since every glow belongs under every sprite.
	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	for (auto &b : data.bullets)
	{
		bulletLook::drawGlow(renderer, b.position, b.fireDirection, b.isEnemy);
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	for (auto &b : data.bullets)
	{
		bulletLook::drawSprite(renderer, b.position, b.fireDirection, b.isEnemy,
			bulletsTexture, bulletsAtlas);
	}

#pragma endregion

#pragma region debug hitboxes

	hitboxDebug::draw(renderer, collisionSystem,
		game::shipHitbox(data.playerPos, shipSize), data.enemies, data.bullets);

#pragma endregion


#pragma region ui

	// The world's flush, routed through the cloak. Down, this is exactly
	// renderer.flush(); up, the world goes into a target and comes back
	// through the shader. hud::draw flushes again straight after, which is a
	// no-op on an empty batch.
	cloak::flushWorld(renderer, data.playerPos, shipSize, w, h, deltaTime * gameSpeedMultiplier());

	hud::draw(renderer, data.health, w, h); // flushes the world, then the HUD

#pragma endregion




	renderer.flush();
	

	//ImGui::ShowDemoWindow();

	// The panel holds nothing (roadmap R11): each feature draws its own
	// controls, and the panel only decides the order and the headings.
	ImGui::Begin("debug");

	debugPanel::renderStats(deltaTime);
	debugPanel::section("Game", gameplayDebugUi);
	debugPanel::section("Camera", zoomControl::debugUi);
	debugPanel::section("Enemies", enemyAi::debugUi);
	debugPanel::section("Hitboxes", hitboxDebug::debugUi);
	debugPanel::section("Shield", shield::debugUi);
	debugPanel::section("Cloak", cloak::debugUi);
	debugPanel::section("CRT", crt::debugUi);

	ImGui::End();


	return true;
#pragma endregion

}

//This function might not be be called if the program is forced closed
void closeGame()
{
	hud::cleanup();
	thruster::cleanup();
	shield::cleanup();
	bulletLook::cleanup();
	cloak::cleanup();
}