#pragma once

// This game's menu (gameplay roadmap U2): the pause screen. Escape, or the
// window losing focus, pauses the game and opens it over the grey world; it
// is drawn over the HUD and does not shake with it.
//
//   PAUSED      Resume, Restart, Settings, Quit. Escape resumes.
//   SETTINGS    Volume, CRT, Fullscreen, Back. Escape goes back.
//
// The widget is wgpu2d::Menu; what is here is which rows there are, what
// they do, how they look, and which keys and buttons drive them. Settings
// are playerSettings'.

#include <render/wgpu2d.h>

namespace menu
{
	enum class Choice
	{
		None,
		Resume,
		Restart,
		Quit,
	};

	// The pause just began: the first page, Resume selected.
	void open();

	// Once a frame while paused, after the HUD: reads the keys and the mouse,
	// draws the menu in screen space, and says what the player chose. `w` and
	// `h` are the framebuffer size. `takeInput` false draws without reading
	// any -- the frame the pause began, whose Escape opened it.
	Choice update(wgpu2d::Renderer2D &renderer, int w, int h, bool takeInput);

	// Whether the last choice came from a mouse press, which is still down
	// as play resumes: the game holds its trigger until it is let go.
	bool choseWithPointer();
}
