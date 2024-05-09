#pragma once
#include <regex>
#include <string>
#include <chrono>
#include <optional>
#include <fmt/core.h>
#include "mimetypes.h"

namespace wingman::util {
	template<typename T>
	std::ostream &operator<<(std::ostream &os, std::optional<T> o)
	{
		if (o) {
			return os << o;
		}
		return os << "<nullopt>";
	}

	struct ci_less {
		// case-independent (ci) compare_less binary function
		struct nocase_compare {
			bool operator() (const unsigned char &c1, const unsigned char &c2) const
			{
				return tolower(c1) < tolower(c2);
			}
		};
		bool operator() (const std::string &s1, const std::string &s2) const
		{
			return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end(), nocase_compare());
		}
	};

#pragma region String Utilities
	inline std::vector<std::string> splitString(const std::string &input, const char delimiter = ',')
	{
		std::vector<std::string> result;
		std::string s;
		size_t start = 0;
		size_t end = input.find(delimiter);

		while (end != std::string::npos) {
			s = input.substr(start, end - start);
			result.push_back(s);
			start = end + 1;
			end = input.find(delimiter, start);
		}

		s = input.substr(start);
		result.push_back(s);

		return result;
	}

	inline std::string joinString(const std::vector<std::string> &input, const std::string &delimeter = ", ")
	{
		std::string result;
		for (const auto &s : input) {
			result.append(s);
			result.append(delimeter);
		}
		if (!result.empty()) {
			result.erase(result.size() - delimeter.size());
		}
		return result;
	}

	inline bool stringCompare(const std::string &first, const std::string &second, const bool caseSensitive = true)
	{
		if (first.length() != second.length())
			return false;

		for (size_t i = 0; i < first.length(); ++i) {
			char a = first[i];
			char b = second[i];

			if (!caseSensitive) {
				a = tolower(a);
				b = tolower(b);
			}

			if (a != b)
				return false;
		}

		return true;
	}

	inline bool regexSearch(const std::string &str, const std::string &pattern, const bool caseSensitive = true)
	{
		std::regex rx;

		if (caseSensitive) {
			rx = std::regex{ pattern };
		} else {
			rx = std::regex{ pattern, std::regex_constants::icase };
		}

		return std::regex_search(str, rx);
	}

	inline bool stringContains(const std::string &haystack, const std::string &needle, const bool caseSensitive = true)
	{
		return regexSearch(haystack, needle, caseSensitive);
	}

	inline bool stringStartsWith(const std::string &str, const std::string &prefix, const bool caseSensitive = true)
	{
		if (str.length() < prefix.length())
			return false;

		return stringCompare(str.substr(0, prefix.length()), prefix, caseSensitive);
	}

	inline bool stringEndsWith(const std::string &str, const std::string &suffix, const bool caseSensitive = true)
	{
		if (str.length() < suffix.length())
			return false;

		return stringCompare(str.substr(str.length() - suffix.length()), suffix, caseSensitive);
	}

	inline std::string stringLower(const std::string &str)
	{
		std::string result(str);
		for (char &c : result) {
			c = std::tolower(c);
		}
		return result;
	}

	inline std::string stringUpper(const std::string &str)
	{
		std::string result(str);
		for (char &c : result) {
			c = std::toupper(c);
		}
		return result;
	}

	inline size_t stringIndexOf(const std::string &str, const std::string &search, const bool caseSensitive = true)
	{
		std::string s = str;
		std::string needle = search;

		if (!caseSensitive) {
			s = stringLower(s);
			needle = stringLower(needle);
		}

		return s.find(needle);
	}

	inline size_t stringLastIndexOf(const std::string &str, const std::string &search, const bool caseSensitive = true)
	{
		std::string s = str;
		std::string needle = search;

		if (!caseSensitive) {
			s = stringLower(s);
			needle = stringLower(needle);
		}

		return s.rfind(needle);
	}

	// stringTrim from left
	inline std::string &stringLeftTrim(std::string &s, const char *t = " \t\n\r\f\v")
	{
		s.erase(0, s.find_first_not_of(t));
		return s;
	}

	// stringTrim from right
	inline std::string &stringRightTrim(std::string &s, const char *t = " \t\n\r\f\v")
	{
		s.erase(s.find_last_not_of(t) + 1);
		return s;
	}

	// stringTrim from left & right
	inline std::string &stringTrim(std::string &s, const char *t = " \t\n\r\f\v")
	{
		return stringLeftTrim(stringRightTrim(s, t), t);
	}

	// copying versions

	inline std::string stringLeftTrimCopy(std::string s, const char *t = " \t\n\r\f\v")
	{
		return stringLeftTrim(s, t);
	}

	inline std::string stringRightTrimCopy(std::string s, const char *t = " \t\n\r\f\v")
	{
		return stringRightTrim(s, t);
	}

	inline std::string stringTrimCopy(std::string s, const char *t = " \t\n\r\f\v")
	{
		return stringTrim(s, t);
	}
#pragma endregion

#pragma region Time Utilities

	inline long long nowInMilliseconds()
	{
		return std::chrono::duration_cast<std::chrono::milliseconds>(
			   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	inline long long nowInSeconds()
	{
		return std::chrono::duration_cast<std::chrono::seconds>(
				   std::chrono::system_clock::now().time_since_epoch())
			.count();
	}

	inline std::time_t now()
	{
		return std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	}

#pragma endregion

	inline std::string prettyBytes(const long long bytes)
	{
		if (bytes < 1024)
			return fmt::format("{} B", bytes);

		const char *suffixes[9];
		suffixes[0] = "B";
		suffixes[1] = "KB";
		suffixes[2] = "MB";
		suffixes[3] = "GB";
		suffixes[4] = "TB";
		suffixes[5] = "PB";
		suffixes[6] = "EB";
		suffixes[7] = "ZB";
		suffixes[8] = "YB";
		unsigned long long s = 0; // which suffix to use
		auto count = static_cast<double>(bytes);
		while (count >= 1024.0 && s < std::size(suffixes) - 1) {
			s++;
			count /= 1024.0;
		}
		return fmt::format("{:.1f} {}", count, suffixes[s]);
	}

	inline std::string calculateDownloadSpeed(const std::time_t start, const long long totalBytes)
	{
		const auto elapsedSeconds = now() - start;
		if (elapsedSeconds <= 0 || totalBytes <= 0)
			return "0 B/s";
		const auto bytesPerSecond = totalBytes / elapsedSeconds;
		return prettyBytes(bytesPerSecond) + "/s";
	}

	inline std::string extractQuantizationFromFilename(const std::string &fileName)
	{
		// quantization is the next to last part of the filename
		const auto &p = util::splitString(fileName, '.');
		const auto &quantization = p[p.size() - 2 /*-2 bc p is zero-based*/];

		std::string ret;
		// find the last 'q' or 'fp' in the quantization string case-insensitively
		auto pos = quantization.find_last_of("qQ");
		if (pos == std::string::npos) {
			pos = stringLastIndexOf(quantization, "fp", false);
			if (pos != std::string::npos) {
				ret = stringUpper(quantization.substr(pos));
			} else {
				pos = stringLastIndexOf(quantization, "f16", false);
				if (pos != std::string::npos) {
					ret = stringUpper(quantization.substr(pos));
				} else {
					pos = stringLastIndexOf(quantization, "f32", false);
					if (pos != std::string::npos)
						ret = stringUpper(quantization.substr(pos));
				}
			}
		} else {
			ret = stringUpper(quantization.substr(pos));
		}

		return ret;
	}

	inline std::string quantizationNameFromQuantization(const std::string &quantization)
	{
		std::string quantizationName;
		// parse q. remove 'Q' from the beginning. Replace '_' with '.' if the next character is a number, otherwise replace with ' '.
		bool prefixDone = false;
		std::string prefix;
		for (size_t i = 0; i < quantization.size(); i++) {
			// skip any letters at the beginning
			if (!prefixDone && std::isalpha(quantization[i])) {
				prefix += quantization[i];
				continue;
			}
			prefixDone = true;
			if (quantization[i] == '_') {
				if (i + 1 < quantization.size() && std::isdigit(quantization[i + 1])) {
					quantizationName += '.';
				} else {
					quantizationName += ' ';
				}
			} else {
				quantizationName += quantization[i];
			}
		}
		if (prefix == "F" || prefix == "FP")
			quantizationName = "FP" + quantizationName;

		return quantizationName;
	}

	inline int quantizationToBits(const std::string &string)
	{
		// skip any letters at the beginning and gather numbers until the first non-number character, or the end of the string
		std::string number;
		for (const char c : string) {
			// skip any letters at the beginning
			if (std::isalpha(c)) {
				continue;
			}
			// gather numbers until the first non-number character, or the end of the string
			if (std::isdigit(c)) {
				number += c;
			} else {
				break;
			}
		}
		if (number.empty())
			return 0;
		// convert the number to an integer
		return std::stoi(number);
	}

	inline std::string getContentType(const std::string &path)
	{
		if (path.empty())
			return "text/plain";
		std::string mimeType;
		if (get_mime_type(path.c_str(), mimeType))
			return mimeType;
		return "text/plain";
	}
}
