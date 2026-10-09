#pragma once

// The player's settings (gameplay roadmap U2): what the menu's Settings page
// changes, kept apart from tuning. Tuning is the designer's file; these are
// the player's, in `settings.cfg` beside `imgui.ini` and `lastLevel.cfg` in
// the working directory, gitignored.
//
// A missing or unreadable file leaves the defaults. A line that is not
// understood is skipped, so an older file still loads.

#include <string>
#include <vector>

namespace playerSettings
{
	struct Settings
	{
		float volume = 1.f;       // 0 .. 1, the master volume
		float crt = 1.f;          // 0 .. 1, of the tuned CRT strength
		bool fullscreen = true;   // on until the player turns it off
		int scheme = 0;           // playerMove::Controls (K2)
		bool hints = true;        // levels' hint scripts run (H2)
		// Levels whose hints the player switched off from a hint's own box:
		// remembered, and cleared when Settings > Hints is switched back on.
		std::vector<std::string> hintsOff;
	};

	// Reads the file and applies what it holds. Call once from initGame,
	// after the audio device and the CRT exist.
	bool init();

	const Settings &get();

	// Whether `level`'s hint script runs: hints on, and not switched off for
	// that level.
	bool hintsOn(const std::string &level);
	// Switches `level`'s hints off, and writes the file.
	void hintsOffFor(const std::string &level);

	// Applies `s` at once and writes the file.
	void set(const Settings &s);

	// Writes the file as it stands: after a rebinding, which lives in
	// `controls`, not in Settings. The bindings that differ from the defaults
	// are written with the rest (K2).
	void save();
}
