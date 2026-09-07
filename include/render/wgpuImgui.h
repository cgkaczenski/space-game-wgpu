#pragma once

// Milestone 8: the Dear ImGui *renderer* backend. The platform backend stays
// imgui_impl_glfw (input, display size, cursors).
//
// Written by hand instead of using the bundled imgui_impl_wgpu.cpp: that
// file is from ImGui 1.89.5 and targets the 2022 webgpu.h (SPIR-V shader
// modules, WGPUProgrammableStageDescriptor, const char* labels), none of
// which exists in wgpu-native v24.
//
// Like wgpuContext.h this header names no WebGPU and no ImGui types, so the
// platform layer can call it without either include.

namespace render
{
	// Builds the ImGui pipeline and uploads the font atlas as a texture.
	// Call after ImGui::CreateContext and the GLFW backend's init, and after
	// render::wgpuInit. Returns false on failure.
	bool wgpuImguiInit();

	// Per-frame hook, called before ImGui::NewFrame. Nothing to do yet;
	// kept so the call sites match.
	void wgpuImguiNewFrame();

	// Draws ImGui::GetDrawData() into the frame's render pass. Call after
	// ImGui::Render() and before render::wgpuEndFrame, so the UI lands on
	// top of the game.
	void wgpuImguiRenderDrawData();

	// Releases the pipeline, the buffers and the font texture. Call before
	// render::wgpuShutdown.
	void wgpuImguiShutdown();
}
