#pragma once

// The player's settings (gameplay roadmap U2): what the menu's Settings page
// changes, kept apart from tuning. Tuning is the designer's file; these are
// the player's, in `settings.cfg` beside `imgui.ini` and `lastLevel.cfg` in
// the working directory, gitignored.
//
// A missing or unreadable file leaves the defaults. A line that is not
// understood is skipped, so an older file still loads.

namespace playerSettings
{
	struct Settings
	{
		float volume = 1.f;       // 0 .. 1, the master volume
		float crt = 1.f;          // 0 .. 1, of the tuned CRT strength
		bool fullscreen = true;   // on until the player turns it off
	};

	// Reads the file and applies what it holds. Call once from initGame,
	// after the audio device and the CRT exist.
	bool init();

	const Settings &get();

	// Applies `s` at once and writes the file.
	void set(const Settings &s);
}
