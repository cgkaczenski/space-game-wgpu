#define GLM_ENABLE_EXPERIMENTAL
#include "gameLayer.h"
#include <glm/glm.hpp>
#include <glm/gtx/transform.hpp>
#include "platformInput.h"
#include "imgui.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstdio>
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
#include <gate.h>
#include <asteroids.h>
#include <outline.h>
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
	// The player's ship: the same kind of body as every enemy's, moved by the
	// same movement::step (gameplay roadmap P1). playerMove turns the keys
	// into its intent.
	movement::Body ship = []
	{
		movement::Body b;
		b.position = {100, 100};
		return b;
	}();
	// Where the player's shots go -- the mouse, not necessarily the hull --
	// kept from the last frame flown, for frames that are not.
	glm::vec2 aim = {1, 0};

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

// The player's weapons (gameplay roadmap B1): a loadout like every enemy's,
// but not part of the Session -- what is in it is tuned in the debug panel
// and chosen with 1-4, and both survive a restart. A new round only resets
// its cooldowns, ammo and charge (weapons::reset).
weapons::Loadout playerWeapons = weapons::playersLoadout();
const glm::vec4 enemyPlumeColour = {1.f, 0.45f, 0.18f, 1.f}; // the player's is blue

// Enemies further than this from the player are removed -- in the endless
// mode only, with no level loaded. Named because the zoom-out limit depends on
// it: past that zoom, the player would see it happen.
constexpr float enemyDespawnDistance = 4000.f;

// ---- The level (gameplay roadmap L2) ----------------------------------------
//
// What was loaded, not what happened: restart builds the round from it, and
// nothing in a round changes it. With no file, the game is the endless mode it
// was before levels -- waves around the player, despawn ring and all.
// Which file is the level. The debug panel can load another, or make a new
// empty one, and switch back; the editor's Save writes to whichever this is.
const std::string levelsDirectory = RESOURCES_PATH "levels/";
std::string levelFile = "level1.txt";
std::string levelPath = levelsDirectory + levelFile;

// The last level played, so the next launch starts there instead of on
// level1. Beside imgui.ini, in the working directory, and gitignored the same
// way: it is this machine's, not the game's.
const char *lastLevelRecord = "lastLevel.cfg";

void rememberLevel()
{
	std::ofstream out(lastLevelRecord);
	if (out) { out << levelFile << "\n"; }
}

// At launch: the recorded level, if it still exists; otherwise level1.
void recallLevel()
{
	std::ifstream in(lastLevelRecord);
	std::string file;
	if (!(in >> file)) { return; }
	const std::vector<std::string> files = level::list(levelsDirectory);
	if (std::find(files.begin(), files.end(), file) == files.end()) { return; }
	levelFile = file;
	levelPath = levelsDirectory + file;
}
level::Level currentLevel;
bool levelLoaded = false;

// Enemies outside the view, grown by this fraction of its size on every side,
// sleep: no update at all, so they stay exactly where they are. Engaged or
// searching enemies stay awake wherever they are, so a chase does not freeze
// just off screen.
float wakeMargin = 0.25f;
// Ships bump (gameplay roadmap P1): how much of the closing speed comes back
// apart. 0 they stop together, 1 a perfect bounce.
float shipBounce = 0.4f;
// A bump between the player and an enemy hurts both, a little: the player's
// shield blocks it as it blocks a shot; enemies have none yet (B1). Only a
// real impact -- closing faster than `bumpMinSpeed` -- and once per
// `bumpGrace`, so ships resting against each other do not grind each other
// down. Enemy against enemy only bumps.
float bumpDamage = 0.05f;        // to the player's hull
float bumpEnemyDamage = 0.05f;   // to the enemy's life (1 at full)
float bumpMinSpeed = 200.f;      // units per second of closing speed
float bumpGrace = 0.5f;          // seconds

// ---- Bumps and intangibility: the convention --------------------------------
//
// A ship either bumps into things or passes through them, and one flag says
// which: `movement::Body::solid`. Cloaked, the player's ship is not solid --
// it passes through enemies and through asteroid field cores, and nothing it
// passes feels it.
//
// To keep that true as the game grows, two rules:
//
//   1. Something that makes a ship intangible clears that ship's `solid`, and
//      does nothing else about bumps. The cloak does it in syncSolid() below;
//      a boss that cloaks (gameplay roadmap B2) would clear its own body's.
//
//   2. Every bump respects `solid`, one of two ways:
//      - Body against Body: go through movement::collide. It already ignores
//        a pair where either side is not solid, so the call site adds no check.
//      - Anything that is not a Body -- a field's core today; a wall, a mine
//        or a gate's rim tomorrow -- checks `body.solid` itself before it
//        pushes or hurts the ship. See the core contacts below.
//
// So a new bump never tests the cloak, and a new kind of intangibility never
// touches a bump: they meet only at `solid`. Grep for `.solid` to find every
// place that honours it.
//
// What `solid` does not cover: shots, the beam, missiles and blasts are not
// bumps. Whether those reach a cloaked ship is energy::onHit's business.

// The player's ship is solid unless cloaked. Called before each pass that can
// bump the ship, because the cloak can drop partway through a frame -- firing
// uncloaks.
void syncSolid()
{
	session.ship.solid = !energy::isCloaked();
}
bool sceneryVisible = true;
int awakeEnemies = 0; // last frame's, for the panel

// Enemy beams (gameplay roadmap B1): an enemy that rolled the laser burns with
// it. Traced in the enemy loop, drawn with the bullets -- this frame's, so
// rebuilt every frame rather than kept with the round.
struct EnemyBeam
{
	glm::vec2 start = {}, end = {};
	bulletLook::BeamImpact impact = bulletLook::BeamImpact::None;
	glm::vec2 surface = {}; // outward, where a deflected beam meets what stops it
};
std::vector<EnemyBeam> enemyBeams;
// How far an enemy's beam reaches: the player's runs to the edge of the
// player's view, which means nothing for an enemy -- this is about a sniper's
// sight.
float enemyBeamRange = 3000.f;
// A beam burning the hull shakes the HUD this often, not every frame.
float enemyBeamShakeSeconds = 0.25f;
float enemyBeamShakeLeft = 0.f;

void loadLevel()
{
	levelLoaded = level::load(levelPath.c_str(), currentLevel);
	if (levelLoaded)
	{
		std::cout << "level: " << levelFile << ", " << currentLevel.enemies.size() << " enemies\n" << std::flush;
	}
	else
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
	// Its start needs the level, so the game does it in restartGame instead.
	{"gate",       gate::init,       nullptr,         gate::cleanup},
	// Its reset needs the level too.
	{"asteroids",  asteroids::init,  nullptr,         asteroids::cleanup},
	{"outline",    outline::init,    nullptr,         outline::cleanup},
	// Its reset needs the level, so the game does it in restartGame instead.
	{"resources",  resources::init,  nullptr,         resources::cleanup},
	{"sfx",        sfx::init,        nullptr,         sfx::cleanup},
	{"effects",    effects::init,    effects::reset,  effects::cleanup},
	{"ram",        nullptr,          ram::reset,      nullptr},
	// After shield and cloak: its reset raises one and lowers the other.
	{"energy",     nullptr,          energy::reset,   nullptr},
	{"weapons",    nullptr,          [] { weapons::reset(playerWeapons); }, nullptr},
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
		session.ship.position = currentLevel.start;
		session.ship.facing = level::direction(currentLevel.startFacingDegrees);
		for (const level::EnemyPlacement &p : currentLevel.enemies)
		{
			session.enemies.push_back(enemyAi::spawnAt(p.position,
				level::direction(p.facingDegrees), p.behaviour, p.weapon));
		}
	}
	if (startAt) { session.ship.position = *startAt; }

	// The way out (gameplay roadmap L5): the level's first gate, if it has one.
	{
		const level::Marker *g = nullptr;
		for (const level::Marker &m : currentLevel.markers)
		{
			if (m.kind == level::Marker::Kind::Gate) { g = &m; break; }
		}
		gate::start(levelLoaded && g, g ? g->position : glm::vec2{});
	}

	// What there is to mine this round (gameplay roadmap L3). Points banked by
	// extracting are not a round's and survive.
	resources::reset();
	asteroids::reset(currentLevel.asteroids, currentLevel.fields); // gameplay roadmap A1, A1b
	// Hit-stop is this round's freeze, not a feature row: the table is GPU,
	// audio, and gameplay modules. The speed slider is a setting and stays.
	gameClock::reset();

	for (const Feature &feature : features)
	{
		if (feature.reset) { feature.reset(); }
	}

	// Zero dead zone and zero leash: snap straight onto the player.
	cameraBase = camera::follow(
		cameraBase, session.ship.position,
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
	asteroids::blast(e.body.position); // the blast shoves rocks near it (A2)
	resources::enemyDropped(e.body.position); // fragments among the wreckage (L3)
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
	ImGui::Text("Player at %.0f, %.0f", session.ship.position.x, session.ship.position.y);
	ImGui::Checkbox("Health regen", &healthRegenEnabled);
}

// Everything about enemies in one place: how many there are, spawning one to
// test against, and each class's tuning (enemyAi). Placed enemies -- kind,
// position, facing, weapon, beside their class's tuning -- are the editor's.
void enemiesDebugUi()
{
	ImGui::Text("%d enemies, %d awake", (int)session.enemies.size(), awakeEnemies);
	if (ImGui::Button("Spawn rusher"))
	{
		session.enemies.push_back(enemyAi::spawnNear(session.ship.position, Enemy::Behaviour::CloseIn));
	}
	ImGui::SameLine();
	if (ImGui::Button("Spawn sniper"))
	{
		session.enemies.push_back(enemyAi::spawnNear(session.ship.position, Enemy::Behaviour::KeepDistance));
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(random weapon)");
	enemyAi::debugUi();
}

// Switches to `file` in the levels folder and starts a round in it. The
// editor's edits to the old level are gone -- the panel says so first.
void switchLevel(const std::string &file)
{
	levelFile = file;
	levelPath = levelsDirectory + file;
	rememberLevel();
	loadLevel();
	levelEditor::clearChanged();
	gameState::reset();
	restartGame();
}

// A new level's name: letters, digits, - and _, so it is a safe file name.
bool validLevelName(const std::string &name)
{
	if (name.empty()) { return false; }
	for (char c : name)
	{
		if (!std::isalnum((unsigned char)c) && c != '-' && c != '_') { return false; }
	}
	return true;
}

// Loading another level, or making a new empty one (gameplay roadmap U3,
// ahead of the menu): a test bench for placing enemies with the editor or
// the spawn buttons.
void levelFilesUi()
{
	static std::vector<std::string> files = level::list(levelsDirectory);
	static int chosen = -1;
	static std::string followed; // the level `chosen` was last set to follow
	static char newName[64] = "";

	const bool unsaved = levelEditor::changed();
	if (unsaved) { ImGui::TextColored({1.f, 0.7f, 0.2f, 1.f}, "Unsaved edits: loading discards them"); }

	if (ImGui::Button("Refresh")) { files = level::list(levelsDirectory); }
	ImGui::SameLine();
	// Follow the current level, so the list opens on it -- but only when the
	// level changes. Re-syncing whenever the pick differed from the loaded
	// level undid every pick on the next frame, before Load could see it.
	if (followed != levelFile || chosen >= (int)files.size())
	{
		followed = levelFile;
		chosen = (int)(std::find(files.begin(), files.end(), levelFile) - files.begin());
		if (chosen >= (int)files.size()) { chosen = -1; }
	}
	ImGui::SetNextItemWidth(160.f);
	if (ImGui::BeginCombo("##levelFile", chosen >= 0 ? files[chosen].c_str() : "(none)"))
	{
		for (int i = 0; i < (int)files.size(); i++)
		{
			if (ImGui::Selectable(files[i].c_str(), i == chosen)) { chosen = i; }
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	if (ImGui::Button(unsaved ? "Load (discard edits)" : "Load level") && chosen >= 0)
	{
		switchLevel(files[chosen]);
		return; // `files` is still good, but the level under this panel changed
	}

	// New: never over an existing file. The next free levelN is offered.
	if (newName[0] == '\0')
	{
		for (int n = 2; ; n++)
		{
			const std::string candidate = "level" + std::to_string(n);
			if (std::find(files.begin(), files.end(), candidate + ".txt") == files.end())
			{
				std::snprintf(newName, sizeof(newName), "%s", candidate.c_str());
				break;
			}
		}
	}
	ImGui::SetNextItemWidth(160.f);
	ImGui::InputText("##newLevel", newName, sizeof(newName));
	const std::string name = newName;
	const bool valid = validLevelName(name);
	const bool taken = std::find(files.begin(), files.end(), name + ".txt") != files.end();
	ImGui::SameLine();
	ImGui::BeginDisabled(!valid || taken);
	if (ImGui::Button(unsaved ? "New level (discard edits)" : "New level"))
	{
		// An arena and a start at its centre: nothing placed, nothing closing,
		// and with no enemies and no gate, a quiet place to set up a fight.
		level::Level empty;
		empty.arenaRadius = 20000.f;
		empty.start = {0.f, 0.f};
		const std::string path = levelsDirectory + name + ".txt";
		if (level::save(path.c_str(), empty))
		{
			files = level::list(levelsDirectory);
			newName[0] = '\0';
			switchLevel(name + ".txt");
		}
		else { std::cerr << "level: could not write " << path << "\n"; }
	}
	ImGui::EndDisabled();
	if (taken) { ImGui::TextDisabled("%s.txt exists: pick another name", name.c_str()); }
	else if (!valid) { ImGui::TextDisabled("Letters, digits, - and _ only"); }
}

void levelDebugUi()
{
	levelFilesUi();
	ImGui::Separator();

	if (levelLoaded)
	{
		ImGui::Text("%s", levelFile.c_str());
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
	ImGui::Checkbox("Scenery", &sceneryVisible);
	arena::debugUi();
}


}

// The editor's panel, and what it asks for. The game does the file and the
// round; the editor only edits the level.
void editorDebugUi()
{
	switch (levelEditor::debugUi(currentLevel, levelEditor::changed()))
	{
	case levelEditor::Request::Save:
		if (level::save(levelPath.c_str(), currentLevel)) { levelEditor::clearChanged(); }
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
	debugPanel::section("Gate", gate::debugUi);
	debugPanel::section("Asteroids", asteroids::debugUi);
	debugPanel::section("Hidden outline", outline::debugUi);
	debugPanel::section("Resources", resources::debugUi);
	debugPanel::section("World grade", worldGrade::debugUi);
	debugPanel::section("Sound", sfx::debugUi);
	debugPanel::section("Clock", gameClock::debugUi);
	debugPanel::section("Player", playerMove::debugUi);
	debugPanel::section("Energy", energy::debugUi);
	debugPanel::section("Weapons", [] { weapons::debugUi(playerWeapons); });
	debugPanel::section("Explosions", effects::debugUi);
	debugPanel::section("Ram", ram::debugUi);
	debugPanel::section("Camera", zoomControl::debugUi);
	debugPanel::section("Enemies", enemiesDebugUi);
	debugPanel::section("Ship bumps", []
	{
		ImGui::SliderFloat("Bounce", &shipBounce, 0.f, 1.f, "%.2f of the closing speed");
		ImGui::TextDisabled("Player against enemy: both hurt, the player's shield blocks; never cloaked");
		ImGui::SliderFloat("Damage to player", &bumpDamage, 0.f, 0.5f, "%.2f of the hull");
		ImGui::SliderFloat("Damage to enemy", &bumpEnemyDamage, 0.f, 1.f, "%.2f of its life");
		ImGui::SliderFloat("Hardest touch that is free", &bumpMinSpeed, 0.f, 2000.f, "%.0f u/s");
		ImGui::SliderFloat("Grace", &bumpGrace, 0.f, 3.f, "%.2f s between hits");
		ImGui::TextDisabled("Masses: player 1; enemies under Enemies -> flight");
	});
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

	recallLevel();
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
		crt::setTransition(gameState::switchOff(), gameState::whiteOut(), gameState::warpBlur());
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

	// The ship's body is moved by playerMove -- or, in the states below that
	// take the controls away, set directly: the warp and the ram are not
	// thrust. `thrust` is set too, since it is what lights the plume.
	if (gameState::current() == gameState::State::Extracting)
	{
		// The warp: straight out along the heading, faster every moment, the
		// ram's afterimages and streaks behind it. The camera holds (below),
		// so the ship leaves the screen before it goes white.
		session.ship.velocity = session.ship.facing * gameState::warpSpeed();
		session.ship.position += session.ship.velocity * time.game;
		session.ship.thrust = session.ship.facing;

		effects::ramTrail(session.ship.position, session.ship.facing, shipSize,
			shipAtlas.get(3, 0), time.game);
	}
	else if (!controls)
	{
		// Paused, the hull holds still -- playerMove would snap it to the
		// mouse even with no time passing. Dying, it is not drawn.
	}
	else if (ram::windingUp())
	{
		// The wind-up: a moment's dip back, facing the ram, while the prow
		// brightens. The anticipation is what makes the lunge read as heavy.
		session.ship.velocity = -ram::direction() * ram::windupBackSpeed();
		session.ship.position += session.ship.velocity * time.game;
		session.ship.facing = ram::direction();
		session.ship.thrust = {};
		session.aim = mouseDirection;
	}
	else if (ram::active())
	{
		// The surge overrides flying: straight along the ram, past the normal
		// top speed. When it ends the ship still has this velocity, and the
		// momentum settings take it from there -- the speed cap brings it back.
		session.ship.velocity = ram::direction() * ram::surgeSpeed();
		session.ship.position += session.ship.velocity * time.game;
		session.ship.facing = ram::direction();
		session.ship.thrust = ram::direction();
		session.aim = mouseDirection;

		effects::ramTrail(session.ship.position, ram::direction(), shipSize,
			shipAtlas.get(3, 0), time.game);
	}
	else
	{
		session.aim = playerMove::update(session.ship, mouseDirection, time.game, energy::isCloaked());
	}

	// An asteroid field's core is solid (gameplay roadmap A2): the ship is put
	// back on its surface, thrown back out, and -- once per touch -- hit.
	// The shield blocks it and breaks, as a shot would; with it down, the
	// hull takes the damage. The ram's prow takes it instead when the core is
	// ahead of it, as it takes shots from the front. A core is not a Body, so
	// this bump checks `solid` itself (the convention, above): cloaked, the
	// ship passes through the core untouched. Uncloaking inside one puts it
	// back on the surface, and that touch hurts.
	syncSolid();
	{
		static float coreGrace = 0.f;
		coreGrace = std::max(0.f, coreGrace - time.game);
		const collision::Circle hull = game::shipHitbox(session.ship.position, shipSize);
		asteroids::CoreContact contact;
		if (gameState::playerPresent() && session.ship.solid
			&& asteroids::coreContact(hull.center, hull.radius, contact))
		{
			const asteroids::CoreRules &rules = asteroids::coreRules();
			session.ship.position = contact.pushTo;
			const float inward = glm::dot(session.ship.velocity, contact.outward);
			if (inward < 0.f) { session.ship.velocity -= (1.f + rules.bounce) * inward * contact.outward; }
			const float out = glm::dot(session.ship.velocity, contact.outward);
			if (out < rules.minOutSpeed) { session.ship.velocity += (rules.minOutSpeed - out) * contact.outward; }

			if (coreGrace <= 0.f)
			{
				coreGrace = rules.grace;
				const bool prowTakesIt = ram::barrierUp() && glm::dot(-contact.outward, ram::direction()) > 0.f;
				if (prowTakesIt)
				{
					shield::ramImpact();
					effects::shake(0.6f);
				}
				else if (energy::onHit(-contact.outward * hull.radius) == energy::HitResult::Damaged)
				{
					if (!hitboxDebug::isDamageFrozen()) { session.health -= rules.damage; }
					hud::onDamage();
					resources::interrupt();
				}
			}
		}
	}

	// The gate, once the ship has moved: it opens on the closing circle's
	// final ring (or at once, on a level that does not close), is ready at
	// once when every enemy is dead, and flying into it ready and uncloaked
	// is what the debug Extract button did.
	if (gate::update(time.game, !arena::closes() || arena::onFinalRing(), session.enemies.empty(),
		session.ship.position, controls && !energy::isCloaked()))
	{
		startExtraction();
	}

	// What the ram strikes: the arc's reach, a little ahead of the hull. Each
	// enemy once per ram; the player takes nothing.
	static bool rammingLastFrame = false;
	const bool newRam = ram::active() && !rammingLastFrame;
	rammingLastFrame = ram::active();
	if (ram::active())
	{
		const collision::Circle front = {session.ship.position + ram::direction() * (shipSize * 0.3f),
			shipSize * 0.65f};

		// Rocks too (gameplay roadmap A2): each struck once per ram, shoved
		// along it and spun if struck off-centre. The ship goes on through.
		asteroids::ram(front.center, front.radius, ram::direction(), newRam);

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
			const float which = glm::dot(enemy.body.position - session.ship.position, side) >= 0.f ? 1.f : -1.f;
			const glm::vec2 away = glm::normalize(ram::direction() + side * which);
			// Engaged first -- which turns it to face the player -- then the
			// spin takes over until the stun runs out.
			enemyAi::alert(enemy, session.ship.position);
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
		// No dead zone. At 1 unit it was far below a pixel and did nothing
		// visible, but it switched on follow's stepped easing (quarter speed
		// within 2, half within 4), and a ship slower than the chase speed
		// kept crossing those steps: the camera fell behind, caught up, fell
		// behind, and the world jittered round a steadily moving ship.
		cameraBase = camera::follow(
			cameraBase, session.ship.position, {(float)w, (float)h},
			{time.real * 550.f, 0.f, 150.f});
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
	gate::drawBody(renderer); // in the world, under the ships
	asteroids::draw(renderer);
#pragma endregion

#pragma region handle bulets


	// Only while playing: paused, the selection holds like everything else.
	if (controls) { weapons::handleInput(playerWeapons); }

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
	fire.origin = session.ship.position;
	fire.aim = session.aim; // the mouse, not necessarily the hull
	fire.shipVelocity = session.ship.velocity;
	fire.shipSize = shipSize;
	fire.shooter = playerShip;
	fire.missileTarget = weapons::nearestEnemy(mouseWorld, session.enemies); // a missile locks onto the enemy nearest the mouse
	// Paused, not at all: a weapon that is ready fires whatever the clock
	// says, and the beam should stay on screen as it was, not switch off.
	const int shots = gameState::paused() ? 0
		: weapons::update(playerWeapons, time.game, trigger, fire, session.bullets);
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

	const weapons::Beam beam = weapons::beam(playerWeapons);
	glm::vec2 beamEnd = {};
	bulletLook::BeamImpact beamImpact = bulletLook::BeamImpact::None;
	glm::vec2 beamSurface = {}; // the core's outward normal where a deflected beam meets it
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

		// A rock stops it (gameplay roadmap A1): nothing behind a rock is
		// burned, and from on top of one it goes nowhere. Burning a rock is
		// how ore is mined (A4) -- asteroids replaced the deposits.
		int rockHit = -1;
		const float rock = asteroids::raycast(beam.origin, beam.direction, reach, &rockHit);
		if (rock >= 0.f)
		{
			reach = rock;
			target = -1;
		}

		beamEnd = beam.origin + beam.direction * reach;
		// A core is the one rock the beam cannot touch (A4): it glances off,
		// and is drawn to, rather than burning.
		glm::vec2 coreCentre;
		if (asteroids::isCore(rockHit, &coreCentre))
		{
			beamImpact = bulletLook::BeamImpact::Deflect;
			beamSurface = beamEnd - coreCentre;
		}
		else if (target >= 0 || rock >= 0.f) { beamImpact = bulletLook::BeamImpact::Burn; }

		// A steady push where it burns (A2), wearing the rock down as it
		// sheds its ore (A4).
		if (rockHit >= 0)
		{
			asteroids::beam(rockHit, beamEnd, beam.direction, beam.damagePerSecond, time.game);
		}

		if (target >= 0 && !hitboxDebug::isDamageFrozen())
		{
			session.enemies[target].life -= beam.damagePerSecond * time.game;
			if (session.enemies[target].life <= 0.f)
			{
				killEnemy(target);
			}
			else
			{
				enemyAi::alert(session.enemies[target], session.ship.position);
			}
		}
	}


	// Before anything moves: missiles turn and speed up, then fly with the rest.
	// What a missile can chase: the enemies, and the player unless cloaked or
	// gone -- so an enemy's missile loses its lock when the player cloaks.
	weapons::Targets targets;
	targets.enemies = &session.enemies;
	targets.player = session.ship.position;
	targets.playerTargetable = gameState::playerPresent() && !energy::isCloaked();
	weapons::steerMissiles(session.bullets, targets, time.game);

	for (int i = 0; i < session.bullets.size(); i++)
	{
		
		if (glm::distance(session.bullets[i].position, session.ship.position) > 5'000)
		{
			session.bullets.erase(session.bullets.begin() + i);
			i--;
			continue;
		}

		// A rock stops any shot, anyone's, missiles too (gameplay roadmap A1),
		// and is pushed by it where it landed (A2).
		{
			const collision::Circle hitbox = session.bullets[i].getHitbox();
			const int rock = asteroids::hitCircle(hitbox.center, hitbox.radius);
			if (rock >= 0)
			{
				const Bullet &b = session.bullets[i];
				asteroids::shot(rock, hitbox.center, b.fireDirection, b.damage,
					b.motion == BulletMotion::Missile);
				session.bullets.erase(session.bullets.begin() + i);
				i--;
				continue;
			}
		}

		if (!hitboxDebug::isDamageFrozen())
		{
			if (!session.bullets[i].fromEnemy())
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
							enemyAi::alert(session.enemies[e], session.ship.position);
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
					game::shipHitbox(session.ship.position, shipSize)))
				{
					// Shot at all -- prow, shield or hull -- and the gate's
					// start is lost (gameplay roadmap L5).
					gate::playerShot();

					// The ram's prow takes shots from the front while it is out.
					if (ram::barrierUp() && glm::dot(session.bullets[i].position - session.ship.position,
						ram::direction()) > 0.f)
					{
						session.bullets.erase(session.bullets.begin() + i);
						i--;
						continue;
					}

					// A missile ignores the shield: it passes through and hits the
					// hull. The bubble does not ripple, break, or lose energy.
					// Anything else asks the shield, which takes it while up.
					energy::HitResult hit = energy::HitResult::Damaged;
					if (session.bullets[i].motion != BulletMotion::Missile)
					{
						// Relative to the ship, because the shield moves with it
						// and the ripple has to stay anchored to the bubble.
						hit = energy::onHit(session.bullets[i].position - session.ship.position);
					}

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
		const float burn = arena::burn(session.ship.position, time.game);
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
		effects::shipDestroyed(shipSheet, shipAtlas.get(3, 0), session.ship.position,
			session.ship.facing, session.ship.velocity, shipSize);
		asteroids::blast(session.ship.position);
		effects::shake(1.f);
		session.ship.velocity = {};
		resources::playerDropped(session.ship.position); // the hold spills at the wreck
		energy::uncloak();
		ram::reset();
		weapons::reset(playerWeapons); // no burst's second shot from the wreck
		gameState::playerDied();
	}
	else if (gameState::playerPresent())
	{
		// Game time. This was the frame's own delta, so at 1% speed the ship
		// healed at full rate while everything shooting at it crawled.
		// Not while burning, or regen would cancel most of it.
		if (healthRegenEnabled && !arena::outside(session.ship.position)) { session.health += time.game * 0.05; }
		session.health = glm::clamp(session.health, 0.f, 1.f);
	}

#pragma endregion

#pragma region handle enemies

	// Waves are the endless mode's; a level places its enemies.
	if (!levelLoaded)
	{
		enemyAi::updateSpawning(session.enemies, session.spawnEnemyTimerSecconds,
			session.ship.position, time.game);
	}

	// The view, grown by the wake margin: inside it, enemies are awake.
	glm::vec4 wakeRect = renderer.getViewRect();
	wakeRect.x -= wakeRect.z * wakeMargin;
	wakeRect.y -= wakeRect.w * wakeMargin;
	wakeRect.z *= 1.f + 2.f * wakeMargin;
	wakeRect.w *= 1.f + 2.f * wakeMargin;
	awakeEnemies = 0;
	enemyBeams.clear();

	// In a field's painted area, no enemy sees the player (A1b).
	const bool playerInField = asteroids::inField(session.ship.position);

	for (int i = 0; i < session.enemies.size(); i++)
	{

		if (!levelLoaded
			&& glm::distance(session.ship.position, session.enemies[i].body.position) > enemyDespawnDistance)
		{
			//dispawn enemy
			session.enemies.erase(session.enemies.begin() + i);
			i--;
			continue;
		}

		if (levelLoaded)
		{
			const Enemy &e = session.enemies[i];
			const bool inView = e.body.position.x >= wakeRect.x && e.body.position.x <= wakeRect.x + wakeRect.z
				&& e.body.position.y >= wakeRect.y && e.body.position.y <= wakeRect.y + wakeRect.w;
			// Asleep -- unless the closing circle has passed it, which wakes it
			// to fly back in (gameplay roadmap L4).
			if (!inView && e.awareness == Enemy::Awareness::Unaware && !arena::outside(e.body.position))
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

		// A core is solid to enemies too (A2): put back on its surface, thrown
		// back out tumbling, and hurt once per touch. Not an alert -- it only
		// bumped a rock. They do not steer round cores yet. With momentum (P1)
		// it also loses the speed it had into the core, as the player does --
		// otherwise it keeps driving into the surface every frame. No enemy
		// can be intangible yet, but this is a bump with something that is not
		// a Body, so it checks `solid` (the convention, at syncSolid).
		{
			Enemy &e = session.enemies[i];
			e.coreGrace = std::max(0.f, e.coreGrace - time.game);
			const collision::Circle hull = e.getHitbox();
			asteroids::CoreContact contact;
			if (e.body.solid && asteroids::coreContact(hull.center, hull.radius, contact))
			{
				const asteroids::CoreRules &rules = asteroids::coreRules();
				e.body.position = contact.pushTo;
				const float inward = glm::dot(e.body.velocity, contact.outward);
				if (inward < 0.f) { e.body.velocity -= inward * contact.outward; }
				if (e.coreGrace <= 0.f)
				{
					e.coreGrace = rules.grace;
					enemyAi::stun(e, contact.outward * rules.enemyKnock, rules.enemyStun);
					if (!hitboxDebug::isDamageFrozen()) { e.life -= rules.damage; }
					if (e.life <= 0.f)
					{
						killEnemy(i);
						i--;
						continue;
					}
				}
			}
		}

		// Cloaked, the player is in no enemy's sight (gameplay roadmap C5).
		// Wreckage or leaving, the player is as gone as cloaked.
		glm::vec2 wayIn;
		const bool comingBack = arena::wayBackIn(session.enemies[i].body.position, wayIn);
		// A rock between them hides the player as well as the cloak does
		// (gameplay roadmap A1): what makes a rock somewhere to hide. Inside a
		// field's painted area the player is hidden outright, gaps and all,
		// like tall grass (A1b). Shooting out does not end it; a hit enemy is
		// alerted as ever, turns, finds nothing, and comes to search.
		const bool hidden = energy::isCloaked() || !gameState::playerPresent()
			|| playerInField
			|| asteroids::blocksSight(session.enemies[i].body.position, session.ship.position);
		Enemy &e = session.enemies[i];
		const enemyAi::Orders orders = enemyAi::update(e, time.game, session.ship.position,
			session.ship.velocity, hidden, comingBack ? &wayIn : nullptr);

		// Its gun, through the same weapons::update as the player's (B1). Only
		// while it fights, as it always was: the AI decides the trigger, the
		// loadout the cooldown. Its shots do not carry its own velocity as the
		// player's do -- they never have, and B1 changes no enemy; whether they
		// should is a question for B2.
		if (orders.fighting)
		{
			weapons::FireContext gun;
			gun.origin = e.body.position;
			gun.aim = e.body.facing;
			gun.shipSize = enemyShipSize;
			gun.shooter = e.id;
			gun.missileTarget = playerShip; // its missiles, if it rolled them, chase the player
			if (weapons::update(e.loadout, time.game, orders.trigger, gun, session.bullets) > 0) { sfx::enemyShot(); }

			// Its beam, if it rolled the laser: traced from the nose, stopped by
			// a rock or by the player, never past enemyBeamRange. It does not
			// push or mine the rock it stops on -- that is the player's beam's
			// job. On the player, energy decides (onBeam): a shield holds it
			// and is not broken, and it splashes off; with the shield down it
			// burns the hull for the weapon's damage per second; cloaked, it
			// passes through.
			const weapons::Beam eb = weapons::beam(e.loadout);
			if (eb.firing)
			{
				if (eb.started) { sfx::enemyShot(); }
				EnemyBeam drawn;
				drawn.start = eb.origin;
				float reach = enemyBeamRange;
				int rock = -1;
				const float toRock = asteroids::raycast(eb.origin, eb.direction, reach, &rock);
				if (toRock >= 0.f)
				{
					reach = toRock;
					glm::vec2 coreCentre;
					drawn.impact = asteroids::isCore(rock, &coreCentre)
						? bulletLook::BeamImpact::Deflect : bulletLook::BeamImpact::Burn;
					drawn.surface = eb.origin + eb.direction * reach - coreCentre;
				}

				const collision::Circle hull = game::shipHitbox(session.ship.position, shipSize);
				const float toPlayer = gameState::playerPresent()
					? collision::rayToCircle(eb.origin, eb.direction, hull) : -1.f;
				if (toPlayer >= 0.f && toPlayer < reach)
				{
					const glm::vec2 at = eb.origin + eb.direction * toPlayer;
					switch (energy::onBeam(at - session.ship.position, time.game))
					{
					case energy::HitResult::Blocked:
						reach = toPlayer;
						drawn.impact = bulletLook::BeamImpact::Deflect;
						drawn.surface = at - session.ship.position;
						break;
					case energy::HitResult::Damaged:
						reach = toPlayer;
						drawn.impact = bulletLook::BeamImpact::Burn;
						if (!hitboxDebug::isDamageFrozen()) { session.health -= eb.damagePerSecond * time.game; }
						resources::interrupt();
						enemyBeamShakeLeft -= time.game;
						if (enemyBeamShakeLeft <= 0.f)
						{
							hud::onDamage(0.25f);
							enemyBeamShakeLeft = enemyBeamShakeSeconds;
						}
						break;
					case energy::HitResult::Missed:
						break; // cloaked: on through
					}
				}
				drawn.end = eb.origin + eb.direction * reach;
				enemyBeams.push_back(drawn);
			}
		}
	}

	// Ships bump (gameplay roadmap P1): every pair of enemies, and the player
	// with each, pushed apart and trading momentum along the line between
	// them -- after every ship has moved, so each pair is judged where it
	// ended up. Not the player while the ram has the ship -- the surge sets
	// its velocity, and strikes the enemies it meets itself -- nor once it is
	// wreckage or leaving. Cloaked needs no check here: these are Body
	// against Body, and collide passes through a ship that is not solid (the
	// convention, at syncSolid).
	syncSolid();
	{
		const float enemyRadius = game::shipHitboxRadius(enemyShipSize);
		for (Enemy &e : session.enemies) { e.bumpGrace = std::max(0.f, e.bumpGrace - time.game); }
		for (size_t a = 0; a < session.enemies.size(); a++)
		{
			for (size_t b = a + 1; b < session.enemies.size(); b++)
			{
				movement::collide(session.enemies[a].body, enemyRadius,
					session.enemies[b].body, enemyRadius, shipBounce);
			}
		}
		if (gameState::playerPresent() && !ram::active() && !ram::windingUp())
		{
			const float playerRadius = game::shipHitboxRadius(shipSize);
			for (int i = 0; i < (int)session.enemies.size(); i++)
			{
				Enemy &e = session.enemies[i];
				const movement::Contact contact = movement::collide(session.ship, playerRadius,
					e.body, enemyRadius, shipBounce);
				if (!contact.touched || contact.impactSpeed < bumpMinSpeed || e.bumpGrace > 0.f) { continue; }
				e.bumpGrace = bumpGrace;

				// The player: the shield takes it if it is up, from the side the
				// enemy struck; with it down, the hull.
				const glm::vec2 toward = e.body.position - session.ship.position;
				const float distance = glm::length(toward);
				const glm::vec2 side = distance > 1e-3f ? toward / distance : glm::vec2(1.f, 0.f);
				if (energy::onHit(side * playerRadius) == energy::HitResult::Damaged)
				{
					if (!hitboxDebug::isDamageFrozen()) { session.health -= bumpDamage; }
					hud::onDamage();
					resources::interrupt();
				}

				// The enemy, which has no shield yet.
				if (!hitboxDebug::isDamageFrozen()) { e.life -= bumpEnemyDamage; }
				if (e.life <= 0.f)
				{
					killEnemy(i);
					i--;
				}
			}
		}
	}

#pragma endregion

	effects::update(time.game);
	resources::update(time.game, session.ship.position, gameState::playerPresent());
	asteroids::update(time.game); // rocks drift, spin, spring home and bump (A2)

#pragma region render enemies

	// What each enemy can see, under the ships (gameplay roadmap C5). A debug
	// toggle, on by default.
	if (enemyAi::showCones())
	{
		renderer.setBlendMode(wgpu2d::BlendMode::Additive);
		for (const auto &e : session.enemies) { effects::drawSight(renderer, e); }
		renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	}

	// Their engines (P1): every body has a thrust now, so every enemy has a
	// plume -- the player's, in a hostile orange, before the hulls so each
	// covers its own. The plume is behind the nose, so it is lit by the part
	// of the thrust along the nose; a sniper strafing sideways shows none.
	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	for (auto &e : session.enemies)
	{
		const float forward = std::max(0.f, glm::dot(e.body.thrust, e.body.facing));
		e.plume = thruster::ease(e.plume, forward, time.game);
		thruster::drawPlume(renderer, e.body.position, enemyShipSize, e.body.facing, e.plume,
			effectClock + (float)(e.id % 7u) * 0.13f, enemyPlumeColour);
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	for (auto &e : session.enemies)
	{
		// Darkened where a field rock's shadow falls on it (A3).
		const float lit = 1.f - asteroids::shadowOn(e.body.position, e.getHitbox().radius);
		renderSpaceShip(renderer, e.body.position, enemyShipSize,
			shipSheet, shipAtlas.get(e.type.x, e.type.y), e.body.facing, {lit, lit, lit, 1.f});
	}

	// What each knows: red engaged, amber searching.
	for (const auto &e : session.enemies) { effects::drawAwareness(renderer, e, effectClock); }

	// Wrecks sit where ships sit: after them, under everything else.
	effects::drawDebris(renderer);


	// A missile's lock on its target: a dashed red box, until impact.
	for (const auto &b : session.bullets)
	{
		if (b.motion != BulletMotion::Missile || b.target == noShip) { continue; }
		for (const auto &e : session.enemies)
		{
			if (e.id != b.target) { continue; }
			effects::drawTargetBox(renderer, e.body.position, enemyShipSize * 1.3f, b.age);
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
		thruster::draw(renderer, session.ship.position, shipSize, session.ship.facing,
			glm::length(session.ship.thrust), time.game);

		// Faded by the cloak. The hull going nearly transparent is half the
		// effect; the other half is the world bending around it, which happens
		// below when the world goes through the cloak's shader. Stretched
		// along its heading while it warps out. In an asteroid field, darker:
		// in the gaps between rocks it is in their shadow (A1b).
		// And in a field rock's shadow, darker still (A3).
		const float shade = (gameState::playerPresent() && asteroids::inField(session.ship.position)
			? asteroids::hiddenShade() : 1.f)
			* (1.f - asteroids::shadowOn(session.ship.position, game::shipHitbox(session.ship.position, shipSize).radius));
		renderSpaceShip(renderer, session.ship.position, shipSize,
			shipSheet, shipAtlas.get(3, 0), session.ship.facing,
			{shade, shade, shade, cloak::shipAlpha()}, gameState::warpStretch());

		// After the hull, so the rim reads as being in front of it. Not while
		// warping: the bubble does not stretch with the hull.
		if (gameState::playerPresent())
		{
			shield::draw(renderer, session.ship.position, shipSize, time.game);
		}
	}

	// Asteroid fields over every ship (A1b): a ship in one is behind its
	// rocks. And the player, hidden there, drawn once more over them as an
	// outline -- you still see where you are, and seeing it is how you know
	// no enemy can.
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	asteroids::drawFields(renderer);
	if (gameState::playerPresent() && asteroids::inField(session.ship.position))
	{
		outline::begin(renderer, 0.5f + 0.5f * std::sin(effectClock * 3.f));
		renderSpaceShip(renderer, session.ship.position, shipSize,
			shipSheet, shipAtlas.get(3, 0), session.ship.facing,
			{1.f, 1.f, 1.f, cloak::shipAlpha()}, gameState::warpStretch());
		outline::end(renderer);
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
		bulletLook::drawGlow(renderer, b.position, b.fireDirection, b.fromEnemy(), b.style, b.size);
	}
	if (beam.firing)
	{
		bulletLook::drawBeamGlow(renderer, beam.origin, beamEnd, beamImpact, effectClock, beamSurface);
	}
	for (const EnemyBeam &b : enemyBeams)
	{
		bulletLook::drawBeamGlow(renderer, b.start, b.end, b.impact, effectClock, b.surface, true);
	}
	effects::drawGlow(renderer);
	resources::drawGlow(renderer, effectClock);
	arena::draw(renderer, renderer.currentCamera.zoom);
	gate::draw(renderer, renderer.currentCamera.zoom);
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	for (auto &b : session.bullets)
	{
		bulletLook::drawSprite(renderer, b.position, b.fireDirection, b.fromEnemy(), b.style, b.size);
	}
	if (beam.firing) { bulletLook::drawBeamCore(renderer, beam.origin, beamEnd, effectClock); }
	for (const EnemyBeam &b : enemyBeams) { bulletLook::drawBeamCore(renderer, b.start, b.end, effectClock, true); }

#pragma endregion

#pragma region debug hitboxes

	hitboxDebug::draw(renderer, collisionSystem,
		game::shipHitbox(session.ship.position, shipSize), session.enemies, session.bullets);

#pragma endregion


#pragma region ui

	// The world's flush, routed through the cloak. Down, this is exactly
	// renderer.flush(); up, the world goes into a target and comes back
	// through the shader. hud::draw flushes again straight after, which is a
	// no-op on an empty batch.
	//
	// The last of the world: asteroid debris in front of it all, nearer the
	// eye than the play (A3).
	asteroids::drawForeground(renderer);

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
		arena::drawBurnFlash(renderer, e.burnFlash, e.body.position, enemyShipSize,
			shipSheet, shipAtlas.get(e.type.x, e.type.y), e.body.facing);
	}
	if (gameState::current() != gameState::State::Dying)
	{
		arena::drawBurnFlash(renderer, arena::burnFlash(), session.ship.position, shipSize,
			shipSheet, shipAtlas.get(3, 0), session.ship.facing, cloak::shipAlpha(),
			gameState::warpStretch());
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);
	// The gate's swirl rides the cloak's pass, so it bends the same target.
	cloak::setSwirl(gate::position(), gate::swirlRadius(), gate::swirlStrength());
	cloak::flushWorld(renderer, session.ship.position, shipSize, w, h, time.game);

	// The arrow to the gate, once it is open. Screen pixels, from the world
	// camera the frame was drawn in -- the HUD pushes its own.
	if (gate::exists() && gate::state() != gate::State::Closed)
	{
		const glm::vec4 view = renderer.getViewRect();
		if (view.z != 0.f && view.w != 0.f)
		{
			const glm::vec2 onScreen = {(gate::position().x - view.x) / view.z * (float)w,
				(gate::position().y - view.y) / view.w * (float)h};
			hud::pointTo(true, onScreen, gate::pulse(), gate::colour());
		}
	}

	hud::WeaponSlot slots[weapons::slotCount];
	for (int s = 0; s < weapons::slotCount; s++)
	{
		const weapons::SlotView v = weapons::slot(playerWeapons, s);
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
