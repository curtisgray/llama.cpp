#pragma once
#include <atomic>

namespace stash
{
	extern volatile std::atomic_bool __keep_running;
	void terminate();
	void wait_for_termination();
}