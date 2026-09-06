#pragma once

// The one include the game files use for 2D rendering. The CMake option
// RENDERER_WEBGPU picks the implementation; the game code names the
// renderer through the r2d alias and never sees which one it got.
//
// wgpu2d mirrors the subset of gl2d's API the game uses, signature for
// signature, so the game files changed only by this include and the alias.
// The implementation underneath is not gl2d's: vertices stay in world
// pixels and the camera is a matrix on the GPU.

#if RENDERER_WEBGPU
#include <render/wgpu2d.h>
namespace r2d = wgpu2d;
#else
#include <gl2d/gl2d.h>
namespace r2d = gl2d;
#endif
