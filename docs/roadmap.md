# Roadmap

Planned work. The companion to [`webgpu-learning-outline.md`](webgpu-learning-outline.md),
with the opposite lifetime: **the outline only grows and only describes things
that exist; this file shrinks.** An item is deleted from here when it lands, and
a block describing it appears in the outline in its place. Nothing on this page
is built.

Every item says where it **lands**: _library_ (`src/render/`), _engine_
(`src/engine/`), _game_ (`src/gameLayer/`) or _app_
(`src/platform/`). See **The shape features take** below for what each one
means and what the test is.

That tag is the point of the tagging, not bookkeeping: it forces the question
"would another project want this?" before the code is written rather than
after.

**Numbers are stable and never reused.** A gap means the item landed and now
has a milestone block in the outline instead. R1–R7 and N1 are gone that way;
outline 11, 12 and 13 are where they went.

---

## The standing goal: `wgpu2d` as a library — reached

`wgpu2d` is a CMake target that links `webgpu`, `glm` and `stb_image`, knows
nothing about GLFW, carries its own shader, and cannot include this game's
headers. All four blockers are gone: the glob, `RESOURCES_PATH`, the game
effect in the library folder, and the window dependency. **Liftable like gl2d,
not frozen at gl2d** — mirroring its signatures was a port tactic that finished
at milestone 7; `BlendMode` and `LayerEffect` are additions past it, and new
drawing capabilities go in `wgpu2d.h` rather than beside it.

Two limits worth stating so nobody assumes more than is true. The wall is
one-directional: a library cannot see the game, but `engine` and `wgpu2d` can
see each other, because both sit under one `include/` root. And nothing has
actually built `wgpu2d` against a second application, which is the only real
test of a library — until something does, "liftable" is a claim.

**Not everything here should travel.** The HUD, the ships, the tuning constants
and the game's ImGui panels are this game's. The renderer, the camera, the
batch, the target and post-process machinery are the library's. Reusable
gameplay systems that draw nothing are `engine`'s.

---

## The shape features take

Everything on this page is an instance of one split, and naming it is worth more
than any individual item: **a feature is a mechanism plus a policy.** The
mechanism is parameterized and reusable and knows nothing about this game; the
policy is a struct of constants plus the wiring that composes it. The split has
already turned up independently three times, which is why it is written down as
a rule rather than proposed as a scheme:

| Feature   | Mechanism (general)                                         | Policy (this game)                                       | Item    |
| --------- | ----------------------------------------------------------- | -------------------------------------------------------- | ------- |
| HUD shake | flush a layer through a target, draw it back transformed    | decay 10/s, 9px, 19 and 24 Hz, 1.4°, triggered by damage | R3 / R4 |
| Camera    | the transform and its slots, **and `follow` as a function** | the _call_: `follow(data.playerPos, 550, 0, 0, w, h)`    | R5      |
| Collision | `ICollisionSystem`, overlap dispatch, `Hitbox` variant      | `shipHitbox()` — _ships_                                 | R7      |
| Movement  | input-independent integrator with a momentum option         | `playerMoveSpeed = 2000.f`, WASD                         | R8      |

### A mechanism is not always one home

The camera row above was wrong in an earlier version of this file, which filed
`follow` itself as policy. Only the **call** is policy. `Camera::follow` is
pure position maths — a chase with a dead zone (`min`), a leash (`max`) and
speed easing near the target — with no drawing and no knowledge of this game;
`w, h` only centre the target in the view. It is a parameterized behaviour, the
same shape as R8's integrator. It ended up as a method on `wgpu2d::Camera`
because gl2d put it there, not because a drawing library needs it. That is the
`shipHitbox` leak a second time.

So the rule needs one more turn of the screw: **a mechanism can span two
homes.** Both of the features this file spends the most words on are three
pieces, not two.

|            | `render/`                                                                                | `engine/`                                              | `gameLayer/`                                                                                             |
| ---------- | ---------------------------------------------------------------------------------------- | ------------------------------------------------------ | -------------------------------------------------------------------------------------------------------- |
| **HUD**    | flush a layer through a target, draw it back transformed                                 | layout maths — already `glui`, and already out of tree | this health bar, these textures, `xLeftPerc(0.65)`, `fill = data.health`, decay 10/s, the damage trigger |
| **Camera** | position, zoom, rotation, `viewProj`, the dynamic-offset slots, `pushCamera`/`popCamera` | `follow` — chase a point with speed / min / max        | `follow(data.playerPos, 550, 0, 0, w, h)`                                                                |

Two things follow from that, and both are cautions against inventing work:

- **There is no HUD system for `engine/` to own.** R3 and R4 cut it that way; the middle column is `glui`, which is a separate CMake target
  under `thirdparty/` and included only by `gameLayer.cpp`. Moving it into
  `src/engine/` would mean vendoring a third-party library into our own tree,
  which is worse than leaving it. It moves if and when something in-tree wants
  it, not to prove the folder exists.
- **Same feel, different home.** A world-camera shake (nudge the view's
  position) would be another `engine/` behaviour. The HUD shake stays a
  `render/` layer effect. One mutates a transform, the other composites a
  texture, and no amount of shared vocabulary makes them the same piece.

`collisionSystem.h` is the instructive one because it is already **half right**:
the interface and the overlap dispatch are exactly what a reusable system should
look like, and then `shipHitboxRadius()` and `shipHitbox()` sit at the bottom of
the same header. Nothing is wrong with that code — it is the boundary that is in
the wrong place. Copy its shape, not its file layout.

### Four homes, not three

`AGENTS.md` describes three homes because three folders exist. Movement,
inventory, combat, enemy AI and in-game UI fit none of them: they are not
drawing, so `render/` is wrong, and burying them in `gameLayer/` welds them to
this game, which is the thing the whole exercise is trying to avoid. That is why
`src/engine/` exists.

| Home             | Holds                                                                       | Travels?                      |
| ---------------- | --------------------------------------------------------------------------- | ----------------------------- |
| `src/render/`    | `wgpu2d` — the drawing library                                              | yes — a CMake target since R6 |
| `src/engine/`    | collision and camera behaviours today; movement, inventory, combat, AI next | maybe, later                  |
| `src/gameLayer/` | ships, enemy types, tuning numbers, HUD content, composition                | no — this _is_ the game       |
| `src/platform/`  | window, input backend, loop, ImGui wiring                                   | no                            |

The test for `engine/` is not "is this generic code." It is **"could a different
game use this without editing it?"** An integrator with a momentum option
passes. `playerMoveSpeed = 2000.f` does not. `follow` passes; the call to it
does not — see **A mechanism is not always one home** above, which is where
that distinction gets applied to the two features it matters most for.

`engine/` gets tenants when something needs to live there, never to prove it
exists. Collision is the first because it is already shaped correctly and only
needs moving; `follow` is the second for the same reason.

Two of the planned features do not sit in one row, and it is cheaper to know
that now:

- **In-game UI is two things.** Layout and widgets are engine — `glui` is
  already exactly this, reduced to `Frame` and `Box` when its gl2d half was
  dropped. _Which_ screens this game has is `gameLayer`. ImGui debug panels are
  neither: they are app.
- **Combat is mostly policy.** The general part is thin — apply damage, resolve
  a hit, handle death. Damage values, what a hit does, who may hit whom: game.
  Build the thin part and stop, or it becomes a config file with extra steps.

---

## The render track: done

R1–R6 have landed, plus R7 from the game track, which came with them because
the boundary work needed the fourth home to exist. Outline milestones 11, 12
and 13 are the write-ups.

What that bought, in one line each: validation errors now name the operation
that caused them; a pipeline is chosen per (target format, blend) instead of
there being exactly one; render targets composite correctly instead of
darkening translucent layers; `hudShake` is a general layer effect plus this
game's numbers in `gameLayer/hud`; `Camera` is a transform and `follow` is a
behaviour; and `wgpu2d` is a CMake target that cannot see the game.

**Two things it did not buy, both recorded rather than fixed:**

- **The sideways wall does not exist.** `engine` including `render/wgpu2d.h`
  compiles and links, because both sit under one `include/` root and the
  executable links both libraries. Only library-to-game is enforced. Fixing it
  means per-library include roots — headers under `src/render/include/render/…`
  and `src/engine/include/engine/…`, each target exposing only its own. A
  repo-shape change touching every header, deliberately not folded into R6.
- **There is nowhere to keep a test — half fixed.** Five harnesses were
  written and thrown away before N3 landed the capture path: the milestone-5
  matrix parity check, the blend readback in 12, the `follow` equivalence check
  in R5, `separation()` in R7, and the frame-capture probe rebuilt four times
  over the graphics features. Each caught or confirmed something worth having —
  the `follow` one caught a silent one-ulp change to the camera's easing.
  **Pixels now have a home**: `WGPU_SCREENSHOT_FRAME` plus `WGPU_OFFSCREEN`
  makes a scripted capture a committed tool. **Numbers still do not.** The
  arithmetic checks — matrix parity, blend sums, `follow`, `separation` — have
  no test target to live in, and each one is still written and deleted.

---

## The stall, reproduced and explained

**It was the fullscreen transition, and the phase timers named it on the first
occurrence after they were added.** The log brackets it exactly: a 197ms frame
with `poll=180ms`, then `WebGPU surface configured at 2560x1440`, and later a
280ms frame with `poll=263ms` followed by `configured at 500x500`. `poll` is
`glfwPollEvents`, which is AppKit performing the transition. Nothing in this
project is involved, and nothing in it could have been found by looking.

**The one-second frames are the Metal drawable timeout.** Both of them report
`acquire≈1001ms`, three to four frames after a surface reconfigure.
`getCurrentTexture` blocks until the presentation engine frees a drawable, and
Metal gives up after one second. That is the 1003–1012ms band seen across
fifteen earlier sessions: the same event, previously unattributable because the
recorder was pairing the wrong frame's counters.

**While fullscreen the game genuinely does not keep up.** 2560x1440 is about 15x
the pixels of the 500x500 window, and four full-screen background layers at
`zoom 0.5` multiply that by their overdraw. The alternating pairs return —
`32.3, 1.22, 31.5, 1.28, 32.6` — each summing to a refresh interval, which is
`Fifo` pairing and a consequence of a long frame rather than a cause.

**The user's own resolution matches:** leaving fullscreen fixed it.

**What is worth doing about it.** The transition stall is the window system's
and should be left alone. The fullscreen frame rate is a workload question, and
`WGPU_RENDER_SCALE` already exists for exactly it — milestone 10 built the
low-resolution path and nothing exposes it in the debug UI yet. That is the
remedy, not a renderer change.

**`other` was hiding up to 125ms, and two things were living in it.** The first
version of the breakdown left the ImGui *new-frame* block untimed — only
`ImGui::Render` was covered — and left the recorder's own write untimed too.
Both are named now. The report's cost is worth measuring rather than assuming:
half a kilobyte to a terminal plus a file flush, fired on consecutive slow
frames, is a diagnostic writing inside the frames it is measuring. Whether that
sustained the plateau of ~120ms frames in the log is now a question the log
itself answers. Against a pipe it reads 0.06ms; a terminal is the case that
matters and is the user's to observe.

**The remedy is exposed now.** `setRenderScale` and a slider in the debug panel,
because the fullscreen frame rate is a fill-rate problem and milestone 10
already built the answer. It had only ever been reachable through an environment
variable read once at startup. Changing it at runtime works because the frame
target is reused by slot and keyed by generation, which the glow bug forced.

**The phase breakdown is complete now:** `audio`, `begin`, `logic`, `ui`,
`endFrame`, `poll`, `report`, and `other` for whatever is left. `other` is the honest
part: it was added because the first version left the audio update, the ImGui
calls and `wgpuBeginFrame` outside every timer, and a 55ms frame with all phases
small could still hide 48ms nobody was measuring. Validated against a deliberate
stall in each phase; `other` reads 0.3ms when a 120ms stall is injected
elsewhere.

## The unreproduced stall

**The recorder was reporting two different frames at once, and every report
written before 2026-09-11 is unreliable for attribution.** `deltaTime` is
measured from the start of one iteration to the start of the next, so it is the
*previous* frame's duration. `frameStats()` returns whatever `wgpuEndFrame` last
published, and the recorder ran straight after `wgpuEndFrame` — so it paired
frame N's counters with frame N-1's duration. That is why a 1004 ms frame could
report `submit->done=0.34ms` and `blocked=0ms` and mean nothing by it. The
recorder now sits at the top of the iteration, where the two line up.

**`submit->done` is not an attribution, and the self-test proved it.** With a
120 ms stall injected into `gameLogic`, one report showed `submit->done=129ms`
and the next showed `0.29ms` for the identical stall. The work-done callback
lands a frame or two late by design, which the renderer already says in a
comment; what is new is knowing that it will happily claim a stall it had no
part in.

**Phases are recorded now:** `logic`, `endFrame` and `poll`. Between them they
cover the iteration, so a slow frame with all three small really is unaccounted
for — where before that conclusion was an assumption. `poll` matters most: it is
the window system's, and it was outside every measurement the recorder had.
Both phases were validated against a deliberate stall in each.

**What the log says independently of attribution.** Across 274 reports in 119
sessions, two shapes stand out and neither depends on the broken pairing:

- **Eighteen frames land between 1003 and 1012 ms**, across fifteen different
  sessions. That tightness is a timeout, not a stall. The only one-second bound
  in the per-frame path is the error-scope drain (`1000 x 1ms`), which only runs
  when something is *created* mid-frame — and which now reports its own cost
  correctly.
- **Alternating pairs that sum to a refresh interval:** `1.57, 31.87, 1.61,
  31.41, 1.61, 31.73`. Each pair is 33.3 ms. That is missed-vsync pairing under
  `Fifo`, the same shape as the 75-to-15 observation below, and it is a
  consequence of a long frame rather than a cause.

**Open.** A single observation: the frame rate fell from its usual 75 to about
15, with visible camera jitter, and did not recur across minutes of play after
a restart. No reproduction yet, so nothing has been changed to "fix" it.

Two things narrow it before any code moves.

**15 fps is exactly 75/5.** `PresentMode::Fifo` quantises to divisors of the
refresh rate, so a frame was taking between 53 and 67 ms — a large, specific
budget, not a small regression.

**The jitter is a symptom, not a second bug.** `camera::follow` can never keep
up while a direction is held: at 75 fps the player moves 26.7 px per frame and
the camera 7.3, so it rides the leash and snaps to exactly 150 behind. On a
turn, the distance dips under the leash for one frame and the *eased* branch
runs instead. Those two branches differ by 19 px at 75 fps and by 96 px at 15.
Same alternation, five times more visible — and gl2d had a disabled
anti-jitter block at exactly that spot, which R5 removed while noting the
jitter it targeted might still be there.

**Candidates, ranked by what the code can actually do:**

1. ~~**A failed pipeline build retried forever.**~~ **Fixed, and measured — and
   it was almost certainly not this.** The retry loop was real: with a variant
   forced to fail, the old code rebuilt it **783 times** in eleven seconds
   against **1** after caching the failure. But two predictions about it were
   wrong. The rate is ~1 rebuild per frame, not the 14 estimated, because the
   draws needing the missing variant are contiguous and form a single run. And
   the cost is near zero — the frame rate held at 75 throughout, with no slow
   frames in either run, because Metal caches pipeline compilation. Worth
   fixing on its own merits; not an explanation for a 15 fps collapse.
2. ~~**A spuriously failed build.**~~ **Not real.** On reading rather than
   remembering: `createQuadPipelineVariant`'s error scope brackets exactly one
   call, `createRenderPipeline`, and claims only `ErrorFilter::Validation`, so
   out-of-memory and internal errors never land in it. There is nothing here to
   narrow.
3. **A mid-frame stall** — a texture or render target allocated during a frame,
   or an error scope draining with 1 ms sleeps.
4. **Outside the process** — thermal throttling, GPU contention, another
   application.

**What was built instead of a fix: a slow-frame recorder** (`glfwMain`). A
frame over three times the running average *and* over 25 ms prints its counters
and the previous eight frames. Every counter is zero in a steady frame, so
whichever is not zero names the cause: `builds` large means 1 or 2, `errors`
means the device complained, `blocked` means it sat draining a callback,
`textures` means an allocation. **All zeros means 4**, which is worth as much,
because it eliminates the rest.

**Candidate 1 was fixed in a way that keeps the evidence**, which turned out to
be better than leaving it: the failure is now cached and reported once, loudly,
by name. A recurrence is a single named line in the log instead of a silent
rebuild loop — more observable than before, not less — and the frame-rate cliff
it might have caused is gone either way.

**That leaves 3 and 4, and 4 is now the front-runner** — thermal throttling,
GPU contention, or another application. The measurements above removed the two
in-process suspects that could be examined by reading. If the recorder ever
fires with every counter at zero, that is the answer.

Run with `2>&1 | tee` so the report survives the session.

---

## Now — the game track

R8–R11. R7 landed early, alongside the render track, because the boundary work
needed `src/engine/` to exist; it is outline 13. Everything below now has a
home to go to.

### R8. Movement as a feature with options

**Lands in:** engine (the integrator) + game (the tuning and the key bindings)

Today movement is roughly forty lines inline at `gameLayer.cpp:170-209`, and the
reason it cannot take a momentum option is not the physics — it is that three
separable things are fused:

1. **input → intent** — WASD / arrows into a direction vector
2. **intent → velocity** — the integrator. Momentum lives here: instant,
   acceleration + drag, thrust relative to facing
3. **velocity → position** — apply, clamp, resolve

The block calls `platform::isButtonHeld` directly, so 1 and 2 are the same code.
The concrete cost: **an enemy can never use this movement, because enemies do
not have keyboards** — and so `Enemy::update` (`enemy.cpp:70`) hand-rolls a
second integrator that shares nothing with the first. Two implementations of one
idea already.

So the rule that makes it a feature: **the integrator must not know where the
intent came from.** Once that holds, the player and the enemy are both
consumers, and momentum is a field in an options struct rather than a rewrite.

Fix while here: `gameSpeedMultiplier()` is applied _inline_ at the player's
movement site but _passed as an argument_ into `Enemy::update` and
`Bullet::update`. Two conventions for one concept, and the integrator is where
they should become one.

### R9. Entity data, behaviour and drawing are three things

**Lands in:** game, mostly

`Enemy` is simultaneously state, AI (`update`, with `turnSpeed`, `fireRange`,
`fireTimeReset`), a hitbox, and a draw call — `render(renderer, sprites, atlas)`,
which is why `enemy.h` includes `render/wgpu2d.h`. `Bullet` is the same shape,
smaller.

Enemy AI cannot become a feature of its own while the struct that holds it also
holds a renderer dependency. Separating update from render is the prerequisite,
not a tidy-up — and it is what lets a behaviour be swapped, tested, or reused
without dragging `wgpu2d` along.

### R10. Ownership: assets, state, and the globals block

**Lands in:** game

`gameLayer.cpp:39-60` is a file-scope block holding the renderer, four texture
handles, two atlases, a `Sound`, and five debug flags. This is what will fight
every extraction on this page: **a feature cannot be self-contained while its
state is a global in another translation unit.**

The real question inside it is _who owns assets_. Textures are loaded in
`initGame` and then passed by reference into `Enemy::render`. That is the same
unanswered question as "who owns the device" in R6, one layer up, and it wants
the same kind of answer rather than another set of globals.

Related and currently unanswered: `restartGame()` resets `data` and nothing
else — not the camera, not the shake, not the debug flags. As features acquire
state, _what restart means_ needs a real definition.

### R11. Each feature owns its debug UI

**Lands in:** game and engine

The ImGui block at `gameLayer.cpp:524-554` is the HUD problem forming a second
time: six controls inline, one per feature, growing. Every item on this page
will want debug controls.

A feature exposes its own debug entry point; the panel calls them and holds
nothing itself. Small, and much cheaper decided at six controls than at twenty.

### Smaller things, worth doing while nearby

- **Two clocks, currently implicit.** `gameSpeedMultiplier()` scales simulation
  time, while `hudShake` deliberately uses `steady_clock` so the debug slider
  cannot slow the shake — a distinction that exists only as a comment in
  `hudShake.cpp`. Name it before more features pick a clock by accident.
- **`Camera::rotation`** is accepted and ignored. Implement it or delete the
  field; carrying an ignored one is worse than either (noted in R5 too).

---

## Then — guide chapters worth doing

Guide chapters that have a genuine use in a 2D sprite renderer, ranked by what
they teach per unit of work rather than by guide order. Each block is the same
shape as a milestone — **concepts → chapters → where it would land** — with a
note on why it earns its place and what to establish before starting. All of
these land in the **library** unless the block says otherwise.

The state they all start from: one render pipeline, one sprite shader, a CPU
growable vertex batch split into texture/camera runs, dynamic-offset camera
uniforms, render targets, and a hand-written ImGui backend. No compute, no
instancing, no storage buffers, no depth, no multisampling, no query sets. The
device is created with `requiredFeatureCount = 0` and `requiredLimits = nullptr`
(`wgpuInit`), so anything needing a feature or a raised limit starts by changing
that call.

**Order, once R3–R6 are done:** N2 → F1 → N5 → N3 → N4 step one → F7 → N4 step two → F5. Instrumentation
before optimisation. F1 is now the cheapest thing on this page: R2 already
built `BlendMode::Additive` and the run splitting, so all that is left is the
game choosing it.

---

### N2. ~~Timestamp queries~~ — **not possible on this adapter**

**Checked before building, and the answer was no.** `printAdapter` now lists
features by name rather than counting them, and this AMD Radeon Pro 560X on
Metal via wgpu-native v24 reports 22 features without the one this item needs:

```
DepthClipControl, Depth32FloatStencil8, TextureCompressionBC,
IndirectFirstInstance, ShaderF16, RG11B10UfloatRenderable,
BGRA8UnormStorage, Float32Filterable, DualSourceBlending
+ 13 wgpu-native extensions (push constants, subgroups, binding arrays, ...)
```

`TimestampQuery` (0x3) is absent, and so are wgpu-native's own
`TimestampQueryInsideEncoders` (0x30024) and `TimestampQueryInsidePasses`
(0x30025). A feature that is not advertised cannot be requested at device
creation, so `QuerySet`, `timestampWrites` and `resolveQuerySet` are all
unreachable. This is a refusal, not a difficulty.

**The consequence, and it matters for later items:** N4 and N5 both make claims
about cost, and those claims cannot be settled by GPU timing on this machine.
Anything that says "measured A/B" about GPU work has to mean one of the three
below, or it is guessing.

#### What was built instead — all three, and they work

**N2a. Frame stats in the debug panel.** *(library + app)* **Landed.** The data
mostly existed and none of it was surfaced. `glfwMain` already computes `deltaTime` and
passes it to `gameLogic` without ever showing it; the renderer counts quads,
cameras and draw runs but prints them **once**, to stdout, on the first flush.
Making those live — CPU frame time, quads, runs, pipeline variants — would
catch regressions in the batch, the uploads and the run splitting, which is
where this renderer's cost actually sits. It measures the CPU side only, and
should say so.

**N2b. `wgpuQueueOnSubmittedWorkDone`.** *(library)* **Landed**, and the first
reading justified the label: `submit->done` came back at 8.56 ms against a CPU
frame of 8.30 ms, so it is tracking frame pacing rather than GPU work. The
panel says `submit->done` and not "gpu" for that reason -- a number called
"gpu" that is not one is worse than no number. Timing submit to callback gives a rough GPU-completion figure. It
includes queue waits and vsync, so it is not clean GPU work — but it is real
signal and it is the closest the API offers here.

**N2c. A Metal frame-capture trigger.** *(app, macOS)* **Landed, and the risky
part paid off.** wgpu-native exposes no `MTLDevice` -- there is no HAL escape
hatch in `wgpu.h` -- so the capture targets `MTLCreateSystemDefaultDevice()`
and bets that Metal device objects are per-GPU singletons and wgpu took the
default. Verified rather than assumed: the trace came back at 16 MB, and
`MTLBuffer-1098-0` is exactly 32768 bytes, which is what the renderer's own log
reports for the ImGui vertex buffer capacity. Those are our resources. A
capture of the wrong device would be empty.

One limit that could not be checked from here: the command stream lives in a
compressed `store0`, so whether R1's labels and debug groups actually render as
a readable tree can only be confirmed by opening the trace in Xcode.

The item whose groundwork was already paid for: R1 added 28 object labels and the debug groups a capture needs to be
readable, so a trace would already show "batch -> surface", "composite scaled
target", "imgui" rather than anonymous draws. What is missing is a way to
*start* one — today capturing means launching the CMake binary under Xcode.
`MTLCaptureManager` can start a capture programmatically into a `.gputrace`,
and `src/platform/wgpuMetalLayer.mm` is already an Objective-C++ TU with Cocoa
and QuartzCore linked, so the hook has a home and a key binding is the rest.

**All three are in.** `MTL_CAPTURE_ENABLED=1` plus `F11` or
`WGPU_GPUTRACE_FRAME=N` records one frame; the panel carries the CPU and
submit-to-done figures and the batch counters.

**And a fourth thing came out of using them.** A frame-rate collapse from 75 to
15 fps was reported once and has not reproduced. Rather than guess, the frame
counters were extended into a **slow-frame recorder**: when a frame exceeds
three times the running average and 25 ms, it prints that frame's counters and
the previous eight frames. Every counter is zero in a steady frame, so whichever
is not zero names the cause — and all zeros means the cost was outside this
process, which eliminates the rest. See **The unreproduced stall** below.

---

### N3. A headless context — *(capture half landed)*

**Landed: screen capture.** `wgpuRequestFrameCapture` / `wgpuTakeFrameCapture`
on `wgpuContext.h`, `CopySrc` permanent on the surface and on render targets,
F12 for a human and `WGPU_SCREENSHOT_FRAME=N` for everything else. The library
hands back tightly packed RGBA and nothing else — no path, no encoder — with
the 256-byte row padding undone and the BGRA swizzle applied, because both are
the copy's rules and not the caller's problem.

That retires the debt this item was really about: the capability had been
rebuilt as throwaway scaffolding five times, and the fifth time it was lost
mid-task to a cleared scratchpad and had to be rewritten before the work could
continue.

**Substituted, deliberately: `WGPU_OFFSCREEN=1` hides the window** rather than
removing it. With the frame trigger that is a scriptable capture of a real
frame, ImGui included, with nothing appearing on screen — which is what the
verification norm in `AGENTS.md` actually needs.

**What remains is a genuinely surfaceless context, and it is not mostly
renderer work.** The renderer half is tractable: eight uses of `g.surface`,
each with an obvious offscreen branch — no `getCurrentTexture`, no `present`, a
texture as the only attachment. The application half is the real cost.
`glfwMain` is built around a window, ImGui's GLFW backend requires one, and the
game reads input through `platform::`, which is wired to GLFW callbacks. Doing
this means stubbing input and bypassing ImGui: app restructuring on top of a
renderer change.

**So the honest question before starting it is what it buys that a hidden
window does not**, and the answer is one thing: running on a machine with no
window system — CI, a container, a remote box. This project has no such
machine today. Until it does, this is a solution without a problem, and the
comment in `glfwMain.cpp` records the gap so nobody assumes it was finished.

**LearnWebGPU:** [Screen capture](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/screen-capture.html) (WIP) · [Headless context](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/headless.html)

---

### N4. Compute pipeline: GPU mipmaps, then a convolution filter

**Concepts:** compute pipeline and compute pass as a sibling of the render pass
on the same encoder, workgroup sizing, `dispatchWorkgroups`, storage textures
and `textureStore`, per-mip-level texture views, the `StorageBinding` usage
flag. Step two adds the one thing compute has that neither other stage does:
`var<workgroup>` memory and `workgroupBarrier`.

**LearnWebGPU:** [Compute Pipeline](https://eliemichel.github.io/LearnWebGPU/basic-compute/compute-pipeline.html) · [Mipmap Generation](https://eliemichel.github.io/LearnWebGPU/basic-compute/image-processing/mipmap-generation.html) · [Convolution Filters](https://eliemichel.github.io/LearnWebGPU/basic-compute/image-processing/convolution-filters.html)

**Step one — landed.** `downsampleRGBA8` is gone; one compute pass, one
dispatch per level, each reading the level above. Verified byte for byte
against the CPU code it replaced by converting each texel back to the integer
it is before averaging, so the GPU performs the same round-half-up: 3,789,232
bytes across every mipped texture, zero differ.

**And it bought no frame rate, which is worth writing down.** Measured at 75Hz:
between 9.5 and 10.2ms of each 13.3ms frame is spent blocked waiting for a
drawable, at 39 quads and 7 draw runs. This renderer is nowhere near GPU bound.
What step one saved is load-time CPU work and upload volume, once, and it is
small. N4 earns its place as the prerequisite for F5, where compute stops being
a cheaper way to do something and becomes the only way to do it at all.

**Step two — landed as the phosphor glow, with one half of it deliberately
skipped.** Bright pass at half resolution, then a separable gaussian across and
down, then the result added back onto the frame's target — before the CRT, so a
curved picture curves its own bloom instead of wearing a flat one.

**Shared workgroup memory is the thing it was for, and the arithmetic is
exact.** At radius 8 every output reads 17 inputs and its neighbour reads 16 of
the same 17. A fragment shader has no way to say so; its invocations are
independent by construction, so it pays 17 every time. The compute version
loads its 64-pixel span plus a margin into `var<workgroup>` once, waits at a
`workgroupBarrier`, and reads neighbours out of that:

| per 64 outputs | texture loads |
| --- | --- |
| independent invocations | 1088 |
| one cooperative tile | 80 |

**Three things the API made explicit.** A separable blur needs two scratch
textures, because a pass cannot read and write the texels it is walking over —
which the mip generator never had to face, since every dispatch there wrote a
level nothing in that dispatch read. Core WebGPU's storage formats do not
include BGRA8Unorm, which is the surface's format here, so the scratch pair is
RGBA8Unorm. And **a view carries the usages it may be bound for**: a texture
that is both sampled as a sprite and written by a dispatch needs one view of
each, not one view claiming both. wgpu refuses the latter, and the message
names the view rather than the binding.

**Measured cost, filter on, glow off then on:** 40 to 42 quads, 8 to 10 draw
runs, 3 to 4 flushes, plus one compute pass of three dispatches. The frame rate
does not move, and cannot: there is ~10ms of idle in every frame.

**Toggling the filter left a ghost of the screen stuck on the screen, and it
was two bugs feeding each other.**

`ensureScaledTarget` checked whether the frame's target already existed by
looking at `scaledTargetId`, which is 0 whenever the target is not in use. So
every switch-off and switch-on built a fresh screen-sized texture that was
already there and still the right size, orphaning the last one — a leak per
toggle, and a new id each time. Meanwhile the glow's bind groups were cached
against the scratch size and the frame size, neither of which changed, so the
bright pass went on reading a target nothing wrote to any more. What it read was
whatever had been on screen at the moment the filter was last switched off,
which is why the residue was bright, screen-space and perfectly still.

Fixed on both sides. The target is checked against its registry *slot*, which
survives the id going to 0, so a toggle reuses the texture instead of building
one: 3 allocations across 35 toggles, against 37 before. And the glow caches
against a **generation counter** bumped whenever that slot's texture is
replaced, not against the id — because reusing a slot keeps the id and swaps the
view underneath it, which is the same bug wearing the fix's clothes.

**The general shape, and it is the second time this session:** a cache key that
names a thing by size or by identity, when what it actually depends on is the
*object*. The composite's partial batch swap was the same mistake in a different
costume. A generation counter is the cheap answer when the object can be
replaced in place.

**Not done, and it is the half that was the point.** The roadmap said this was a
comparison — compute against a *separable* fragment blur, settled with N2's
timing rather than asserted. Only the compute side is built. On a renderer with
10ms of slack per frame the comparison would not resolve anyway, so it wants a
deliberate stress case (a much larger radius, or many iterations) rather than
the game as it stands.

**Superseded: the original step two.** It was going to run over
milestone 10's low-resolution stand-in. That was chosen because the target
already existed, but it only exists when render scale is below one, which is a
debug setting — so the blur would have been invisible in normal play. It now
has a real consumer: **the phosphor glow of F7's CRT pass.** Sequence it after
F7, not before, and blur the bright parts of the composited frame.

**Why step two is a second half rather than a repeat.** A mip reduction reads
four texels per output and neighbouring invocations share nothing. A blur of
radius r reads 2r+1 per output and the invocation next door reads almost all of
the same ones. That redundancy is what workgroup shared memory exists for: one
workgroup loads its tile plus a margin once, waits at a barrier, and every
invocation reads neighbours out of that instead of out of the texture.

**And it is a comparison, not an unlock.** Two fullscreen fragment passes would
blur perfectly well — F2 and F6 already make that a shader and a draw. Compute
should win by cutting redundant reads and skipping the rasteriser, and N2's
timing can settle whether it does on this adapter instead of leaving it a
claim. With ~10ms idle per frame the difference will not be visible either way;
the point is the measurement. **Separability first:** an NxN gaussian is a 1xN
pass then an Nx1 pass, 81 reads becoming 18 at radius 4, and that saving is
available to the fragment version too — so measure compute against a *separable*
fragment blur, not against a naive one.

**Why it earns its place:** the largest single hole in the port — milestone 7
took the CPU half of that chapter and left the compute half.

---

### N5. Instanced drawing

**Concepts:** `VertexStepMode::Instance`, a second vertex buffer holding
per-instance data (rect, uv rect, colour, rotation), `draw` with an instance
count, and moving the corner and rotation maths from `pushQuad` into the vertex
shader.

**LearnWebGPU:** [Instanced Drawing](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/instanced-drawing.html) — **an empty TODO page.** The technique, not the chapter: the only guide material is the `stepMode` field noted in [A first Vertex Attribute](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/input-geometry/a-first-vertex-attribute.html), so read `WGPUVertexStepMode` and `wgpuRenderPassEncoderDraw`'s `instanceCount` in the vendored `webgpu.h` and work from the spec

**Would land:** `pushQuad`, `flushBatch`, `createQuadPipeline`'s vertex layout,
`quad.wgsl`.

**Why it earns its place:** `createQuadPipeline` already carries the comment
"advance once per vertex, not per instance" — this is the question that comment
raises. Roughly a fifth of the upload volume, and with N2 in place it becomes a
measured A/B against milestone 6a rather than a rewrite on faith. Per-quad
colour arrays (`Color4f colors[4]`) are the one part of the gl2d signature that
does not fit an instance record; the fallback is to keep both paths and pick per
run.

---

### N6. Multisampling

**Concepts:** `multisample.count` on the pipeline, a multisampled colour
attachment with `resolveTarget` set, and the constraint that a multisampled
texture cannot be sampled in a shader — it has to resolve first.

**LearnWebGPU:** [Multi-Sampling](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/multi-sampling.html) — **an empty TODO page.** Work from `WGPUMultisampleState` and `WGPURenderPassColorAttachment::resolveTarget` in the vendored header

**Would land:** `createQuadPipeline`, `ensurePassBegun`, `createRenderTarget`.

**Why it earns its place:** the visible aliasing in this game is the debug
hitbox outlines and `renderLine` quads. It also collides productively with
milestone 10 — a render target that is both multisampled and later sampled needs
two textures, which is the clearest possible demonstration of what a resolve is.

---

### N7. A depth buffer, used as a sort key

**Concepts:** depth attachment, `depthCompare`, `depthWriteEnabled`, the depth
format, and the reason a 2D renderer would want any of it: a per-sprite layer
value lets draws be reordered by _texture_ without changing what the player
sees, which attacks the run splitting in `flushBatch` directly.

**LearnWebGPU:** [Depth buffer](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/3d-meshes/depth-buffer.html)

**Would land:** `createQuadPipeline`, `ensurePassBegun`, a depth field on the
batch vertex.

**The honest limit:** it only works for opaque sprites. Blended draws still have
to go back to front, so this splits the batch into an opaque pass (texture
sorted, depth tested) and a blended pass (draw order, depth test on, depth write
off). That split is the actual lesson.

---

### N8. 2D lighting with normal maps, via multiple render targets

**Concepts:** more than one colour attachment on a pass (albedo + normal), a
fragment shader returning a struct of `@location`s, and a second full-screen
pass that consumes both. Point lights for thrusters and bullets.

**LearnWebGPU:** [Normal mapping](https://eliemichel.github.io/LearnWebGPU/basic-3d-rendering/lighting-and-material/normal-mapping.html) is written and real · [Deferred Shading](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/deferred-shading.html) is an empty TODO page, so the multi-attachment half comes from `FragmentState::targets` in the header, not from the guide

**Would land:** a second pipeline and shader pair; `createRenderTarget` grows a
multi-attachment variant.

**Why it earns its place:** the multi-attachment pass is genuinely new
machinery, and this is the one place in a 2D game where it is the natural
answer rather than an imported 3D habit. Needs normal maps for the sprite atlas,
which is art work, not code — the reason it sits this far down.

---

### N9. HDR and tonemapping

**Concepts:** a float colour target (`RGBA16Float`), values above 1, a tonemap
on composite, and what the surface can actually present.

**LearnWebGPU:** [High Dynamic Range Textures](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/hdr-textures.html) (WIP)

**Would land:** `createRenderTarget`, `compositeScaledTarget`, and the format
choice in `configureSurface`.

**Head start:** the surface-format survey above already records two 10/16-bit
formats offered on this machine. Also note the pipeline/format rule from
milestone 10 — every pipeline drawing into a float target must be built for that
format, so this is not a one-line change.

---

### N10. RAII

**Concepts:** the guide's wrapper-plus-RAII pattern, replacing paired
`create` / `release` calls.

**LearnWebGPU:** [RAII](https://eliemichel.github.io/LearnWebGPU/advanced-techniques/raii.html)

**Would land:** `wgpuContext.cpp` throughout; `Context`'s fields.

**Why it earns its place:** several early-return paths in the file leak.
`createCameraBindGroup` releases correctly on failure and shows how much care
that takes by hand.

---

### Still out for good

Cube maps, IBL, PBR, tesselation, raytracing, procedural geometry and neural
networks have no 2D analogue worth the detour. Building for the Web is worse
than it looks: `wgpuMetalLayer.mm` and the wgpu-native pin both stand in the
way. Render bundles stay marginal here because the batch is rebuilt from scratch
every frame — the tiled background is the only static geometry, and it is nine
quads — and the guide's page for them is an empty TODO anyway.

A general warning about this whole section: most of Advanced Techniques is
unwritten. Instanced Drawing, Multi-Sampling, Deferred Shading, Render Bundles,
Shadow maps, Tesselation and Raytracing are all empty TODO pages today; Screen
capture, HDR and Debugging are partial. Only RAII and Benchmarking/Time are
finished. Basic Compute is in much better shape: Compute Pipeline, Mipmap
Generation and Convolution Filters are all written, which is another reason N4
is the best-supported large item here.

---

## Features that deepen what is already built

Game-facing rather than chapter-facing. Each one re-enters a finished milestone
from a new angle, and none of them needs a chapter that is not already read.
These are where the library/game line gets tested in practice, so each says
which side it falls on.

**F1. ~~Additive bullets~~ — landed, and not the way this was written.** _(game)_
Making the bullet *sprite* additive was built, looked at, and reverted: it
replaced the art rather than lighting it, and `Bullet::render`'s five
overlapping trail quads summed to 4x and clipped the sprite into a featureless
white blob. What shipped instead is a generated capsule glow drawn *underneath*
the sprite, which stays in alpha and stays crisp — plasma for the player, ice
for the enemy, which land almost exactly on the existing atlas art.

The lesson generalised into outline 14: additive is nearly invisible on this
game's existing art, because every texture in it is binary alpha — 0% partially
transparent pixels — so re-blending only changes which equation a fully opaque
pixel goes through. Additive earns its place on soft-edged, overlapping content
over a dark ground, which had to be *generated* because none existed.

**F2. A post-process chain on the world target.** _(library mechanism, game policy — the split R3 made)_ Milestone 10 built the
machinery and then used it twice (render scale, HUD shake). A damage vignette, a
chromatic-aberration hit flash, a warp on death — each is a different _shader_
over the same vertex format and the same attachment. It separates pipeline from
shader from target in a way a single-shader renderer cannot.

**The wanted feature that lands here: a refraction cloak.** Not the fade-out
kind — that is one float of vertex alpha on the existing pipeline and needs
nothing from this item — but the shimmer, where the background _warps_ behind
the ship rather than showing through it. That has to sample the scene behind
the ship, which means the world goes to a render target and a shader offsets
its UVs. A blend mode cannot do it: blending combines a fragment with the
destination, it cannot _read_ the destination and move it, and core WebGPU has
no framebuffer fetch (outline 14).

So this item's real prerequisite is naming it: **the pipeline key has to grow a
shader.** Today it is `(format, blend)` because there is one shader. The first
effect with its own WGSL is what extends it, and a refraction cloak is that
effect. Small change — the key is a two-field struct with a linear scan behind
it — but it should be a deliberate one rather than something discovered while
writing a shader.

**F3. Parallax background layers through the camera stack.** _(library: the camera behaviour from R5; game: which layers and at what depth)_ Milestone 6b built
dynamic uniform offsets and `pushCamera` / `popCamera`; the game uses one world
camera and one HUD camera. Three layers at 0.3x / 0.6x / 1.0x needs no new API
at all, but exercises the slot stride, the run splitting and `getViewRect` under
real pressure — and it is the cheapest thing here that makes the game look
better. It is also the first real consumer of R5, and the honest test of
whether that camera design is general or just rearranged.

**F4. Present mode from the debug window.** _(library setting, app control)_ `configureSurface` picks one.
Switching Fifo / Immediate / Mailbox at runtime and watching what it does to
N2's numbers is a few lines, and it is the clearest way to learn what the
surface actually is.

**Landed outside this list, on request:** the engine plume
(`gameLayer/shipThruster`), the shield bubble (`gameLayer/shipShield`) and the
bullet glow (`gameLayer/bulletGlow`). None were roadmap items; all three are
the same pattern, which is worth naming because a fourth would be too. Each
generates its own gradient texture at init rather than loading art — a radial
disc, a fresnel sphere plus a ring, a capsule distance field — and each puts
the shape in the texture's alpha and the intensity in the vertex colour.

**Three modules now hand-roll a gradient generator.** A `wgpu2d` helper for
generated textures would serve all three, and is the obvious refactor when a
fourth arrives. Not done: three is where a pattern becomes visible, four is
where extracting it stops being speculative.

**F6. Effect shaders, with parameters — landed.** _(library mechanism, game
policy)_ The renderer could draw a textured quad tinted by one colour. That was
the whole vocabulary: `quad.wgsl` is `in.color * textureSample(...)`, and the
only per-quad channel was four floats of vertex colour. Everything the shield
could become ran into that wall at once, so this was one item rather than
several.

**What landed.** `createEffect` compiles application WGSL into a fragment stage
that shares the sprite vertex stage; `setEffect` / `clearEffect` attach one plus
eight floats of parameters to the quads that follow. The shield impact ripple is
the consumer, and the cloak (F2) was retrofitted onto the same channel.

**One row of the table below was wrong, and finding out was the point.**
"several overlapping ripples" was predicted to need "an array — a uniform or
storage buffer". It needs neither. With a per-quad parameter channel, several
ripples are simply several quads, each carrying its own impact point and age:
no array, no upper bound baked into a shader, and overlapping waves brighten
where they cross for free because they are already additive. Measured cost is
one quad and one draw run per live ripple — distinct parameters mean a distinct
slot, which means a distinct run, which is the run key doing what it was
extended to do.

**Two things the ripple taught that the shader did not.** The impact has to be
stored *relative to the ship*, because the shield moves with it and a wave
anchored to the world slides off the bubble. And it has to be stored as a
*direction*, not as the collision point: the collision is against the hull's
circle, well inside the bubble, so the reported point starts the wave in the
shield's interior and it has to travel out before reaching where the player
watched the bullet strike. A shell is a surface; the direction is the whole of
what distinguishes one hit from another.

**A shared effect can be drowned by an unshared one.** The hit flare and the
ripple are the same event drawn twice, and the flare's decay was tuned when it
was the only signal a hit had happened. Left alone it saturated both rim rings
for the first quarter-second, so the wave appeared to start late. Two effects
that describe one event have to be tuned together.

**What the shield wants, and what each thing actually needs:**

| want                                     | needs                                                         |
| ---------------------------------------- | ------------------------------------------------------------- |
| look _spherical_ rather than like a ring | **nothing new** — see below                                   |
| a specular highlight that tracks a light | nothing new: rotate the quad, the highlight rotates with it   |
| ripple outward from an impact point      | a shader + per-draw parameters (point, elapsed)               |
| several overlapping ripples              | ~~an array — a uniform or storage buffer~~ — nothing more: several quads |
| a dissolve / shatter on depletion        | a shader + one threshold parameter + a noise texture          |
| fragments flying apart                   | many quads with per-quad state — N5, or F5's machinery        |
| refraction, heat haze                    | F2: it has to _read_ the scene, so it needs the render target |

**The rounded look needs no renderer change at all, and is worth doing first.**
Fresnel — the reason a bubble looks like a sphere — is, for a screen-facing
sphere, a pure function of distance from the centre: the normal tilts as
`Nz = sqrt(1 - r²)` and the rim term is `(1 - Nz)^p`. A radial texture already
stores exactly "a function of distance from the centre", so a spherical-looking
bubble is a change to the texture generator in `shipShield.cpp`, not to the
pipeline. The same is true of a baked specular highlight, and rotating the quad
rotates the highlight for free.

**What actually needs building is two capabilities:**

1. **A shader per effect.** The pipeline key is `(format, blend)` because there
   is one shader. It grows a shader entry, and effects bring their own WGSL.
   F2 needs this too — it is the shared prerequisite, and whichever of the two
   is done first should build it.
2. **A per-draw parameter channel.** An impact point and an elapsed time are
   not a colour, and smuggling them through the vertex colour would cost the
   colour. Two honest options, and they are not new machinery: per-instance
   attributes (N5 — this is what instance data _is_), or a dynamic-offset
   uniform slot, which is exactly what milestone 6b already built for the
   camera. Either way a time uniform falls out for free.

**F7. A CRT pass over the finished frame — landed, glow excepted.** _(library
mechanism, game policy)_ One retro filter over the game and the HUD, with
sliders, for a unified look. `setFinalEffect` is the mechanism;
`resources/shaders/crt.wgsl` and `gameLayer/crt.cpp` are the policy. Measured
cost while on: one extra quad, two extra draw runs, one extra flush. Off: zero.

**Two bugs found and fixed.**

1. **`drawFullscreenEffect` flushed to a literal target 0.** A full-screen
   effect is an ordinary screen-space draw and has to land wherever screen-space
   draws are going — the frame's target, when the frame is being routed through
   one. Flushing to 0 put the cloak straight onto the surface while everything
   after it went into the target, and the end-of-frame composite then covered
   the cloak with a target that had never received the world: **cloak plus CRT
   turned the whole screen black.** `compositeScaledTarget` flushes to a literal
   0 and is right to — putting the target *onto* the surface is the one draw
   that must not be redirected back into it.

2. **Scanlines were a count of lines, and that is why the hull looked
   translucent.** 240 lines across a 500-pixel window is a period of 2.083
   pixels, so the dark band drifts a twelfth of a pixel per row and beats
   against the sprite's own pixel grid. The result is not scanlines, it is a
   moving screen door over the artwork — and a screen door over a sprite on a
   dark background reads as the sprite being see-through. A period measured in
   whole output pixels cannot drift. Measured on the hull, filter off = 213:

   | settings | hull mean |
   | --- | --- |
   | 240-line count, strength 0.35 | 162 |
   | whole-pixel period, lighter hand | 180 |

   **The general lesson, and it is not only about scanlines:** any screen-space
   pattern laid over pixel art has to be locked to whole output pixels. Express
   it as a period, never as a count across the picture. The aperture mask was
   already written this way and never had the problem.

3. **The composite borrowed the batch without setting all of it aside, and
   that was three bugs wearing one coat.** Two callers lend the pending batch
   to a quad of their own: this composite and `drawFullscreenEffect`. The batch
   is eight parallel arrays and they swapped four and five of them. What is
   left behind does not vanish — it sits at index 0, and the borrowed quad then
   reads somebody else's blend mode, shader and effect parameters.

   That one defect produced **the ship going translucent under thrust** (the
   plume leaves an Additive quad at the head of the batch, the composite
   inherited it, and the frame was added to the surface instead of replacing
   it) and **the black quadrant under curvature** (the CRT read another quad's
   parameter slot, so the curvature it applied was whatever happened to be
   there). The fix is a guard that swaps the whole batch, not a longer list of
   swaps, so the next array added to the batch cannot bring this back.

   **The lesson is about diagnosis, not about batching.** Hours went into that
   shader on the strength of a contradiction: every factor measured correct on
   its own and the product came out black. That contradiction was the evidence.
   Parallel arrays mutated in two places is a shape worth distrusting, and the
   file that keeps testing clean is not the file to keep testing.

4. **Stacked darkening is a dimmer, not a filter.** Scanlines and the aperture
   mask each remove light *on average*, so turning the master up made the whole
   picture fall away. A real tube does not dim as its mask gets finer: the
   bright parts get brighter and the average holds. Each term now records the
   mean it took and one capped multiply gives it back, and both means are known
   exactly rather than estimated — `line` averages 0.5 over a period, and each
   channel is full on one column in three. Picture mean with the filter off is
   28.1:

   | master | picture mean |
   | --- | --- |
   | 1.0 | 27.3 |
   | 2.0 | 26.5 |

   What is left is the vignette, which is deliberate.

**The trap that remains: pixel art under curvature.** Bending the coordinate
asks for positions between texels, and with nearest filtering that gives rows
and columns of unequal thickness that crawl as the camera moves. The frame's
target is therefore sampled with *linear* filtering whenever a final effect is
set, unlike the low-res upscale's nearest. The softness that introduces is most
of the blending the glow would add, it costs nothing, and a real phosphor mask
is soft anyway — which is part of why the glow can wait.

**Sliders:** a master strength plus curvature, scanline strength, scanline
period in pixels, aperture mask, fringing and vignette. They need separating
because they go wrong at different rates: curvature reads as broken well before
the scanlines do. Defaults are an 8-pixel period at a light strength,
and the slider runs to 32 — a narrow period at strength reads as texture laid on
the artwork, where a wide one puts the dark band far enough apart that the eye
reads it as a line. The brightness compensation does not care which: the wave
averages 0.5 over any period.

**F5. Capstone: a GPU-driven starfield.** _(library: the compute-driven instanced particle system; game: that they are stars)_ Star positions in a storage buffer,
advanced each frame by a compute shader, drawn as instanced quads that read that
buffer. Combines N4, N5 and storage buffers — none of which the port has — into
one thing the game visibly gains, replacing a tiled texture with a particle
count the CPU batch could not carry. Also the natural first place to exceed a
default limit and have to fill in `requiredLimits`.
