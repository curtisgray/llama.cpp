#include <map>
#include <regex>
#include <sstream>
#include <string>
#include <vector>
#include "parse_evals.h"
#include "util.hpp"

// Utility function to split a string by a delimiter
std::vector<std::string> split(const std::string& s, char delimiter)
{
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (getline(stream, token, delimiter)) {
        tokens.push_back(token);
    }
    return tokens;
}

// Parses the leaderboard data from a multiline string
std::map<std::string, std::pair<EqBenchData, MagiData>> parseLeaderboardData(const std::string& input)
{
    std::map<std::string, std::pair<EqBenchData, MagiData>> leaderboard;

    // Use regex to find the leaderboard data strings
    std::regex eqbenchRegex("leaderboardDataEqbench = `([\\s\\S]*?)`;");
    std::regex magiRegex("leaderboardDataMagi = `([\\s\\S]*?)`;");

    std::smatch eqbenchMatch, magiMatch;
    std::regex_search(input, eqbenchMatch, eqbenchRegex);
    std::regex_search(input, magiMatch, magiRegex);

    // Extract EQBench data
    if (eqbenchMatch.size() == 2) {
        std::istringstream eqbenchStream(eqbenchMatch[1].str());
        std::string line;
        getline(eqbenchStream, line); // Skip header
        while (getline(eqbenchStream, line)) {
            auto tokens = split(line, ',');
            if (tokens.size() >= 3) {
                EqBenchData data { std::stod(tokens[1]), tokens[2] };
				// Convert model name to lowercase and remove leading '*' character if it exists
				const auto modelName = wingman::util::stringLower(tokens[0].substr(tokens[0].find_first_not_of('*')));
                leaderboard[modelName].first = data;
            }
        }
    }

    // Extract Magi data
    if (magiMatch.size() == 2) {
        std::istringstream magiStream(magiMatch[1].str());
        std::string line;
        getline(magiStream, line); // Skip header
        while (getline(magiStream, line)) {
            auto tokens = split(line, ',');
            if (tokens.size() == 2) {
                MagiData data { std::stod(tokens[1]) };
				// Convert model name to lowercase and remove leading '*' character if it exists
				const auto modelName = wingman::util::stringLower(tokens[0].substr(tokens[0].find_first_not_of('*')));
				leaderboard[modelName].second = data;
            }
        }
    }

    return leaderboard;
}

// int main()
// {
//     std::string line, input;

//     // Read lines from stdin until EOF
//     while (getline(std::cin, line)) {
//         input += line + '\n';
//     }

//     // Parse the accumulated input data
//     auto leaderboard = parseLeaderboardData(input);

//     // Output header row for the CSV
//     std::cout << "Model,EQBench Score,EQBench Params,Magi Score\n";

//     // Print parsed data in CSV format
//     for (const auto& entry : leaderboard) {
//         std::cout << entry.first << "," // Model
//                   << entry.second.first.score << "," // EQBench Score
//                   << entry.second.first.params << "," // EQBench Params
//                   << entry.second.second.score << "\n"; // Magi Score
//     }

//     return 0;
// }
