#pragma once

// WebGPU render context. Only built when RENDERER_WEBGPU=1; the OpenGL path
// never includes WebGPU headers. This header deliberately exposes no WebGPU
// types so glfwMain.cpp compiles identically in both configurations.

struct GLFWwindow;

namespace render
{
	// Creates the WebGPU instance, wraps the GLFW window into a surface, and
	// requests an adapter that can present to it. Prints the adapter's
	// vendor, device, backend, limits, and features to stdout.
	// The window must have been created with GLFW_CLIENT_API = GLFW_NO_API.
	// Returns false if any step fails; details are printed to stderr.
	bool wgpuInit(GLFWwindow *window);

	// Releases everything wgpuInit created, in reverse order.
	void wgpuShutdown();
}
