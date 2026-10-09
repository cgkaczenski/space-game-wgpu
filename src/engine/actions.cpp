#include <engine/actions.h>

namespace actions
{

namespace
{
	bool modifierHeld(const Source &s, int modifier)
	{
		return modifier < 0 || (s.keyHeld && s.keyHeld(modifier));
	}

	// Most specific wins. A plain binding is shadowed while any binding in the
	// table on the same input has its modifier held: with Ctrl + wheel bound
	// to zoom, holding Ctrl takes the wheel away from whatever the plain wheel
	// does. The modified binding never needs to know the plain one exists.
	bool shadowed(const Table &table, const Source &s, const Binding &b)
	{
		if (b.modifier >= 0) { return false; }
		for (const Action &a : table.actions)
		{
			for (int i = 0; i < a.count; i++)
			{
				const Binding &o = a.bindings[i];
				if (o.bound() && o.modifier >= 0 && o.device == b.device && o.code == b.code && modifierHeld(s, o.modifier))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool live(const Table &table, const Source &s, const Binding &b)
	{
		return b.bound() && modifierHeld(s, b.modifier) && !shadowed(table, s, b);
	}

	float wheelOf(const Source &s, const Binding &b)
	{
		return s.wheel ? s.wheel(b.code) : 0.f;
	}

	enum class Query { Held, Pressed, Released, Repeated };

	bool ask(const Source &s, const Binding &b, Query q)
	{
		switch (b.device)
		{
		case Device::Key:
			switch (q)
			{
			case Query::Held: return s.keyHeld && s.keyHeld(b.code);
			case Query::Pressed: return s.keyPressed && s.keyPressed(b.code);
			case Query::Released: return s.keyReleased && s.keyReleased(b.code);
			case Query::Repeated: return s.keyRepeated && s.keyRepeated(b.code);
			}
			break;
		case Device::Mouse:
			switch (q)
			{
			case Query::Held: return s.mouseHeld && s.mouseHeld(b.code);
			case Query::Pressed:
			case Query::Repeated: return s.mousePressed && s.mousePressed(b.code);
			case Query::Released: return s.mouseReleased && s.mouseReleased(b.code);
			}
			break;
		case Device::Wheel:
			// A wheel has no up and down, only a frame it moved: that is held,
			// pressed and repeated at once, and it is never released.
			return q != Query::Released && wheelOf(s, b) != 0.f;
		}
		return false;
	}

	bool any(const Table &table, const Source &s, int action, Query q)
	{
		if (action < 0 || action >= (int)table.actions.size()) { return false; }
		const Action &a = table.actions[action];
		for (int i = 0; i < a.count; i++)
		{
			if (live(table, s, a.bindings[i]) && ask(s, a.bindings[i], q)) { return true; }
		}
		return false;
	}
}

bool held(const Table &table, const Source &source, int action) { return any(table, source, action, Query::Held); }
bool pressed(const Table &table, const Source &source, int action) { return any(table, source, action, Query::Pressed); }
bool released(const Table &table, const Source &source, int action) { return any(table, source, action, Query::Released); }
bool repeated(const Table &table, const Source &source, int action) { return any(table, source, action, Query::Repeated); }

float steps(const Table &table, const Source &source, int action)
{
	if (action < 0 || action >= (int)table.actions.size()) { return 0.f; }
	const Action &a = table.actions[action];
	float total = 0.f;
	for (int i = 0; i < a.count; i++)
	{
		const Binding &b = a.bindings[i];
		if (b.device == Device::Wheel && live(table, source, b)) { total += wheelOf(source, b); }
	}
	return total;
}

std::string name(const Source &source, const Binding &b)
{
	std::string out;
	if (!b.bound()) { return out; }
	if (b.modifier >= 0)
	{
		const char *m = source.keyName ? source.keyName(b.modifier) : nullptr;
		out += m ? m : "?";
		out += " + ";
	}
	switch (b.device)
	{
	case Device::Key:
	{
		const char *k = source.keyName ? source.keyName(b.code) : nullptr;
		out += k ? k : "?";
		break;
	}
	case Device::Mouse: out += b.code == 0 ? "LEFT MOUSE" : b.code == 1 ? "RIGHT MOUSE" : "MOUSE"; break;
	case Device::Wheel: out += b.code == 0 ? "WHEEL" : "SIDEWAYS WHEEL"; break;
	}
	return out;
}

std::string names(const Table &table, const Source &source, int action, const char *separator)
{
	if (action < 0 || action >= (int)table.actions.size()) { return {}; }
	const Action &a = table.actions[action];
	std::string out;
	for (int i = 0; i < a.count; i++)
	{
		if (!a.bindings[i].bound()) { continue; }
		if (!out.empty()) { out += separator; }
		out += name(source, a.bindings[i]);
	}
	return out;
}

}
