#pragma once
#include <map>
#include <regex>
#include <string>

// Structs to hold the leaderboard data
struct EqBenchData {
    double score;
    std::string params;
};

struct MagiData {
    double score;
};

// Parses the leaderboard data from a multiline string
std::map<std::string, std::pair<EqBenchData, MagiData>> parseLeaderboardData(const std::string& input);
