#pragma once
#include <filesystem>
#include <atomic>

namespace wingman {
	inline std::filesystem::path argv0;
	inline std::atomic control_server_should_be_listening = false;
	inline std::atomic control_server_started = false;
	inline std::atomic control_server_listening = false;
	void Start(const int controlPort, const bool disableCtrlCInterrupt, const bool resetAfterCrash);
	bool ResetAfterCrash(bool force = false);
	void RequestSystemShutdown();
}
