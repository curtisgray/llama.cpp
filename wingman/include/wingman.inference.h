#pragma once
#include <functional>

#include <nlohmann/json.hpp>

int run_inference(int argc, char **argv, const std::function<bool(const nlohmann::json &metrics)> &onProgress);
void stop_inference();
