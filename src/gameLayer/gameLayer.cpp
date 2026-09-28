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
#include <gameState.h>
#include <level.h>
#include <arena.h>
#include <scenery.h>
#include <levelEditor.h>
#include <resources.h>
#include <worldGrade.h>
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

	// Last frame's flying: what the ship shows while it is not being flown --
	// paused, or wreckage.
	playerMove::Result player;

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

// Enemies further than this from the player are removed -- in the endless
// mode only, with no level loaded. Named because the zoom-out limit depends on
// it: past that zoom, the player would see it happen.
constexpr float enemyDespawnDistance = 4000.f;

// ---- The level (gameplay roadmap L2) ----------------------------------------
//
// What was loaded, not what happened: restart builds the round from it, and
// nothing in a round changes it. With no file, the game is the endless mode it
// was before levels -- waves around the player, despawn ring and all.
const char *levelPath = RESOURCES_PATH "levels/level1.txt";
level::Level currentLevel;
bool levelLoaded = false;

// Enemies outside the view, grown by this fraction of its size on every side,
// sleep: no update at all, so they stay exactly where they are. Engaged or
// searching enemies stay awake wherever they are, so a chase does not freeze
// just off screen.
float wakeMargin = 0.25f;
bool markersVisible = true;
bool sceneryVisible = true;
int awakeEnemies = 0; // last frame's, for the panel

void loadLevel()
{
	levelLoaded = level::load(levelPath, currentLevel);
	if (!levelLoaded)
	{
		currentLevel = {};
		std::cerr << "level: no " << levelPath << ", playing the endless mode\n";
	}
}

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
	{"worldGrade", worldGrade::init, nullptr,         worldGrade::cleanup},
	{"crt",        crt::init,        nullptr,         crt::cleanup},
	{"background", background::init, nullptr,         background::cleanup},
	{"scenery",    scenery::init,    nullptr,         scenery::cleanup},
	// Its reset needs the level, so the game does it in restartGame instead.
	{"resources",  resources::init,  nullptr,         resources::cleanup},
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

// `startAt`, when given, overrides the level's start: the editor's "test from
// here".
// Leaving with the haul: the hold becomes points, then the ship warps out.
// One place, so L5's gate does exactly what the debug button does.
void startExtraction()
{
	if (gameState::current() != gameState::State::Playing) { return; }
	resources::extracted();
	gameState::extract();
}

void restartGame(const glm::vec2 *startAt = nullptr)
{
	session = {};

	// The edge, and the closing circle starting over (gameplay roadmap L4).
	if (levelLoaded) { arena::start(currentLevel.arenaRadius, currentLevel.rings); }
	else { arena::start(0.f, {}); }
	if (levelLoaded)
	{
		session.playerPos = currentLevel.start;
		session.playerFacing = level::direction(currentLevel.startFacingDegrees);
		for (const level::EnemyPlacement &p : currentLevel.enemies)
		{
			session.enemies.push_back(enemyAi::spawnAt(p.position,
				level::direction(p.facingDegrees), p.behaviour));
		}
	}
	if (startAt) { session.playerPos = *startAt; }

	// What there is to mine this round (gameplay roadmap L3). Points banked by
	// extracting are not a round's and survive.
	resources::reset(currentLevel.resources);
	// Hit-stop is this round's freeze, not a feature row: the table is GPU,
	// audio, and gameplay modules. The speed slider is a setting and stays.
	gameClock::reset();

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
	effects::enemyKilled(e, shipSheet, shipAtlas.get(e.type.x, e.type.y));
	resources::enemyDropped(e.position); // fragments among the wreckage (L3)
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
		gameState::reset(); // straight back, no transition
		restartGame();
	}
	ImGui::SameLine();
	// Until the gate exists (gameplay roadmap L5).
	if (ImGui::Button("Extract"))
	{
		startExtraction();
	}

	ImGui::SliderFloat("Player Health", &session.health, 0, 1);
	ImGui::Text("Player at %.0f, %.0f", session.playerPos.x, session.playerPos.y);
	ImGui::Checkbox("Health regen", &healthRegenEnabled);
}

void levelDebugUi()
{
	if (levelLoaded)
	{
		ImGui::Text("%s", levelPath);
		ImGui::Text("%d enemies placed, %d awake", (int)currentLevel.enemies.size(), awakeEnemies);
	}
	else
	{
		ImGui::TextDisabled("No level: endless mode");
	}
	if (ImGui::Button("Reload level"))
	{
		loadLevel();
		levelEditor::clearChanged();
		gameState::reset();
		restartGame();
	}
	ImGui::SameLine();
	if (ImGui::Button("Edit level"))
	{
		// No file yet: the editor starts a new level, saved on first Save.
		levelLoaded = true;
		const glm::vec4 view = renderer.getViewRect();
		levelEditor::open(glm::vec2(view.x, view.y) + glm::vec2(view.z, view.w) * 0.5f,
			renderer.currentCamera.zoom);
	}
	ImGui::SliderFloat("Wake margin", &wakeMargin, 0.f, 2.f, "%.2f of view");
	ImGui::Checkbox("Level markers", &markersVisible);
	ImGui::SameLine();
	ImGui::Checkbox("Scenery", &sceneryVisible);
	arena::debugUi();
}

void drawMarkers(float zoom)
{
	if (!levelLoaded || !markersVisible) { return; }
	levelEditor::drawMarkers(currentLevel, renderer, zoom);
}

}

// The editor's panel, and what it asks for. The game does the file and the
// round; the editor only edits the level.
void editorDebugUi()
{
	switch (levelEditor::debugUi(currentLevel, levelEditor::changed()))
	{
	case levelEditor::Request::Save:
		if (level::save(levelPath, currentLevel)) { levelEditor::clearChanged(); }
		else { std::cerr << "level: could not write " << levelPath << "\n"; }
		break;
	case levelEditor::Request::Reload:
		loadLevel();
		levelEditor::clearChanged();
		break;
	case levelEditor::Request::Exit:
		levelEditor::close();
		gameState::reset();
		restartGame();
		break;
	case levelEditor::Request::TestHere:
	{
		const glm::vec2 here = levelEditor::cameraCentre();
		levelEditor::close();
		gameState::reset();
		restartGame(&here);
		break;
	}
	default: break;
	}
}

// The panel holds nothing (roadmap R11): each feature draws its own
// controls, and the panel only decides the order and the headings.
void debugPanelUi()
{
	ImGui::Begin("debug");

	debugPanel::renderStats();
	if (levelEditor::active()) { debugPanel::section("Editor", editorDebugUi); }
	debugPanel::section("Session", sessionDebugUi);
	debugPanel::section("State", gameState::debugUi);
	debugPanel::section("Level", levelDebugUi);
	debugPanel::section("Resources", resources::debugUi);
	debugPanel::section("World grade", worldGrade::debugUi);
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
}

// A frame of the editor instead of the game (gameplay roadmap L2b). The round
// is not simulated at all -- no clock, no state, no enemies -- and the level is
// drawn as data over the same backdrop the game uses.
void editorFrame(float deltaTime, int w, int h)
{
	crt::setTransition(0.f, 0.f);
	crt::apply();

	// Only the edge: the editor draws the rings itself, all at once.
	arena::start(currentLevel.arenaRadius, {});
	levelEditor::update(currentLevel, renderer, platform::getRelMousePosition(), w, h, deltaTime);

	background::draw(renderer);
	if (sceneryVisible) { scenery::draw(renderer, currentLevel.scenery); }

	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	arena::draw(renderer, renderer.currentCamera.zoom);
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	levelEditor::Look look;
	look.shipSheet = shipSheet;
	look.playerCell = shipAtlas.get(3, 0);
	look.rusherCell = shipAtlas.get(0, 0);
	look.sniperCell = shipAtlas.get(2, 0);
	look.shipSize = shipSize;
	look.enemySize = enemyShipSize;
	levelEditor::draw(currentLevel, renderer, look);

	renderer.flush();
	debugPanelUi();
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

	loadLevel();
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

	if (levelEditor::active())
	{
		editorFrame(deltaTime, w, h);
		return true;
	}

	// Where the round is (gameplay roadmap L1), before the clock is read and
	// before the CRT is set, because both follow it. A restart happens here,
	// at the top of a frame, while the transition has the screen covered.
	{
		const bool escape = !ImGui::GetIO().WantCaptureKeyboard
			&& platform::isButtonPressedOn(platform::Button::Escape);
		if (gameState::update(deltaTime, {escape, platform::isFocused()}))
		{
			restartGame();
		}
		gameClock::setPaused(gameState::paused());
		crt::setTransition(gameState::switchOff(), gameState::whiteOut());
	}
	const bool controls = gameState::controlsLive();

	// Before anything is drawn: setting this is what routes the frame through
	// a target, and the target has to exist before the first quad lands.
	crt::apply();

	// The two clocks, named once (roadmap R8). The simulation takes
	// `time.game`; camera, zoom and the panel take `time.real`.
	const FrameTime time = gameClock::tick(deltaTime);

	// The closing circle runs on game time, so a pause holds it too.
	arena::update(time.game);
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
	// Not during a ram: the ram uncloaks on start, and E would otherwise
	// cloak again while the prow is out -- invulnerable and still striking.
	if (controls && platform::isButtonPressedOn(platform::Button::E) && !ram::barrierUp())
	{
		energy::cloak();
	}
	energy::update(time.game);

#pragma endregion

#pragma region movement

	// Facing is the hull; aim is the gun. They are the same vector unless the
	// ship is turned with A/D, when the mouse aims independently.
	// The ram, before flying: Space starts it toward the mouse, and ramming
	// uncloaks, as firing does (gameplay roadmap C4b).
	// Out of control -- paused, dying, leaving -- a wind-up keeps the heading
	// it had rather than following the mouse.
	ram::update(time.game, controls ? mouseDirection : ram::direction());
	if (controls && !ImGui::GetIO().WantCaptureKeyboard
		&& platform::isButtonPressedOn(platform::Button::Space)
		&& ram::tryStart(mouseDirection))
	{
		energy::uncloak();
	}
	shield::setRam(ram::barrierLevel(), ram::direction());

	playerMove::Result player;
	if (gameState::current() == gameState::State::Extracting)
	{
		// The warp: straight out along the heading, faster every moment, the
		// ram's afterimages and streaks behind it. The camera holds (below),
		// so the ship leaves the screen before it goes white.
		session.playerVelocity = session.playerFacing * gameState::warpSpeed();
		session.playerPos += session.playerVelocity * time.game;
		player = session.player;
		player.facing = session.playerFacing;
		player.throttle = 1.f;

		effects::ramTrail(session.playerPos, session.playerFacing, shipSize,
			shipAtlas.get(3, 0), time.game);
	}
	else if (!controls)
	{
		// Paused, the hull holds still -- playerMove would snap it to the
		// mouse even with no time passing. Dying, it is not drawn.
		player = session.player;
	}
	else if (ram::windingUp())
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
	session.player = player;

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
			// Engaged first -- which turns it to face the player -- then the
			// spin takes over until the stun runs out.
			enemyAi::alert(enemy, session.playerPos);
			enemyAi::stun(enemy, away * (ram::surgeSpeed() + ram::knockbackSpeed()),
				ram::stunSeconds());
		}
	}

#pragma endregion

#pragma region follow

	// Real time: the camera is presentation, and should keep settling while
	// the game is slowed. It holds during the warp, so the ship leaves it.
	if (gameState::current() != gameState::State::Extracting)
	{
		cameraBase = camera::follow(
			cameraBase, session.playerPos, {(float)w, (float)h},
			{time.real * 550.f, 1.f, 150.f});
	}

	// The world shake rides on top: the whole world moves, background and all,
	// and the HUD, drawn with its own screen camera, stays still.
	renderer.currentCamera.position = cameraBase + effects::shakeOffset(time.real)
		+ ram::cameraLean(); // and leans ahead while ramming

#pragma endregion

#pragma region render background

	// Wall time, not game time: see zoomControl.h.
	// With a level, nothing is removed for distance, so no ring bounds the zoom.
	renderer.currentCamera.zoom = zoomControl::update(time.real,
		{(float)w, (float)h}, levelLoaded ? 0.f : enemyDespawnDistance);

	background::draw(renderer);
	if (levelLoaded && sceneryVisible) { scenery::draw(renderer, currentLevel.scenery); }
#pragma endregion

#pragma region handle bulets


	// Only while playing: paused, the selection holds like everything else.
	if (controls) { weapons::handleInput(); }

	// Held, not clicked: the selected weapon fires whenever it is ready.
	// Clicks on the debug panel are the panel's.
	const bool trigger = controls && platform::isLMouseHeld() && !ImGui::GetIO().WantCaptureMouse;
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
	// Paused, not at all: a weapon that is ready fires whatever the clock
	// says, and the beam should stay on screen as it was, not switch off.
	const int shots = gameState::paused() ? 0
		: weapons::update(time.game, trigger, fire, session.bullets);
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
		if (beam.started && !gameState::paused()) { sfx::playerShot(); }

		float reach = distanceToViewEdge(beam.origin, beam.direction, view);
		int target = -1;
		for (int e = 0; e < (int)session.enemies.size(); e++)
		{
			const float t = collision::rayToCircle(beam.origin, beam.direction,
				session.enemies[e].getHitbox());
			if (t >= 0.f && t < reach) { reach = t; target = e; }
		}

		// A deposit stops the beam as an enemy does, and being burned is how
		// it is mined (gameplay roadmap L3). Nearer than the enemy, the ore
		// takes the beam and the enemy behind it is spared.
		const float ore = resources::rayToDeposit(beam.origin, beam.direction, reach);
		bool miningNow = false;
		if (ore >= 0.f)
		{
			reach = ore;
			target = -1;
			miningNow = true;
		}

		beamEnd = beam.origin + beam.direction * reach;
		beamHit = target >= 0 || miningNow;

		if (miningNow) { resources::mine(beam.origin, beam.direction, time.game); }

		if (target >= 0 && !hitboxDebug::isDamageFrozen())
		{
			session.enemies[target].life -= beam.damagePerSecond * time.game;
			if (session.enemies[target].life <= 0.f)
			{
				killEnemy(target);
			}
			else
			{
				enemyAi::alert(session.enemies[target], session.playerPos);
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
						else
						{
							// Hit, it knows: engaged, turned toward the shooter.
							enemyAi::alert(session.enemies[e], session.playerPos);
						}

						session.bullets.erase(session.bullets.begin() + i);
						i--;
						breakBothLoops = true;
						break;
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
				// Not once it is wreckage or leaving: those shots fly on.
				if (gameState::playerPresent() && !energy::isCloaked() &&
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
						resources::interrupt(); // and the drill loses its hold
					}

					session.bullets.erase(session.bullets.begin() + i);
					i--;
					continue;
				}

			}
		}

		session.bullets[i].update(time.game);

	}

	// Outside the closing circle the hull burns, past the shield and the cloak
	// (gameplay roadmap L4). Not a hit: the drill keeps its hold, so mining
	// out there is allowed and paid for in health. When the last ring has
	// closed to nothing, whoever is still here is done.
	if (controls)
	{
		const float burn = arena::burn(session.playerPos, time.game);
		if (burn > 0.f)
		{
			session.health -= burn;
			hud::onDamage(0.5f);
		}
		if (arena::collapsed()) { session.health = 0.f; }
	}

	if (session.health <= 0 && controls)
	{
		// The ship goes the way enemies do, and the world runs on around the
		// wreck until gameState switches the picture off (gameplay roadmap L1).
		effects::shipDestroyed(shipSheet, shipAtlas.get(3, 0), session.playerPos,
			session.playerFacing, session.playerVelocity, shipSize);
		effects::shake(1.f);
		session.playerVelocity = {};
		resources::playerDropped(session.playerPos); // the hold spills at the wreck
		energy::uncloak();
		ram::reset();
		weapons::reset(); // no burst's second shot from the wreck
		gameState::playerDied();
	}
	else if (gameState::playerPresent())
	{
		// Game time. This was the frame's own delta, so at 1% speed the ship
		// healed at full rate while everything shooting at it crawled.
		// Not while burning, or regen would cancel most of it.
		if (healthRegenEnabled && !arena::outside(session.playerPos)) { session.health += time.game * 0.05; }
		session.health = glm::clamp(session.health, 0.f, 1.f);
	}

#pragma endregion

#pragma region handle enemies

	// Waves are the endless mode's; a level places its enemies.
	if (!levelLoaded)
	{
		enemyAi::updateSpawning(session.enemies, session.spawnEnemyTimerSecconds,
			session.playerPos, time.game);
	}

	// The view, grown by the wake margin: inside it, enemies are awake.
	glm::vec4 wakeRect = renderer.getViewRect();
	wakeRect.x -= wakeRect.z * wakeMargin;
	wakeRect.y -= wakeRect.w * wakeMargin;
	wakeRect.z *= 1.f + 2.f * wakeMargin;
	wakeRect.w *= 1.f + 2.f * wakeMargin;
	awakeEnemies = 0;

	for (int i = 0; i < session.enemies.size(); i++)
	{

		if (!levelLoaded
			&& glm::distance(session.playerPos, session.enemies[i].position) > enemyDespawnDistance)
		{
			//dispawn enemy
			session.enemies.erase(session.enemies.begin() + i);
			i--;
			continue;
		}

		if (levelLoaded)
		{
			const Enemy &e = session.enemies[i];
			const bool inView = e.position.x >= wakeRect.x && e.position.x <= wakeRect.x + wakeRect.z
				&& e.position.y >= wakeRect.y && e.position.y <= wakeRect.y + wakeRect.w;
			// Asleep -- unless the closing circle has passed it, which wakes it
			// to fly back in (gameplay roadmap L4).
			if (!inView && e.awareness == Enemy::Awareness::Unaware && !arena::outside(e.position))
			{
				continue;
			}
		}
		awakeEnemies++;

		// Outside the closing circle enemies burn as the player does, fighting
		// or not, and a burn that finishes one is a kill like any other.
		session.enemies[i].life -= arena::burnEnemy(session.enemies[i], time.game);
		if (session.enemies[i].life <= 0.f)
		{
			killEnemy(i);
			i--;
			continue;
		}

		// Ship-ship (player vs enemy, enemy vs enemy) will use
		// collisionSystem.overlaps(hitboxA, hitboxB) and
		// collisionSystem.separation(circleA, circleB) to push them apart.

		// Cloaked, the player is in no enemy's sight (gameplay roadmap C5).
		// Wreckage or leaving, the player is as gone as cloaked.
		glm::vec2 wayIn;
		const bool comingBack = arena::wayBackIn(session.enemies[i].position, wayIn);
		if (enemyAi::update(session.enemies[i], time.game, session.playerPos,
			energy::isCloaked() || !gameState::playerPresent(), comingBack ? &wayIn : nullptr))
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
	resources::update(time.game, session.playerPos, gameState::playerPresent());

#pragma region render enemies

	// What each enemy can see, under the ships (gameplay roadmap C5). A debug
	// toggle, on by default.
	if (enemyAi::showCones())
	{
		renderer.setBlendMode(wgpu2d::BlendMode::Additive);
		for (const auto &e : session.enemies) { effects::drawSight(renderer, e); }
		renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	}

	for (auto &e : session.enemies)
	{
		renderSpaceShip(renderer, e.position, enemyShipSize,
			shipSheet, shipAtlas.get(e.type.x, e.type.y), e.viewDirection);
	}

	// What each knows: red engaged, amber searching.
	for (const auto &e : session.enemies) { effects::drawAwareness(renderer, e, effectClock); }

	// Deposits sit in the world like ships do, under the wrecks.
	resources::draw(renderer);

	// Wrecks sit where ships sit: after them, under everything else.
	effects::drawDebris(renderer);

	drawMarkers(renderer.currentCamera.zoom);

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

	// Dying, the ship is its debris, drawn with the wrecks.
	if (gameState::current() != gameState::State::Dying)
	{
		// Before the hull, so the hull covers the end of the plume inside it.
		thruster::draw(renderer, session.playerPos, shipSize, player.facing,
			player.throttle, time.game);

		// Faded by the cloak. The hull going nearly transparent is half the
		// effect; the other half is the world bending around it, which happens
		// below when the world goes through the cloak's shader. Stretched
		// along its heading while it warps out.
		renderSpaceShip(renderer, session.playerPos, shipSize,
			shipSheet, shipAtlas.get(3, 0), player.facing,
			{1.f, 1.f, 1.f, cloak::shipAlpha()}, gameState::warpStretch());

		// After the hull, so the rim reads as being in front of it. Not while
		// warping: the bubble does not stretch with the hull.
		if (gameState::playerPresent())
		{
			shield::draw(renderer, session.playerPos, shipSize, time.game);
		}
	}

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
	resources::drawGlow(renderer, effectClock);
	arena::draw(renderer, renderer.currentCamera.zoom);
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
	//
	// Paused, the world is graded grey and dim first. The grade is per pixel
	// and the cloak only moves pixels, so grading before bending is the same
	// picture as after -- see worldGrade.h.
	// And outside the closing circle -- or the edge, if nothing closes -- the
	// world is grey, so being out there is seen wherever the ring is.
	const zone::Circle safe = arena::safeZone();
	worldGrade::apply(renderer, gameState::pauseLook(), arena::radius() > 0.f ? &safe : nullptr, w, h);

	// Burn ticks flash hulls red, drawn over the grade so the red survives it.
	// Still in the world's batch, so the cloak bends them with the rest.
	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	for (const Enemy &e : session.enemies)
	{
		arena::drawBurnFlash(renderer, e.burnFlash, e.position, enemyShipSize,
			shipSheet, shipAtlas.get(e.type.x, e.type.y), e.viewDirection);
	}
	if (gameState::current() != gameState::State::Dying)
	{
		arena::drawBurnFlash(renderer, arena::burnFlash(), session.playerPos, shipSize,
			shipSheet, shipAtlas.get(3, 0), session.player.facing, cloak::shipAlpha(),
			gameState::warpStretch());
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
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

	debugPanelUi();


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
