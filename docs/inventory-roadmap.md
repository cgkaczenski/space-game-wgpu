# Hints, controls and inventory roadmap

Two threads, in this order:

- **Teaching the controls:** hints that appear in the level and on the HUD,
  levels made to teach one thing each, and the controls in the menu.
- **The inventory:** what the player owns, what they take into a mission,
  what they find there, and what they lose. Weapons are the first items.

Work on this happens on the `text-menus` branch, after the pause menu
(gameplay roadmap U2). The file is still called `inventory-roadmap.md`
because that is how it started.

**How this file works.** It works like `gameplay-roadmap.md` and
`sight-roadmap.md`. The author sets the requirements. Items stay loose until
the author and Claude refine them, and each session does one milestone. What
the author asked for is marked **Asked**. Everything else is **Proposed** or
a **Suggestion**, and none of it is decided until it says so.

Each item has two kinds of note, kept apart:

- **Open questions:** what has to be answered before building. The author
  has asked for these to stay open and be settled as each one becomes
  necessary.
- **Engine ideas:** suggestions based on what `wgpu2d` and `engine/` already
  make cheap.

Items carry a letter and a number: **K** for controls (keys and the
actions they drive), **H** for hints, **T** for tutorial levels, **I** for
the inventory. Numbers are never reused.

---

## Teaching the controls, as asked

**Asked:**

- **Hints for the controls**, before the inventory. For example, when a level
  first loads, a prompt says to press 4 or use the mouse wheel to select the
  beam, so the asteroids can be mined.
- **A hint lives in the level.** It pops up in the world and hovers at a
  specific place in the level, such as the asteroid to mine.
- **A hint also uses the HUD.** For example, it highlights the weapon slot,
  which animates by changing size.
- **Levels dedicated to teaching** the different controls.
- **Controls settings in the menu.**

### The controls today

Everything the player can press, as of U2. Inputs are read straight from
`platform::Button` in four files (`gameLayer`, `playerMove`, `weapons`,
`zoomControl`); there is no table of actions in between.

| Input | Does | Read in |
|---|---|---|
| W A S D or arrows | Fly. What they mean depends on the control scheme: toward or away from the mouse (MouseThrust, the default), turn and thrust (TurnWithKeys), or screen directions | `playerMove` |
| Mouse | Aim. The hull faces it, except under TurnWithKeys | `gameLayer` |
| Left button, held | Fire the selected weapon | `gameLayer` |
| 1–4, or the wheel (wrapping) | Select a weapon. 4 is the beam, which mines | `weapons` |
| Right button: hold, then let go | Aim the ram (its path is drawn), then ram | `gameLayer` |
| E | Cloak | `gameLayer` |
| Tab | Fight or flight mode | `gameLayer` |
| Shift, held | Brake; in a lane in flight mode, slide | `gameLayer` |
| V, held | The long-range scope | `gameLayer` |
| M, held | The whole map | `gameLayer` |
| Ctrl + wheel, or = and − | Zoom | `zoomControl` |
| Escape | The pause menu | `gameLayer` |

Not pressed, but also to teach: mining (the beam on a rock throws ore that
comes to the ship), hovering at the gate to extract, flying into a jump gate,
riding a lane, and staying inside the closing circle.

The control scheme is a debug-panel setting. U2 left it out of the menu;
K2 reopens that.

### What exists already, for hints

| What | Where | What it gives this work |
|---|---|---|
| A pixel font, `measureText`, a shadow | `wgpu2d::Font`, `textLook` (U1) | A hint's text, and the size of the box around it |
| World points drawn crisp in screen space | `damageNumbers` (U1) | How a hint hovers over a place in the level and stays on the pixel grid |
| Off-screen chevrons | `hud::markOffScreen`, `hud::pointTo` | Pointing at a hint's place when it is off screen |
| Lines, circles, rectangles | `renderLine`, `renderCircleOutline`, `renderRectangle` | A leader line from the box to its target, a ring round it |
| An outline of any sprite's shape | `outline` | Outlining the asteroid or enemy a hint is about |
| HUD slots drawn from rectangles; the HUD as one layer | `hud`, `LayerEffect` | A slot drawn larger is a bigger rectangle; the whole HUD can move as one |
| Additive glow and its bloom | `BlendMode::Additive`, `FinalGlow` | A highlight that glows |
| The ram's path, the player's hull drawn faint | `ramPath`, `theirGhost` | Showing a move before the player makes it |
| State a step can wait for | `Loadout.selected`, `resources::held`, `energy::isCloaked`, `shipMode`, `scope::held`, `ram`, `jumpGates`, `gate`, `lanes` | Most "did the player do it" checks are one read |
| Levels as plain text; markers; editor tools | `level`, `levelEditor` | Where a hint is placed, and how it is saved |
| Loading any level; the last one played | `switchLevel`, `lastLevel.cfg` (U3) | Moving from one tutorial level to the next |
| A choice row and a menu page | `wgpu2d::Menu` (U2) | The control scheme in the menu, and a rebinding page |
| A small player file | `playerSettings`, `settings.cfg` (U2) | Bindings, and which hints have been seen |
| The game clock's speed | `gameClock` | Slow motion during a hint, if wanted |
| The CRT's switch-off and white-out | `crt`, `gameState` | Cuts between tutorial levels |

---

## Now: actions, hints, tutorial levels, and the controls in the menu

### K1. Actions: one table between the keys and the game — *built*

Today each feature reads its own keys. A hint that says "press 4" is then
written by hand, and is wrong as soon as a key is rebound.

**Proposed**
- **A table of actions:** Thrust, Turn, Fire, Select weapon 1–4, Next
  weapon, Ram, Cloak, Mode, Brake, Scope, Map, Zoom, Pause. Each has one or
  more bindings: a key, a mouse button or the wheel.
- **Every feature reads actions, never keys.** The input code in the four
  files moves onto the table, and nothing changes for the player.
- **Every action can say its binding's name**, such as "4", "WHEEL" or
  "RIGHT MOUSE". That is what hints print.
- **Lands:**
  - `engine/`: the table, and reading it through a function the
    application supplies (is this input held, pressed, released). It never
    sees GLFW, so another game could use it unchanged.
  - Platform: that function.
  - Game: the actions and their default bindings.

**Built**
- `engine/actions`:
  - a `Table` of actions, each with up to 4 bindings;
  - a binding is a key, a mouse button or a wheel axis, optionally with a
    modifier key held;
  - `held`, `pressed`, `released`, `repeated` (for menus) and `steps` (wheel
    notches);
  - `name` and `names` for hints ("4 / WHEEL", "CTRL + WHEEL").

  It reads input only through a `Source` of plain function pointers, so it
  never sees GLFW.
- **Most specific wins:** a plain binding is shadowed while a binding on the
  same input has its modifier held. With Ctrl + wheel bound to Zoom, holding
  Ctrl takes the wheel from weapon cycling. That replaced the weapons
  module's own "not with Ctrl" check, and is the general form of it.
- `platform::actionSource()` and `platform::buttonName`.
- `gameLayer/controls`: 26 actions with the defaults, key for key what the
  game read before. The menu's keys are their own actions (Menu up, Menu
  confirm...), defaulting to the same keys, so rebinding flying does not
  move the menu.
- Every gameplay input reads actions now: `gameLayer`, `playerMove`,
  `weapons`, `zoomControl` and `menu`. Still raw: the level editor (a tool),
  the menu's pointer, and the debug panel.
- A **Controls** section in the debug panel lists every action, its bindings,
  and lights the ones held.
- **Not moved: whether ImGui has the input.** Each call site keeps its own
  check, as before. With ImGui's keyboard navigation on, ImGui claims the
  keyboard whenever the panel is focused, so one blanket rule would stop the
  ship flying while tuning. The checks are uneven today. Movement and cloak
  have none; Tab, Shift, V and M check the keyboard. That is worth settling
  in K2, where a rebound action can change device. _(Settled in K2.)_
- One difference, in practice unseen: zoom sums the wheel's two axes, where
  it used to take horizontal only when vertical was 0. They differ only when
  both move in one frame.
- Checked:
  - a test of the engine's rules with a fake input layer: every combination
    of Ctrl and both wheel axes against the old hand-written rule;
    any-binding holds; pressed, held and released on the mouse; the wheel
    shadowed under Ctrl; names; out-of-range actions;
  - the game builds and runs with no errors.

  Not yet played with real input.

**Open questions**
- Is a controller ever likely? The platform layer already reads GLFW
  gamepads (`ControllerButtons`), so an action table is where that would
  plug in: a `Device::Pad` and one more function in the `Source`.

### H1. Hints on screen: in the level, and on the HUD — *built*

**Asked:** a hint hovers at a place in the level, and highlights the HUD,
such as a weapon slot that animates by changing size.

**Proposed**
- **A callout:** a box of text with a leader line to a world point, drawn in
  screen space as the damage numbers are. When the point is off screen, the
  box sits at the edge with a chevron toward it.
- **The target can be outlined:** a ring round a point, or the outline of the
  thing itself, such as a rock or an enemy.
- **HUD highlights**, one per element: a weapon slot, the ram slot, the mode
  slot, the health bar, the energy bar, the map. A highlighted element pulses
  larger and smaller about its centre, with a bright frame.
- **Keys as key-caps:** "PRESS [4] OR [WHEEL]", with the binding's name in a
  small drawn box rather than plain text. A key-cap lights while the player
  holds that action, so the hint reacts to the press.
- **Lands:**
  - `wgpu2d`: the callout (box, text, leader, edge clamping) and the
    key-cap. Any game's tutorial wants both.
  - Game: `hud::highlight(element, strength)`, the colours, which hints
    exist.

**Decided**
- **A speech bubble with a tail**, not a box with a leader line: the tail is
  a pointed triangle from the box to the target.
- **Mouse buttons and the wheel are drawn**: a small mouse with the meant
  part filled, and words beside it in the same cap (`LEFT CLICK`,
  `RIGHT CLICK`, `WHEEL`; markup `[mouse:wheel WHEEL]`). The words were added
  after a first look. Other keys are key-caps with their names.
- **Hints have their own colour**, a cyan-green used only for teaching
  (`textLook::hintColour`): bubbles, rings and HUD highlights.

**Built**
- `wgpu2d::drawCallout`, `measureCallout` and `renderMarkup`, in
  `src/render/callout.cpp`:
  - The bubble is two triangles and some rectangles. The frame colour is
    drawn first, then the fill inset by one font pixel over both, the
    tail's fill reaching into the box so the seam is covered.
  - The text is markup: `[4]` a key-cap, `[!4]` lit, `[mouse:left]`,
    `[mouse:right]` and `[mouse:wheel]` icons, `\n` between lines.
  - `offset` is the gap between the target and the box's nearest edge, so
    a wide box never covers its own tip.
  - Off screen, the tip is held at the screen's edge and the box comes in
    from there.
  - Sizes are in font pixels times the scale, so a pixel font's bubble stays
    on the font's grid.
- `hud::highlight(element)` and `hud::elementRect(element, w, h)`:
  - Elements are the four weapon slots, the ram, the mode, health, energy
    and the haul line.
  - A highlighted element grows by up to 14% about its centre at 1.6 Hz on
    real time, so a pause doesn't freeze it, and gets a frame in the hint
    colour. The haul line, being text, gets the frame only.
  - The slot row's geometry is one function now, shared by drawing and by
    `elementRect`, so a tail ends exactly on the slot.
- `gameLayer/hints`:
  - `keys(action)` turns an action's current bindings into caps
    ("[4] OR [mouse:wheel]"), lit while the action is held, so hints follow
    rebinding.
  - `atWorld(point, markup, ring)` adds a pulsing ring round a world point.
    `atHud(element, markup)` points the tail at the element's edge nearest
    the screen's centre and highlights the element.
  - Hints are drawn in screen space over the HUD and the map, unshaken,
    under the menu.
- **Debug panel → Hints:** three test hints. The beam ahead of the ship,
  weapon 4 and the ram on the HUD, and a point far off screen.
- Checked with offscreen captures at 2560 × 1440, all three tests on:
  - each bubble's tail ends on its target: the ring, slot 4, the ram slot,
    and the screen's edge toward the far point;
  - the slots carry the highlight frame;
  - the mouse icon shows its button.

  The first capture found the off-screen bubble covering its own tip, which
  is why the offset is measured to the box's edge.

**Not done, for H2**
- Whether a hint pauses, slows or leaves the game running, and what
  dismisses it.
- Outlining the target itself (a rock, an enemy) with `outline`. The ring
  stands in for now.
- A highlight on the map, which `explorationMap` draws: it would need to
  expose its rectangle.

**Engine ideas**
- The size animation is cheap: `hud` draws slots as rectangles, so a pulse
  is a scale on one rectangle about its centre on real time. An additive
  copy of the slot's frame, bright enough to bloom (FinalGlow), makes it
  glow with no new effect.
- The outline on the target is `outline::begin` / `end`, as the ghosts use.
- A hint about the ram can draw the ram's own path preview (`ramPath`) from
  the ship toward the target. A hint about flying somewhere can draw the
  player's hull faint where it should go, as `theirGhost` draws it.

### H2. Hint scripts: steps, and what finishes each — *built*

**Asked:** the beam example. When the level loads: select the beam, then
mine.

**Proposed**
- **A script is a list of steps.** Each step has text, an optional place in
  the level, optional HUD highlights, and **what finishes it**:
  - an action pressed (Cloak);
  - a state reached (the beam selected, ore in the hold, flight mode);
  - a place reached (the ship within a radius of a point).
- A step can also **start** something when it begins, such as spawning a
  placed enemy, so a step can say "now ram that one".
- **Authored in the level:** steps are lines in the level file, placed with
  an editor tool, the way gates and enemies are. A level without steps has
  no hints.
- **Lands:**
  - `engine/`: the step machine (the current step, its conditions, moving
    on). It knows nothing of what a condition means.
  - Game: the conditions, the reading of the level file, and the editor
    tool.

**Decided**
- **The game keeps running** while a step is up.
- **A step ends when it is done, or on Skip hint**: a new rebindable action,
  Enter by default.
- **Any level with a script has hints.** A tutorial level is a level that
  has one.
- **A script runs on every load and every restart.** A **HINTS** row in
  Settings turns them all off.
- **The DISABLE HINTS box is inside the bubble**, on its last line beside the
  skip key. It switches off **this level's** hints, the rest of the script and
  on every later load, and is remembered in `settings.cfg`
  (`hintsOff level4.txt`). Other levels keep theirs. Switching Settings >
  Hints back on brings every level's back.

**Built**
- **`engine/sequence`:** the current step, its time, a finished step's
  linger (0.7 s, showing a lit DONE) before the next, a skip that does not
  linger, and one event per frame: Entered, Finished, Done.
- **The file format**, `hint "TEXT" [at x y [ring r] | hud element]
  [until word args...]`, in `level.h`:
  - The text is quoted by hand: only `\"` is escaped. A typed `\n` stays
    two characters and becomes a line break when drawn. `std::quoted` would
    have read it as `n`.
  - `{action}` in the text becomes that action's key-caps (`{weapon4}`,
    `{fire}`), so a step follows rebinding.
- **`gameLayer/hintScript`** gives the words a meaning:
  - `pressed <action>`, `selected <slot>`, `hold <ore>`, `near x y r`,
    `mode flight|fight`, `cloaked`, `kills n` (since the step began),
    `seconds s`, or nothing, so only a skip ends it.
  - A word it does not know waits for a skip rather than failing.
  - It runs before the trigger is read, so a click on DISABLE HINTS is the
    box's, not a shot.
- **Two small buttons** on a row of their own under the text, one text step
  smaller: `[SKIP]` in the bottom-left corner, `[ ] DISABLE HINTS` in the
  bottom-right. Each lights under the pointer, and a click on either is the
  button's, not a shot. SKIP moves on as the Skip hint key (Enter) does; the
  key still works but is not printed.
  - `drawCallout` takes `cornerLeft` and `cornerRight`: markup at
    `cornerScale` in the bottom corners. The box widens to keep them apart.
  - _(Was `[ENTER] SKIP` centred on the last line; changed at the author's
    request.)_
  - The box is the bubble's last key-cap. `drawCallout` can report where it
    drew each cap (`capRects`), so a cap can be a button. `hints` keeps them
    for a tagged bubble, and the script tests a click against last frame's,
    which a click never notices.
  - _(Was a bar at the top of the screen, switching every level's hints off;
    moved and narrowed to the level at the author's request.)_
- **The editor's Hints tool:**
  - a list of steps (add, delete, up, down);
  - the text, and where it points: the top of the screen, a place (click to
    put it there, ring width), or a HUD element;
  - the condition from a list, its arguments typed, with what it expects
    shown under it;
  - "Near the step's point" fills `near` from the place.

  The editor draws each step's place as a ring in the hints' colour.
- **Debug panel → Hints:** the current step, its time and condition, and
  "Restart the script".
- `session.kills` counts enemies killed this round.
- Checked:
  - a test of `engine/sequence` outside the game: entering, finishing,
    lingering, skipping out of a linger, Done once, no linger, an empty
    script;
  - in the game, with temporary scaffolding since removed: a three-step
    script saved and loaded back identical. The first try was not: a line
    break in the text split the line, which is how the escaping above came
    about.
  - selecting slot 4 finished step 1 (DONE shown) and step 2 began: its
    bubble at its place with the ring, two lines of text;
  - the bar drew at the top.

  - level 4 with the box in its bubble: switching the level off saved
    `hintsOff level4.txt` and the bubble went. Hovering could not be tested
    offscreen: GLFW only moves the cursor in a focused window.

  Not yet played with real input. Skipping, clicking the box and the
  editor panel have not been driven by hand.

**Still open**
- A world hint's bubble can sit over the HUD (the haul line, the bars). It
  could steer clear of the HUD's rectangles.
- Starting something when a step begins (spawning an enemy) is not built.
  T1 will show whether a tutorial needs it.

**Engine ideas**
- Most finishing conditions are one read of state that already exists (see
  **What exists already, for hints**). The "a place reached" check is a
  distance, as the gate's hover already uses.
- Spawning on a step reuses `enemyAi::spawnAt`, which placed enemies and the
  debug panel's spawn buttons already go through.
- "+ ORE" or "DONE" rising over the ship as a step finishes reuses the damage
  numbers' rise and fade.

### T1. Tutorial levels — *proposed*

**Asked:** levels dedicated to teaching the different controls.

**Proposed** (a suggestion for how to divide them, to refine):
1. **Flying:** thrust, aim, the brake, zoom.
2. **Fighting:** selecting weapons, firing, missiles locking on.
3. **Mining:** the beam, ore, the hold, extracting at the gate.
4. **Hiding:** the cloak, the fog, the scope, the map.
5. **Ramming and the shield:** the ram's aim, energy, the shield.
6. **Getting around:** fight and flight, lanes, jump gates.
7. **The closing circle:** staying inside it, and the burn outside it.

- Each is an ordinary level file with an H2 script. Reaching the gate
  finishes it and loads the next.
- **Lands:** levels (data) and the game.

**Open questions**
- Must the tutorials be played first, or can they be skipped?
- Does finishing one unlock the next, or are all open from the start?
- Where are they listed? U3's level select, or a Tutorials row in the menu?
- Enemies in them: placed, spawned by a step, or neither?

**Engine ideas**
- `switchLevel` already loads any level and starts a round in it; the next
  tutorial is a call to it. The white-out after extraction already hides the
  cut.
- The editor already places enemies, gates, lanes and jump gates, so the
  tutorial levels can be built with the tools that exist, plus H2's tool.

### K2. The controls in the menu — *built*

**Asked:** controls settings in the menu.

**Decided**
- **The scheme and rebinding.** A **CONTROLS** page under Settings: the
  control scheme as a choice row, then one row per gameplay action, then
  **Reset to defaults** and **Back**.
- **Two slots per action**, a primary and a secondary, as two columns. Left
  and right pick the column; pressing a cell shows PRESS A KEY and waits for
  the next key or mouse button. Escape cancels.
- **A key another action has is taken from it** (the author's rule). The
  other action's slot is cleared and shows NEEDS A KEY in red, and the page
  cannot be left until that slot has a key again. **Reset to defaults** always
  clears it.
- **The debug panel has a key only while text is typed into it**, and a
  mouse button or the wheel only while the pointer is over it. This is
  decided by the binding's device, in one place, so a rebound action follows
  its new device. Every call site's own check is gone.
  - _Changed:_ Tab, Shift, V, M, 1–4, = and −, and Escape now work while the
    panel has focus but nothing is being typed. Before, they did not.
- **What is not rebindable:** Escape and the menu's keys, which are the way
  back out of a bad binding, and the wheel's actions (Cycle weapon, Zoom),
  which need a wheel.
- **Keys** stay the platform's 48 (A–Z, 0–9, Space, Enter, the arrows, Left
  Ctrl, Tab, −, =, Shift), plus the two mouse buttons. Teaching the platform
  the rest of the keyboard is still open.
- **Saved** in `settings.cfg` as the scheme, plus one `bind` line for each
  slot that differs from its default (`bind cloak 0 key W`,
  `bind forward 0 none`). A changed default therefore still reaches a
  player who never touched that action.
- **The scheme is the player's now**, not tuning: it left the tuning file and
  is saved with the settings. The debug panel's radio buttons still change it
  for the session.

**Built**
- `actions::Binding` can be **unbound** (`code` −1), skipped by every query
  and by `names`. A cleared slot needs this.
- `wgpu2d::Menu::fields`: a label and several selectable cells, the column
  kept as the selection moves, so a page of them reads as a table.
  `Menu::note`: an unselectable line of text.
- `controls`: `rebindable`, `binding`, `slotName`, `bind` (returns the slot
  it took), `resetDefaults`, `firstPressed` / `anythingHeld` for the wait,
  and `write` / `read` for the file. The ImGui rule lives in its `Source`.
- `playerMove::scheme` / `setScheme`; `playerSettings::save`.
- The wait for a key arms only once everything is let go, so the click or
  Enter that opened it is not taken as the binding. The frame the wait ends
  gives the menu no input, so the Escape that cancels does not also leave
  the page.
- Checked, with temporary injected actions since removed:
  - from the pause menu to Controls;
  - W bound to Cloak took Forward's primary slot (Forward kept UP);
  - Back was refused with "GIVE FORWARD A KEY FIRST";
  - `settings.cfg` held exactly the two changed slots;
  - all 17 rows fit at 1440;
  - the engine test also covers unbound bindings.

  Not yet played with real input.

**Open questions**
- Should the platform learn the rest of the keyboard (F-keys, punctuation,
  Alt, the right-hand modifiers), and the middle mouse button?
- A page this long would need to scroll on a much smaller window. Fine at
  500 to 1440 tall today.

**Engine ideas**
- `wgpu2d::Menu` already has a choice row, so the scheme is one row.
- Waiting for a key is a loop over `platform::Button`'s list for whichever
  is pressed this frame.
- The menu pauses, so rebinding never happens mid-fight.

---

## Then: the inventory

### The loop, as asked

**Asked:**

- **Extracted weapons are banked.** Before the next mission the player
  chooses what to take. A light loadout is the economical choice, because
  anything taken in can be lost.
- **Weapons come from three places:** salvage from kills, buying with
  banked points, and finding them in the level.
- **On death everything carried is lost**, equipped weapons included.
- **The loadout menu does not pause.** While it is open the ship's controls
  are off, the ship coasts on its momentum, and the world keeps running.
  Opening it mid-fight is a real risk.
- **The loadout can change at any time**, mid-fight included. A weapon swapped
  into a slot starts on its cooldown, so a swap always costs something.

```
   stash ──choose──▶ carried into the mission ──extract──▶ stash
     ▲                 │    ▲          │
     │ buy             │    │ salvage, │ swap in the
  banked points        │    │ pickups  │ live menu
     ▲                 ▼    │          ▼
     └── ore, on    equipped (4 slots) ◀┘
        extraction     │
                       └──death──▶ lost, all of it
```

This is the same rule the ore hold already follows (L3): it spills when the
player dies and becomes points when they extract. Weapons join it.

### What exists already, for the inventory

| What | Where | What it gives this work |
|---|---|---|
| `Loadout`: 4 slots of `Weapon` by value, cooldowns, ammo, selection | `weapons` | Equipping is copying a `Weapon` into a slot. `loadoutOf(weapons, count)` builds a ready one. |
| A weapon is data: kind, damage, cooldown, best range, stun / lockdown / spread | `weapons::Weapon` (C2, B2) | An item needs no new type to start with. |
| Enemies roll their weapons and modifiers; bosses carry 2–4 | `enemyAi` (B1, B2) | Varied weapons already exist in the world, so salvage has something to drop. |
| `weapons::reset`: ammo refilled, cooldowns cleared | `weapons` | The rule for what a new mission does to a loadout. |
| The hold, banked points, `playerDropped` (the hold spills at the wreck) | `resources` (L3) | The extraction rule, already in code for ore. **Banked points live in memory only and vanish on quit.** |
| Orbs: thrown clear, a pause, then homing on the ship with a turn that keeps growing | `resources` | A pickup that comes to the player, ready to reuse for salvage. |
| An enemy's death: a fireball, and its own sprite tumbling as debris | `effects::enemyKilled` (C4) | Where a salvage drop comes out. |
| Weapon icons, by bullet style | `bulletLook::drawIcon` | What a weapon looks like in a menu or as a pickup. |
| HUD slots: cooldown shade, ammo pips, selection, dimmed under lockdown | `hud` | A swapped-in weapon on its cooldown already reads correctly. |
| `wgpu2d::Menu`: rows, a selection, keys and mouse, sliders, a drag | `render/menu.cpp` (U2) | The base for the live menu. It is a list, not a grid. |
| `wgpu2d::Font`, `measureText`, damage numbers | `render/font.cpp`, `damageNumbers` (U1) | Item names, prices, and a "+ PICKED UP" line over the ship. |
| Off-screen chevrons; the map's marks; fog that hides what isn't seen | `hud::markOffScreen`, `explorationMap`, `sight` | Salvage that is seen, shown, and pointed at. |
| Level markers and editor tools | `level::Marker` (only `Gate` so far), `levelEditor` | A new marker kind and tool for placed pickups. |
| Ship kit flags: shield, cloak, ram, per ship | `energy`, `ram`, level enemy lines (B1) | Kit modules could become items too (see suggestions). |
| Mass on every body, used by bumps | `movement::Body` (P1) | Weight, if a loadout should have any. |
| `safeSave`: a save library with backups, built for games | `thirdparty/safeSave` | **Linked, unused.** What a stash that survives quitting would be saved with. |
| The game clock's speed | `gameClock` | Slow motion, if the live menu ever wants it. |
| The world grade, the CRT's switch-off and white-out | `worldGrade`, `crt` | Looks for the live menu and for extraction. |

### I1. Items: the stash, the hold, what is equipped — *built*

The model, and the rules that move things between its parts. Nothing to look
at yet except the debug panel.

**Decided**
- **An item is a weapon kind and its modifiers** (`heavy` with `stun`), not a
  copy of a weapon's numbers. Its stats come from the kind as tuned, so the
  debug panel's weapon tuning reaches every one of that kind. Modifiers do
  not change an item's shape.
- **The beam is free and never lost.** A round without one gets a plain one,
  so a player can always mine their way back.
- **The hold is a 5 x 4 grid.** Everything carried but not equipped goes in
  it, as a shape that can be turned to fit:
  - burst 2 x 2 (4), missile an L (5), heavy 3 x 2 (6), beam 4 x 2 (8);
  - 23 squares in all, so the four never all fit spare at once;
  - the shapes are tunable, as tick boxes in the panel, saved with the
    tuning.
- **Ore takes squares too**, in stacks of up to 64 a square, so ore and spare
  weapons compete for room.
  - **One orb is one unit**: an orb's value over 0.25, rounded, at least
    1. A fragment is 1, a boss orb 2, a spilled orb 4.
  - Points are banked in the same units.
  - The HUD reads whole numbers: `HOLD 37   BANKED 120`.
  - **With no room, orbs stop coming and wait in space.** An orb that only
    partly fits leaves the rest waiting.
- **Not saved yet.** The stash lasts while the game runs, as points do.
- **A new player has the beam only**, in slot 4.
  - The debug panel's **Inventory → Slots** has a pick list per weapon
    slot, with stun, lockdown and spread. Saved with the tuning, it is the
    default loadout.
  - A change there equips at once, so it can be tried.
  - **Kept through death** (asked): a slot the pick list sets is refilled
    with its default every round the stash cannot fill it. It is a test
    bench, and a death should not cost what is being tested. Extracting
    banks it and the next round takes it back, so it is never doubled.
    Checked: a picked heavy laser with spread stayed through a death,
    two deaths and an extraction, the stash staying empty.
- **Until I4**, a round equips what was equipped last time, taken out of the
  stash as far as the stash still has it.

**Built**
- **`engine/hold`:** a packing grid.
  - Shapes as squares and quarter turns (`turned` normalises to the
    top-left).
  - `fits`, `place` (which moves a piece already there), `remove`, `find`,
    `findSpot` (reading order, every turn at each square) and
    `freeSquares`.
  - It knows ids and squares, nothing of what a piece is.
- **`gameLayer/inventory`:**
  - the stash, four equipped slots, the hold's grid with what each piece
    is (a weapon, or an ore stack);
  - `roundStart`, `extracted`, `died`;
  - ore: `ore`, `oreRoom`, `addOre`, `takeOre`;
  - `applyTo` builds the equipped slots into the player's `Loadout` every
    frame, from the tuned kinds and each item's modifiers.
- **Weapons:**
  - **A slot can be empty** (`Weapon::empty`), so key 3 is still slot 3:
    it cannot be selected or fired, and the wheel skips it.
  - `weapons::refit` puts a weapon in a slot, keeping its state if it is
    the same kind. A different kind starts on its cooldown (the asked swap
    cost). A new round still starts everything ready.
  - The debug panel's **Weapons** tunes the kinds (`playerKinds` in the
    game), with the same tuning keys as before. Enemies still take
    `shipWeapon`, untuned, as they did.
- **The HUD** draws an empty slot as its frame alone.
- `resources`: ore enters the hold as units, orbs wait when it is full,
  death spills the hold's ore, extraction banks it.
- Level 4's script: "hold 0.5" became "hold 1".
- Checked:
  - a test of `engine/hold` outside the game: turning, fitting, refusing an
    overlap without changing anything, moving, removing. With the beam
    standing and the heavy in, the L no longer fits but the burst does.
  - in the game, with a temporary self-test since removed:
    - a new player has the beam alone;
    - the hold takes 1280 orbs empty, and 640 with two weapons in it;
    - extracting banks all three into the stash, and the next round takes
      the beam back out;
    - dying loses them, and the next round gets the free beam;
    - an owned heavy laser with lockdown is equipped, and lost on death.
  - a capture: slots 1-3 empty, the beam in slot 4, `HOLD 0   BANKED 0`.

**Still open**
- Level 4's first step ("press 4 for the beam") finishes at once now,
  because the beam is the only weapon and already selected. Its weapons
  step assumes four weapons. The script wants a look once I2 or I3 changes
  what a player has.
- Saving, when it is wanted: `safeSave` is linked and keeps backups.

### I2. The live loadout menu — *built*

**Asked:** it does not pause. Controls are off, the ship coasts, and the
world runs. Swaps start on cooldown.

**Decided**
- **I opens and closes it** (the Loadout action, rebindable). Escape closes
  it too, rather than pausing over it.
- **Mouse drag; R or the right button turns the piece** while dragging. Its
  icon follows the pointer. Only over the hold does its shape show, where it
  would go, green where it fits and red where not; it is held by its middle.
  _(The shape followed the pointer everywhere at first; changed at the
  author's request.)_
- **The HUD's weapon row is the same four slots** while the menu is open:
  drag from it, drop on it, slot to slot, to and from the hold. A slot a
  weapon was lifted from is covered; one under a drag is framed green, or
  red for ore.
- **With the menu closed, the HUD's row can still be rearranged** (asked):
  drag one slot onto another and they swap (on their cooldowns, as any
  swap). Let go anywhere else and it goes back; nothing is dropped and
  there is no hold. A press that starts on a slot does not fire, and the
  ship flies on meanwhile.
- **Text:** the line under the grid says `[R] OR [RIGHT CLICK] TO ROTATE`
  while a weapon is dragged. About to be dropped, `LET GO TO DROP` goes
  along under the item. _(Was "LET GO TO JETTISON" on that line.)_
- _(A rotate button on each weapon in the hold was tried and taken out at
  the author's request: R while dragging is enough.)_ The HUD drag with
  the menu closed has not been played.
- **Dropped outside the panel, it is jettisoned** behind the ship. Ore goes
  as orbs, which wait until the ship has left them once, or they would fly
  straight back into the room just made. A weapon is gone, until salvage
  (I3) can leave it in space as a pickup.
- **A hit does not close it.** The player chose to open it mid-fight.
- **Defaults taken:** the HUD stays visible, and the world takes a light
  grey (0.45 of the paused grade, tunable) so it reads as "not flying".

**Built**
- **`gameLayer/loadoutMenu`:** a panel of the four slots and the 5 x 4 hold.
  - Each weapon is a coloured shape with its bullet's icon; ore stacks are
    amber squares with their count.
  - A line under the grid names what the pointer is over, and "LET GO TO
    JETTISON" outside the panel.
  - **Drops:**
    - on the hold: moved there;
    - on a slot: equipped, and what was there goes to the hold, where the
      dragged weapon was if it fits, else anywhere, else the move is
      refused;
    - slot to slot: swapped;
    - ore on a slot: refused.
  - Closes by itself when the round stops being played, and on a restart.
- **The ship while it is open:** a new `piloting` (live and the menu
  closed) gates the player's hands: flying, aiming, firing, the ram, the
  cloak, fight/flight, the scope. `controls` still says the round is being
  played, which the burn, the gates and death go by. The ship coasts: no
  thrust, no falloff, the cloak's drift.
- **Swap cost:** a weapon put in a slot by hand starts on its cooldown even
  when it is the same kind as what it replaced (`swappedIn`, applied by
  `applyTo`).
- **`inventory`:** `heldAt`, `equippedAt`, `moveInHold`, `holdToSlot`,
  `slotToHold`, `swapSlots`, `jettisonHeld`, `jettisonSlot`. Each happens
  whole or not at all, and the slots as they then are become what the next
  round equips.
- **`resources::jettison`:** orbs marked as thrown out.
- **`wgpu2d::GridView`:**
  - which square is where, the square under a point, the nearest square,
    the grid drawn;
  - `fillCells` draws a piece's squares with the gaps between neighbours
    filled, so it reads as one shape.
- **Controls:** Loadout (I) and Menu rotate (R or the right button) actions.
- Checked, with temporary scaffolding since removed:
  - the moves a drop makes, from code:
    - the missile from the hold into slot 1;
    - slots 2 and 4 swapped;
    - the heavy into slot 2 sent the beam back into the hold where the
      heavy had been;
    - a move onto an occupied place refused, nothing changed;
    - a stack of 36 jettisoned;
  - a capture of the menu open over a stocked hold, the world greyed, and
    the HUD's slot 2 shaded on its swap's cooldown.

  The capture came out at 1920 x 1080: with fullscreen on by default, the
  run went fullscreen on the primary display. A drag itself cannot be
  driven offscreen and has not been played.

**Still open**
- A swap refills a missile's ammo, since ammo is not kept on the item. A
  missile swapped out and back is full again. Worth keeping ammo on the
  item if it matters.
- Keyboard-only use (arrows and Enter) is not built; the mouse is the way.

### I3. Getting weapons: weapon crates — *built*

**Asked:** salvage from kills, buying with banked points, finding them in
the level.

**Decided**
- **Weapons come in crates.** A crate floats in space.
  - **Opening:** near the ship, hold the pointer on it. A ring fills over
    3 s, like the gate's, and the loadout menu opens with the crate's own
    hold beside the player's.
  - **Inside:** a crate has 4 x 2 squares, more than it uses, and every
    thing in it takes one square whatever its shape.
  - **Unknown until looked at:** a weapon a crate rolled is a grey 1x1 box
    until it is dragged over the player's hold or slots. Then it shows
    what it is and its shape, and stays known.
- **What a crate holds** is rolled by tunable percentages:
  - 1 to 3 weapons (60/30/10);
  - each kind by weight (burst 35, heavy 25, missile 20, beam 20);
  - stun 15%, lockdown 10%, spread 15%, never spread on a beam.
- **Salvage:** a kill leaves a crate at 35%. A boss always does, with 3.
- **Placed:** `crate x y gun:...` lines, each item a kind or random with
  each modifier yes, no or random, as an enemy's guns. No items: rolled. An
  editor tool places them and edits what is in them.
- **A crate stays until it is empty**, then goes. The player can come back
  to it.
- **A crate holds anything.** The player can drag a weapon or ore into an
  open crate's free squares. A weapon dropped outside the menu leaves a
  crate of its own holding it, already known. (I2 destroyed it.)
- **The shop waits for I4**, the screen between missions.

**Built**
- **`gameLayer/crates`:**
  - each crate is a position, a drift and a spin dying away, and a 4 x 2
    `hold::Grid` of 1x1 things (a weapon, known or not, or ore);
  - `reset` (placed crates), `enemyKilled`, `dropped`;
  - `update` drifts them and fills the hover ring of the one under the
    pointer, within reach of the ship and not under fog, and returns it
    when full;
  - `take`, `put`, `putAnywhere`, `move`;
  - drawn as a box in the crate's colours, with the ring in the hints'
    colour;
  - a **Crates** part of the debug panel's Inventory section tunes all of
    it.
- **The loadout menu:**
  - the crate's grid beside the hold, a `?` on each unknown weapon;
  - drags from the crate into the hold (its real shape, green or red) or a
    slot (what was there goes into the crate, in the square it left);
  - the player's things into a free crate square (green or red);
  - ore from a crate merges into the hold's stacks, and what does not fit
    stays in the crate;
  - the crate's side closes if it drifts out of reach.
- `inventory::addToHold` and `putInSlot`, for things from outside.
- The level's `gun:` word parser and writer are shared by enemies and
  crates.
- **The editor's Crate tool:** place, move, delete, and the selected
  crate's items as a list like an enemy's weapons.
- Checked, with temporary scaffolding since removed:
  - 2000 kills left 728 crates (36%). They held 1/2/3 weapons in 408/233/87
    of them (56/32/12%). Kinds came out 37/24/19/19%, stun 16%, lockdown
    10%, spread on 16% of non-beams and never on a beam;
  - a boss's crate held 3, and a full square refused a second thing;
  - placed crates saved and loaded back identical;
  - a capture of the menu with a crate: two unknown `?`, a stack of 12 ore,
    a known burst laser;
  - a capture of a crate in space beside the ship.

  The hover ring was not seen: a capture cannot hold the pointer on a
  crate. Opening by hovering, the reveal and the crate drags have not been
  played.

**Still open**
- With a crate open the panel is wider and covers the HUD's bars at the
  top right.
- Crates are not on the map and have no off-screen chevron. Both exist for
  the gate and ghosts (`explorationMap`, `hud::markOffScreen`), so either
  is a small step.

### I4. The hub, between missions — *built*

**Asked:** the player chooses what to take, and a light loadout is the
economical choice.

**Decided**
- **Launch goes straight into the last level**, as before. The pause menu
  has **QUIT TO HUB**, which banks nothing: what is carried is lost, as in
  a death. The debug panel's picked slots still come back.
- **Death restarts in place**, as before. **Extraction ends on the hub.**
- **The hub has:**
  - the last run's tally;
  - the stash;
  - the loadout: what to take, in the four slots and the 5 x 4 hold;
  - the shop;
  - level select.
- **Behind it, the starfield**, drifting slowly.
- **The shop's stock is rolled**, like crate contents, modifiers and all:
  6 items, restocked after each mission. Prices are by kind (burst 20,
  heavy 40, missile 50, beam 30) plus 25 a modifier, all tunable. Click a
  tile to buy into the stash.
- **Selling:** drag a weapon onto the shop for half its price.
- **The stash is unlimited tiles**, 5 wide, scrolling with the wheel.
- **Spares can be taken in the hold** as well as the slots, at the cost of
  room for ore, and lost with everything on a death.

**Built**
- **`gameLayer/hub`:** the screen, in screen space over the background.
  - **HANGAR** and the tally, e.g. "EXTRACTED: 37 ORE BANKED, 2 WEAPONS TO
    THE STASH", or "LEFT THE MISSION: NOTHING BANKED, n WEAPONS LOST".
  - **Points.**
  - **Three panels:** STASH, TAKING (slots and hold) and SHOP.
  - **Drags:**
    - stash to a slot (what was there goes home);
    - stash to the hold (its shape, green or red, R to turn);
    - slot or hold back to the stash;
    - slot to slot;
    - any of them onto the shop, to sell.
  - Under the panels, a line names what the pointer is over, with its sell
    price or its price.
  - The levels as buttons, **LAUNCH** and **QUIT GAME**.
- **The flow:**
  - an extraction ends on the hub (`openHub`, the shop restocked) instead
    of restarting;
  - `hubFrame` runs while it is open and nothing of the round does;
  - LAUNCH switches level if another was chosen, and starts the round.
- **`inventory`:**
  - `stageLast`: the hub opens with the slots as they were, from the
    stash;
  - `launch`: the next round keeps the slots and hold as packed, instead of
    re-equipping;
  - `abandoned`, `stashItems`, `takeFromStash`, `addToStash`,
    `carriedWeapons`.
- `resources::spend` and `earn`. `crates::roll`, for the shop's stock.
- **`gameLayer/itemLook`:** each kind's colour, its name, a weapon's tile.
  Shared by the loadout menu and the hub.
- **Debug panel → Inventory → Hub:** prices, stock, restock, +100 points,
  and "Open the hub".
- Checked, with temporary scaffolding since removed:
  - from code: the hub opened with the beam staged in slot 4 and two
    weapons in the stash; a heavy into slot 1 and a missile into the hold;
    LAUNCH kept both rather than re-equipping;
  - captures of the hub with a tally, six stash tiles, a stocked shop with
    prices, and the level buttons. The first capture found the top row of
    prices hidden, and the levels showing `.txt`.

  Buying, selling and the drags have not been played with the mouse.

**Still open**
- The background is the game's whole parallax backdrop, nebulae and all, not
  only stars. It is bright behind the panels in places.
- Saving the stash and points, when wanted.

---

## More suggestions in line with this

These are labelled suggestions, all built from what exists.

**For teaching the controls**

- **Hints that wait for a need.** A hint in an ordinary level that appears
  the first time it is useful: "press [E] to cloak" the first time an enemy
  engages the player; "hold [V] to scope" the first time a ghost is off
  screen; "[TAB] for flight" the first time the player enters a lane in
  fight mode. Each condition is one read of state that already exists.
- **A practice target that does not fight back.** A placed enemy with no
  weapons, for the fighting level, so firing can be practised without being
  shot. A `Loadout` can be empty, but today an enemy placed with no weapons
  listed gets rolled ones, so the editor would need a "None" choice.
- **The key-caps on the HUD.** The weapon slots could show their number, and
  the ram slot "RMB", in the pixel font under each icon. They teach without a
  hint, and follow rebinding through K1.
- **A replay of the move.** For a manoeuvre, such as braking into a lane or
  ramming round a rock, the hint plays the player's hull doing it, faint, as
  a loop: recorded positions drawn with `theirGhost`'s look.

**For the inventory**

- **Weight.** Every body has a mass (P1), and bumps already use it. Each
  carried weapon could add mass, so a heavy loadout accelerates slower,
  turns wider and hits harder in a bump. A light loadout is then cheaper
  *and* faster. That gives the economical choice a second reason, through
  the existing integrator.
- **Rarity is the modifiers.** A weapon with stun, lockdown or spread is
  rarer. Enemy shots already glow yellow for stun and violet for lockdown
  (B2), so pickups and menu icons could use the same colours. The player
  learns one visual language.
- **See the loot before the fight.** Since enemy shots already show their
  modifiers, a dangerous enemy is a valuable one. The scope (S4b) or the
  missile lock box could also name the target's weapons in text, so the
  player can pick their fights.
- **Kit modules as items.** Shield, cloak and ram are already per-ship flags
  (B1), set per placed enemy in level files. As equipment they would be lost
  on death and found as salvage, like weapons. Losing your cloak would
  change how the next mission is played.
- **Ammo as a consumable.** Missiles already have ammo (5). Rounds could be
  carried and bought, and run out across a mission rather than refilling on
  every round.
- **Caches in the fields.** Placed pickups deep inside asteroid paint, where
  the ship is slow (W2) and the fog hides what is around it (S3): risk for a
  reward, using levels that exist.
- **Salvage the circle takes.** Drops outside the closing circle burn away
  after a few seconds (`arena::burn`), so the circle pushes the player to
  choose between loot and safety.
- **Scavengers.** An enemy behaviour that flies to dropped salvage and picks
  it up, arming itself through its `Loadout` and `weapons::choose`. The loot
  becomes contested. `enemyAi`'s steering already flies to points.
- **A secure slot.** One equipped slot that is not lost on death. It is a
  common lever in extraction games for softening the "run out" problem
  without a free weapon. Pure rules, no engine work.

---

## What the render API already gives this

Four rows ask for anything new in the library.

| Want | Existing mechanism | New library work |
|---|---|---|
| A hint's box with a leader line to a world point | `renderRectangle`, `renderLine`, `renderText`, `measureText` | **a callout in `wgpu2d`**: box, leader, clamping to the screen's edge |
| A key-cap: a key's name in a drawn box | the same | **a key-cap in `wgpu2d`** (small) |
| A HUD slot pulsing larger | `hud`'s rectangles, scaled about a centre | none |
| A glow on a highlight | Additive blend, FinalGlow | none |
| Outlining a hint's target | `outline` | none |
| A hint's place off screen | `hud::markOffScreen` | none |
| Weapon icons in a menu | `bulletLook::drawIcon` (game) | none |
| A grid of items with a selection | `Menu` is a list | **a grid in `wgpu2d`, beside `Menu`** |
| Dragging an item into a slot | `MenuInput`'s held pointer, the slider drag | **a drag-and-drop on that grid** (small) |
| Item names, prices, tallies | `Font`, `measureText`, `renderText` | none |
| Pickups that glow | Additive blend, FinalGlow | none |
| Pickups under the fog, pointed at, on the map | `sight`, `hud::markOffScreen`, `explorationMap` | none |
| Rarity colours | Vertex colour tint | none |
| The menu sliding in | `LayerEffect` | none |
| A lighter grade behind the live menu | `worldGrade`'s pause grade at a strength | none |
| Saving the stash | `safeSave` (not render) | none |

---

## Suggested order

**K1 → H1 → H2 → T1 → K2, then I1 → I2 → I3 → I4.** The suggestions can go
anywhere along the way. Weight fits best alongside I1, and rarity colours
alongside I3.

- **K1** first: hints print bindings, and rebinding needs the table. It
  changes nothing the player sees, so it is safe to do before anything is
  built on it.
- **H1** before H2: a hint that can be drawn can be tried by hand from the
  debug panel before scripts exist.
- **H2** before T1: a tutorial level is a level with a script.
- **K2** can move anywhere after K1. It is last only because hints are what
  was asked for first.

- **I1** before anything else: a loadout menu with nothing to swap tests
  nothing. Its debug panel fills the containers until I3 exists.
- **I2** is the asked-for menu, and the first place the model is seen.
- **I3** before I4: choosing what to bring only matters once there is a
  stash worth choosing from.
- **I4** changes the game's flow (death and extraction lead to a screen), so
  it goes last, and goes with U3's level select.
