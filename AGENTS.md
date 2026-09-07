#pragma once

# Working agreements for coding agents

Read this before touching the repo. Claude Code and other agents that read
`AGENTS.md` pick it up automatically.

## Git: never commit unless asked

**Leave finished work uncommitted in the working tree.** The author reviews the
diff before it becomes history; a commit made on your own initiative takes that
away, even when the work itself is correct and even when previous, similar work
in the same session was committed on request.

- Do not `git commit` unless the author says so in that message. "Proceed",
  "go ahead", "do milestone N" are instructions to write code, not to commit.
- Permission to commit is per-commit. Being asked to commit one change does not
  authorize committing the next one.
- Never `git push`, force-push, rebase, amend, or reset without being asked.
- Never commit on `master`. Work happens on a branch (currently `webgpu-port`).
- When work is done, say what changed and where, and let the author look. If a
  commit seems due, offer one — with the message you would use — and wait.
- If you have already committed something unasked, say so plainly and offer
  `git reset --soft <last-approved-commit>` so the change goes back to being a
  reviewable diff.

Related: don't quietly delete or revert the author's uncommitted edits, and read
a file before patching it — the author edits committed files between sessions.

## What the project is

A 2D space shooter (GLFW + WebGPU). The render layer was ported from OpenGL +
gl2d to WebGPU (wgpu-native on Metal) as a deliberate learning exercise. The
concepts, and where each one landed, are in `docs/webgpu-learning-outline.md`.
Read the outline first — it is the map.

## Rules the author set for the port

- **One milestone per session.** Stop and wait after each one.
- **Explain the concepts and the API shape before writing code**, and leave room
  for questions. The author is learning graphics through this port; a correct
  patch with no explanation is a failed answer.
- **Render layer only.** Do not touch gameplay, input, or simulation code
  (`src/gameLayer/`, `platformInput`). If a feature seems to need a gameplay
  change, say so instead of making it.
- **Never write wgpu API calls from memory.** The C API churns between releases.
  Read the vendored headers:
  - `build/_deps/wgpu-macos-x86_64-release-src/include/webgpu/webgpu.h`
  - `.../include/webgpu/wgpu.h` (wgpu-native extensions)
  - `build/_deps/webgpu-distribution-src/wgpu-native/include/webgpu/webgpu.hpp`
- **When something is broken, list candidate causes before proposing a fix.**

## Build and run

One configuration. WebGPU is the only renderer.

```bash
cmake -S . -B build
cmake --build build -j8
```

```bash
./build/spaceGame
WGPU_RENDER_SCALE=0.25 ./build/spaceGame   # low-res target, upscaled
```

The game runs until its window is closed, so cap it when running it yourself —
macOS has no `timeout`:

```bash
perl -e 'alarm 8; exec @ARGV' ./build/spaceGame
```

Redirected stdout is fully buffered; the renderer's reports flush explicitly, so
console evidence survives being killed.

## Verifying render work

`screencapture` from an agent session returns only the desktop wallpaper (the
terminal lacks Screen Recording permission), so **do not claim to have looked at
the window.** Two honest options:

- Console evidence: surface configuration, texture and pipeline creation, first
  flush, "first frame presented", absence of validation errors.
- A GPU readback: draw a fixed, time-independent scene and copy the frame back
  to the CPU (`copyTextureToBuffer` + `mapAsync`), then compare numerically.
  This is how the milestone 7-parity check was done and it is stronger than a
  screenshot; write PNGs and send them to the author to look at.

Temporary verification scaffolding is fine — say that it is temporary, keep it
out of the commit, and remove it when done.

## Layering

| File | Role |
|---|---|
| `include/render/wgpu2d.h` | gl2d's public shape, WebGPU behind it; the include game files use |
| `include/render/wgpuContext.h` | what the platform layer sees: init / begin / end / shutdown, no WebGPU types |
| `include/render/wgpuFrame.h` | what other render TUs see: device, queue, pass, texture bind groups |
| `src/render/wgpuContext.cpp` | instance through batch flush |
| `src/render/wgpuImgui.cpp` | the hand-written ImGui renderer backend |
| `src/platform/glfwMain.cpp` | window, frame bracket, ImGui wiring |

`wgpu2d` mirrors gl2d's **signatures** name for name. It does not mirror gl2d's
implementation: vertices stay in world pixels and the camera is a matrix on the
GPU.
