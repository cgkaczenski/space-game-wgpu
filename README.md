# spaceGame

A 2D space shooter used to learn WebGPU: GLFW for the window, wgpu-native
(Metal on macOS) for drawing.

The game-facing renderer is `wgpu2d` (`include/render/wgpu2d.h`). Gameplay code
does not mention WebGPU types; it talks in textures, atlases, and rectangles.

Concepts, LearnWebGPU chapters, and where each one landed:
[docs/webgpu-learning-outline.md](docs/webgpu-learning-outline.md).

## Requirements

- CMake 3.16 or newer
- A C++17 compiler
- On macOS, Metal via wgpu-native. First configure fetches
  [WebGPU-distribution](https://github.com/eliemichel/WebGPU-distribution) and
  [glfw3webgpu](https://github.com/eliemichel/glfw3webgpu).

## Build

```bash
cmake -S . -B build
cmake --build build -j8
```

## Run

```bash
./build/spaceGame

# Draw the world into a smaller target and upscale with nearest filtering
WGPU_RENDER_SCALE=0.25 ./build/spaceGame
```

`PRODUCTION_BUILD=ON` makes `RESOURCES_PATH` relative to the executable (for a
shippable layout). Use a fresh build directory after flipping that option.

## Play

- **Move:** WASD or arrow keys
- **Aim:** mouse
- **Shoot:** left click

Enemy waves and shoot sound start off. The ImGui **debug** window can spawn
enemies, toggle sound and hitboxes, change game speed, and reset.

## Layout

| Path | Role |
|---|---|
| `src/gameLayer/` | gameplay: ships, bullets, enemies, collision, HUD |
| `src/platform/` | GLFW window, input, frame loop, ImGui wiring |
| `src/render/` | WebGPU context, sprite batch, ImGui backend |
| `include/render/` | wgpu2d API, platform-facing init/begin/end |
| `resources/` | sprites, shoot sound, WGSL shaders |
| `docs/webgpu-learning-outline.md` | WebGPU concepts and where they live in this repo |

`wgpu2d` mirrors gl2d's **signatures**. It does not mirror gl2d's implementation:
vertices stay in world pixels and the camera is a matrix on the GPU.

## Origin

Started from [meemknight/game-in-cpp-full-course](https://github.com/meemknight/game-in-cpp-full-course)
(GLFW, gl2d, ImGui, raudio). The render layer was ported to WebGPU; gl2d, glad,
and the OpenGL ImGui backend were then removed.
