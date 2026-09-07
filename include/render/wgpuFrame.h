#pragma once

// The render layer's internal view of the WebGPU context. wgpuContext.h is
// the platform layer's view and deliberately mentions no WebGPU types; this
// header does the opposite and is only for files under src/render, which
// already include <webgpu/webgpu.hpp>.
//
// Milestone 8 added it: the ImGui backend lives in its own translation unit
// and needs the device, the queue, the surface format, the frame's render
// pass, and the bind group behind a wgpu2d::Texture id.

#include <webgpu/webgpu.hpp>
#include <cstdint>

namespace render
{
	// Created once by wgpuInit, valid until wgpuShutdown. Null before init.
	wgpu::Device wgpuDevice();
	wgpu::Queue wgpuQueue();
	wgpu::TextureFormat wgpuSurfaceFormat();

	// Group 0 of the sprite pipeline: a sampled 2D texture plus its sampler.
	// Any pipeline that samples a wgpu2d::Texture reuses this layout so it
	// can bind the group the texture registry already built for it.
	wgpu::BindGroupLayout wgpuTextureBindGroupLayout();

	// The bind group for a wgpu2d::Texture handle's id, or null if the id is
	// not a live texture.
	wgpu::BindGroup wgpuTextureBindGroup(uint32_t textureId);

	// The size the surface is currently configured at, in framebuffer pixels.
	void wgpuSurfaceSize(int &width, int &height);

	// Reads a WGSL file and compiles it. Errors arrive through the device's
	// uncaptured-error callback. Returns null if the file cannot be read.
	wgpu::ShaderModule wgpuCreateShaderModuleFromFile(const char *path);

	// The frame's single render pass, begun (clearing to the recorded color)
	// if nothing has drawn yet. Null when no frame is open. Whatever is
	// recorded into it after the game's flush draws on top.
	wgpu::RenderPassEncoder wgpuCurrentRenderPass();
}
