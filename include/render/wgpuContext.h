#pragma once

// WebGPU render context. Only built when RENDERER_WEBGPU=1; the OpenGL path
// never includes WebGPU headers. This header deliberately exposes no WebGPU
// types so glfwMain.cpp compiles identically in both configurations.

struct GLFWwindow;

namespace render
{
	// Creates the WebGPU instance, wraps the GLFW window into a surface,
	// requests an adapter that can present to it, requests a device and its
	// queue, and configures the surface at the window's framebuffer size.
	// Prints the adapter and the chosen surface format to stdout.
	// The window must have been created with GLFW_CLIENT_API = GLFW_NO_API.
	// Returns false if any step fails; details are printed to stderr.
	bool wgpuInit(GLFWwindow *window);

	// One frame: acquire the surface texture, record a render pass that clears
	// it, submit, present. If the surface reports it is outdated (resized),
	// reconfigures it at the current framebuffer size and skips the frame.
	void wgpuRenderFrame();

	// Releases everything wgpuInit created, in reverse order.
	void wgpuShutdown();
}
