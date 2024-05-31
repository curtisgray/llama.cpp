#pragma once
#include <string>
#include "orm.h"

namespace wingman::clients {
	enum struct DownloaderResult {
		success,
		failed,
		already_exists,
		bad_model_moniker,
		bad_url
	};
	DownloaderResult DownloadModel(const std::string &modelMoniker, orm::ItemActionsFactory &actions, bool showProgress = true, bool force = false);
} // namespace wingman::clients
