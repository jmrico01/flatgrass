#ifndef INCLUDED_MOMSRVR_PROFILER
#define INCLUDED_MOMSRVR_PROFILER

namespace BloombergLP {
namespace momsrvr {

struct ProfilerTimerScoped
{
	ProfilerTimerScoped();
	~ProfilerTimerScoped();
};

}
}

#endif INCLUDED_MOMSRVR_PROFILER