#define GLM_ENABLE_EXPERIMENTAL
#include "gameLayer.h"
#include <tuning.h>
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
#include <shipMode.h>
#include <gate.h>
#include <asteroids.h>
#include <outline.h>
#include <sight.h>
#include <lastKnown.h>
#include <scope.h>
#include <theirGhost.h>
#include <weapons.h>
#include <effects.h>
#include <ram.h>
#include <ramPath.h>
#include <interior.h>
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

	// The player's energy, and with it the shield it raises (gameplay roadmap
	// B1: every ship's is its own). A new round starts it full, shield up.
	energy::Energy energy;

	// Fight or flight (sight roadmap M1): Tab switches. A new round starts in
	// fight mode, shield up.
	shipMode::Mode mode = shipMode::Mode::Fight;

	// The player's ram, as every ship that rams has one (B1).
	ram::Ram ram;
	// The right button went down over the world and is still held: the ram
	// is being aimed, and letting go starts it.
	bool aimingRam = false;

	// Struck by an enemy's ram (B1 step 3): out of control this long more,
	// tumbling at this rate.
	float stunned = 0.f;
	float stunSpin = 0.f;

	// Seconds before another stun or lockdown takes (B2): a modified weapon's
	// effect, then a grace.
	float effectImmune = 0.f;

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
// its cooldowns, ammo, charge and a lockdown (weapons::reset).
weapons::Loadout playerWeapons = weapons::playersLoadout();

// Its weapons' tuning (platform/tuning.h), by each weapon's file key -- the
// player's loadout is where the panel tunes them.
const tuning::Group tunedBurst("weapons.burst", {
	{"cooldown", playerWeapons.slots[0].cooldown}, {"damage", playerWeapons.slots[0].damage},
	{"speed", playerWeapons.slots[0].speed}, {"burstGap", playerWeapons.slots[0].burstGap},
});
const tuning::Group tunedHeavy("weapons.heavy", {
	{"cooldown", playerWeapons.slots[1].cooldown}, {"damage", playerWeapons.slots[1].damage},
	{"speed", playerWeapons.slots[1].speed},
});
const tuning::Group tunedMissile("weapons.missile", {
	{"cooldown", playerWeapons.slots[2].cooldown}, {"damage", playerWeapons.slots[2].damage},
	{"speed", playerWeapons.slots[2].speed},
});
const tuning::Group tunedLaser("weapons.laser", {
	{"cooldown", playerWeapons.slots[3].cooldown}, {"damage", playerWeapons.slots[3].damage},
});
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

// What a boss bursts into when it dies (B2): a big haul of orbs.
int bossOrbCount = 24;
float bossOrbValue = 0.5f;

// The player's tumble after an enemy's ram (B1 step 3): the drag on the blow,
// as a rammed enemy's (Enemies -> Steering and stun).
float playerStunDrag = 4.f;

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
	session.ship.solid = !energy::isCloaked(session.energy);
}

// A modified weapon's hit that reached the hull (gameplay roadmap B2) -- the
// shield took nothing, so the effect lands: a stun, as a ram's; a lockdown of
// every weapon, then their cooldowns. Then a grace, so a beam carrying either
// cannot hold a ship for good. The same rule on both sides.
void hitEffectsOnPlayer(bool stun, bool lockdown)
{
	if ((!stun && !lockdown) || session.effectImmune > 0.f) { return; }
	float lasts = 0.f;
	if (stun)
	{
		if (session.stunned <= 0.f)
		{
			const float turns = 6.f + (rand() % 1000) / 200.f;
			session.stunSpin = (rand() % 2) ? turns : -turns;
		}
		session.stunned = std::max(session.stunned, weapons::stunSecondsOnPlayer());
		ram::stop(session.ram);
		lasts = weapons::stunSecondsOnPlayer();
	}
	if (lockdown)
	{
		weapons::lockdown(playerWeapons, weapons::lockdownSeconds());
		lasts = std::max(lasts, weapons::lockdownSeconds());
	}
	session.effectImmune = lasts + weapons::effectGraceSeconds();
}

void hitEffectsOnEnemy(Enemy &e, bool stun, bool lockdown)
{
	if ((!stun && !lockdown) || e.effectImmune > 0.f) { return; }
	float lasts = 0.f;
	if (stun)
	{
		enemyAi::stun(e, {}, weapons::stunSecondsOnEnemy());
		lasts = weapons::stunSecondsOnEnemy();
	}
	if (lockdown)
	{
		weapons::lockdown(e.loadout, weapons::lockdownSeconds());
		lasts = std::max(lasts, weapons::lockdownSeconds());
	}
	e.effectImmune = lasts + weapons::effectGraceSeconds();
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

// This frame's sight (sight roadmap S1): where each awake enemy looks from,
// for the debug lines, and whether one engaged enemy can see the player in a
// field, which turns the hidden outline amber.
std::vector<sight::Viewer> sightViewers;
bool playerSeenInField = false;
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
	{"shield",     shield::init,     nullptr,         shield::cleanup},
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
	// After shield and cloak: its reset raises one and lowers the other.
	{"weapons",    nullptr,          [] { weapons::reset(playerWeapons); }, nullptr},
};

// A setting, so it survives restart.
bool healthRegenEnabled = true;

// The game's own tunables (platform/tuning.h): registered after everything
// above, so each default is the value it is declared with.
const tuning::Group tunedGame("game", {
	{"healthRegen", healthRegenEnabled}, {"wakeMargin", wakeMargin}, {"scenery", sceneryVisible},
	{"bump.bounce", shipBounce}, {"bump.damageToPlayer", bumpDamage}, {"bump.damageToEnemy", bumpEnemyDamage},
	{"bump.minSpeed", bumpMinSpeed}, {"bump.grace", bumpGrace},
	{"enemyBeam.range", enemyBeamRange},
	{"playerStunDrag", playerStunDrag},
	{"boss.orbCount", bossOrbCount}, {"boss.orbValue", bossOrbValue},
});

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
	cloak::setActive(false); // a new round is not cloaked

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
				level::direction(p.facingDegrees), p.behaviour, p.guns, p.shield, p.cloak, p.ram));
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
	asteroids::reset(currentLevel.asteroids, level::fieldsAsPlayed(currentLevel, gate::clearingRadius())); // A1, A1b; the gate's clearing (W1)
	lastKnown::reset(); // a new round's ghosts (sight roadmap S4)
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
	const bool boss = e.behaviour == Enemy::Behaviour::Boss;
	asteroids::blast(e.body.position, boss ? 3.f : 1.f); // the blast shoves rocks near it (A2)
	resources::enemyDropped(e.body.position); // fragments among the wreckage (L3)
	if (boss)
	{
		// A boss bursts a big haul of orbs (B2), thrown out all round.
		effects::shake(1.f);
		for (int k = 0; k < bossOrbCount; k++)
		{
			const float angle = 6.2831853f * ((float)k + (rand() % 100) / 100.f) / (float)bossOrbCount;
			resources::emitOrb(e.body.position, {std::cos(angle), std::sin(angle)}, bossOrbValue);
		}
	}
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

	tune::SliderFloat("Player Health", &session.health, 0, 1);
	ImGui::Text("Player at %.0f, %.0f", session.ship.position.x, session.ship.position.y);
	tune::Checkbox("Health regen", &healthRegenEnabled);
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
	if (ImGui::Button("Spawn boss"))
	{
		session.enemies.push_back(enemyAi::spawnNear(session.ship.position, Enemy::Behaviour::Boss));
	}
	ImGui::SameLine();
	ImGui::TextDisabled("(rolled weapons)");
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
	tune::SliderFloat("Wake margin", &wakeMargin, 0.f, 2.f, "%.2f of view");
	tune::Checkbox("Scenery", &sceneryVisible);
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
	debugPanel::section("Sight", sight::debugUi);
	debugPanel::section("Last known", lastKnown::debugUi);
	debugPanel::section("Scope", scope::debugUi);
	debugPanel::section("Their ghost", theirGhost::debugUi);
	debugPanel::section("Hidden outline", outline::debugUi);
	debugPanel::section("Resources", resources::debugUi);
	debugPanel::section("World grade", worldGrade::debugUi);
	debugPanel::section("Sound", sfx::debugUi);
	debugPanel::section("Clock", gameClock::debugUi);
	debugPanel::section("Player", playerMove::debugUi);
	debugPanel::section("Energy", [] { energy::debugUi(session.energy); });
	debugPanel::section("Flight", [] { shipMode::debugUi(session.mode, session.energy); });
	debugPanel::section("Interior", interior::debugUi);
	debugPanel::section("Weapons", [] { weapons::debugUi(playerWeapons); });
	debugPanel::section("Explosions", effects::debugUi);
	debugPanel::section("Ram", [] { ram::debugUi(session.ram); ramPath::debugUi(); });
	debugPanel::section("Camera", zoomControl::debugUi);
	debugPanel::section("Enemies", enemiesDebugUi);
	debugPanel::section("Ship bumps", []
	{
		tune::SliderFloat("Bounce", &shipBounce, 0.f, 1.f, "%.2f of the closing speed");
		ImGui::TextDisabled("Player against enemy: both hurt, the player's shield blocks; never cloaked");
		tune::SliderFloat("Damage to player", &bumpDamage, 0.f, 0.5f, "%.2f of the hull");
		tune::SliderFloat("Damage to enemy", &bumpEnemyDamage, 0.f, 1.f, "%.2f of its life");
		tune::SliderFloat("Hardest touch that is free", &bumpMinSpeed, 0.f, 2000.f, "%.0f u/s");
		tune::SliderFloat("Grace", &bumpGrace, 0.f, 3.f, "%.2f s between hits");
		ImGui::TextDisabled("Masses: player 1; enemies under Enemies -> flight");
	});
	debugPanel::section("Hitboxes", hitboxDebug::debugUi);
	debugPanel::section("Shield", [] { shield::debugUi(session.energy.bubble); });
	debugPanel::section("CRT", crt::debugUi);
	// Last: saving and loading all of the above, and what has changed.
	debugPanel::section("Tuning", tuning::debugUi);

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
	look.bossCell = shipAtlas.get(1, 0);
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

	// Saved tuning (platform/tuning.h): the last set chosen, before anything
	// reads it. Choosing a set later restarts the round, so what is read only
	// at spawn or when rocks are grown takes effect too.
	tuning::init(RESOURCES_PATH "tuning/", "lastTuning.cfg", []
	{
		gameState::reset();
		restartGame();
	});

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
		session.aimingRam = false; // the editor's right button is its own
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

	// The long-range scope (sight roadmap S4b): hold V, in control and not
	// rammed. While it is held the view leans off the ship, so the aim is
	// measured from where the ship is on screen, not from the screen's centre.
	scope::update(controls && session.stunned <= 0.f && !ImGui::GetIO().WantCaptureKeyboard
		&& platform::isButtonHeld(platform::Button::V), (mousePos - screenCenter) / screenCenter, time.real);
	if (scope::held())
	{
		const glm::vec4 view = renderer.getViewRect();
		if (view.z != 0.f && view.w != 0.f)
		{
			const glm::vec2 shipOnScreen = {(session.ship.position.x - view.x) / view.z * (float)w,
				(session.ship.position.y - view.y) / view.w * (float)h};
			const glm::vec2 toPointer = mousePos - shipOnScreen;
			if (glm::length(toPointer) > 0.f) { mouseDirection = glm::normalize(toPointer); }
		}
	}

#pragma endregion


#pragma region energy

	// Before movement, because a cloaked ship drifts instead of flying.
	// Not during a ram: the ram uncloaks on start, and E would otherwise
	// cloak again while the prow is out -- invulnerable and still striking.
	// Rammed by an enemy (B1 step 3), the ship is out of control for a moment:
	// no flying, firing, ramming or cloaking until the stun runs out.
	const bool stunnedNow = session.stunned > 0.f;
	session.effectImmune = std::max(0.f, session.effectImmune - time.game);
	if (controls && !stunnedNow && platform::isButtonPressedOn(platform::Button::E) && !ram::barrierUp(session.ram))
	{
		energy::cloak(session.energy);
	}
	// Tab: fight or flight (M1). Into flight the shield drops; into fight the
	// bar starts empty, so the shield comes back only once it has refilled.
	if (controls && !stunnedNow && !ImGui::GetIO().WantCaptureKeyboard
		&& platform::isButtonPressedOn(platform::Button::Tab))
	{
		shipMode::toggle(session.mode, session.energy);
	}
	energy::update(session.energy, time.game);
	// The world bending round the cloaked ship follows the player's energy:
	// the look is the cloak module's, whether to show it is energy's.
	cloak::setActive(energy::isCloaked(session.energy));

#pragma endregion

#pragma region movement

	// Facing is the hull; aim is the gun. They are the same vector unless the
	// ship is turned with A/D, when the mouse aims independently.
	// The ram, before flying: holding the right button aims it -- ramPath
	// draws where it will end -- and letting go starts it toward the mouse.
	// Ramming uncloaks, as firing does (gameplay roadmap C4b). Stunned or
	// scoped, the aim is dropped.
	// Out of control -- paused, dying, leaving -- a wind-up keeps the heading
	// it had rather than following the mouse.
	ram::update(session.ram, time.game, controls ? mouseDirection : ram::direction(session.ram));
	const bool canAimRam = controls && !stunnedNow && !scope::held(); // no ramming while scoped (S4b)
	if (!canAimRam) { session.aimingRam = false; }
	else if (platform::isRMousePressed() && !ImGui::GetIO().WantCaptureMouse) { session.aimingRam = true; }
	if (session.aimingRam && !platform::isRMouseHeld())
	{
		session.aimingRam = false;
		if (ram::tryStart(session.ram, mouseDirection)) { energy::uncloak(session.energy); }
	}
	shield::setRam(session.energy.bubble, ram::barrierLevel(session.ram), ram::direction(session.ram));

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
	else if (session.stunned > 0.f)
	{
		// Rammed (B1 step 3): tumbling on the blow, as a rammed enemy does --
		// no thrust, no speed cap, and the tumble's quick drag.
		session.stunned -= time.game;
		const float turn = session.stunSpin * time.game;
		const glm::vec2 f = session.ship.facing;
		session.ship.facing = glm::normalize(glm::vec2(f.x * std::cos(turn) - f.y * std::sin(turn),
			f.x * std::sin(turn) + f.y * std::cos(turn)));
		session.ship.thrust = {};
		movement::integrate(session.ship.position, session.ship.velocity, {},
			movement::momentum(0.f, playerStunDrag), time.game);
	}
	else if (ram::windingUp(session.ram))
	{
		// The wind-up: a moment's dip back, facing the ram, while the prow
		// brightens. The anticipation is what makes the lunge read as heavy.
		session.ship.velocity = -ram::direction(session.ram) * ram::windupBackSpeed();
		session.ship.position += session.ship.velocity * time.game;
		session.ship.facing = ram::direction(session.ram);
		session.ship.thrust = {};
		session.aim = mouseDirection;
	}
	else if (ram::active(session.ram))
	{
		// The surge overrides flying: straight along the ram, past the normal
		// top speed. When it ends the ship still has this velocity, and the
		// momentum settings take it from there -- the speed cap brings it back.
		session.ship.velocity = ram::direction(session.ram) * ram::surgeSpeed();
		session.ship.position += session.ship.velocity * time.game;
		session.ship.facing = ram::direction(session.ram);
		session.ship.thrust = ram::direction(session.ram);
		session.aim = mouseDirection;

		effects::ramTrail(session.ship.position, ram::direction(session.ram), shipSize,
			shipAtlas.get(3, 0), time.game);
	}
	else
	{
		// Scoped (S4b), no thrust -- the ship drifts as if cloaked -- and it
		// brakes to a stop, still turning to the aim.
		// Inside a field, slower (W2).
		session.ship.medium = interior::at(session.ship.position);
		// Shift brakes: no thrust, and quickly, not at once, to a stop.
		const bool braking = !ImGui::GetIO().WantCaptureKeyboard && platform::isButtonHeld(platform::Button::Shift);
		session.aim = playerMove::update(session.ship, mouseDirection, time.game,
			energy::isCloaked(session.energy) || scope::held(), braking, session.mode);
		if (scope::held()) { session.ship.velocity *= scope::brake(time.game); }
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
				const bool prowTakesIt = ram::barrierUp(session.ram) && glm::dot(-contact.outward, ram::direction(session.ram)) > 0.f;
				if (prowTakesIt)
				{
					shield::ramImpact(session.energy.bubble);
					effects::shake(0.6f);
				}
				else if (energy::onHit(session.energy, -contact.outward * hull.radius) == energy::HitResult::Damaged)
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
		session.ship.position, controls && !energy::isCloaked(session.energy)))
	{
		startExtraction();
	}

	// What the ram strikes: the arc's reach, a little ahead of the hull. Each
	// enemy once per ram; the player takes nothing.
	if (ram::active(session.ram))
	{
		const collision::Circle front = {session.ship.position + ram::direction(session.ram) * (shipSize * 0.3f),
			shipSize * 0.65f};

		// Rocks too (gameplay roadmap A2): each struck once per ram, shoved
		// along it and spun if struck off-centre. The ship goes on through.
		asteroids::ram(front.center, front.radius, ram::direction(session.ram), session.ram.serial);

		for (int e = 0; e < (int)session.enemies.size(); e++)
		{
			Enemy &enemy = session.enemies[e];
			// A cloaked enemy is not solid (the convention, at syncSolid): the prow
			// passes it.
			if (!enemy.body.solid || !collisionSystem.overlaps(front, enemy.getHitbox())
				|| !ram::firstHit(session.ram, enemy.id)) { continue; }

			// The instant of contact: the game stops dead for a moment, the prow
			// flares, the world shakes -- then the enemy goes.
			gameClock::hitStop(ram::hitStopSeconds());
			shield::ramImpact(session.energy.bubble);
			effects::shake(1.f);
			// Through its energy (B1): a shield takes the strike as it takes a
			// shot, and breaks; without one, the hull. The blow lands either way.
			if (energy::onHit(enemy.energy, session.ship.position - enemy.body.position) == energy::HitResult::Damaged
				&& !hitboxDebug::isDamageFrozen())
			{
				enemy.life -= ram::hitDamage();
			}
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
			const glm::vec2 side = {-ram::direction(session.ram).y, ram::direction(session.ram).x};
			const float which = glm::dot(enemy.body.position - session.ship.position, side) >= 0.f ? 1.f : -1.f;
			const glm::vec2 away = glm::normalize(ram::direction(session.ram) + side * which);
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
		// Scoped (S4b), the view leans toward the pointer -- the nearer the
		// screen's edge, the further -- and eases there, and back, at a rate:
		// the chase's fixed speed and its leash would crawl or snap across a
		// lean that size. Settled again, the chase takes over.
		const glm::vec2 viewSize = {(float)w, (float)h};
		const float zoomNow = std::max(renderer.currentCamera.zoom, 1e-4f);
		const glm::vec2 lean = scope::lean(viewSize / zoomNow);
		// Only the scope's lean and its way back: a restart or a teleport
		// still snaps by the chase's leash, as it always has.
		static bool comingBack = false;
		if (scope::amount() > 0.f) { comingBack = true; }
		else if (glm::distance(cameraBase + viewSize * 0.5f, session.ship.position) <= 150.f) { comingBack = false; }
		if (scope::amount() > 0.f || comingBack)
		{
			cameraBase = camera::ease(cameraBase, session.ship.position + lean, viewSize,
				scope::cameraRate(), time.real);
		}
		else
		{
			cameraBase = camera::follow(
				cameraBase, session.ship.position, viewSize,
				{time.real * 550.f, 0.f, 150.f});
		}
	}

	// The world shake rides on top: the whole world moves, background and all,
	// and the HUD, drawn with its own screen camera, stays still.
	renderer.currentCamera.position = cameraBase + effects::shakeOffset(time.real)
		+ ram::cameraLean(session.ram); // and leans ahead while ramming

#pragma endregion

#pragma region render background

	// Wall time, not game time: see zoomControl.h.
	// With a level, nothing is removed for distance, so no ring bounds the zoom.
	// Scoped (S4b), zoomed out further, toward the scope's zoom.
	renderer.currentCamera.zoom = scope::zoom(zoomControl::update(time.real,
		{(float)w, (float)h}, levelLoaded ? 0.f : enemyDespawnDistance));

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
	// No firing while scoped (S4b): the scope is for finding, not fighting.
	const bool trigger = controls && !stunnedNow && !scope::held()
		&& platform::isLMouseHeld() && !ImGui::GetIO().WantCaptureMouse;
	// What the player can see from where the ship now is (sight roadmap S2):
	// rebuilt once a frame, before anything asks -- a missile's lock first.
	// Scoped, its long narrow cone joins the sight and the all-round sight
	// shrinks (S4b).
	scope::turnToward(session.aim, time.real); // slowly, as a periscope turns
	const sight::Scope scoped = scope::view();
	sight::updatePlayer(session.ship.position, energy::isCloaked(session.energy), session.aim, time.real,
		scope::amount() > 0.f ? &scoped : nullptr);

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
	// A missile locks onto the enemy nearest the mouse -- one the player can
	// see, if the Sight section says so (S2). Once locked it chases it,
	// seen or not.
	fire.missileTarget = weapons::nearestEnemy(mouseWorld, session.enemies, [](const Enemy &e)
	{
		return !sight::locksNeedSight() || sight::playerSees(e.body.position);
	});
	// Paused, not at all: a weapon that is ready fires whatever the clock
	// says, and the beam should stay on screen as it was, not switch off.
	const int shots = gameState::paused() ? 0
		: weapons::update(playerWeapons, time.game, trigger, fire, session.bullets);
	if (shots > 0)
	{
		// Firing is how the player leaves the cloak, and the shot still goes out.
		energy::uncloak(session.energy);
		for (int s = 0; s < shots; s++) { sfx::playerShot(); }
	}

	// The laser: traced, not flown. It reaches the edge of the view unless an
	// enemy is in the way, and burns the first one it touches for as long as
	// it touches it (gameplay roadmap C3b). A shielded enemy is where it
	// stops without its damage: the shield holds it, unbroken, and it
	// splashes off (B1).
	static float effectClock = 0.f; // drives the beam's scroll and flicker
	effectClock += time.game;

	const weapons::Beam beam = weapons::beam(playerWeapons);
	glm::vec2 beamEnd = {};
	bulletLook::BeamImpact beamImpact = bulletLook::BeamImpact::None;
	glm::vec2 beamSurface = {}; // the core's outward normal where a deflected beam meets it
	if (beam.firing)
	{
		energy::uncloak(session.energy);
		if (beam.started && !gameState::paused()) { sfx::playerShot(); }

		float reach = distanceToViewEdge(beam.origin, beam.direction, view);
		int target = -1;
		for (int e = 0; e < (int)session.enemies.size(); e++)
		{
			// Cloaked, it is not there to the beam (energy::onBeam would say
			// Missed): the beam runs on past it.
			if (energy::isCloaked(session.enemies[e].energy)) { continue; }
			const float t = collision::rayToCircle(beam.origin, beam.direction,
				session.enemies[e].getHitbox());
			if (t >= 0.f && t < reach) { reach = t; target = e; }
		}

		// In fight mode only a core stops it, as every weapon. In flight mode
		// it stops at the first rock on its line, in a field or out, and burns
		// it: that is how ore is mined (A4, sight roadmap M1).
		int rockHit = -1;
		const float rock = sight::beam(beam.origin, beam.direction, reach,
			shipMode::beamMines(session.mode), &rockHit);
		if (rock >= 0.f)
		{
			reach = rock;
			target = -1;
		}

		beamEnd = beam.origin + beam.direction * reach;
		// What it burns, it lights: a circle of sight round the burn, so a
		// rock being mined shows in colour under the fog.
		if (rockHit >= 0 || target >= 0) { sight::reveal(beamEnd, sight::beamLight()); }
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

		energy::HitResult onEnemy = energy::HitResult::Missed;
		if (target >= 0)
		{
			Enemy &struck = session.enemies[target];
			onEnemy = energy::onBeam(struck.energy, beamEnd - struck.body.position, time.game);
			if (onEnemy == energy::HitResult::Blocked)
			{
				beamImpact = bulletLook::BeamImpact::Deflect;
				beamSurface = beamEnd - struck.body.position;
				// The shield held it, but the enemy felt the beam: engaged,
				// turned toward the player, as a hit on the hull already does.
				enemyAi::alert(struck, session.ship.position);
			}
		}
		if (onEnemy == energy::HitResult::Damaged && !hitboxDebug::isDamageFrozen())
		{
			hitEffectsOnEnemy(session.enemies[target], beam.stun, beam.lockdown);
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
	targets.playerTargetable = gameState::playerPresent() && !energy::isCloaked(session.energy);
	weapons::steerMissiles(session.bullets, targets, time.game);

	for (int i = 0; i < session.bullets.size(); i++)
	{
		
		if (glm::distance(session.bullets[i].position, session.ship.position) > 5'000)
		{
			session.bullets.erase(session.bullets.begin() + i);
			i--;
			continue;
		}

		// Only a core stops a shot, anyone's, missiles too: every other rock
		// and every field edge it passes (sight roadmap M1). Rocks are cover
		// from sight, not from fire.
		{
			Bullet &b = session.bullets[i];
			const collision::Circle hitbox = b.getHitbox();
			const glm::vec2 flewFrom = b.sweptFrom;
			const bool missile = b.motion == BulletMotion::Missile;
			const sight::Stop stop = sight::shot(hitbox.center, hitbox.radius);
			b.sweptFrom = hitbox.center;
			// A shot of the player's that crosses an enemy's cone wakes it,
			// even when the shot goes on to miss or to stop on a rock. The
			// part past a rock was never flown.
			if (!b.fromEnemy())
			{
				const glm::vec2 flewTo = stop.stopped ? stop.point : hitbox.center;
				for (Enemy &enemy : session.enemies)
				{
					enemyAi::noticeShot(enemy, flewFrom, flewTo, session.ship.position);
				}
			}
			if (stop.stopped)
			{
				if (stop.rock >= 0)
				{
					asteroids::shot(stop.rock, stop.point, b.fireDirection, b.damage, missile);
				}
				// A missile bursts on a core (S2): the one rock it does not pass.
				if (missile && asteroids::isCore(stop.rock)) { effects::fireball(stop.point, shipSize * 0.6f * b.size); }
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
						// Through its energy (B1): a shield takes the shot and
						// starts to break; without one up, the hull. A missile
						// ignores the shield, as one does against the player:
						// through to the hull, and the bubble does not ripple,
						// break, or lose energy. Cloaked, either way, it passes
						// through and the shot flies on.
						Enemy &struck = session.enemies[e];
						energy::HitResult result = energy::HitResult::Damaged;
						if (session.bullets[i].motion != BulletMotion::Missile)
						{
							result = energy::onHit(struck.energy,
								session.bullets[i].position - struck.body.position);
						}
						else if (energy::isCloaked(struck.energy))
						{
							result = energy::HitResult::Missed;
						}
						if (result == energy::HitResult::Missed) { continue; }
						if (result == energy::HitResult::Damaged)
						{
							struck.life -= session.bullets[i].damage;
							hitEffectsOnEnemy(struck, session.bullets[i].stun, session.bullets[i].lockdown);
						}

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
				if (gameState::playerPresent() && !energy::isCloaked(session.energy) &&
					collisionSystem.overlaps(session.bullets[i].getHitbox(),
					game::shipHitbox(session.ship.position, shipSize)))
				{
					// Shot at all -- prow, shield or hull -- and the gate's
					// start is lost (gameplay roadmap L5).
					gate::playerShot();

					// The ram's prow takes shots from the front while it is out.
					if (ram::barrierUp(session.ram) && glm::dot(session.bullets[i].position - session.ship.position,
						ram::direction(session.ram)) > 0.f)
					{
						session.bullets.erase(session.bullets.begin() + i);
						i--;
						continue;
					}

					// A missile ignores the shield, as the player's do against an
					// enemy: it passes through and hits the hull. The bubble
					// does not ripple, break, or lose energy. Anything else
					// asks the shield, which takes it while up.
					energy::HitResult hit = energy::HitResult::Damaged;
					if (session.bullets[i].motion != BulletMotion::Missile)
					{
						// Relative to the ship, because the shield moves with it
						// and the ripple has to stay anchored to the bubble.
						hit = energy::onHit(session.energy, session.bullets[i].position - session.ship.position);
					}

					if (hit == energy::HitResult::Damaged)
					{
						// The shot's own damage, as the player's shots do to
						// enemies -- since enemies carry the player's weapons
						// (B1), a heavy laser or a missile hits harder than the
						// old fixed 0.1.
						session.health -= session.bullets[i].damage;
						hud::onDamage();  // shake the HUD when the hull is hit
						resources::interrupt(); // and the drill loses its hold
						hitEffectsOnPlayer(session.bullets[i].stun, session.bullets[i].lockdown);
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
		energy::uncloak(session.energy);
		session.ram = {};
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
	sightViewers.clear();
	playerSeenInField = false;
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
		// Its energy refills and its shield breaks on time, as the player's
		// (B1). Burning outside the circle is not a hit: no shield stops it.
		energy::update(session.enemies[i].energy, time.game);
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
					if (energy::onHit(e.energy, -contact.outward * hull.radius) == energy::HitResult::Damaged
						&& !hitboxDebug::isDamageFrozen())
					{
						e.life -= rules.damage;
					}
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
		// (gameplay roadmap A1): what makes a rock somewhere to hide. And a
		// field's edge, from either side: a field is a room, so an enemy in
		// the same field sees the player and one outside does not (sight
		// roadmap S1). A hit enemy that can't see is alerted as ever, turns,
		// finds nothing, and comes to search.
		// Looking out of a field, it looks along its own sight cone.
		sight::Viewer viewer;
		viewer.position = session.enemies[i].body.position;
		viewer.look.facing = session.enemies[i].body.facing;
		viewer.look.halfAngle = session.enemies[i].sightHalfAngle;
		viewer.look.range = session.enemies[i].sightRange;
		sightViewers.push_back(viewer);
		const bool hidden = energy::isCloaked(session.energy) || !gameState::playerPresent()
			|| !sight::clear(viewer.position, session.ship.position, &viewer.look);
		Enemy &e = session.enemies[i];
		enemyAi::Player seen;
		seen.position = session.ship.position;
		seen.velocity = session.ship.velocity;
		seen.facing = session.ship.facing;
		seen.hidden = hidden;
		seen.shielded = session.energy.hasShield && (session.energy.state == energy::State::Full
			|| session.energy.state == energy::State::Breaking);
		e.effectImmune = std::max(0.f, e.effectImmune - time.game);
		e.body.medium = interior::at(e.body.position); // slower inside a field, as the player is (W2)
		const enemyAi::Orders orders = enemyAi::update(e, time.game, seen, comingBack ? &wayIn : nullptr);
		if (playerInField && !hidden && e.awareness == Enemy::Awareness::Engaged) { playerSeenInField = true; }
		if (orders.phaseChanged)
		{
			// A boss entering its next phase (B2): felt, and seen in its shield.
			effects::shake(0.7f);
			shield::hit(e.energy.bubble, {0.f, -1.f}, 1.f);
		}

		// Its gun, through the same weapons::update as the player's (B1). Only
		// while it fights, as it always was: the AI decides the trigger, the
		// loadout the cooldown -- and, with several weapons, which one (B2).
		if (orders.fighting)
		{
			weapons::FireContext gun;
			gun.origin = e.body.position;
			gun.aim = e.body.facing;
			gun.shipVelocity = e.body.velocity; // its shots carry its motion, as the player's do (B2)
			gun.shipSize = e.size;
			gun.shooter = e.id;
			// Its missiles, if it rolled them, chase the player: locked only
			// while it can see the player, if the Sight section says so (S2),
			// and chasing once locked, seen or not.
			gun.missileTarget = (!sight::locksNeedSight() || !hidden) ? playerShip : noShip;
			const int fired = weapons::update(e.loadout, time.game, orders.trigger, gun, session.bullets);
			if (fired > 0) { sfx::enemyShot(); }
			// Firing uncloaks it, as it does the player -- and the shot still
			// goes out: the ambush (B1 step 3).
			if ((fired > 0 || weapons::beam(e.loadout).firing) && energy::isCloaked(e.energy))
			{
				energy::uncloak(e.energy);
			}

			// Its beam, if it rolled the laser: traced from the nose, stopped by
			// what its mode says (M1: in fight mode, only a core) or by the
			// player, never past enemyBeamRange. It does not push or mine the
			// rock it stops on -- that is the player's beam's job. On the player, energy decides (onBeam): a shield holds it
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
				const float toRock = sight::beam(eb.origin, eb.direction, reach,
					shipMode::beamMines(e.mode), &rock);
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
					switch (energy::onBeam(session.energy, at - session.ship.position, time.game))
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
						hitEffectsOnPlayer(eb.stun, eb.lockdown);
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

		// Its cloak decides whether it is solid (the convention, at
		// syncSolid), and a ram leaves the player's afterimages behind it.
		e.body.solid = !energy::isCloaked(e.energy);
		if (ram::active(e.ram))
		{
			effects::ramTrail(e.body.position, ram::direction(e.ram), e.size,
				shipAtlas.get(e.type.x, e.type.y), time.game);
			// And its prow shoves the rocks it meets, as the player's does.
			const glm::vec2 heading = ram::direction(e.ram);
			asteroids::ram(e.body.position + heading * (e.size * 0.3f), e.size * 0.65f,
				heading, e.ram.serial);
		}
	}

	// An enemy's ram (B1 step 3): its prow strikes the player once per ram.
	// The game stops for an instant and shakes, as for the player's own; the
	// hit goes through the player's energy (the shield takes it and breaks);
	// and the player is knocked aside, as a rammed enemy is, and stunned
	// briefly. A cloaked player is not solid and is passed. The player's own
	// prow, out and facing the rammer, takes the hit instead -- no damage and
	// no stun, only the blow -- as it takes a core.
	syncSolid();
	if (gameState::playerPresent() && session.ship.solid)
	{
		const collision::Circle playerHull = game::shipHitbox(session.ship.position, shipSize);
		for (Enemy &e : session.enemies)
		{
			if (!ram::active(e.ram)) { continue; }
			const glm::vec2 heading = ram::direction(e.ram);
			const collision::Circle prow = {e.body.position + heading * (e.size * 0.3f), e.size * 0.65f};
			if (!collisionSystem.overlaps(prow, playerHull) || !ram::firstHit(e.ram, playerShip)) { continue; }

			gameClock::hitStop(ram::hitStopSeconds());
			shield::ramImpact(e.energy.bubble);
			effects::shake(1.f);
			const glm::vec2 toRammer = e.body.position - session.ship.position;
			const bool prowTakesIt = ram::barrierUp(session.ram) && glm::dot(toRammer, ram::direction(session.ram)) > 0.f;
			if (prowTakesIt)
			{
				shield::ramImpact(session.energy.bubble);
			}
			else
			{
				if (energy::onHit(session.energy, toRammer) == energy::HitResult::Damaged)
				{
					if (!hitboxDebug::isDamageFrozen()) { session.health -= ram::hitDamage(); }
					hud::onDamage();
					resources::interrupt();
				}
				session.stunned = ram::playerStunSeconds();
				const float turns = 6.f + (rand() % 1000) / 200.f; // 6 .. 11 rad/s, as a rammed enemy
				session.stunSpin = (rand() % 2) ? turns : -turns;
				ram::stop(session.ram); // a ram of its own, struck, ends there
			}

			// Aside, 45 degrees off the ram's line toward the player's side, as
			// the player's ram throws enemies.
			const glm::vec2 side = {-heading.y, heading.x};
			const float which = glm::dot(session.ship.position - e.body.position, side) >= 0.f ? 1.f : -1.f;
			movement::push(session.ship, glm::normalize(heading + side * which) * (ram::surgeSpeed() + ram::knockbackSpeed()));
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
		// Each its own size: a boss is larger (B2).
		auto radiusOf = [](const Enemy &e) { return game::shipHitboxRadius(e.size); };
		for (Enemy &e : session.enemies) { e.bumpGrace = std::max(0.f, e.bumpGrace - time.game); }
		for (size_t a = 0; a < session.enemies.size(); a++)
		{
			for (size_t b = a + 1; b < session.enemies.size(); b++)
			{
				movement::collide(session.enemies[a].body, radiusOf(session.enemies[a]),
					session.enemies[b].body, radiusOf(session.enemies[b]), shipBounce);
			}
		}
		if (gameState::playerPresent() && !ram::active(session.ram) && !ram::windingUp(session.ram))
		{
			const float playerRadius = game::shipHitboxRadius(shipSize);
			for (int i = 0; i < (int)session.enemies.size(); i++)
			{
				Enemy &e = session.enemies[i];
				const float enemyRadius = radiusOf(e);
				const movement::Contact contact = movement::collide(session.ship, playerRadius,
					e.body, enemyRadius, shipBounce);
				if (!contact.touched || contact.impactSpeed < bumpMinSpeed || e.bumpGrace > 0.f) { continue; }
				e.bumpGrace = bumpGrace;

				// The player: the shield takes it if it is up, from the side the
				// enemy struck; with it down, the hull.
				const glm::vec2 toward = e.body.position - session.ship.position;
				const float distance = glm::length(toward);
				const glm::vec2 side = distance > 1e-3f ? toward / distance : glm::vec2(1.f, 0.f);
				if (energy::onHit(session.energy, side * playerRadius) == energy::HitResult::Damaged)
				{
					if (!hitboxDebug::isDamageFrozen()) { session.health -= bumpDamage; }
					hud::onDamage();
					resources::interrupt();
				}

				// The enemy, through its energy as the player is: a shield takes it.
				if (energy::onHit(e.energy, -side * enemyRadius) == energy::HitResult::Damaged
					&& !hitboxDebug::isDamageFrozen())
				{
					e.life -= bumpEnemyDamage;
				}
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
	// Each enemy's cloak, eased (B1 step 3): how faint its hull is, and how
	// hard the world bends round it -- its own field in the cloak's pass.
	// Mostly cloaked, it gives nothing away: no cone, no awareness mark.
	for (auto &e : session.enemies)
	{
		e.cloakLevel = cloak::ease(e.cloakLevel, energy::isCloaked(e.energy), time.game);
	}
	auto hidden = [](const Enemy &e) { return e.cloakLevel > 0.5f; };

	// What the fog hides (sight roadmap S3). Under it the world is greyed,
	// and a grey enemy still says where it is, so an enemy the player cannot
	// see is not drawn at all -- nor anything that gives it away: its cone,
	// plume, shield, mark, lock box, burn flash, the world bending round it
	// cloaked, its shots and its ram's trail. Its beam is drawn from where the
	// player's sight first reaches it. Once any of its hull is in sight it is
	// drawn whole, and the part still in the fog comes out grey. No fog in the
	// editor, or with the fog off.
	const bool fogged = !levelEditor::active() && worldGrade::fogOn();
	const bool fogHides = fogged && sight::hidesUnseenEnemies();
	static std::vector<char> enemyInSight;
	enemyInSight.resize(session.enemies.size());
	for (size_t i = 0; i < session.enemies.size(); i++)
	{
		const Enemy &e = session.enemies[i];
		enemyInSight[i] = !fogHides || sight::playerSeesShip(e.body.position, e.getHitbox().radius);
	}
	auto inSight = [&](const Enemy &e) { return enemyInSight[(size_t)(&e - session.enemies.data())] != 0; };

	// Where the player last saw each (sight roadmap S4): an enemy is lost when
	// the fog hides it, or when it cloaks in sight. With nothing hidden there
	// is nothing to remember.
	if (fogHides)
	{
		lastKnown::update(session.enemies, [&](const Enemy &e) { return inSight(e) && !hidden(e); },
			[&](const Enemy &e) { return shipAtlas.get(e.type.x, e.type.y); }, time.game);
	}
	else
	{
		lastKnown::reset();
	}
	const effects::Shown traceShown = fogHides
		? effects::Shown([](glm::vec2 p) { return sight::playerSeesShip(p, 60.f); }) : effects::Shown();
	const effects::Shown explosionShown = fogged
		? effects::Shown([](glm::vec2 p) { return sight::explosionShown(p); }) : effects::Shown();

	for (const auto &e : session.enemies)
	{
		if (inSight(e)) { cloak::addField(e.body.position, e.size, e.cloakLevel); }
	}

	if (enemyAi::showCones())
	{
		renderer.setBlendMode(wgpu2d::BlendMode::Additive);
		// Each cone as the enemy really sees (S6): its own polar map, so rocks
		// and field edges cut it exactly where they would hide the player.
		static visibility::PolarMap coneMap;
		for (const auto &e : session.enemies)
		{
			if (hidden(e) || !inSight(e) || e.stunned > 0.f) { continue; }
			sight::Look look;
			look.facing = e.body.facing;
			look.halfAngle = e.sightHalfAngle;
			look.range = e.sightRange;
			sight::coneView(e.body.position, look, coneMap);
			effects::drawSight(renderer, e, coneMap);
		}
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
		if (!inSight(e)) { continue; }
		thruster::drawPlume(renderer, e.body.position, e.size, e.body.facing, e.plume,
			effectClock + (float)(e.id % 7u) * 0.13f, enemyPlumeColour);
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	for (auto &e : session.enemies)
	{
		if (!inSight(e)) { continue; }
		// Darkened where a field rock's shadow falls on it (A3).
		const float lit = 1.f - asteroids::shadowOn(e.body.position, e.getHitbox().radius);
		// A boss's hull warmer, toward the enemies' red (B2), so it reads as
		// their champion even with its shield down.
		const glm::vec3 tint = e.behaviour == Enemy::Behaviour::Boss ? glm::vec3(1.f, 0.78f, 0.72f) : glm::vec3(1.f);
		renderSpaceShip(renderer, e.body.position, e.size,
			shipSheet, shipAtlas.get(e.type.x, e.type.y), e.body.facing,
			{lit * tint.r, lit * tint.g, lit * tint.b, cloak::shipAlpha(e.cloakLevel)});
	}

	// Their shields, over the hulls as the player's is (B1) -- only on an
	// enemy that has one, in the enemies' colours -- and their rams' prows,
	// which the bubble draws in its place (B1 step 3).
	for (auto &e : session.enemies)
	{
		shield::setRam(e.energy.bubble, ram::barrierLevel(e.ram), ram::direction(e.ram));
		if (inSight(e) && (e.energy.hasShield || ram::barrierUp(e.ram)))
		{
			shield::draw(renderer, e.energy.bubble, e.body.position, e.size, time.game);
		}
	}

	// What each knows: red engaged, amber searching.
	for (const auto &e : session.enemies) { if (!hidden(e) && inSight(e)) { effects::drawAwareness(renderer, e, effectClock); } }

	// Wrecks sit where ships sit: after them, under everything else.
	effects::drawDebris(renderer, explosionShown);


	// A missile's lock on its target: a dashed red box, until impact.
	for (const auto &b : session.bullets)
	{
		if (b.motion != BulletMotion::Missile || b.target == noShip) { continue; }
		for (const auto &e : session.enemies)
		{
			if (e.id != b.target || !inSight(e)) { continue; }
			effects::drawTargetBox(renderer, e.body.position, e.size * 1.3f, b.age);
		}
	}

#pragma endregion

#pragma region render ship

	// The ram's afterimages, under everything of the ship's own.
	effects::drawAfterimages(renderer, shipSheet, traceShown);

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
			shield::draw(renderer, session.energy.bubble, session.ship.position, shipSize, time.game);
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
		// Amber while an enemy in the field can see you, if the Sight
		// section says so (sight roadmap S1): hidden from outside, not from it.
		const float pulse = 0.5f + 0.5f * std::sin(effectClock * 3.f);
		if (playerSeenInField && sight::warnsWhenSeen()) { outline::begin(renderer, pulse, sight::seenColour()); }
		else { outline::begin(renderer, pulse); }
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
	// An enemy's shot is drawn once it is in the player's sight (S3): one
	// coming out of the fog is a sniper's whole threat.
	auto shotInSight = [&](const Bullet &b)
	{
		return !fogHides || !b.fromEnemy() || sight::playerSeesShip(b.position, bulletHitboxRadius * b.size);
	};
	// And its beam from where that sight first reaches it, so it does not
	// point back at a shooter the player cannot see.
	static std::vector<EnemyBeam> beamsInSight;
	beamsInSight.clear();
	for (EnemyBeam b : enemyBeams)
	{
		if (fogHides)
		{
			const float from = sight::beamSeenFrom(b.start, b.end);
			if (from < 0.f) { continue; }
			const glm::vec2 line = b.end - b.start;
			const float length = glm::length(line);
			if (length > 0.f) { b.start += line / length * from; }
		}
		beamsInSight.push_back(b);
	}

	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	for (auto &b : session.bullets)
	{
		if (!shotInSight(b)) { continue; }
		if (b.motion == BulletMotion::Missile)
		{
			// The ship's plume, small and green: out while the missile is
			// pushed clear, then brightening as it picks up speed.
			thruster::drawPlume(renderer, b.position, 130.f * b.size, b.fireDirection,
				weapons::missileThrottle(b), b.age, glm::vec4(0.30f, 0.85f, 0.35f, 1.f));
		}
		bulletLook::drawGlow(renderer, b.position, b.fireDirection, b.fromEnemy(), b.style, b.size,
			b.lockdown ? bulletLook::Mark::Lockdown : b.stun ? bulletLook::Mark::Stun : bulletLook::Mark::None);
	}
	if (beam.firing)
	{
		bulletLook::drawBeamGlow(renderer, beam.origin, beamEnd, beamImpact, effectClock, beamSurface,
			false, shipMode::beamMines(session.mode));
	}
	for (const EnemyBeam &b : beamsInSight)
	{
		bulletLook::drawBeamGlow(renderer, b.start, b.end, b.impact, effectClock, b.surface, true);
	}
	effects::drawGlow(renderer, explosionShown, traceShown);
	resources::drawGlow(renderer, effectClock);
	arena::draw(renderer, renderer.currentCamera.zoom);
	gate::draw(renderer, renderer.currentCamera.zoom);
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	for (auto &b : session.bullets)
	{
		if (!shotInSight(b)) { continue; }
		bulletLook::drawSprite(renderer, b.position, b.fireDirection, b.fromEnemy(), b.style, b.size);
	}
	if (beam.firing)
	{
		bulletLook::drawBeamCore(renderer, beam.origin, beamEnd, effectClock, false,
			shipMode::beamMines(session.mode));
	}
	for (const EnemyBeam &b : beamsInSight) { bulletLook::drawBeamCore(renderer, b.start, b.end, effectClock, true); }

#pragma endregion

#pragma region debug hitboxes

	hitboxDebug::draw(renderer, collisionSystem,
		game::shipHitbox(session.ship.position, shipSize), session.enemies, session.bullets);
	sight::drawDebug(renderer, sightViewers, session.ship.position);

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
	// And what the player cannot see is fogged (sight roadmap S3): greyed,
	// with the player's sight drawn over it in colour. Not in the editor.
	// The fog is drawn from the eased map, so its edge does not jitter, and
	// with what the player's beam lights (S3 playtest).
	static std::vector<worldGrade::Reveal> lit;
	lit.clear();
	for (const sight::Reveal &r : sight::reveals()) { lit.push_back({r.centre, r.radius}); }
	worldGrade::apply(renderer, gameState::pauseLook(), arena::radius() > 0.f ? &safe : nullptr, w, h,
		fogged ? &sight::playerDrawnMap() : nullptr, fogged ? &lit : nullptr);

	// Burn ticks flash hulls red, drawn over the grade so the red survives it.
	// Still in the world's batch, so the cloak bends them with the rest.
	renderer.setBlendMode(wgpu2d::BlendMode::Additive);
	for (const Enemy &e : session.enemies)
	{
		if (!inSight(e)) { continue; }
		arena::drawBurnFlash(renderer, e.burnFlash, e.body.position, e.size,
			shipSheet, shipAtlas.get(e.type.x, e.type.y), e.body.facing);
	}
	if (gameState::current() != gameState::State::Dying)
	{
		arena::drawBurnFlash(renderer, arena::burnFlash(), session.ship.position, shipSize,
			shipSheet, shipAtlas.get(3, 0), session.ship.facing, cloak::shipAlpha(),
			gameState::warpStretch());
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	// The ghosts of enemies out of sight (S4), over the grade so the fog does
	// not grey them; still in the world's batch, so the cloak bends them.
	if (fogHides) { lastKnown::draw(renderer, shipSheet); }

	// And where the enemies think the player is (S5): the player's own hull,
	// faint, where each searching enemy last saw it.
	if (!levelEditor::active() && gameState::playerPresent())
	{
		theirGhost::draw(renderer, session.enemies,
			[&](const Enemy &e) { return inSight(e) || lastKnown::remembers(e.id); },
			shipSheet, shipAtlas.get(3, 0), shipSize);
	}
	renderer.setBlendMode(wgpu2d::BlendMode::Alpha);

	// The ram's aim, while the right button is held: where it will end. Over
	// the grade, so the fog does not grey it.
	if (session.aimingRam && gameState::playerPresent() && !ram::barrierUp(session.ram))
	{
		const float hull = game::shipHitbox(session.ship.position, shipSize).radius;
		ramPath::draw(renderer, session.ship.position,
			ramPath::end(session.ship.position, mouseDirection, hull), ram::ready(session.ram) >= 1.f);
	}

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

	// And to each ghost off screen (sight roadmap S4): a smaller chevron in the
	// ghosts' colour toward where an enemy was last seen, fading as it does.
	if (fogHides && lastKnown::arrowsShown())
	{
		const glm::vec4 view = renderer.getViewRect();
		if (view.z != 0.f && view.w != 0.f)
		{
			const glm::vec3 colour = lastKnown::ghostColour();
			lastKnown::forEachGhost([&](glm::vec2 position, float alpha)
			{
				const glm::vec2 onScreen = {(position.x - view.x) / view.z * (float)w,
					(position.y - view.y) / view.w * (float)h};
				hud::markOffScreen(onScreen, {colour, alpha});
			});
		}
	}

	hud::WeaponSlot slots[weapons::slotCount];
	for (int s = 0; s < weapons::slotCount; s++)
	{
		const weapons::SlotView v = weapons::slot(playerWeapons, s);
		slots[s] = {v.style, v.ready, v.ammo, v.maxAmmo, v.selected, v.usable};
	}

	hud::showMode(session.mode == shipMode::Mode::Flight);
	// Flushes the world, then the HUD.
	hud::draw(renderer, session.health, energy::level(session.energy), slots, weapons::slotCount,
		ram::ready(session.ram), w, h);

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
