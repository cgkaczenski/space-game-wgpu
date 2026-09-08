#include <GLFW/glfw3.h>
#include <stb_image/stb_image.h>
#include <stb_image/stb_image_write.h>
#include <stb_truetype/stb_truetype.h>
#include <iostream>
#include <ctime>
#include "platformTools.h"
#include <raudio.h>
#include "platformInput.h"
#include "otherPlatformFunctions.h"
#include "gameLayer.h"
#include <render/wgpuContext.h>
#include <glfw3webgpu.h>   // glfwCreateWindowWGPUSurface: the app owns the window
#include <platform/wgpuMetalLayer.h>
#include <fstream>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <chrono>

#define REMOVE_IMGUI 0

#if REMOVE_IMGUI == 0
	#include "imgui.h"
	#include "backends/imgui_impl_glfw.h"
	#include <platform/wgpuImgui.h>
	#include "imguiThemes.h"
#endif

#ifdef _WIN32
#include <Windows.h>
#endif

#undef min
#undef max

#pragma region globals 
bool currentFullScreen = 0;
bool fullScreen = 0;

#pragma endregion



void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{

	if ((action == GLFW_REPEAT || action == GLFW_PRESS) && key == GLFW_KEY_BACKSPACE)
	{
		platform::internal::addToTypedInput(8);
	}

	bool state = 0;

	if(action == GLFW_PRESS)
	{
		state = 1;
	}else if(action == GLFW_RELEASE)
	{
		state = 0;
	}else
	{
		return;
	}

	if(key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
	{
		int index = key - GLFW_KEY_A;
		platform::internal::setButtonState(platform::Button::A + index, state);
	}else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
	{
		int index = key - GLFW_KEY_0;
		platform::internal::setButtonState(platform::Button::NR0 + index, state);
	}else
	{
	//special keys
		//GLFW_KEY_SPACE, GLFW_KEY_ENTER, GLFW_KEY_ESCAPE, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_LEFT, GLFW_KEY_RIGHT

		if (key == GLFW_KEY_SPACE)
		{
			platform::internal::setButtonState(platform::Button::Space, state);
		}
		else
		if (key == GLFW_KEY_ENTER)
		{
			platform::internal::setButtonState(platform::Button::Enter, state);
		}
		else
		if (key == GLFW_KEY_ESCAPE)
		{
			platform::internal::setButtonState(platform::Button::Escape, state);
		}
		else
		if (key == GLFW_KEY_UP)
		{
			platform::internal::setButtonState(platform::Button::Up, state);
		}
		else
		if (key == GLFW_KEY_DOWN)
		{
			platform::internal::setButtonState(platform::Button::Down, state);
		}
		else
		if (key == GLFW_KEY_LEFT)
		{
			platform::internal::setButtonState(platform::Button::Left, state);
		}
		else
		if (key == GLFW_KEY_RIGHT)
		{
			platform::internal::setButtonState(platform::Button::Right, state);
		}
		else
		if (key == GLFW_KEY_LEFT_CONTROL)
		{
			platform::internal::setButtonState(platform::Button::LeftCtrl, state);
		}
		if (key == GLFW_KEY_TAB)
		{
			platform::internal::setButtonState(platform::Button::Tab, state);
		}
	}
	
};

void mouseCallback(GLFWwindow *window, int key, int action, int mods)
{
	bool state = 0;

	if (action == GLFW_PRESS)
	{
		state = 1;
	}
	else if (action == GLFW_RELEASE)
	{
		state = 0;
	}
	else
	{
		return;
	}

	if(key == GLFW_MOUSE_BUTTON_LEFT)
	{
		platform::internal::setLeftMouseState(state);
	}else
	if (key == GLFW_MOUSE_BUTTON_RIGHT)
	{
		platform::internal::setRightMouseState(state);
	}
	

}

bool windowFocus = 1;

void windowFocusCallback(GLFWwindow *window, int focused)
{
	if (focused)
	{
		windowFocus = 1;
	}
	else
	{
		windowFocus = 0;
		//if you not capture the release event when the window loses focus,
		//the buttons will stay pressed
		platform::internal::resetInputsToZero();
	}
}

void windowSizeCallback(GLFWwindow *window, int x, int y)
{
	platform::internal::resetInputsToZero();
}

int mouseMovedFlag = 0;

void cursorPositionCallback(GLFWwindow *window, double xpos, double ypos)
{
	mouseMovedFlag = 1;
}

void characterCallback(GLFWwindow *window, unsigned int codepoint)
{
	if (codepoint < 127)
	{
		platform::internal::addToTypedInput(codepoint);
	}
}

#pragma region platform functions

GLFWwindow *wind = 0;

namespace platform
{

	void setRelMousePosition(int x, int y)
	{
		glfwSetCursorPos(wind, x, y);
	}

	bool isFullScreen()
	{
		return fullScreen;
	}

	void setFullScreen(bool f)
	{
		fullScreen = f;
	}

	glm::ivec2 getFrameBufferSize()
	{
		int x = 0; int y = 0;
		glfwGetFramebufferSize(wind, &x, &y);
		return {x, y};
	}

	glm::ivec2 getRelMousePosition()
	{
		double x = 0, y = 0;
		glfwGetCursorPos(wind, &x, &y);
		return { x, y };
	}

	glm::ivec2 getWindowSize()
	{
		int x = 0; int y = 0;
		glfwGetWindowSize(wind, &x, &y);
		return { x, y };
	}

	//todo test
	void showMouse(bool show)
	{
		if(show)
		{
			glfwSetInputMode(wind, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
		}else
		{
			glfwSetInputMode(wind, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
		}
	}

	bool isFocused()
	{
		
		return windowFocus;
	}

	bool mouseMoved()
	{
		return mouseMovedFlag;
	}

	bool writeEntireFile(const char *name, void *buffer, size_t size)
	{
		std::ofstream f(name, std::ios::binary);

		if(!f.is_open())
		{
			return 0;
		}

		f.write((char*)buffer, size);

		f.close();

		return 1;
	}


	bool readEntireFile(const char *name, void *buffer, size_t size)
	{
		std::ifstream f(name, std::ios::binary);

		if (!f.is_open())
		{
			return 0;
		}

		f.read((char *)buffer, size);

		f.close();

		return 1;
	}

};
#pragma endregion


int main()
{

#ifdef _WIN32
#ifdef _MSC_VER 
#if PRODUCTION_BUILD == 0
	AllocConsole();
	(void)freopen("conin$", "r", stdin);
	(void)freopen("conout$", "w", stdout);
	(void)freopen("conout$", "w", stderr);
	std::cout.sync_with_stdio();
#endif
#endif
#endif


#pragma region window

	permaAssertComment(glfwInit(), "err initializing glfw");

	// No OpenGL context: WebGPU drives the window's Metal layer through a surface.
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	// WGPU_OFFSCREEN=1 runs with the window hidden. With WGPU_SCREENSHOT_FRAME
	// that is a scriptable capture of a real frame -- the whole pipeline, ImGui
	// included -- without a window appearing.
	//
	// This is not a *surfaceless* headless context, which is the other half of
	// roadmap N3 and a much larger change: the renderer's eight uses of the
	// surface would each need an offscreen branch, and more to the point the
	// application would need to run with no window at all, which means stubbing
	// the GLFW input callbacks the game reads through platform:: and bypassing
	// the ImGui GLFW backend. A hidden window buys nearly all of the practical
	// value for one hint. The remaining gap is a machine with no window system
	// at all, which this project does not have.
	const bool offscreen = getenv("WGPU_OFFSCREEN") != nullptr;
	if (offscreen) { glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); }


	int w = 500;
	int h = 500;
	wind = glfwCreateWindow(w, h, "geam", nullptr, nullptr);

	glfwSetKeyCallback(wind, keyCallback);
	glfwSetMouseButtonCallback(wind, mouseCallback);
	glfwSetWindowFocusCallback(wind, windowFocusCallback);
	glfwSetWindowSizeCallback(wind, windowSizeCallback);
	glfwSetCursorPosCallback(wind, cursorPositionCallback);
	glfwSetCharCallback(wind, characterCallback);

	// Bring-up is three steps because creating a surface needs an instance and
	// only this layer knows what a window is: the library makes the instance,
	// we make the surface from it and keep it, the library borrows it for the
	// frame bracket.
	WGPUInstance wgpuInstance = render::wgpuInitInstance();
	permaAssertComment(wgpuInstance != nullptr, "err creating the WebGPU instance");

	WGPUSurface wgpuSurface = glfwCreateWindowWGPUSurface(wgpuInstance, wind);
	permaAssertComment(wgpuSurface != nullptr, "err creating the WebGPU surface");

	// No-op off Apple. Needs the window, and needs the surface to exist first
	// -- glfw3webgpu is what attaches the CAMetalLayer.
	render::pinMetalLayerColorSpaceToSRGB(wind);

	{
		int fbw = 0, fbh = 0;
		glfwGetFramebufferSize(wind, &fbw, &fbh);
		permaAssertComment(render::wgpuInit(wgpuSurface, fbw, fbh), "err initializing WebGPU");
	}


#pragma endregion


#pragma region imgui
	#if REMOVE_IMGUI == 0
		ImGui::CreateContext();
		//ImGui::StyleColorsDark();
		imguiThemes::embraceTheDarkness();

		ImGuiIO& io = ImGui::GetIO(); (void)io;
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		// GLFW platform backend feeds input and display size;
		// render::wgpuImgui* draws the result. Docking is core ImGui.
		// Multi-viewport stays off: every torn-off window would need its
		// own WebGPU surface.
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

		// Transparent window background so the game shows through the debug
		// window and only its title bar, text, and widgets draw.
		ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 0.f;

		ImGui_ImplGlfw_InitForOther(wind, true);
		permaAssertComment(render::wgpuImguiInit(), "err initializing the WebGPU ImGui backend");
	#endif
#pragma endregion

#pragma region audio
	InitAudioDevice();
	if (!IsAudioDeviceReady())
	{
		std::cerr << "AUDIO: InitAudioDevice failed; shoot.flac will be silent\n";
	}

	Music m = LoadMusicStream(RESOURCES_PATH "target.ogg");
	// Music m = {}; // NOTE: duplicate declaration of m; would fail to compile. LoadMusicStream already initializes m.
	UpdateMusicStream(m);
	StopMusicStream(m);
	PlayMusicStream(m);

#pragma endregion

#pragma region initGame
	if (!initGame())
	{
		return 0;
	}
#pragma endregion


	//long lastTime = clock();
	
	auto stop = std::chrono::high_resolution_clock::now();

	while (!glfwWindowShouldClose(wind))
	{
		UpdateMusicStream(m);
		PlayMusicStream(m);

	#pragma region deltaTime

		//long newTime = clock();
		//float deltaTime = (float)(newTime - lastTime) / CLOCKS_PER_SEC;
		//lastTime = clock();
		auto start = std::chrono::high_resolution_clock::now();

		float deltaTime = (std::chrono::duration_cast<std::chrono::nanoseconds>(start - stop)).count() / 1000000000.0;
		stop = std::chrono::high_resolution_clock::now();

		float augmentedDeltaTime = deltaTime;
		if (augmentedDeltaTime > 1.f / 10) { augmentedDeltaTime = 1.f / 10; }
	
	#pragma endregion

	#pragma region frame start
			// F12: ask the renderer for this frame. The request is fulfilled
			// inside wgpuEndFrame and collected after it -- the library hands
			// back pixels, and turning them into a file is our job, not its.
			// Edge-detected here rather than added to platformInput, because
			// this is a developer tool and not a game input.
			{
				static bool screenshotHeld = false;
				const bool down = glfwGetKey(wind, GLFW_KEY_F12) == GLFW_PRESS;
				if (down && !screenshotHeld) { render::wgpuRequestFrameCapture(); }
				screenshotHeld = down;

				// WGPU_SCREENSHOT_FRAME=N shoots frame N and needs no keyboard,
				// which is what makes this usable from a script or an agent
				// session -- the verification norm in AGENTS.md wants a fixed
				// scene rendered to a PNG, and a key binding cannot provide it.
				static long long frameIndex = 0;
				static const char *shotAt = getenv("WGPU_SCREENSHOT_FRAME");
				++frameIndex;
				if (shotAt && frameIndex == atoll(shotAt)) { render::wgpuRequestFrameCapture(); }
			}

			// Push the framebuffer size before acquiring anything. The
			// library no longer asks the window for it, and on Metal the
			// surface never reports itself Outdated, so this has to be told
			// every frame rather than only from the resize callback --
			// otherwise sprites stretch while the visible world does not
			// change. wgpuResize is a no-op when the size is unchanged.
			{
				int fbw = 0, fbh = 0;
				glfwGetFramebufferSize(wind, &fbw, &fbh);
				render::wgpuResize(fbw, fbh);
			}

			// Acquire the surface texture and open the frame's encoder. The
			// game's flush() draws into it; wgpuEndFrame submits and presents.
			render::wgpuBeginFrame();
	#pragma endregion

	#pragma region imgui
		#if REMOVE_IMGUI == 0
				render::wgpuImguiNewFrame();
				ImGui_ImplGlfw_NewFrame();
				ImGui::NewFrame();
				// PassthruCentralNode: the host window and the empty middle
				// of the dockspace draw nothing, so the game stays visible
				// underneath.
				ImGui::DockSpaceOverViewport(ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
		#endif
	#pragma endregion

	#pragma region game logic

		if (!gameLogic(augmentedDeltaTime))
		{
			closeGame();
			return 0;
		}

	#pragma endregion


	#pragma region fullscreen 

		if (platform::isFocused() && currentFullScreen != fullScreen)
		{
			static int lastW = w;
			static int lastH = w;
			static int lastPosX = 0;
			static int lastPosY = 0;

			if (fullScreen)
			{
				lastW = w;
				lastH = h;

				//glfwWindowHint(GLFW_DECORATED, NULL); // Remove the border and titlebar..  
				glfwGetWindowPos(wind, &lastPosX, &lastPosY);


				//auto monitor = glfwGetPrimaryMonitor();
				auto monitor = getCurrentMonitor(wind);


				const GLFWvidmode* mode = glfwGetVideoMode(monitor);

				// switch to full screen
				glfwSetWindowMonitor(wind, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);

				currentFullScreen = 1;

			}
			else
			{
				//glfwWindowHint(GLFW_DECORATED, GLFW_TRUE); // 
				glfwSetWindowMonitor(wind, nullptr, lastPosX, lastPosY, lastW, lastH, 0);

				currentFullScreen = 0;
			}

		}

	#pragma endregion

	#pragma region reset flags

		mouseMovedFlag = 0;
		platform::internal::updateAllButtons(deltaTime);
		platform::internal::resetTypedInput();

	#pragma endregion

	#pragma region window stuff

		#if REMOVE_IMGUI == 0
			ImGui::Render();
			render::wgpuImguiRenderDrawData(); // on top of the game, same pass
		#endif
		render::wgpuEndFrame(); // end pass, submit, present

		// Collect a screenshot if one was asked for. The renderer hands over
		// tightly packed RGBA and nothing else -- no path, no format, no
		// encoder -- so the policy about where files go lives here.
		{
			std::vector<unsigned char> pixels;
			int shotW = 0, shotH = 0;
			if (render::wgpuTakeFrameCapture(pixels, shotW, shotH))
			{
				char name[64] = {};
				std::snprintf(name, sizeof(name), "screenshot-%lld.png",
					(long long)std::chrono::duration_cast<std::chrono::seconds>(
						std::chrono::system_clock::now().time_since_epoch()).count());
				if (stbi_write_png(name, shotW, shotH, 4, pixels.data(), shotW * 4))
				{
					std::cout << "screenshot: " << name << " (" << shotW << "x" << shotH << ")\n";
				}
				else
				{
					std::cerr << "screenshot: stbi_write_png failed for " << name << "\n";
				}
				std::cout.flush();
			}
		}

		glfwPollEvents();

	#pragma endregion

	}

	closeGame();

	#if REMOVE_IMGUI == 0
		// Before the device goes away: the backend owns GPU objects.
		render::wgpuImguiShutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
	#endif
	render::wgpuShutdown();
	wgpuSurfaceRelease(wgpuSurface); // we created it; the library only borrowed it
	glfwDestroyWindow(wind);
	glfwTerminate();

	//if you want the console to stay after closing the window
	//std::cin.clear();
	//std::cin.get();
}