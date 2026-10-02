#include <tuning.h>

#include "imgui.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace tuning
{

namespace
{
	struct Tunable
	{
		std::string key;
		Entry::Type type;
		void *variable;
		double defaults[4] = {};
	};

	// Function-local, so it exists before the first Group registers, whatever
	// order the files' start-up code runs in.
	std::vector<Tunable> &registry()
	{
		static std::vector<Tunable> all;
		return all;
	}
	std::unordered_map<const void *, size_t> &byAddress()
	{
		static std::unordered_map<const void *, size_t> map;
		return map;
	}

	int components(Entry::Type type)
	{
		switch (type)
		{
		case Entry::Type::Vec2: return 2;
		case Entry::Type::Vec3: return 3;
		case Entry::Type::Vec4: return 4;
		default: return 1;
		}
	}

	void read(const Tunable &t, double out[4])
	{
		switch (t.type)
		{
		case Entry::Type::Float: out[0] = *(float *)t.variable; break;
		case Entry::Type::Int:   out[0] = *(int *)t.variable; break;
		case Entry::Type::Bool:  out[0] = *(bool *)t.variable ? 1.0 : 0.0; break;
		default:
			for (int i = 0; i < components(t.type); i++) { out[i] = ((float *)t.variable)[i]; }
			break;
		}
	}

	void write(const Tunable &t, const double in[4])
	{
		switch (t.type)
		{
		case Entry::Type::Float: *(float *)t.variable = (float)in[0]; break;
		case Entry::Type::Int:   *(int *)t.variable = (int)std::lround(in[0]); break;
		case Entry::Type::Bool:  *(bool *)t.variable = in[0] != 0.0; break;
		default:
			for (int i = 0; i < components(t.type); i++) { ((float *)t.variable)[i] = (float)in[i]; }
			break;
		}
	}

	// A float counts as changed past a hair of its own size: what a slider
	// dragged away and back exactly lands on is still the default.
	bool differs(const Tunable &t)
	{
		double now[4] = {};
		read(t, now);
		for (int i = 0; i < components(t.type); i++)
		{
			const double tolerance = (t.type == Entry::Type::Int || t.type == Entry::Type::Bool)
				? 0.0 : 1e-6 * std::max(1.0, std::fabs(t.defaults[i]));
			if (std::fabs(now[i] - t.defaults[i]) > tolerance) { return true; }
		}
		return false;
	}

	std::string format(const Tunable &t, const double values[4])
	{
		std::string s;
		char buffer[32];
		for (int i = 0; i < components(t.type); i++)
		{
			if (t.type == Entry::Type::Int || t.type == Entry::Type::Bool)
			{
				std::snprintf(buffer, sizeof(buffer), "%s%d", i ? " " : "", (int)std::lround(values[i]));
			}
			else
			{
				std::snprintf(buffer, sizeof(buffer), "%s%.9g", i ? " " : "", values[i]);
			}
			s += buffer;
		}
		return s;
	}

	// Every changed value as the file's lines, in registration order.
	std::string changes()
	{
		std::string out;
		for (const Tunable &t : registry())
		{
			if (!differs(t)) { continue; }
			double now[4] = {};
			read(t, now);
			out += t.key + " " + format(t, now) + "\n";
		}
		return out;
	}

	void resetAll()
	{
		for (const Tunable &t : registry()) { write(t, t.defaults); }
	}

	// ---- Sets ----
	std::string directory;
	std::string lastRecord;
	void (*onApplied)() = nullptr;

	std::string current = "";       // the set loaded, by file name; "" is Defaults
	std::string savedChanges;       // what it held when loaded or saved: unsaved is the difference

	std::vector<std::string> listSets()
	{
		std::vector<std::string> names;
		std::error_code error; // a missing folder is no sets, not an exception
		for (const auto &entry : std::filesystem::directory_iterator(directory, error))
		{
			if (entry.is_regular_file(error) && entry.path().extension() == ".txt")
			{
				names.push_back(entry.path().filename().string());
			}
		}
		std::sort(names.begin(), names.end());
		return names;
	}

	void remember()
	{
		std::ofstream out(lastRecord);
		if (out) { out << (current.empty() ? "Defaults" : current) << "\n"; }
	}

	// The defaults, then the file's values on top. False if it cannot be read.
	bool loadSet(const std::string &file)
	{
		resetAll();
		if (file.empty()) { current = ""; savedChanges = ""; return true; }

		std::ifstream in(directory + file);
		if (!in.is_open())
		{
			std::cerr << "tuning: cannot open " << directory << file << ", using the defaults\n";
			current = "";
			savedChanges = "";
			return false;
		}
		std::unordered_map<std::string, size_t> byKey;
		for (size_t i = 0; i < registry().size(); i++) { byKey[registry()[i].key] = i; }

		std::string line;
		int number = 0;
		while (std::getline(in, line))
		{
			number++;
			const size_t hash = line.find('#');
			if (hash != std::string::npos) { line.erase(hash); }
			std::istringstream words(line);
			std::string key;
			if (!(words >> key)) { continue; }
			const auto found = byKey.find(key);
			if (found == byKey.end())
			{
				std::cerr << "tuning: " << file << ":" << number << ": no tunable \"" << key << "\", skipped\n";
				continue;
			}
			const Tunable &t = registry()[found->second];
			double values[4] = {};
			bool ok = true;
			for (int i = 0; i < components(t.type); i++) { ok = ok && (bool)(words >> values[i]); }
			if (!ok)
			{
				std::cerr << "tuning: " << file << ":" << number << ": cannot read \"" << line << "\"\n";
				continue;
			}
			write(t, values);
		}
		current = file;
		savedChanges = changes();
		return true;
	}

	bool saveSet(const std::string &file)
	{
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		std::ofstream out(directory + file);
		if (!out.is_open())
		{
			std::cerr << "tuning: cannot write " << directory << file << "\n";
			return false;
		}
		out << "# Tuning: only what differs from the code's defaults (debug panel, Tuning).\n";
		out << changes();
		current = file;
		savedChanges = changes();
		remember();
		return true;
	}

	bool validName(const std::string &name)
	{
		if (name.empty() || name.size() > 48) { return false; }
		for (char c : name)
		{
			if (!(std::isalnum((unsigned char)c) || c == '-' || c == '_')) { return false; }
		}
		return true;
	}

	std::string stem(const std::string &file)
	{
		return file.size() > 4 ? file.substr(0, file.size() - 4) : file;
	}
}

Group::Group(const char *prefix, std::initializer_list<Entry> entries)
{
	for (const Entry &e : entries)
	{
		Tunable t;
		t.key = std::string(prefix) + "." + e.key;
		for (const Tunable &other : registry())
		{
			if (other.key == t.key) { std::cerr << "tuning: \"" << t.key << "\" registered twice; files will set only the first\n"; }
		}
		t.type = e.type;
		t.variable = e.variable;
		read(t, t.defaults);
		byAddress()[e.variable] = registry().size();
		registry().push_back(std::move(t));
	}
}

void init(const std::string &dir, const std::string &lastRecordFile, void (*applied)())
{
	directory = dir;
	lastRecord = lastRecordFile;
	onApplied = applied;

	// The last set chosen, if it is still there; otherwise the defaults.
	std::ifstream in(lastRecord);
	std::string name;
	if (in >> name && name != "Defaults")
	{
		if (loadSet(name)) { std::cout << "tuning: " << name << "\n" << std::flush; }
	}
}

bool saveAs(const std::string &file) { return saveSet(file); }

bool changed(const void *variable)
{
	const auto found = byAddress().find(variable);
	return found != byAddress().end() && differs(registry()[found->second]);
}

void debugUi()
{
	static std::vector<std::string> sets = listSets();
	static char newName[64] = "tuning1";

	const std::string now = changes();
	const bool unsaved = now != savedChanges;
	if (unsaved) { ImGui::TextColored({1.f, 0.7f, 0.2f, 1.f}, "Unsaved changes: choosing a set discards them"); }

	// Choosing applies it at once; Defaults restores the code's values.
	if (ImGui::Button("Refresh")) { sets = listSets(); }
	ImGui::SameLine();
	ImGui::SetNextItemWidth(180.f);
	if (ImGui::BeginCombo("Set", current.empty() ? "Defaults" : stem(current).c_str()))
	{
		std::string pick;
		bool picked = false;
		if (ImGui::Selectable("Defaults", current.empty())) { pick = ""; picked = true; }
		for (const std::string &s : sets)
		{
			if (ImGui::Selectable(stem(s).c_str(), s == current)) { pick = s; picked = true; }
		}
		ImGui::EndCombo();
		if (picked)
		{
			loadSet(pick);
			remember();
			if (onApplied) { onApplied(); }
		}
	}

	ImGui::BeginDisabled(current.empty() || !unsaved);
	if (ImGui::Button("Save")) { saveSet(current); }
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::SetNextItemWidth(140.f);
	ImGui::InputText("##tuningName", newName, sizeof(newName));
	const std::string name = newName;
	const bool taken = std::find(sets.begin(), sets.end(), name + ".txt") != sets.end();
	ImGui::SameLine();
	ImGui::BeginDisabled(!validName(name) || taken);
	if (ImGui::Button("Save as new"))
	{
		if (saveSet(name + ".txt")) { sets = listSets(); }
	}
	ImGui::EndDisabled();
	if (taken) { ImGui::SameLine(); ImGui::TextDisabled("taken"); }

	// What differs from the code, so it is plain what has been tuned.
	int count = 0;
	for (const Tunable &t : registry()) { if (differs(t)) { count++; } }
	ImGui::SeparatorText(count ? "Changed from the code" : "Nothing changed from the code");
	if (count == 0) { return; }
	if (ImGui::SmallButton("Reset all to defaults")) { resetAll(); if (onApplied) { onApplied(); } }
	for (const Tunable &t : registry())
	{
		if (!differs(t)) { continue; }
		double nowValues[4] = {};
		read(t, nowValues);
		ImGui::PushID(t.variable);
		if (ImGui::SmallButton("reset")) { write(t, t.defaults); }
		ImGui::SameLine();
		ImGui::TextColored({1.f, 0.78f, 0.35f, 1.f}, "%s = %s", t.key.c_str(), format(t, nowValues).c_str());
		ImGui::SameLine();
		ImGui::TextDisabled("(default %s)", format(t, t.defaults).c_str());
		ImGui::PopID();
	}
}

}

namespace tune
{

Highlight::Highlight(const void *variable, const void *second)
{
	if (!tuning::changed(variable) && !(second && tuning::changed(second))) { return; }
	const ImVec4 amber = {1.f, 0.78f, 0.35f, 1.f};
	ImGui::PushStyleColor(ImGuiCol_Text, amber);
	ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.42f, 0.27f, 0.05f, 0.85f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.55f, 0.36f, 0.08f, 0.9f));
	ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.65f, 0.43f, 0.1f, 1.f));
	ImGui::PushStyleColor(ImGuiCol_SliderGrab, amber);
	ImGui::PushStyleColor(ImGuiCol_CheckMark, amber);
	pushed = 6;
}

Highlight::~Highlight()
{
	if (pushed) { ImGui::PopStyleColor(pushed); }
}

bool SliderFloat(const char *label, float *v, float min, float max, const char *format, int flags)
{
	Highlight h(v);
	return ImGui::SliderFloat(label, v, min, max, format, (ImGuiSliderFlags)flags);
}

bool SliderInt(const char *label, int *v, int min, int max, const char *format, int flags)
{
	Highlight h(v);
	return ImGui::SliderInt(label, v, min, max, format, (ImGuiSliderFlags)flags);
}

bool DragFloat(const char *label, float *v, float speed, float min, float max, const char *format, int flags)
{
	Highlight h(v);
	return ImGui::DragFloat(label, v, speed, min, max, format, (ImGuiSliderFlags)flags);
}

bool DragFloatRange2(const char *label, float *lo, float *hi, float speed, float min, float max,
	const char *format, const char *formatMax, int flags)
{
	Highlight h(lo, hi);
	return ImGui::DragFloatRange2(label, lo, hi, speed, min, max, format, formatMax, (ImGuiSliderFlags)flags);
}

bool Checkbox(const char *label, bool *v)
{
	Highlight h(v);
	return ImGui::Checkbox(label, v);
}

bool ColorEdit3(const char *label, float *rgb, int flags)
{
	Highlight h(rgb);
	return ImGui::ColorEdit3(label, rgb, (ImGuiColorEditFlags)flags);
}

bool ColorEdit4(const char *label, float *rgba, int flags)
{
	Highlight h(rgba);
	return ImGui::ColorEdit4(label, rgba, (ImGuiColorEditFlags)flags);
}

}
