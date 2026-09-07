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
#include <platformTools.h>
#include <tiledRenderer.h>
#include <bullet.h>
#include <vector>
#include <enemy.h>
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
bool spawnEnemiesEnabled = false;
bool showHitboxes = false;
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

void spanwEnemy() 
{
	glm::uvec2 shipTypes[] = {{0,0}, {0,1}, {2,0}, {3, 1}};

	Enemy e;
	e.position = data.playerPos;

	glm::vec2 offset(2000, 0);
	offset = glm::vec2(  glm::vec4(offset,0,1) * glm::rotate(glm::mat4(1.f), glm::radians((float)(rand()%360)), glm::vec3(0,0, 1))  );

	e.position += offset;

	e.speed = 800 + rand() % 1000;
	e.turnSpeed = 2.2f + (rand() & 1000) / 500.f;
	e.type = shipTypes[rand() % 4];
	e.fireRange = 1.5 + (rand() % 1000) / 2000.f;
	e.fireTimeReset = 0.1 + (rand() % 1000) / 500;
	e.bulletSpeed = rand() % 3000 + 1000;

	data.enemies.push_back(e);
}

bool gameLogic(float deltaTime)
{

#pragma region init stuff
	int w = 0; int h = 0;
	w = platform::getFrameBufferSizeX(); //window w
	h = platform::getFrameBufferSizeY(); //window h
	
	renderer.clearScreen({0, 0, 0, 1}); //clear screen (the pass's load op, applied at flush)

	renderer.updateWindowMetrics(w, h);
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

	renderer.currentCamera.zoom = 0.5;

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

		if (!showHitboxes)
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
					shield::hit();    // and flare the bubble, if it is up

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

	if (spawnEnemiesEnabled && data.enemies.size() < 15) 
	{
		data.spawnEnemyTimerSecconds -= deltaTime;

		if (data.spawnEnemyTimerSecconds < 0)
		{
			data.spawnEnemyTimerSecconds = rand() % 6 + 1;

			spanwEnemy();
			if (rand() % 3 == 0)
			{
				spanwEnemy();
				spanwEnemy();
			}

		}
	
	}


	for (int i = 0; i < data.enemies.size(); i++)
	{

		if (glm::distance(data.playerPos, data.enemies[i].position) > 4000.f)
		{
			//dispawn enemy
			data.enemies.erase(data.enemies.begin() + i);
			i--;
			continue;
		}

		// Ship-ship (player vs enemy, enemy vs enemy) will use
		// collisionSystem.overlaps(hitboxA, hitboxB) and
		// collisionSystem.separation(circleA, circleB) to push them apart.

		if (data.enemies[i].update(deltaTime, data.playerPos, gameSpeedMultiplier()))
		{
			Bullet b;
			b.position = data.enemies[i].position;
			b.fireDirection = data.enemies[i].viewDirection;
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
		e.render(renderer, spaceShipsTexture, spaceShipsAtlas);
	}

#pragma endregion

#pragma region render ship

	// Before the hull, so the hull covers the end of the plume inside it.
	thruster::draw(renderer, data.playerPos, shipSize, mouseDirection,
		playerThrottle, deltaTime * gameSpeedMultiplier());

	renderSpaceShip(renderer, data.playerPos, shipSize,
		spaceShipsTexture, spaceShipsAtlas.get(3, 0), mouseDirection);

	// After the hull, so the rim reads as being in front of it.
	shield::draw(renderer, data.playerPos, shipSize, deltaTime * gameSpeedMultiplier());

#pragma endregion

#pragma region render bullets

	for (auto &b : data.bullets)
	{
		b.render(renderer, bulletsTexture, bulletsAtlas);
	}

#pragma endregion

#pragma region debug hitboxes

	if (showHitboxes)
	{
		auto hitboxColor = [](bool overlapping) {
			return overlapping ? Colors_Red : Colors_Green;
		};

		const auto playerHitbox = game::shipHitbox(data.playerPos, shipSize);

		bool playerHit = false;
		for (auto &b : data.bullets)
		{
			if (b.isEnemy && collisionSystem.overlaps(b.getHitbox(), playerHitbox))
			{
				playerHit = true;
				break;
			}
		}
		renderer.renderCircleOutline(playerHitbox.center, hitboxColor(playerHit),
			playerHitbox.radius, 8.f, 32);

		for (auto &e : data.enemies)
		{
			const auto enemyHitbox = e.getHitbox();
			bool enemyHit = false;
			for (auto &b : data.bullets)
			{
				if (!b.isEnemy && collisionSystem.overlaps(b.getHitbox(), enemyHitbox))
				{
					enemyHit = true;
					break;
				}
			}
			renderer.renderCircleOutline(enemyHitbox.center, hitboxColor(enemyHit),
				enemyHitbox.radius, 8.f, 32);
		}

		for (auto &b : data.bullets)
		{
			const auto bulletHitbox = b.getHitbox();
			bool bulletHit = false;
			if (b.isEnemy)
			{
				bulletHit = collisionSystem.overlaps(bulletHitbox, playerHitbox);
			}
			else
			{
				for (auto &e : data.enemies)
				{
					if (collisionSystem.overlaps(bulletHitbox, e.getHitbox()))
					{
						bulletHit = true;
						break;
					}
				}
			}
			renderer.renderCircleOutline(bulletHitbox.center, hitboxColor(bulletHit),
				bulletHitbox.radius, 6.f, 16);
		}
	}

#pragma endregion


#pragma region ui

	hud::draw(renderer, data.health, w, h); // flushes the world, then the HUD

#pragma endregion




	renderer.flush();
	

	//ImGui::ShowDemoWindow();

	ImGui::Begin("debug");

	ImGui::Text("Bullets count: %d", (int)data.bullets.size());
	ImGui::Text("Enemies count: %d", (int)data.enemies.size());

	if (ImGui::Button("Spawn enemy"))
	{
		spanwEnemy();
	}

	if (ImGui::Button("Reset game"))
	{
		restartGame();
	}

	ImGui::SliderFloat("Player Health", &data.health, 0, 1);

	ImGui::Checkbox("Spawn enemies", &spawnEnemiesEnabled);

	ImGui::SliderFloat("Game speed", &gameSpeedScale, 0, 100);

	ImGui::Checkbox("Hitboxes", &showHitboxes);

	shield::debugUi(); // the feature owns its own controls (roadmap R11)

	if (ImGui::Checkbox("Sound effects", &soundEffectsEnabled))
	{
		if (!soundEffectsEnabled)
		{
			StopSound(shootSound);
		}
	}

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
}