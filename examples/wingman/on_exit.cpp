#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>
#include <on_exit.h>

using namespace std;
namespace stash
{
	volatile std::atomic_bool __keep_running = true;

	void signal_sigterm_callback_handler(int signal)
	{
		terminate();
	}

	void terminate()
	{
		__keep_running = false;
	}

	void wait_for_termination()
	{
		while (__keep_running)
		{
			std::this_thread::sleep_for(100ms);
		}
	}
	
	bool __signal_method_activated = []()
	{
		std::signal(SIGTERM, signal_sigterm_callback_handler);
		return true;
	}();
}

