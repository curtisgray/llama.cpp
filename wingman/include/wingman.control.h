#pragma once
#include <filesystem>

namespace wingman {
	inline std::filesystem::path argv0;
	void Start(const int controlPort, const bool disableCtrlCInterrupt = false);
	bool ResetAfterCrash(bool force = false);
	void RequestSystemShutdown();
}
