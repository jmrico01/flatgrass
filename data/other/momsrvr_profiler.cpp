#include <momsrvr_profiler.h>

#include <bsl_array.h>
#include <bsl_vector.h>
#include <bsls_atomic.h>
#include <pthread.h>

namespace BloombergLP {
namespace momsrvr {

struct ProfilerNode
{
	bsl::vector<ProfilerNode> children;
	float startTime;
	float endTime;
	bool done;
};

struct ProfilerThreadData
{
	ProfilerNode root;

	ProfilerThreadData() {
		root.startTime = 0.0f;
		root.done = false;
	}
};

static const size_t MAX_THREADS = 4096; // TODO is there a better way that is also lock-free?
static bsl::array<ProfilerThreadData, MAX_THREADS> profilerThreadData_;

static size_t ProfilerGetCurrentThreadIndex()
{
	static bsls::AtomicUint numRegisteredThreads = 0;
	static bsl::array<pthread_t, MAX_THREADS> registeredThreads;

	pthread_t currentThread = pthread_self();
	for (size_t i = 0; i < numRegisteredThreads; i++) {
		if (pthread_equal(currentThread, registeredThreads[i])) {
			return i;
		}
	}

	if (numRegisteredThreads >= MAX_THREADS) {
		// TODO log error
		return 0;
	}
	size_t index = numRegisteredThreads++; // TODO hmm, I think this is thread-safe?
	registeredThreads[index] = currentThread;
	return index;
}

ProfilerNode* GetLowestActiveNode(ProfilerNode* root)
{
}

ProfilerTimerScoped::ProfilerTimerScoped()
{
	size_t index = ProfilerGetCurrentThreadIndex();
	ProfilerThreadData& data = profilerThreadData_[index];
	ProfilerNode* lowestActiveNode = GetLowestActiveNode(&data.root);
}

ProfilerTimerScoped::~ProfilerTimerScoped()
{
}

}
}