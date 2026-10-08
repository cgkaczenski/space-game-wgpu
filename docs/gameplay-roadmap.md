# Gameplay roadmap

The render track (`docs/roadmap.md`) built the engine. This file is for the
game: what the player does, why, and what stops them.

**How this file works.** The author drives the requirements here. Items start
loose on purpose and are refined together before any code, one milestone per
session, as `AGENTS.md` asks. Nothing below is decided unless it says so.

Two kinds of note appear under the items, and they are kept apart:

- **Open questions** — what has to be answered before building.
- **Engine ideas** — _suggestions only_, from what `wgpu2d` already makes
  cheap: per-quad effect parameters (F6), effects that read the world target
  (F2, the cloak), `LayerEffect` (the HUD shake), additive glow, render
  targets, compute. Take them or leave them.

The goal the items lead to is **a first playable level**. Combat comes first,
because a weapon or a shield can only be tuned against something the player is
trying to do — but the level is what gives it that.

---

## Now — combat

### C1. Energy: the shield and the cloak become gameplay

Today both are debug toggles with no cost. Wire them into play, possibly with a
second HUD bar (new textures, or the health bar's reused).

**Decided (first pass)**
- The shield is up by default, and energy is one blue bar under the health bar.
  It has its own art: the health bar hue-shifted, because a tint multiplies
  and a red fill tinted blue comes out near black.
- A hit on the shield is blocked, the bar drops to zero, and the shield breaks
  when that hit's ripple finishes. Hits during the ripple are blocked and ripple
  too, but do not delay the break. A missile is not such a hit: it passes
  through and damages the hull, and the shield is left as it was.
- With the shield down, energy refills slowly (8 s, a debug slider) and the
  shield returns when it is full. A hit while it is down does damage and empties
  the bar again.
- **E** cloaks, and only with a full bar: energy to zero, shield breaks,
  thrusters off. The ship drifts on its momentum with no falloff and cannot
  thrust, be hit, or refill energy — the path is something to watch, not
  control — but the hull still turns, so a shot can be lined up. **Firing**
  uncloaks (the shot goes out), falloff returns, and the refill starts from
  empty.
- The shield breaks with a **dissolve**: the bubble burns away in patches with
  a bright edge, a computed-noise effect on its own quads.
- Health regeneration is a debug option, on by default.
- The camera starts as far out as the window allows.

**Still open**
- Will energy later power other things — weapons, a boost?
- Should a blocked hit shake the HUD? For now only damage does.

**Engine ideas**
- The shield already takes per-quad parameters. Energy can drive its rim
  brightness, and low energy can flicker the ripple, so the bubble itself shows
  how much is left before the HUD does.
- The cloak's refraction strength is one parameter. Scale it by energy and the
  cloak visibly thins as it runs out.
- The shield breaking could be a dissolve on the bubble: one threshold
  parameter plus a noise texture, nothing new in the renderer.
- A second HUD bar is what `hud.cpp` was shaped for, and the empty-energy
  moment can use the same `LayerEffect` shake as damage.

### C2. Multiple weapons with different behaviour

**Decided (first pass)**
- Four weapons, each with its own cooldown, held to auto-fire:
  1. **Burst laser** — two shots in quick succession, then the cooldown.
  2. **Heavy laser** — one larger shot doing triple a regular shot's damage.
  3. **Missile** — special ammunition, 5 for now (pickups later). Its flight
     is C3.
  4. **Laser** — a beam, C3; selectable in the meantime but does not fire.
- Keys **1–4** or the **wheel** (wrapping) select. An empty weapon can be
  selected but not fired. **Ctrl + wheel** zooms (Shift + wheel until Shift
  became the brake).
- A slot row centred along the bottom of the HUD: icon, cooldown shade,
  selected frame, ammo pips. No key numbers.
- Cooldowns longer than first proposed; all tunable in the debug panel.

**Still open**
- Do enemies get the same weapons?

**Engine ideas**
- Each weapon can have its own glow colour and capsule length for free; the
  bullet glow is one tinted texture.
- A charge shot can grow its glow while held. A beam is a stretched capsule.
- Weapons as data (fire rate, count, spread, speed, damage) with one fire
  function for player and enemies was sketched during R9.

### C3. Bullets that behave differently in flight

Homing, piercing, and so on — only if C2 wants them.

**Decided for C2's weapons**
- **Missile:** targets the enemy nearest the mouse when fired. It leaves at
  the player's speed, travelling parallel to the player, then steadily speeds
  up and homes. It cannot miss unless the target cloaks (enemies cannot yet).
  If the target dies first, it flies on in the same direction with no more
  homing, and can still hit something else.
- **Laser:** a beam across the screen that stops at the first thing it hits,
  doing constant damage while it touches. Blocked by shields, and cannot break
  one. Fires while held for up to 5 s, then cools down.
- Split in two: **C3a** the missile, **C3b** the laser.
- C3a, after the first playtest: the missile **faces the mouse** from the
  moment it fires and is **pushed sideways** out from alternating wings with
  its motor off (0.3 s, 700 u/s across the line of fire, keeping pace with the
  ship). Then the motor lights from zero and it **accelerates hard** — 14000
  u/s² to 6000 — while the slide fades, turning at 3 rad/s plus 6 rad/s for
  every second of chase. The exhaust brightens with its speed. With no target
  when fired, it keeps to where the player aimed. All debug sliders.
- C3b, the laser: **5 s of charge** that drains while the beam is on and is
  **kept when released**; only an empty charge starts the 4 s cooldown, whose
  end refills it. The HUD slot shows the charge draining. The beam is traced,
  not flown: it reaches the edge of the view unless an enemy's hitbox is in
  the way, and burns the first one at 0.4 life per second while it touches.
  Drawn as the glow capsule stretched along it plus the sheet's cyan beam
  segment tiled and scrolling, with a flickering burst where it hits. Firing
  it uncloaks. Shield blocking waits for enemies to have shields.
- C3b, revised in L3: a part-used charge **trickles back** after a second
  without firing, refilling over 8 s. Keeping what was left and never
  restoring it worked while the beam was only a weapon; once it also mines,
  short burns left a laser that could only be restored by wasting the rest of
  it. Holding the trigger regains nothing and an empty charge still pays the
  4 s cooldown, so running dry is still the mistake. Both times are sliders.
- Engine ideas taken: the beam as the capsule stretched, bloomed by the CRT
  glow, with the sheet's tileable beam segment available; the missile's
  exhaust as the ship's plume scaled down; a ripple ring on the locked target.

**Engine ideas**
- A motion kind on `Bullet` with a switch, not a class per bullet (R9).
- The glow already rotates to the bullet's heading, so a homing shot's curve
  reads with no extra work.

### C4. Hit feedback, enemy deaths, ship collision

Enemies currently vanish when killed, and ships pass through each other.

**Decided — C4a, deaths:** a small explosion — a white-hot flash inside an
orange fireball that swells and fades over 1.1 s — and the enemy's own sprite
cut into six pieces that fly apart spinning, then slow, darken and **stay** as
a wreck field, still turning a little. 400 pieces are kept (about 66 kills);
past that the oldest fade out. Every kill goes through one function, whatever
did it. Tunable under Explosions in the debug panel. The missile's lock is a
**dashed red box** round its target, dashes marching.

**Decided — C4b, the ram:** Space, later **right click**: holding it draws a
red line to where the ram will end, and letting go rams. A 0.1 s **wind-up** first: the ship dips
back as the shield gathers into a **prow** — two thick white-hot bars meeting in
a point ahead of the nose, no blue glass — then the ship surges toward the mouse
at about 6000 for 0.8 s, past its normal top speed, then its momentum settings
take over. **Afterimages** of the ship fall behind it and **streaks** of light
rush past; the **camera leans** ahead and eases back. Ships still pass through
each other outside a ram. An enemy rammed takes 0.4 damage (each at most once
per ram; one ram can hit several), is knocked **aside** — 45° off the ram,
toward the side it was on, faster than the surge, so the ship passes it rather
than running it down — and spins, disabled — no moving, no firing — for 2 s.
On each strike the game **freezes for 60 ms** (hit-stop), the prow flares and
the world shakes; the player takes no damage. It works whatever the energy bar
says and spends none, blocks shots from the front while the prow is out, and
uncloaks. 8 s cooldown, shown as a fifth slot with a shield icon set apart to
the left of the weapon row. All of it on debug sliders. The world shake and
the lean move the camera rather than running the world through the HUD's
LayerEffect: the same result, since the HUD keeps its own screen camera,
without a second full-screen target.

**Open questions**
- How should a kill feel — burst, dissolve, debris?
- Should ships collide with each other, and does ramming do damage?

**Engine ideas**
- A dissolve on death needs nothing new (see C1).
- The shield's ripple effect can run on an enemy hull to show where it was hit.
- A world-space shake, not just the HUD's, is the same `LayerEffect` pointed at
  the world.
- `collision::separation` exists and is tested; nothing calls it yet.
- Many flying fragments is where instancing (N5) starts to pay.

### C5. Cloak against enemy sight

Cloak can only matter once enemies can lose track of the player.

**Decided (first pass):** enemies see along a **cone** from the nose —
rushers 90° wide out to 2500, snipers 60° out to 3500 — and hear anything
within 400 at any angle. Three states: **engaged** (sees the player; fights as
before, and only an engaged enemy fires), **searching** (lost sight: flies to
the last known position and scans, sweeping its cone, for 4 s), **unaware**
(never saw, or gave up: wanders in slow curves at part speed, until levels
bring patrols). Any hit — bullet, missile, laser, ram — engages an enemy and
turns it toward the player. Cloaked, the player is in no one's sight; cloaking
in front of an enemy sends it to search where the player vanished. Cones are
drawn as faint wedges — grey, amber, red by state — behind a **Vision cones**
debug toggle, on by default; a small diamond over each enemy shows red engaged
or pulsing amber searching.

**Open questions**
- What can an enemy see: a range, a cone, line of sight?
- When the player cloaks, do enemies search the last known position?

---

## Next — a first playable level

### L1. Game states
Playing, paused, extracted, dead, restart. R10 defined what restart means.

**Decided (first pass):** no words on screen — every state is shown by what
the world does, since `wgpu2d` has no text yet.
- **Paused:** **Escape** toggles it, and losing window focus pauses. Only
  Escape resumes — coming back to the window stays paused. Game time stops, so
  everything on it freezes mid-motion: ships, bullets, explosions, the beam,
  the shield ripple. The world goes **desaturated and dim**; the HUD stays
  crisp. The camera, zoom and debug panel keep working.
- **Dead:** at zero health the player's ship explodes the way enemies do (C4a,
  fireball and its own sprite as debris). The world runs on at normal speed
  with the controls off and the camera on the wreck; enemies lose the player as
  if cloaked. Then the **CRT collapses to static**, like a set switching off,
  and the game restarts on its own — no key.
- **Extracted:** the ship **warps out** — stretches and shoots forward along
  its heading with the ram's afterimages and streaks — the screen **fades to
  white**, and a new round starts. Until L5's gate exists, a debug button
  triggers it.
- **Restart** is R10's: the round's state goes, settings stay.

### L2. Levels: placing resources and enemies
Hand-placed for now; eventually procedurally generated, so both should load the
same format.

**Known constraint:** enemies are despawned at 4000 units from the player, which
would delete a placed enemy before the player arrives. Levels have to change
that — and doing so also lifts the zoom-out floor.

**Decided (first pass)**
- A level is a **circle** (radius set by the level, about 20000 for the
  first). Past the edge the ship is pushed back gently, and a faint ring shows
  where the edge is. L4's closing circle later shrinks this same radius.
  _(Changed in L4:)_ the push is gone; past the edge you burn instead.
- A level places **enemies** (position, rusher or sniper, facing — they start
  unaware), the **player start** (position and facing), **markers** for
  resources (L3) and the gate (L5), shown only in debug until those exist, and
  **scenery**: backdrop pieces, each with a parallax depth. Decoration, with no
  collision. _(After the first look:)_ the planets are background4.png's four,
  cut out and placed one at a time — that layer is gone from the starfield.
  The black-hole and shattered-planet art is too coarse up close and is only
  for small pieces far back. Depth 0 moves with the world, and reads as the
  furthest thing here, since the starfield's nearer layers move faster.
  _(Changed in A3:)_ the starfield now moves slower than the world, as a
  background should. Planets sit at depth 0.25, between it and the play.
- With a level loaded, **waves are off** and nothing is deleted for distance.
  Enemies away from the view **sleep** — no update, staying where they are —
  and wake when they come within a margin of the view's edge, so everything on
  screen is alive and nothing wakes in sight. The zoom-out floor goes with the
  despawn ring. _(L2a, to confirm:)_ an enemy that is engaged or searching
  stays awake wherever it is, so a chase does not freeze just off screen.
- The format is a **plain text file**, one thing per line, read into a `Level`
  struct that a procedural generator will also fill later.
- An **in-game editor** in the debug panel: pick a type, click to place, drag
  to move, right-click to delete, with facing, radius and depth in panel
  fields. Editing freezes the game and frees the view to pan and zoom over the
  whole arena. Save writes the file; reload reads it back without restarting
  the game; "test from here" starts a round with the ship where the camera is.
- _(L2b, choices made while building — to confirm:)_ a right **click**
  deletes and a right **drag** pans, told apart by whether the mouse moves; the
  wheel zooms about the cursor, down to 0.01. Leaving the editor restarts the
  round from the level as edited, saved or not; the panel shows unsaved
  changes, and Reload throws them away. The start can be moved, not deleted.
  Scenery is picked and dragged where it is drawn, so a deep piece still moves
  under the cursor.
- The first level is sketched by Claude for the author to rework in the
  editor: the player near the edge, a few groups of rushers and snipers,
  markers for three resources and a gate, two or three planets.

### L3. Resources for extraction
_(Changed in A4:)_ deposits are gone. Asteroids are the ore: every rock but a
field's core is mined with the beam, and sheds these same orbs. The rest of L3
-- orbs, the hold, interruption, banking -- stands.
For now, points earned.

**Decided (first pass)**
- Levels place **deposits** — `resource x y amount` — each worth what the
  level says. They are mined with the **laser beam and nothing else**: hold it
  on one and it drains into the hold. The beam's 5 s charge and 4 s cooldown
  mean a big deposit takes more than one pull, and firing uncloaks, so there is
  no mining unseen. A deposit **stops the beam** as an enemy does; ordinary
  shots pass through.
- A **hit interrupts** the drain for 0.6 s; what was taken stays taken.
- An emptied deposit is **destroyed the way a ship is** (C4a), through that
  same function: one blast and the rock's own sprite cut into pieces that fly
  apart, spin, slow and stay. Rubble is what marks a worked route.
- The **hold is unlimited**. Extracting turns it into points, which survive
  the next round. Dying **spills the hold at the wreck** as a deposit to be
  mined again.
- Ore does not appear in the hold. The beam knocks it off as **orbs** — one
  per 0.5 mined — thrown back toward the ship, which **hang still** where they
  stop. An orb within 2600 of the ship **comes for it**, chasing like a
  missile (C3a): accelerating, and turning harder the longer it chases.
  Further away it waits indefinitely, so ore mined from cover has to be
  collected afterwards.
- A killed enemy leaves the same orbs, worth very little each.
- A deposit is a **dim rock with a gold glow** pulsing over it, brighter while
  the beam is on it, and darkening as it empties. _(Tried and dropped:)_ the
  rock cut into a grid of chips that eroded from the beam's side, each orb one
  of those chips — it read worse than the plain glowing orbs, so the rock
  stays whole and breaks up once, at the end.
- The count is in the **debug panel** until there is a font to show it with.

**Engine ideas**
- Additive, pulsing glows read as valuable against the starfield.

### L4. Closing circle
Forces the player and enemies toward the centre.

**Decided (first pass)**
- A level names **several rings** — `ring x y radius` — and the safe zone
  closes through them in turn, largest first, starting from the arena's own
  circle, so the level decides where the fight ends up. The last ring then
  closes to nothing.
- **Stages with pauses:** hold, close, hold, close. Before each close the ring
  **flashes at a rising tempo** and turns blue → amber → red; it pulses red
  while it closes. Each close is **faster** than the one before.
- Outside the zone the player **takes damage over time**. Deposits out there
  can still be mined, paid for in health.
- **Sleeping enemies wake** when the zone passes them and fly back in.
- **Enemies burn outside too.** One that is fighting keeps fighting out there
  and takes damage for it. A burn that finishes an enemy is a kill like any
  other: explosion, wreck and orbs.
- The level's **outer edge no longer pushes**. It is the safe zone's first
  circle, so past it you burn like anywhere else outside. A level with no rings
  keeps its circle as the zone for good.
- If you haven't extracted when the last ring closes, you **die**.
- Being outside is shown as the **world greyed past the ring's edge**: the
  pause grade with a circle, so the line where colour stops marks the edge even
  with the ring off screen.

_(Choices made while building — to confirm:)_
- Timing and damage are **debug sliders**, not level data: 60 s holds, the
  first close's edge at 150 u/s and each later one ×1.6, a 10 s warning
  flashing from 1 to 7 Hz, and 0.08 health/s landing in 0.5 s ticks, with the
  same rate for enemies on its own slider. The close
  speed is the speed of the edge's **fastest point**. When the centre moves, the
  side it moves away from travels further than the rest, so nothing on the
  edge outruns that number.
- The burn goes **through the shield and the cloak**, and **stops health
  regen** while outside. It is not a hit, so it doesn't interrupt mining. Each
  tick shakes the HUD at half strength and flashes the hull red, enemies' hulls
  too. The flash is drawn after the grey grade, or it would come out grey. A
  tick needs a full half-second outside, so grazing the edge is free.
- A **faint white ring** shows where the zone closes to next. The outer edge
  isn't drawn once the zone has left it, since it no longer does anything.
- Only enemies that **aren't engaged** fly in. A search is dropped, not
  resumed, so it doesn't lead them back out.
- In the editor, **Ring** places one at half the smallest ring's radius and
  drags it by a handle at its centre. Order doesn't matter, because rings are
  sorted by radius, and a ring not inside the one before it is drawn red.
- Level 1's three rings close toward the gate, so L5's exit sits inside the
  last one. A round runs about 6½ minutes.

**Engine ideas**
- Outside the circle, a full-screen effect over the world target (the cloak's
  path) can desaturate or distort the scene, so being outside is felt, not
  just shown.

### L5. Extraction warp gate

**Decided (first pass)**
- **One gate** per level, from its `marker gate` line. It only **opens on the
  final ring**, once the closing circle has landed on the level's last ring.
- **Starting it:** hover inside it, **uncloaked**, for **3 s**. Then it
  **charges on its own for about 30 s** (a debug slider). The tempo of its
  pulse rises as it charges, and that tempo is the countdown. The player can
  leave and fight meanwhile.
- **Ready:** the player has to **fly into it** again, uncloaked, to extract.
- **An enemy shot that hits you resets it**, and the start has to be done again.
- A **HUD arrow** points to the gate.
- **All enemies dead means immediate extraction:** the gate goes straight to
  Ready, with no hover and no charge, and flying in extracts at once.
  _(To confirm:)_ this works even before the final ring, and a level placing no
  enemies has its gate ready from the start.
- The gate's body is **Black Hole2.png**, a black disc in a cyan ring, sized so
  the art's ring sits on the gate's. It is pixelated like the scenery from the
  same set, dim while closed, and **spins** faster as the gate charges
  (0.25 → 3 rad/s). The swirl bends the art too. The state-coloured ring and
  the arc are drawn over it; the inner debug ring is gone.
- The engine suggestions, taken:
  - **The swirl** is a second field in the cloak's pass that turns the world
    round the gate, stronger as it charges.
  - **The charge** is shown as an arc round the gate.
  - **The warp-out** is a zoom blur in the CRT pass, so the picture streaks
    outward as the ship leaves.
- _(Dropped:)_ orbs drawn into the gate.

_(Choices made while building — to confirm:)_
- **Any** enemy shot that reaches the ship resets the gate: through the hull,
  blocked by the shield, or taken on the ram's prow. A shot also resets a
  **Ready** gate. Burning outside the ring does not reset it.
- The 3 s hover must be **unbroken**: leaving the gate or cloaking starts it
  over. The arc fills during the hover, and during the charge it shows how much
  is done.
- Being **already inside** when the gate turns Ready is enough; you don't have
  to leave and re-enter.
- The look runs **grey** closed, **cyan** with a slow breath when open, cyan
  whitening with a faster and faster pulse while charging, and **steady gold**
  when ready. The swirl is off while closed and at full strength when ready.
- The arrow only appears **once the gate opens** (its appearance says so),
  only while the gate is off screen, and pulses in the gate's colour.
- The warp still leaves **along the ship's heading**. The blur is centred on
  the screen, where the camera holds.
- A level with **no rings** has its gate open from the start.
- Level 1's final ring moved onto the gate (14800, -1000), so the circle
  collapses there.
- In play the gate replaces the debug marker, so the "Level markers" toggle is
  gone. The editor still draws markers. The debug **Extract** button stays.

**Engine ideas**
- The gate can be a refraction effect like the cloak, swirled. The warp-out can
  be a final effect over the whole frame, the way the CRT is.

---

## Next — a game around the level, and opponents worth fighting

A playable level exists. What comes next is two threads that meet at the end:

- **Menus:** text, then a menu, then a choice of levels, including empty ones
  for testing.
- **Opponents:** enemies that move like the player, then enemies with the
  player's kit.

The two threads meet at the empty level: it is the test bench for the physics
and for bosses. Nothing in this section is decided yet.

### U1. Text

Nothing in the game can draw a word; every string on screen today is ImGui's.

**Where things stand**
- The port skipped gl2d's text milestone (outline, "Milestone 9 (text)")
  because the game drew no text then. It needs text now.
- `stb_truetype` is already linked. gl2d's approach was almost all CPU: pack
  the glyphs into an atlas texture, then draw one quad per character through
  the existing batch.
- **Lands:** _library_ (`wgpu2d.h`) for the font and the drawing, _game_ for
  which font and where it is used. Another game would want text as it is, which
  is what makes it a library feature.

**Open questions**
- Which font? A pixel font suits the CRT. Whatever it is needs a licence that
  allows shipping it. ImGui ships a few open ones, including ProggyClean,
  Roboto and Cousine.
- One size, or many? That decides the next question.
- Once text exists, does it replace anything the debug panel shows today, such
  as L3's points and hold count?

**Engine ideas**
- **Bitmap atlas** (gl2d's way): crisp at the size it was baked at, blurry
  scaled up. **Signed distance field** (`stbtt_GetCodepointSDF`, already in the
  header): one atlas for any size, a smoothstep in the shader, and outlines or
  glow almost free as a per-quad effect (F6). SDF is the more interesting
  thing to learn and suits a zoomable game.
- Text drawn in the world, like damage numbers, gets bloomed by the CRT glow
  for free. On the HUD it goes through the HUD's layer, so it shakes with the
  bars.

**Decided (first pass)**
- **ProggyClean, as a bitmap.** It is a pixel font drawn on a 13 px grid, so
  it is baked at 13 px and only ever scaled by whole numbers, sampled
  nearest. SDF would round its square corners; it waits for a smooth font
  that wants any size.
- **Sizes are whole multiples**, one step per 480 px of window height: 3 at
  1440. Headings are three steps.
- **All three uses**, as the first real text:
  - The **hold and banked points** in a line under the energy bar, to one
    decimal. The debug panel keeps its readout.
  - **PAUSED** across the middle while paused, with "ESC TO RESUME" under it.
    _(Replaced in U2:)_ the pause menu's title.
  - **Damage numbers** on enemies the player hits — shots, beam, ram, bump —
    as life × 100, so a standard hull is 100. Hits on one enemy within
    0.35 s add up into one number, which is what makes a beam read as one
    climbing figure. They rise and fade over 0.9 s of game time, so a pause
    freezes them. None over an enemy the fog hides. All tunable under
    **Damage numbers** in the debug panel.
- Damage numbers are drawn in **screen space**, not in the world: a world
  camera's zoom would take a pixel font off its grid. They sit above the world
  and under the HUD, so they do not shake with it.
- _Built:_ `wgpu2d::Font`, `measureText`, `Renderer2D::renderText`
  (`src/render/font.cpp`); `gameLayer/textLook` (the font, the scale, a
  one-pixel shadow); `gameLayer/damageNumbers`; `hud::showHaul`
  (and `hud::showPaused`, gone in U2).

**Still open**
- Damage the player takes: numbers over the player's ship too, in red, or is
  the health bar enough?
- Burn damage on enemies outside the circle shows no numbers. Should it?

### U2. In-game menu

The natural home for the control scheme and settings that live in the debug
panel today.

**Decided (first pass)**
- **The menu is the pause.** Escape, or the window losing focus, pauses and
  opens it over the grey world. The menu owns Escape while it is open:
  Escape on the first page resumes, on Settings it goes back.
- **Straight in at launch**, as before: the last level played, no title
  screen.
- **Pages:** PAUSED (Resume, Restart, Settings, Quit) and SETTINGS (Volume,
  CRT, Fullscreen, Back).
- **Player settings: volume, CRT strength, fullscreen.** The control scheme
  stays in the debug panel. They are kept in `settings.cfg` beside
  `lastLevel.cfg`, gitignored, and written on every change. Fullscreen is
  on by default.
  - CRT is a 0..1 multiplier on the tuned strength and glow, so the tuning
    file keeps the designer's look and the player can only turn it down.
    At 0 the CRT pass is skipped; transitions still run.
  - Volume is raudio's master volume.
- **Keyboard and mouse.** W/S or arrows move, repeating when held. A/D or
  arrows change a value. Enter or Space presses. Hovering selects, a click
  presses, and a drag along a slider's bar sets it. No controller yet.
- **The mechanism is in `wgpu2d`** (`Menu`, `MenuInput`, `MenuStyle`): an
  immediate-mode menu that reads actions, not keys. The game maps its keys
  onto it and says what the rows are.
- A choice is acted on at the **top of the next frame**. A restart starts
  features that draw into their own targets, and a target's flush takes
  every quad pending, menu included.
- A choice made with a click leaves the button down as play resumes, so the
  trigger waits for it to be let go.
- _Built:_ `wgpu2d::Menu` (`src/render/menu.cpp`); `gameLayer/menu`;
  `gameLayer/playerSettings`; `gameState::resume`; `crt::setPlayerStrength`.

**Still open**
- Menu transitions through the CRT's switch-off and switch-on (an engine idea
  below). Restart is an instant cut for now.
- A Levels page belongs to U3.

**Open questions**
- Is Escape still pause, now opening the menu over the paused world? Or is the
  menu separate from pause?
- A title screen at launch, or straight into a level with the menu on Escape?
- Which items? Resume, restart, level select (U3), settings, quit. Which
  settings belong to the player (control scheme, CRT strength, volume), and
  which stay debug-only?
- Mouse, keyboard, or both? Is a controller ever likely?

**Engine ideas**
- The paused look (L1) is already the right background: grey, dim, frozen.
  Menu text sits on top, crisp, in the HUD's layer.
- glui (already linked) does the layout: frames and boxes by percentage, the
  way the HUD bars are placed. What's missing is only the widgets: a
  highlighted row and a slider.
- Menu transitions can use the CRT's existing switch-on and white-out rather
  than new effects.

### U3. Level select, and empty levels

A list of levels to pick from, and a way to make a new empty one, so the debug
panel and the editor can set up a fight for testing enemy AI and combat.

**Where things stand**
- The level path is one hard-coded file, `resources/levels/level1.txt`.
  Everything downstream (load, save, reload, "test from here") already works on
  whatever `Level` is loaded, so choosing a file is mostly the menu's job.
- An empty level is an arena and a start: two lines. Without rings nothing
  closes; with no enemies the gate is ready at once. So an empty level is
  already a calm sandbox, and "Spawn rusher" / "Spawn sniper" already exist in
  the debug panel.

**Decided (first pass):** empty levels come **first**, ahead of U1 and U2, in
the debug panel's Level section:
- **New level** writes a new file, never over an existing one. It offers the
  next free `levelN`; any name made of letters, digits, `-` and `_` works, and
  a name already taken is refused. The new file is an arena of 20000 and a
  start at its centre, and the game switches to it at once. With nothing
  placed, nothing closing, no enemies and no gate, it's a quiet place to set up
  a fight with the editor or the spawn buttons.
- **Load level** picks any `.txt` in `resources/levels/` and switches to it, so
  `level1` is kept and you can go back and forth. The editor's Save writes to
  whichever level is loaded.
- _(To confirm:)_ loading or creating while the editor has unsaved edits
  discards them, and the button says so.
- The game **starts on the last level played**. Its file name is kept in
  `lastLevel.cfg` beside `imgui.ini`, in the working directory and gitignored.
  If the record is missing, or names a level that is gone, the game starts on
  `level1`.

**Open questions**
- Are levels just the files in `resources/levels/`, listed by name? Or is there
  an order, where finishing one unlocks the next?
- Naming a new level needs typed input. Is that the menu's job (needs U1), or
  the editor's in ImGui for now?
- What does a test level need beyond placing enemies? For example: god mode,
  frozen AI, enemies that don't fire, a "respawn everything placed" button.
- Are banked points per level or global?

### P1. One body for every ship

The player and the enemies move differently, and they should not.

**Where things stand**
- The integrator is already shared (R8, `engine/movement`). What differs is
  what each ship rolls into it:
  - **Player:** `Momentum`, accelerating at 6000, coasting with 0.3 drag, speed
    capped. It has inertia: it drifts, and takes time to stop and turn around.
  - **Enemies:** `Instant`, with velocity set fresh every frame from intent ×
    speed. No inertia: they start at full speed, stop dead, and reverse on the
    spot.
- Turning differs too. The player's hull snaps to the mouse. Enemies turn at a
  capped rate, and rushers turn by a blend-and-normalize that is not quite a
  rate.
- Being knocked about is a separate path. A rammed enemy carries a
  `knockback` vector outside its velocity, rather than taking an impulse on it.
- Enemies have no thruster plume, because nothing tracks their throttle.

**The shape it would take (suggestion):** a ship **body** — position, velocity,
facing, movement options, turn rate, throttle — updated by one function for
every ship. The player's controls and the enemy AI both only produce an
_intent_ (where to thrust, where to face). Knockback becomes an impulse on
velocity. The plume comes free from throttle. The body and its update are
_engine_; each ship class's numbers are the _game_'s.

**The catch:** AI written for instant motion fails with momentum. A rusher
that thrusts straight at the player will overshoot and orbit, and a sniper
holding a range will oscillate. The policies need steering that knows about
inertia: arrive (brake before the target), lead a moving target, and
orbit using thrust rather than by setting position. Those are generic steering
behaviours, and they belong in _engine_ beside the body.

**Open questions**
- Same model with different numbers (a light, twitchy rusher; a heavy sniper),
  or literally the player's tuning?
- Should the player's hull turn at a limited rate too, as the enemies do, or
  keep snapping to the mouse?
- Do ships collide now? C4 left them passing through, and
  `collision::separation` is written and tested but unused. With real
  velocities, a bump can be an exchange of momentum.
- Should the arena's leftovers (enemy sleep, flying back into the zone) also go
  through intent?

**Decided (first pass)**
- **Same model, own numbers.** Every ship flies on Momentum, as the player
  does. Each class has its own tuning: a light, twitchy rusher (high thrust,
  low cap) and a heavy sniper that is slow to turn. A boss can match the
  player exactly.
- **Enemies thrust in any direction.** Thrust and facing stay separate, as in
  the player's ScreenDirections scheme, so a sniper keeps facing you while it
  strafes and backs off. Their behaviour survives; only the motion gains
  inertia.
- **The player's hull keeps snapping to the mouse.** Enemies keep a capped turn
  rate, which is part of how you outmanoeuvre them.
- **Ships collide, bump and exchange momentum.** They push apart and trade
  velocity on contact, using the existing `collision::separation`. A plain
  bump does no damage; the ram stays the damaging hit.
- Everything that moves a ship goes through intent, including waking up and
  flying back into the zone, because every ship is one body.

**Steps (suggestion), one session each:**
1. **The body and intents.** One `Body` (position, velocity, facing, turn
   rate, thrust options, throttle) and one update for every ship. The player's
   keys and the AI each produce an intent. Enemies switch to Momentum and are
   expected to fly badly, overshooting and oscillating, which is what step 2
   is for. Enemy plumes come from the throttle.
   - _Built:_ `movement::Body`, `movement::Intent` and `movement::step` in
     `engine/movement`. The intent is a facing, a thrust in the world's frame,
     and `forward` along the nose once it has turned. The player's ship is
     `session.ship`; `playerMove` only turns keys into an intent, with turn
     rate 0 (it snaps). Each enemy owns a body; its policies return intents
     and one `step` moves it. Rushers: thrust 9000, falloff 0.6, top speed
     800–1800, turn 2.2–4.2. Snipers: thrust 3600, falloff 0.6, top speed
     1200–1600, turn 1.8–2.4. All on sliders under **Enemies → Rusher / Sniper
     flight**, rolled at spawn.
   - Enemy plumes are the player's in orange, lit by the thrust along the nose
     (a strafing sniper shows none), eased by `thruster::ease`.
   - A stunned enemy coasts under its own drag as well as the blow; one
     touching a core loses its speed into it.
   - Measured, two of each spawned round a still ship: rushers held top speed
     and swung 80–940 from it, passing it and turning back; snipers swung
     700–2250 around their 1600–2200 band. As expected, until step 2.
2. **Steering with inertia**, in `engine/` beside the body: arrive (brake at
   the stopping distance, v² / 2a), pursue (lead a moving target) and orbit
   (thrust sideways while correcting range). Rushers and snipers are rebuilt
   on them.
   - _Built:_ `engine/steering`. One controller underneath,
     `matchVelocity`: the thrust that closes the gap to a wanted velocity in
     a response time (0.25 s), plus what holding it against drag costs. The
     behaviours differ only in the velocity they want. **Arrive** wants
     `sqrt(2 a d)` toward the point, braking at 0.6 of full thrust so some is
     left to steer with. **Pursue** arrives at where the target will be.
     **Orbit** wants the centre's velocity, plus going round, plus arriving
     onto the ring, with the circle's inward pull (speed² / radius) added up
     front; beyond twice the radius it pursues. Facing stays the caller's.
   - Both classes now circle the player facing it: rushers at 550, going
     round at 700; snipers at 1900, at 350. Searching arrives at the last
     known spot and holds it while scanning; wandering holds part of the top
     speed instead of part thrust. Sliders under **Enemies → Rusher / Sniper
     tactics** and **Steering**, live.
   - Measured round a still ship: both rushers held 550 exactly, snipers
     1900 (step 1: 80–940 and 700–2250). Round a ship moving at 800: a rusher
     held 543–557 and a sniper 1598–1912, following it. A rusher that rolled a
     top speed near 800 could only match the ship and sat at 1355: the
     rusher's speed range starts at the player's cruising speed.
3. **Impulses and collisions.** Ram knockback becomes an impulse on velocity,
   so a stunned ship drifts under its own drag; ship-on-ship bumps exchange
   momentum.
   - _Built:_ a `Body` has a **mass** (player 1, rusher 0.8, sniper 1.4 --
     on the flight sliders). `movement::push` is a blow: velocity changes by
     impulse / mass. `movement::collide` takes two bodies as circles: they
     move apart by inverse mass and, if closing, exchange an equal and
     opposite impulse along the line between them, so momentum is kept and
     the restitution share of the closing speed comes back (**Ship bumps →
     Bounce**, 0.4). Speed across the line is untouched: a glancing touch
     only deflects.
   - Every enemy pair, and the player with each enemy, after all have moved.
     Not the player while the ram has the ship, or once it is wreckage or
     leaving -- or **cloaked: a cloaked ship passes through**, and nothing it
     passes feels it.
   - **A player–enemy bump hurts both, a little**: 0.05 of the player's
     hull, which the shield blocks as it blocks a shot, and 0.05 of the
     enemy's life (no shields until B1). Only a real impact -- closing faster
     than 200 u/s, as `collide` reports -- and once per 0.5 s per enemy, so
     ships resting against each other do not grind each other down. Enemy
     against enemy only bumps. Sliders under **Ship bumps**.
   - Checked by throwing an enemy at a still ship: shield up, the shield took
     it and the enemy lost 0.05; shield down, both lost 0.05; cloaked, it
     passed through.
   - **Cloaked, nothing bumps the ship -- one rule for every bump.** A
     `Body` has `solid`; the cloak clears the player's (`syncSolid` in
     `gameLayer.cpp`, where the convention is written out). Body-on-Body
     bumps go through `movement::collide`, which ignores a pair that is not
     solid; a bump with something that is not a Body -- a field's core --
     checks `solid` itself. So a cloaked ship also passes through cores
     untouched; uncloaking inside one puts it back on the surface, and that
     touch hurts. A new bump never tests the cloak, and a new way of being
     intangible (a cloaking boss, B2) only clears `solid`. Checked: flown
     at level2's core at 1500 u/s, cloaked it went straight through with the
     hull untouched; uncloaked it stopped at the surface and was hit.
   - The ram's blow (and a core's) is a push; the old `knockback` vector is
     gone. A stunned ship tumbles: no thrust, no cap, and the old fade as its
     drag (**Enemies → Stun drag**, 4). A rusher is thrown 1.25 times as far
     as before, a sniper 0.71 times.
   - Checked: 200,000 random pairs keep momentum to 4 parts in a million,
     match restitution to 0.003 u/s and leave no overlap. In the game, six
     rushers crowding one ring and two snipers never overlapped, and a rammed
     rusher (blow 8200, mass 0.8) went 2584 against 2562 predicted, recovered
     and came back.

### B1. The player's kit for any ship

A boss that has a shield, weapons, a cloak or a ram needs those systems to
belong to a ship instead of to the game.

**Where things stand**
- `energy` (shield and cloak rules), `weapons` (four slots, cooldowns, ammo),
  `ram`, and the `shield` look each hold the state of exactly one ship: the
  player's. Enemies have a bullet cooldown parked on `Enemy` ("because there is
  no Weapon yet").
- C2's open question, "do enemies get the same weapons?", and the R9 sketch —
  weapons as data, one fire function for player and enemies — are this item.
- Some rules already wait on it. C3b: "Shield blocking waits for enemies to have
  shields". C3a: a missile "cannot miss unless the target cloaks (enemies
  cannot yet)".

**The shape it would take (suggestion):** each system's state becomes a struct
a ship owns (`Energy`, `Loadout`, `Ram`), with the functions taking the one
they act on. The player is one instance. Behaviour stays the same for the
player; this is a refactor you can check by playing. It is worth doing
**before** any boss, and after P1, so the body and the kit land in one place.

**Decided (first pass)**
- **Refactor, plus the shared weapons and rolled abilities.** The kit becomes
  per-ship and the player plays exactly as now. Enemies carry the shared
  weapons (below), and _(revised in step 2)_ every enemy has energy and rolls,
  each on its own, a shield, a cloak and a ram -- 20% each by default, the
  chances tunable and saved with tuning. Some have energy and no shield.
- **Weapons are data, and enemies roll the player's.** _(Revised during step
  1.)_ Every enemy rolls one of the player's four weapons, at random and with
  the player's attributes: burst laser, heavy laser, missiles or the beam.
  First, rushers and snipers kept guns of their own matching their old
  shots; the author preferred the shared weapons. **Saved for B2:** some
  enemies carrying two or more weapons, and modifiers that extend or change a
  base weapon.
- **Every ship has the full kit, with flags.** Energy, loadout and ram on every
  ship as plain values; a rusher's energy has no shield and no cloak, and its
  ram is off. One code path, and the flags say what a ship can do.
- **Missiles can home on the player** and lose lock when the player cloaks.
  Bullets get an owner and a target can be the player, so the fire function
  is truly shared. Nothing fires missiles at the player until B2 equips a
  ship with them.

**Steps (suggestion), each checkable by playing:**
1. **Weapons:** a per-ship `Loadout`; enemy guns move onto it; bullets carry
   who fired them, and a missile's target can be the player.
   - _Built:_ `weapons::Weapon` is public data (with a `beam` flag in place of
     "slot 3 is the laser"), and a `weapons::Loadout` holds a ship's weapons
     by value plus everything that changes as it fires -- cooldowns, ammo,
     burst, laser charge, beam, missile side. Every function takes the
     loadout it acts on. The player's lives outside the Session, so its
     tuning and selection survive a restart; restart only resets its state.
   - **Each placed enemy can name its weapon.** In the editor, a selected
     enemy shows its kind, position, facing, weapon (Random, or one of the
     four) and its class's tuning together. The weapon is kept on the
     placement and saved with the level on Save:
     `enemy rusher 4000 1200 180 missile`; without the last word it rolls.
     The Enemies section groups the same way -- the spawn buttons (random
     weapon), then a Rusher and a Sniper tree with each class's flight and
     tactics, then awareness, then steering and stun.
   - Each enemy rolls one of the four shared weapons (`weapons::shipWeapon`, as
     defined -- not as tuned in the panel, which tunes the player's own),
     first shot a second after engaging. `enemyAi::update` now returns
     `Orders` -- fighting, and trigger -- and the game fires the gun through
     the same `weapons::update` as the player's. Their shots still do not
     inherit their velocity, and still look like enemy shots, whatever the
     weapon (bigger for the heavy laser and missiles).
   - **Enemy missiles** home on the player and lose the lock when the player
     cloaks. Five each, then the enemy has nothing left to fire. They pass
     through the shield and hit the hull; the shield does not ripple or break.
   - **The enemy beam** is traced from the nose up to 3000, stopped by rocks
     (not pushed or mined) and by the player. On the player, energy decides
     (`energy::onBeam`): a raised shield holds it without breaking, as the
     player's beam is held by shields (C3b), and it splashes off; with the
     shield down it burns the hull for 0.4 a second and keeps the bar empty;
     cloaked, it passes through. Drawn in the enemies' blue.
   - `shipId.h`: `ShipId`, with `noShip` and `playerShip`. A bullet has a
     `shooter` (`fromEnemy()` replaces `isEnemy`) and a missile a `target` and
     its own `topSpeed`. `steerMissiles` takes `Targets` -- the enemies, and
     the player unless cloaked or gone.
   - Checked: the player's missiles lock the enemy nearest the mouse and
     hit; enemy missiles home on the player and lose the lock once the player
     cannot be targeted. 400 rolls came out 95 / 95 / 120 / 90 across the
     four. Two beam enemies on a shielded ship splashed off for 4 s with the
     hull and the bar untouched; with the shield broken they burned 0.8 a
     second between them, then ran dry and cooled down as the player's
     laser does.
2. **Energy and the shield's look**, per ship (the bubble gains a colour).
   - _Built:_ `energy::Energy` is a ship's bar, state and break timer, with
     `hasShield` and `canCloak`, and the `shield::Bubble` it raises and
     breaks (`shieldBubble.h`: a bubble's look state and its `Palette`, kept
     apart from the drawing so `Enemy` does not take the renderer). The
     player's is in the session; every enemy has one. The ripple's colour now
     comes from the bubble, so an enemy's bubble is red-orange.
   - A ship with energy and no shield fills and empties by the same rules;
     a hit with the bar full does damage and empties it. The cloak's
     world-bend now follows the player's energy each frame, rather than
     energy switching it.
   - **A placed enemy's shield is the level's choice:** No (the default),
     Yes, or Random, in the editor's enemy panel below the weapon, saved on
     its line as `shield:yes` / `shield:random`. Spawned enemies -- the
     buttons, waves -- and placed ones set to Random roll it.
   - Enemies roll their other abilities at spawn (**Enemies -> Abilities**,
     20% each). Every way an enemy is hurt -- the player's shots and beam, the
     ram, cores and bumps -- goes through its energy: a shield takes shots
     and the ram and breaks, and holds the beam unbroken, which splashes off.
     Burning outside the circle is not a hit. Cloak and ram are rolled but
     unused until step 3.
   - Checked: the player's shield breaks after the ripple, refills in 8 s and
     blocks again; 1000 rolls gave 196 / 193 / 176; a shielded enemy blocked
     the first burst, lost its shield, then took 0.2 a burst; the beam was
     held by a shielded enemy and burned a plain one at 0.4 a second.
3. **The ram and the cloak's look**, per ship. The cloak's world-bend stays
   one centre until a second cloaked ship exists (B2).
   - _Decided:_ an enemy's ram on the player hits (through the player's
     energy), knocks aside and briefly stuns; enemies ram when engaged, in
     range and lined up; they cloak to escape when hurt, then ambush from
     behind; a cloaked enemy looks like the player's cloak, the world bending
     round it.
   - _Built:_ `ram::Ram` is a ship's wind-up, surge, cooldown, heading and
     struck list; the player's is in the session and every enemy has one.
     The numbers stay shared, plus a **player stun** of 0.5 s. An enemy's
     ram runs on its body in place of steering; it starts one engaged, within
     1500 and lined up within 12 degrees, and keeps track of the player for
     1.5 s after so the surge does not lose it. Its prow strikes the player
     once per ram: hit-stop and shake, the ram's damage through the player's
     energy, and the player knocked aside and stunned -- tumbling under drag
     4, no flying, firing, ramming or cloaking. The player's own prow, out
     and facing the rammer, takes it instead, as it takes a core.
   - An enemy cloaks with a full bar when its life is below 0.5; drifts,
     turning to face the player; and fires -- which uncloaks it, as it does
     the player -- when within 900, behind the player and lined up, or after
     6 s regardless. Cloaked, it is not solid, and the player's shots, beam,
     missiles and ram pass it; it shows no sight cone or awareness mark.
   - **The cloak's look per enemy:** `cloak::ease` and `shipAlpha(level)` for
     any ship, and `cloak::addField`: each cloaked enemy is a quad over its
     own field, sampling the world target through the same shader with its
     texture coordinates set to that patch's place on the screen -- which is
     all the shader reads its pixel from. No full-screen pass per enemy, and
     nothing at all while none is cloaked. Up to 8.
   - Placed enemies get **Cloak** and **Ram** pickers like Shield (No by
     default, Yes, Random), saved as `cloak:yes|random`, `ram:yes|random`.
     The rules are tunable under **Enemies -> Abilities**.
   - Checked: a ramming rusher struck at once (the shield took it, the player
     knocked away at ~4800 and stunned 0.5 s), came back to its ring and
     rammed again each cooldown -- the second through the hull for 0.4. A
     hurt enemy cloaked, stopped being solid, drifted, and after 6 s fired
     and uncloaked; the player's shots passed through it while cloaked. With
     the field's strength raised 6x for the test, the bending sat on the
     enemy with no seam at the quad. Not yet seen: the ambush from behind.
   - **Enemy rams shove asteroids** as the player's does. Each ram gets a
     serial number when it starts, across every ship (`ram::Ram::serial`), and
     a rock remembers the serial that struck it -- where before one counter
     belonged to the player's ram. Checked: a rock at the start of an enemy's
     ram was shoved several hundred units in under a second, nothing else
     touching it.

### B2. Bosses

Enemies closer to the player: shield, weapons, maybe the cloak and the ram.
Built on P1 (moves like the player) and B1 (has the player's kit).

**Open questions**
- Which abilities? Shield and weapons are the ones named. Cloak and ram?
- Where does a boss appear? Placed by a level (`enemy boss x y facing`), one
  per level, and is killing it tied to the gate (L5)?
- How does it decide when to shield, cloak, ram, or switch weapons? Is it
  scripted phases (at half health it starts cloaking) or rules from what it
  sees?
- How do the player's weapons meet its shield? The beam is "blocked by shields,
  and cannot break one" (C3b), so the laser would need another weapon to break
  the shield first.
- What does it drop? A deposit, or a big haul of orbs?
- Does it need a health bar, and a name (U1)?

**Decided (first pass)**
- **A boss kind**, a third class beside rusher and sniper: an enemy hull
  scaled up (about 1.6x) and tinted, much more life (about 10x), the
  player's flight tuning, every ability (shield, cloak, ram), and 2 to 4
  weapons. Placed in the editor; its numbers on sliders.
- **Phases by health.** Above 2/3 it fights with its guns; below, it adds the
  ram and the cloak; below 1/3 it is enraged -- faster, shorter cooldowns.
  Thresholds on sliders.
- **Optional, with no health bar.** Not tied to the gate. Killing it bursts a
  big haul of orbs. Its health is read from its damage.
- **Several weapons, chosen by range and situation.** Each weapon has a best
  range; a ship picks the best one that is ready -- missiles from afar, lasers
  to break a raised shield, the beam on a target with its shield down.
  Bosses carry 2 to 4, each picked per slot in the editor or rolled;
  ordinary enemies get a second weapon at a tunable chance (low by default).
- **Weapon modifiers**, on a weapon in a loadout (picked or rolled, chances
  tunable):
  - **Stun:** a hit stuns the target as a ram does.
  - **Lockdown:** a hit disables all of the target's weapons for 2 s; then
    each must go through its cooldown before it fires again.
  - **Spread:** extra shots in a fan -- a missile salvo, a shotgun laser.
  - _(Suggested, to confirm:)_ stun and lockdown only on a hit that reaches
    the hull -- a raised shield blocks them, as it blocks the damage; 0.5 s
    of stun on the player, the ram's on an enemy; and a target is immune to
    another stun or lockdown for 1 s after one ends, so a beam carrying
    either cannot hold it forever. Spread: two extra shots, 15 degrees either
    side. Ordinary enemies: 10% chance of a second weapon, 10% of each
    modifier per weapon.
- **Enemy shots carry the shooter's velocity**, as the player's do -- every
  enemy.

**Steps (suggestion):**
1. **Weapons:** several per loadout and choosing between them by range and
   situation; the three modifiers and their on-hit effects on any ship;
   enemy shots with the shooter's velocity; ordinary enemies' second weapon
   and modifiers by chance.
   - _Built:_ a `Weapon` has a best range (burst 1600, heavy 2200, missile
     3500, beam 2500) and its modifiers (`stun`, `lockdown`, `spread`).
     `weapons::choose` picks the ready weapon that suits the distance --
     lasers against a raised shield, never the beam against one -- and keeps
     the current one unless another is clearly better. Spread fans its extra
     shots in the fire function (15 degrees apart). A lockdown is part of the
     loadout: nothing fires for 2 s, then every weapon starts its cooldown;
     the HUD dims its slots meanwhile.
   - Stun and lockdown ride on the bullet or the beam, and act through one
     rule on either side when a hit reaches the hull: the player is stunned
     0.5 s (tumbling, as after an enemy's ram), an enemy 2 s (the ram's);
     a lockdown takes the loadout; then 1 s of grace. Enemy shots carrying
     either glow yellow (stun) or violet (lockdown).
   - Ordinary enemies: a second weapon of another kind at 10%, and each
     weapon's stun, lockdown and spread (not on a beam) at 10% each, under
     **Enemies -> Abilities -> Weapons**. `enemyAi::update` now takes a
     `Player` view -- position, velocity, facing, hidden, shielded.
   - Enemy shots carry the enemy's velocity.
   - _Found:_ an enemy's shot hitting the player always took a fixed 0.1 of
     the hull, whatever the weapon, so since B1 enemies' heavy lasers and
     missiles hit no harder than their old gun. It now takes the shot's own
     damage, as the player's shots do to enemies.
   - Checked: 2000 rolls gave 203 second weapons and about 10% of each
     modifier, none spread on a beam; `choose` picks burst up close, the beam
     at 2400 unshielded but the heavy laser shielded, missiles at 3600; a
     spread heavy laser fires three, each carrying the enemy's velocity; its
     first volley through the hull stunned the player 0.5 s and locked their
     weapons 2 s, after which the burst laser cycled its cooldown, and a hit
     inside the grace did damage but no stun or lockdown.
2. **The boss:** the kind, its look, flight and life; its phases; its
   weapons and modifiers in the editor; the orb haul.
   - _Built:_ `Behaviour::Boss`, placed with the editor's **Boss** tool or
     the Kind picker, and spawned with **Spawn boss**. The column-1 hull of
     the ship sheet, 1.6x the size (every place an enemy is drawn or hit now
     uses its own `size`), tinted warm; life 10; the player's flight (6000
     thrust, 0.3 falloff, top speed 2000), mass 3; circling at 1200. It has
     the shield, cloak and ram.
   - **Phases:** above 2/3 of its life it fights with its guns; below, it
     rams and cloaks too -- cloaking whenever its bar is full; below 1/3 it is
     enraged for good: thrust and top speed x1.3, cooldowns x0.6. Entering a
     phase shakes the screen and flares its shield.
   - **Weapons, for any placed enemy:** the editor's enemy panel lists its
     weapon slots, up to four -- each a kind or Random, with Stun, Lockdown
     and Spread each No, Yes or Random -- saved as
     `gun:missile:spread=yes:stun=random` words on its line. With none, they
     are rolled: one (sometimes two) for an ordinary enemy, two to four
     different ones for a boss, its modifiers at 0.3 each. A bare weapon word
     from an older file loads as one slot with its modifiers rolled.
   - **Death:** a bigger blast, and 24 orbs of 0.5 thrown all round.
   - Tuning under **Enemies -> Boss** (and the boss's tree beside it in the
     editor): life, size, phase thresholds, enraged speed and cooldowns, how
     many weapons and the modifier chance; the haul under the game's tuning.
   - Checked: a level with a boss (missile with spread, heavy with stun and
     random lockdown, one random), a rusher with an old bare weapon word and
     a plain sniper loaded and spawned as written, and saved back in the new
     form. A boss through its phases: circling at 1200 switching heavy laser
     and beam; at phase 2 it cloaked at once and drifted off; at phase 3
     accel 6000 -> 7800, top speed 2000 -> 2600, cooldowns cut to 60%, and
     it rammed and locked down the player; killed, the player's hold went
     from 0 to 12.3 (12 of orbs, the rest the usual fragments).

**Engine ideas**
- The shield bubble already takes per-quad parameters. A boss's shield can be
  the same bubble in another colour, rippling where it is hit.
- A cloaked boss can bend the world around it as the player's cloak does, with
  one more field in the cloak's pass, the way the gate's swirl was added.
- A boss health bar is the HUD bar again, and the empty-energy shake exists.

### Asteroids (A1–A5)

Rocks with a shape of their own, textured from a real rock material, that move
when you shoot them. The questions under each item are settled **when that item
starts**, one milestone at a time. The concepts involved are listed in the
learning outline, under "Asteroids: the concepts".

**Decided**
- Asteroids are **resources** and **hiding places**, for the player and for
  certain enemy types.
- The physics is for a **realistic response to being shot or beamed**: a hit
  off-centre pushes and spins, and a held beam pushes steadily. It is **not**
  for collisions with the player or with enemies; ships don't bump into rocks.
- The textures come from a 4K rock material (colour, height, normal,
  roughness) in `resources/textures/`, which is gitignored.
  `tools/asteroidTextures.sh` makes the 512 px copies in `resources/asteroid/`
  that are committed:
  - **colour** and **height**, averaged down;
  - **normals worked out from the height**, because the material's own normal
    map is an EXR that neither `stb_image` nor `sips` can read.
  - Roughness is left out: it hardly matters in 2D.

#### A1. A rock: generated, drawn, and hit

A procedural shape from a seed, textured, placed in a level, and solid to
shots, the beam, and sight. It doesn't move yet.

**Proposed:** a **star-shaped polygon**, with vertices at jittered angles
around a circle and radii nudged by smooth, looping noise from a seed. So each
seed gives a different rock and the same seed always gives the same one. Every
point of such a polygon can be seen from the centre, so a **triangle fan from
the centre** always triangulates it, with no ear clipping. The fan's triangles
are also the collision shape. Ear clipping waits until a shape can stop being
star-shaped (A4, or craters).

**Lands:** the shape, fan and collision tests are _engine_ (polygon geometry,
no idea what a rock is). Drawing triangles is _library_: `wgpu2d` only draws
rectangles today, and a `renderTriangles` is something any 2D game would want.
The rock itself — sizes, what it blocks, how it's placed — is _game_.

**Decided (first pass)**
- **The shape** is a star-shaped polygon from a seed, drawn and hit-tested as a
  triangle fan from its centre. Corners are spaced by distance round the
  outline (160 units each), so big rocks are as craggy as small ones. Ear
  clipping waits for A4.
- **A rock blocks** the player's shots and missiles, enemy shots, the beam
  (before an enemy or a deposit behind it), and **enemy sight**. A rock on the
  line between an enemy and the player hides the player as the cloak does, and
  a watching enemy goes to search where it lost you (C5).
- **Which enemies use cover** is left to enemy AI after P1.
- **Placement:** a level line `asteroid x y radius seed` and an editor
  **Asteroid** tool — click to place, drag, radius field, a seed field and a
  **Reroll** button. The first rocks are in `level2` only.
- **Drawing:** real triangles. `wgpu2d` gains `renderTriangles`, and the batch
  now keeps where each record's vertices start, so a run draws however many
  vertices its records hold.
- **The texture** sits at a fixed scale, one repeat per 1800 world units, so
  every rock is the same stone. `Texture::loadFromFile` takes `repeat`, which
  picks a repeating sampler; big rocks wrap onto the texture. Each seed also
  picks where on the texture a rock starts.
- **The look until A3:** the colour map, smoothly filtered and mipmapped, with
  the fan's outer corners shaded so the rock darkens toward its edge.

_(Choices made while building — to confirm:)_
- **One rule for every shot:** anything touching a rock stops, including a shot
  or beam fired from on top of one. Ships fly over rocks (drawn under the
  ships), and a ship on top of a rock is hidden. So a rock is cover to hide in,
  not a bunker to fight from: to shoot, you have to leave it.
- A new rock gets a random seed; its default radius is 800.
- Sliders under **Asteroids**: corner spacing, roughness, angle jitter,
  texture scale, rim, brightness, and outlines.

#### A1b. Asteroid fields

An area painted with a brush and filled with many rocks from a seed: somewhere
to hide, like tall grass. Single rocks (A1) stay alongside.

**Decided (first pass)**
- **Painting:** an editor **Paint field** tool.
  - Left drag lays brush stamps (circles); **Shift** + left drag lays erasers;
    **−** / **=** shrink and grow the brush.
  - It paints the selected field, else the field under the brush, else a new
    one.
  - With Select, clicking inside a field picks it, and dragging moves the
    whole field.
  - A field is deleted only with the panel's Delete, not by right-click, so a
    stray click can't wipe it.
- **In the file:** `field seed maxSize maxGap`, followed by its `paint x y r` /
  `erase x y r` stamps. A point is in the field if the last stamp covering it
  painted rather than erased.
- **Rocks:** each field has its own seed (**Reroll**), **max size** and **max
  gap** (default 40, on a logarithmic slider up to 1500). They're scattered in
  **layers** (`engine/scatter`):
  - Big rocks go on a coarse grid. Each layer after it has rocks half the size
    in cells half the size, filling the spaces and skipping any spot that would
    come closer than the gap to a rock already placed.
  - Each cell's rock comes from a hash of the field's seed and that cell.
    Painting more area adds rocks without moving the ones already there, except
    that a few small rocks near a newly painted edge can give way to a big one
    arriving there.
  - **Each rock keeps its own clearance**, drawn with its cell from 0 up to
    the max gap. Two rocks keep the average of theirs apart. One gap for every
    rock made all the spacings the same once the layers packed in, which read
    as a grid. **Gap variation** (under Asteroids, default 1) runs from uniform
    at 0 to anywhere-up-to-the-max at 1.
  - **Clumping was built, then taken out of fields.** A smooth density drifts
    across a region from the seed (value noise). Below a threshold that rises
    with clumping, cells are empty in every layer; roughly, clumping × 70% of
    the region is gap. It looked right. But "hidden" means *inside the painted
    area*, so the empty patches still hid the player with nothing overhead. A
    painted area is the designer saying "cover here", so fields don't clump.
    The mechanism stays in `engine/scatter` (`clumping`, `density`), off by
    default, for procedural levels (see **Later**).

_(Along the way:)_ the player's **jitter at slow speeds** was the camera.
- **What it was:** it chases at a fixed 550 u/s, and `camera::follow` never
  limited a step to the distance left. So below 550 it overshot the ship and
  stepped back.
- **The second half:** the game's 1-unit dead zone switched on follow's stepped
  easing (quarter speed within 2, half within 4). A slow ship kept crossing
  those steps, so the camera fell behind and caught up every few frames.
- **Measured** in a simulation of the real `follow`: the ship's screen position
  wobbled up to 2 units a frame at 100–500 u/s.
- **Fixed:** steps are clamped to the distance (engine), and the dead zone is 0
  (game). The wobble is now 0 at every speed, at 60 and 120 fps.
  - The first version used one grid only. Its cells had to fit the largest rock,
    so even at gap 0 rocks sat about 280 apart. Three layers at gap 40 bring
    that to about 70.
- **The hidden look:** a ship in a field is **darkened** (45% of its light), so
  in the gaps it looks in shadow. The outline pass over the rocks draws only
  the line. Its old fill was the ship's own colours at 18%, which tinted any
  rock covering the ship and made the ship look blended into it rather than
  behind it. The fill is now an optional flat dark silhouette, off by default.
- **Layering:** field rocks draw **over** the ships, so a ship in a field is
  behind its rocks. Single rocks stay under the ships.
- **Hidden:** anywhere **inside a field's painted area**, gaps included, the
  player is hidden from every enemy. The ship is drawn once more over the rocks
  as an **outline** with a faint fill, so you can see where you are and know
  no enemy can.
- **Shooting:** field rocks block shots, the beam and sight like single rocks,
  so you can shoot out through the gaps. An enemy you hit is alerted as always:
  it turns, can't see you, and comes to search where you are.
- **Parallax:** none yet; it comes back with A3's look.

_(Choices made while building — to confirm:)_
- The outline's colour is pale mint, pulsing gently, and at least 1.5 screen
  pixels wide at any zoom. Its sliders are under **Hidden outline**, the shade
  under **Asteroids**.
- Shared field sliders under **Asteroids**:
  - layers: 3;
  - smallest rock: 0.12 of the max;
  - lean small within a layer: 1.5;
  - fill: 90% of cells try for a rock;
  - the editor's area-dot spacing.
- A cloaked ship in a field is outlined at the cloak's faintness.
- The painted area shows in the editor as a grid of dots, so overlapping
  stamps read as one flat region.

#### A2. Physics: shot and beamed

**Proposed:** each rock is a **rigid body**: position, velocity, angle and
spin. Its mass, centre of mass and moment of inertia come straight from the fan
triangles, so its shape decides how it turns. A hit is an **impulse at the
contact point**: the change in velocity is the impulse over the mass, and the
change in spin is (contact offset × impulse) over the moment of inertia, so an
off-centre hit spins it. The beam is a steady **force** at its contact point.
_Engine_: the body knows nothing about rocks, and P1's ship body may end up
using the same one.

**Decided (first pass)**
- **Every rock is a rigid body** (`engine/rigidBody`). Its mass, centre of
  mass and moment of inertia come from its fan at one density for all rocks,
  so mass grows with area: a rock of radius 350 is about sixty times heavier
  than a pebble of 40.
- **Pushes:**
  - **Shots** (anyone's) push by damage × a push value, at the point they
    land, so an off-centre hit spins the rock too. **Missiles** push 4×.
  - **The beam** is a steady force where it burns.
  - **Explosions** (every kill, and the player's death) shove nearby rocks
    outward, less the further off.
  - **The ram** strikes each rock its prow touches, once per ram. The ship
    goes on through.
- **Single rocks** drift freely and slowly come to rest; drift and spin are
  damped. **Field rocks** spring gently back to where they grew, so a field's
  painted area stays the truth about its cover.
- **Rocks bump each other** as circles of their own area. They pass through
  ships.
- **Edges:** nothing special. A rock can drift out of the arena or the closing
  circle.

_(After the first playtest: "the rocks don't even bounce, they just get pushed
in one direction".)_ Measured with a scripted ram through `level2`'s field: the
moving rocks' directions spread 0.01 (0 is all one way), there were 10
rock-on-rock contacts in the whole event, and speed was gone in half a second.
Three causes, all fixed:
- **The ram pushed every rock straight along itself.** Now it throws each rock
  **outward from the prow** (along the line to its centre) plus some of its own
  direction, applied on the rock's near side, so rocks either side scatter
  and glancing strikes spin.
- **The spring held field rocks from the first moment.** Its damping alone
  (2ζω) halved their speed in a third of a second. Now a struck rock is
  **loose for 3 s**, then the spring eases in over 1 s.
  - Looseness spreads *with the original clock*: a rock knocked by a loose one
    is loose until that one's spring returns, so a ram's whole chain comes
    home together.
  - Home is a drift, not a slingshot: critically damped, and capped at 500 u/s.
- **Bounces were dead (0.3) and never spun.** Now 0.85, with **friction** at
  the contact, so glancing bumps hand over spin. Touches slower than 120 u/s
  don't bounce, so rocks resting against each other don't buzz.
- **A field rock on its way home only meets rocks that are still loose.** Its
  home lies through a crowd, and colliding there turned the return into a jam.
  Settled rocks let it drift back into place.

After: directions spread up to 0.88, about 500 contacts in the wild phase,
rocks thrown up to 2900, and the field home and asleep within 12 s.

_(Choices made while building — to confirm:)_ all on sliders under
**Asteroids → Physics**:
- **pushes:**
  - shots: 12000 per unit of damage (missiles 4×);
  - beam: 6000;
  - blast: 3000, reaching 1800;
  - ram: 30000, with 0.7 of its own direction added to the outward push;
- **damping:** drift 0.5/s, spin 0.3/s. A pebble at the speed cap glides about
  3600 units; at 0.25 and a 2500 cap it glided 10000, clean out of its field;
- **caps:** speed 1800, spin 10 rad/s;
- **bounce:** 0.85, friction 0.4, resting speed 120;
- **field spring:** loose 3 s, easing in over 1 s, a 5 s period, critically
  damped, returning at no more than 500 u/s.

Only moving rocks are stepped and collided; they sleep once still, and, for
field rocks, home.

**Cores** _(after the second playtest: "strange how they return with nothing
seemingly pulling them back")_
- **Every field has a core:** a rock bigger than any of its own (2 × the
  field's max size), grown from the field's seed. It sits at the painted area's
  middle (the average of the painted points, moved to the nearest painted
  point if the field's shape puts that outside it). In the editor it can be
  dragged, and is then saved as `core x y` under its field. "Back to the
  middle" (or a right-click) undoes that.
- **The field's rocks are scattered clear of it:** `engine/scatter` gained
  keep-out circles.
- **It is what the rocks are seen to fall back toward.** Its presence is the
  explanation; nothing extra is drawn. The spring itself still pulls each rock
  to its own home, so the painted cover stays true.
  - _(Tried and dropped:)_ a faint gravity well, with space bending toward the
    core while rocks drifted home. It was a third field in the cloak's pass,
    which needed a fourth vec4 of effect parameters. Both came out again.
- **It never moves:** it's an immovable body (zero inverse mass), so shots,
  missiles, the beam, blasts and the ram don't move it and it can't be broken.
  It still stops shots, the beam and sight like any rock, and rocks bounce off
  it.
- **Ships can't pass through it.**
  - **The player** is put back on its surface and thrown back out (60% of the
    speed in comes back, at least 700 u/s). Once per touch (0.5 s grace), it's
    a hit: the shield blocks it and breaks, or with the shield down the hull
    takes 0.15. The ram's prow takes it instead when the core is ahead.
    Cloaked, nothing hits.
  - **Enemies** are thrown back out tumbling (a short stun) and lose 0.15.
    It's not an alert.
- Enemies don't steer round cores until **P1**; a rusher chasing you through a
  field will hit it.
- Sliders under **Asteroids → Cores**: core size, hit damage, ship bounce,
  minimum bounce speed, grace, enemy knock and stun.
- Measured: `level2`'s core is radius about 700, and none of the field's 84
  rocks overlap it. Flying in at 2000 u/s, the player comes back out at about
  1100 with the shield broken and health untouched; a second touch with the
  shield down takes 0.15. An enemy thrown in comes back out at 0.85 life.
- _(Known:)_ rocks bounce off the core as a circle of its area, so a rock can
  graze one of its outer bumps.

#### A3. The shader: a lit rock

**Proposed:** an app effect (`asteroid.wgsl`, F6's per-quad effects) that
lights the colour map with the normal map, and uses the height map as a mask.

**Decided (first pass)**
- **One packed texture**, `rock_packed.png` (made by `tools/asteroidTextures.sh`):
  - R: brightness; G, B: the normal's x and y; A: height.
  - The normal's z is worked out in the shader (it's a unit vector).
  - The rock's hue comes back as one **tint**, its average colour over its
    average brightness (1.154, 0.976, 0.783, as the tool prints it).
  - No library change: an effect still reads one texture.
- **Per-rock data rides on the vertex colour**, so every rock in a layer is
  still one draw:
  - red and green: the light's direction turned into *that rock's* frame;
  - blue: the rim;
  - alpha: heat. Rocks are opaque, so alpha was free.
- **The light:** one direction for now, a slider. From the upper left
  (azimuth 225°), 40° above the plane, with ambient 0.35. Per level later.
- **Beam heat:** the beam alone heats rocks, rising at 0.8/s and cooling at
  0.35/s, spread 380 units round where it burns. It glows in the **cracks
  first**: the local lows of the height map, found by comparing the height
  with a coarse mip level of itself. It runs orange to yellow and never
  reaches white. The core can't be beamed, so it never heats.
- **Damage cracks** move to A4, where rocks first take damage.
- **Depth without parallax:**
  - **Shadows on ships only.** A field rock casts its outline away from the
    light, and a ship inside it is darkened by how much of its hull is
    covered (0.6 when fully in shadow). It fades over about 150 units, so a
    ship slides into a shadow rather than blinking. Nothing is drawn on the
    starfield.
  - **Foreground debris:** small, dark and never solid, round each field and
    2500 past it. Drawn last in the world, moving 35% faster than the world
    as the camera moves and drawn larger by the same factor.
- **Style:** coarsened a little by default (0.3). The texture is snapped to a
  coarser grid and the light falls in bands, so it sits nearer the pixel-art
  ships. A slider runs from photographic to blocky.
- **The core:** the same stone, only bigger.

_(After the playtest: "too much stuff in the foreground — both asteroids and
stars".)_
- **The foreground debris is off** by default (fill 0); its sliders stay.
- **Shadows were drawn on the starfield**, as soft dark halos behind every rock.
  But the starfield is far behind everything, and a rock in space can't shade
  it, so they read as smudges hanging in space. Now shadows fall only on
  ships: a ship in a field rock's shadow is darkened, sampled at nine points
  over its hull. Single rocks lie under the ships and cast nothing on them.
- **The starfield was never behind the play.** `TiledRenderer` shifted each
  layer by −view × strength, which moves it *faster* than the world, while its
  comment said slower. So the star layers slid past at 1.2× and 1.4× the
  world's speed and read as nearer than the rocks. Now a layer shifts *with*
  the view:
  - the three star layers move at 0.2×, 0.4× and 0.6× the world's speed;
  - the play is the fastest-moving thing on screen, so it reads as nearest;
  - planets sit between, at depth 0.25 (moving 0.75×). That's `level1`'s
    four, and the editor's default for new scenery.
  - L2's "depth 0 reads as the furthest" was true only of the old starfield.

_(Found while building:)_ the first heat went by "lower than the heat",
and this height map is mostly broad hills and hollows, so the burn was one flat
white blob. Comparing each point with its neighbourhood (a coarse mip) finds
the cracks instead.

_(To confirm:)_ all on sliders under **Asteroids → Look**:
- the light's direction, height and strength, ambient, and tint;
- coarsening;
- heat colour, reach, rise, cool and radius;
- shadow distance (160) and darkness on a ship (0.6);
- debris parallax, shade (0.25) and fill (0: off after the playtest).

#### A4. Breaking up, and what a rock is worth

**Decided (first pass)**
- **Every rock but a field's core breaks**, field rocks included. Anything
  damages a rock: shots (anyone's), missiles, the beam, blasts, the ram. **Only
  the beam mines.**
- **Asteroids replace deposits completely.** They're the only ore.
  - `resources` keeps the orbs, the hold, interruption and banking.
  - Old `resource x y amount` lines (and the older `marker resource`) load as
    single rocks sized by the amount; `level1`'s three are now written as
    `asteroid` lines.
  - A dead player's hold scatters as orbs rather than a deposit. The round
    restarts on death, so this was always only for the moments before the
    restart.
- **The core** yields no ore, never glows, and can't be broken. The beam on
  it **splashes off**: no flare, no heat, just a hard white bar flat on the
  surface and sparks skating off along it both ways.
- **The beam barely moves a rock.** Its push is a small force (400), and
  whatever it adds is held to a 25 u/s creep and a 0.25 rad/s roll -- only
  what the beam added, so a rock already flying from a shot keeps its speed.
  A force alone wasn't enough: a pebble weighs a sixtieth of a big rock, and
  the same force hurls it. Measured: a radius-380 rock moved 26 units over the
  3 s it took to mine, and didn't turn.
- **A real crack network.** Each rock has one from its seed: Voronoi sites
  inside it, one per 70000 of area (2 to 6).
  - The borders between the Voronoi pieces are its cracks. They show as dark
    lines one at a time, each at its own damage threshold.
  - When the rock breaks, it breaks **along those same cracks**.
  - Pieces that can't be fanned from their centre are ear-clipped, which is
    what ear clipping waited for since A1.
- **Breaking looks like an enemy's death:** a dust burst (the blast without
  its fire), and the pieces fly apart spinning, then **sink into the
  background** like a wreck.
  - Pieces are **shards, out of play**: nothing hits them, they block no
    shot, beam or sight, and they're drawn under the ships, behind every
    rock. Over 2 s they darken to 0.45 and shrink to 0.85, so they read as
    further off.
  - Each flies as that part of the parent was moving (v + ω × r), kicked
    outward, slowing like wreckage.
  - After 1.2 s a spring eases in and **gathers them where the rock stood**
    -- its home, for a field rock the spot it springs back to -- each to its
    own place in the rock's shape, spread 1.12× so the cracks show. The
    spring is critically damped and capped at 300 u/s, like the field's
    rocks coming home.
  - Shards don't come back to life. What's left where a rock was is a dark
    broken silhouette of it.
  - _(Replaced:)_ the first pass made pieces real rocks that blocked, hid,
    pushed and broke again, and drifted back into the field's paint. Shooting
    them ground them away -- each shot re-broke a piece until its pieces were
    under radius 20 and became dust -- so fragments seemed to vanish.
- **A rock the beam destroys bursts out its ore**, like a spent deposit's
  last orbs. Everything it still held, the fraction not yet a whole orb
  included, is thrown back the way the beam came. Pebbles hold less than one
  orb and used to break before shedding any, so burning through a field
  yielded nothing; now each gives one. A rock broken any other way throws
  nothing: **its ore is lost**. Shooting a rock to pieces wastes it.
- **The glow means "ore coming off".** A rock glows only while the beam is
  shedding orbs from it: orange cracks near the burn, and the shader's heat.
  It fades within a second once the beam leaves. Nothing else glows, so what
  you can collect is clear.

_(Choices made while building — to confirm:)_ all on sliders under
**Asteroids → Breaking and ore**:
- **Health** grows with area^0.75, at 0.3 for a rock of area 10000. A pebble
  takes a shot or two, and a rock of radius 380 about 3 s of beam. The beam
  wears rock 4× faster than it hurts ships, since it's the mining tool.
- **Other damage:** blasts 0.5 at their centre; the ram 1.5.
- **Ore** grows with area, 1.2e-5 per unit (a radius-380 rock holds about 5),
  shed as orbs of 0.25.
- **Pieces:**
  - smaller than radius 20 are dust;
  - thrown out at 220 u/s with a spin kick.
- **Shards:** flight 1.2 s (drag 1.6/s), sink 2 s, gather spring 4 s, 300
  u/s, spread 1.12, rest spin 0.12 rad/s; 400 kept, the oldest shrinking away
  over 1 s.

_(Found while building:)_
- **Pieces vanished.** With a minimum piece radius of 60, blasting a field left
  only dust: its rocks are mostly pebbles, whose pieces were all too small. At
  20 a pebble leaves pieces too.
- **Rocks never glowed.** Making the glow fade quickly made it cool faster than
  the beam heats it, so a burning rock never warmed. Now it only cools once
  the beam has left.

Measured on `level2`:
- The beam wore the rock by the start from 5.3 ore to 0.3 in 3 s, and it
  broke into 6 shards, bursting the rest: 5.29 ore reached the hold.
- A strong blast at the field's edge broke 20 rocks into 36 shards, thrown up
  to 1150 from their places. All were back within 2 units by 9 s after the
  blast.
- The core was never hurt.

#### A5. Rocks with volume, and a core of its own stone

The A3 shader lit each pixel from the texture's normal map alone. That is
small bumps only, so a rock had no lit side and no dark side, only a
straight-line darkening toward the rim; its edge was a hard polygon
stair-step; and the core, three times the size, read as a flat cut-out of
repeating wallpaper.

**Decided:** all four of the shading changes below, and a different stone for
the core. Rocks don't shadow each other.

- **A dome.** Each rock is a height field: a dome for its shape, flat in the
  middle and turning away toward the edge, with the texture's bumps on top.
  It has a lit side, a dark side and a line between them.
- **Shadows inside the rock.** From each pixel the shader steps 12 times
  across the height map toward the light; where a ridge stands above the ray,
  the pixel is in its shadow.
- **Smooth edges.** The last pixel inside the outline fades out.
- **A large-scale layer.** The same texture again at five times the size,
  blended in, so a big rock has big features.
- **The core's own stone:** dark, cool and glossy, like basalt, with a
  specular highlight, stronger large features and rounder edges. It reads
  at a glance as a different thing: the one rock that never moves, can't be
  mined and hurts to touch.

_(Choices made while building -- to confirm:)_ on sliders under
**Asteroids → Look → Stone** and **Core stone**:
- Stone: ambient 0.35, large features 0.3, round edges 1.6, self-shadow 0.8,
  shine 0.06.
- Core stone: tint (0.62, 0.68, 0.82), ambient 0.2, large features 0.8, round
  edges 2.5, self-shadow 1, shine 0.5.
- The old "Rim shade" slider is gone: the dome replaced it.

_(Found while building:)_ `wgpu2d` effects had three free vec4s, and a lit
material needs sixteen numbers. `EffectParams` gained a fourth, `d`, the way
`c` was added for the CRT.

**Cores fit their fields** (found in `level1`, fixed with A5):
- `level1`'s field is a thin strip (about 480 wide) painted with max size
  400, so its core was 2400 across: it covered the strip and left no room for
  rocks, and it swallowed the four enemies beside the start.
- **A core now shrinks to fit inside its paint.** It's grown at full size,
  then each corner is walked out along its own direction to where the paint
  ends, and the whole outline is scaled so every corner fits. The real shape
  fits, not a circle round it. Erased holes don't count, since a clearing
  round the core is its room: `level2`'s core sits over one and keeps its full
  size. It never goes below 0.5× the max size (**Cores → Core min fit**), so
  one dragged out of the paint is still a rock. `level1`'s is now 239;
  `level2`'s and `level3`'s are unchanged.
- **The editor never deletes a field on one click, but no longer ignores it.**
  Right-clicking a field or its core selects the field, and the panel's
  button reads **Delete field**. It used to do nothing on a core, and a right
  click on a field could fall through to the planet behind it and delete
  that. The core's handle and pick radius follow its fitted size.
- **Each painted patch is its own field.** A Paint stroke used to go into the
  selected field wherever it started, and the field stays selected after a
  stroke, so a second patch far away joined the first and their one core
  landed between them. Now a stroke grows the field it starts on (its paint
  under the brush's middle or half-way out), and one started on open space
  starts a new field. "Start a new field" still forces one, even over another
  field's paint.
- **New fields default to max size 90, gap 40**: `level3`'s small, many
  rocks, which look best.

---

## Later

- **Enemy AI:** patrols, ambushing the player at resources, team attacks. Needs
  L2's places to exist, and P1's steering.
- **Procedural levels**, on L2's format.
  - **Asteroid fields in clumps.** The generator decides *where to paint* from
    `engine/scatter`'s `density` (built and measured in A1b): several fields
    with open space between them, rather than one field with gaps inside it.
    The painted area then always matches where the rocks are, so "hidden in a
    field" stays honest.
  - _(An alternative, if large max gaps ever feel like hiding in the open:)_
    hidden could mean "a field rock within a couple of ship lengths" rather than
    "inside the paint". It's a proximity query each frame, so it needs a
    spatial lookup.
- **Engine idea:** the GPU starfield (roadmap F5) as each level's backdrop.
