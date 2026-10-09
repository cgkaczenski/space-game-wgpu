#include <engine/sequence.h>

namespace sequence
{

void start(Sequence &s, int count)
{
	s = {};
	s.count = count;
}

bool running(const Sequence &s) { return s.current < s.count; }
bool finishing(const Sequence &s) { return running(s) && s.finishing >= 0.f; }

namespace
{
	Event next(Sequence &s)
	{
		s.current++;
		s.inStep = 0.f;
		s.finishing = -1.f;
		s.entered = false;
		return s.current >= s.count ? Event::Done : Event::None;
	}
}

Event update(Sequence &s, float dt, bool met, bool skip, float finishSeconds)
{
	if (!running(s)) { return Event::None; }

	if (!s.entered)
	{
		s.entered = true;
		return Event::Entered;
	}

	s.inStep += dt;

	if (s.finishing >= 0.f)
	{
		s.finishing -= dt;
		if (s.finishing < 0.f || skip) { return next(s); }
		return Event::None;
	}

	if (skip) { return next(s); }
	if (met)
	{
		// No linger: straight on. The last step then reports Done, which
		// says more than Finished would.
		if (finishSeconds <= 0.f) { return next(s) == Event::Done ? Event::Done : Event::Finished; }
		s.finishing = finishSeconds;
		return Event::Finished;
	}
	return Event::None;
}

}
