#pragma once
#include <functional>

#include <nlohmann/json.hpp>

int run_inference(int argc, char **argv, const std::function<bool(const nlohmann::json &metrics)> &onProgress,
				  const std::function<void(const std::string &alias, const wingman::WingmanItemStatus &status)> &onStatus);
void stop_inference();
