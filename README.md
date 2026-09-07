# spaceGame

A 2D space shooter: fly, shoot, take hits, recover. Built with GLFW, originally rendered with OpenGL + [gl2d](https://github.com/meemknight/gl2d). The render layer is being ported to WebGPU (wgpu-native on Metal) as a learning exercise; both backends stay working until the port reaches parity.

The game-facing renderer is `r2d` (`include/render/renderer.h`). CMake picks OpenGL/gl2d or WebGPU/`wgpu2d`; gameplay code does not care which one it got.

## Requirements

- CMake 3.16 or newer
- A C++17 compiler
- On macOS, the WebGPU path uses Metal via wgpu-native. First configure of that build fetches [WebGPU-distribution](https://github.com/eliemichel/WebGPU-distribution) and [glfw3webgpu](https://github.com/eliemichel/glfw3webgpu).

## Build

Two configurations, two directories. Do not mix them.

```bash
# OpenGL (default)
cmake -S . -B build
cmake --build build -j8

# WebGPU
cmake -S . -B build-webgpu -DRENDERER_WEBGPU=ON
cmake --build build-webgpu -j8
```

## Run

```bash
./build/spaceGame
./build-webgpu/spaceGame

# WebGPU only: draw the world into a smaller target and upscale with nearest filtering
WGPU_RENDER_SCALE=0.25 ./build-webgpu/spaceGame
```

`PRODUCTION_BUILD=ON` makes `RESOURCES_PATH` relative to the executable (for a shippable layout). Use a fresh build directory after flipping that option.

## Play

- **Move:** WASD or arrow keys
- **Aim:** mouse
- **Shoot:** left click

Enemy waves and shoot sound start off. The ImGui **debug** window can spawn enemies, toggle sound and hitboxes, change game speed, and reset.

## Layout

| Path | Role |
|---|---|
| `src/gameLayer/` | gameplay: ships, bullets, enemies, collision, HUD |
| `src/platform/` | GLFW window, input, frame loop, ImGui wiring |
| `src/render/` | WebGPU context, sprite batch, ImGui backend |
| `include/render/` | `r2d` alias, wgpu2d API, platform-facing init/begin/end |
| `resources/` | sprites, shoot sound, WGSL shaders |
| `docs/` | WebGPU port plan and learning notes |

## WebGPU port

Tracked in [docs/webgpu-port-plan.md](docs/webgpu-port-plan.md). Concepts, LearnWebGPU chapters, and where each one landed: [docs/webgpu-learning-outline.md](docs/webgpu-learning-outline.md). Call-flow diagrams: [docs/render-port.md](docs/render-port.md).

`wgpu2d` mirrors gl2d's **signatures** so game files change by an include and a namespace. It does not mirror gl2d's implementation: vertices stay in world pixels and the camera is a matrix on the GPU.

## Origin

Started from [meemknight/cmakeSetup](https://github.com/meemknight/cmakeSetup) (GLFW, gl2d, ImGui, raudio). The OpenGL path still uses that stack.
