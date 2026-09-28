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
  too, but do not delay the break.
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
  selected but not fired. **Shift + wheel** zooms.
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

**Decided — C4b, the ram:** Space. A 0.1 s **wind-up** first: the ship dips
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

### U2. In-game menu

The natural home for the control scheme and settings that live in the debug
panel today.

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

**Engine ideas**
- The shield bubble already takes per-quad parameters. A boss's shield can be
  the same bubble in another colour, rippling where it is hit.
- A cloaked boss can bend the world around it as the player's cloak does, with
  one more field in the cloak's pass, the way the gate's swirl was added.
- A boss health bar is the HUD bar again, and the empty-energy shake exists.

### Asteroids (A1–A4)

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

**Open questions**
- Fan from a star-shaped polygon, as proposed, or general polygons with ear
  clipping from the start?
- What does a rock block? The player's shots, enemy shots, the beam (it
  already stops at deposits), **enemy sight** (C5's cones — a line-of-sight
  test against rocks, which is what makes a rock a hiding place)?
- How are they placed: a level line (`asteroid x y radius seed`) and an editor
  tool, or fields scattered by a rule?
- Sizes: one range, or classes (pebble, boulder, big rock)?
- Drawing triangles: teach the batch real triangles, or send each one as a
  rectangle with a repeated corner through the batch as it is?
- The texture can't repeat today (the sampler clamps at the edge), so each rock
  takes a random window of it. Is that enough variety?
- "Certain enemy types" hide: which ones, and is that A1, or enemy AI after P1?

#### A2. Physics: shot and beamed

**Proposed:** each rock is a **rigid body**: position, velocity, angle and
spin. Its mass, centre of mass and moment of inertia come straight from the fan
triangles, so its shape decides how it turns. A hit is an **impulse at the
contact point**: the change in velocity is the impulse over the mass, and the
change in spin is (contact offset × impulse) over the moment of inertia, so an
off-centre hit spins it. The beam is a steady **force** at its contact point.
_Engine_: the body knows nothing about rocks, and P1's ship body may end up
using the same one.

**Open questions**
- How hard does each weapon push? Scale with damage, or a push value of its
  own per weapon (missile hardest)? And the ram?
- Do rocks collide with **each other**? (Not with ships — decided.) That is
  most of the extra work: polygon contact and a bounce.
- Space drag: do they drift forever, or slowly come to rest? Spin too?
- Anything from the closing circle (L4): do rocks outside it burn, drift, or
  nothing?
- Mass from area alone, or a density per rock?

#### A3. The shader: a lit rock

**Proposed:** an app effect (`asteroid.wgsl`, F6's per-quad effects) that
lights the colour map with the normal map, and uses the height map as a mask.

**Open questions**
- Where is the light? One direction per level (a sun), fixed for the game, or
  something that moves?
- Two maps in one texture (colour and normal side by side, sampled at two
  places — no library change), or teach effects a **second texture binding**
  (library change, overlapping N8's normal-mapped lighting)?
- Heat from the beam: a glow spreading from where it hits and cooling after,
  with the low (dark) areas of the height map glowing first, like molten
  cracks?
- Damage cracks: as a rock weakens, the height map reveals cracks before it
  breaks — the shield dissolve's threshold trick?
- A rim toward the silhouette: a value 1 at the outline and 0 at the centre,
  blended across each fan triangle by the GPU?
- Photographic, or coarsened in the shader to sit with the pixel-art ships?

#### A4. Breaking up, and what a rock is worth

**Open questions**
- How does it break?
  - **a.** into wedges along the fan: still star-shaped from the old centre,
    so no new algorithm;
  - **b.** into newly generated smaller rocks: simplest, but they don't fit
    together;
  - **c.** cut along the shot's line and ear-clip the pieces: the most real,
    and the most work.
- What breaks it: damage from any weapon, or only the beam, as with deposits?
- How small before a piece stops being a rock, and what is left then: debris
  like the wrecks (C4a), orbs (L3), nothing?
- **As a resource:** does mining a rock work like a deposit (beam in, orbs
  out)? Do rocks replace deposits, or sit beside them?
- Do the pieces keep the parent's motion? (Each piece's velocity should be the
  parent's velocity plus its spin at that point.)
- A cap on pieces alive, like the wreck field's 400?

---

## Later

- **Enemy AI:** patrols, ambushing the player at resources, team attacks. Needs
  L2's places to exist, and P1's steering.
- **Procedural levels**, on L2's format.
- **Engine idea:** the GPU starfield (roadmap F5) as each level's backdrop.
