#pragma once

class owned_cstrings {
	std::vector<char *> cstring_array;
public:
	explicit owned_cstrings(const std::vector<std::string> &source) :
		cstring_array(source.size())
	{
		std::transform(source.begin(), source.end(), cstring_array.begin(), [](const auto &elem_str) {
			char *buffer = new char[elem_str.size() + 1];
			std::copy(elem_str.begin(), elem_str.end(), buffer);
			buffer[elem_str.size()] = 0;
			return buffer;
		});
		cstring_array.push_back(nullptr);
	}

	owned_cstrings(const owned_cstrings &other) = delete;
	owned_cstrings &operator=(const owned_cstrings &other) = delete;

	owned_cstrings(owned_cstrings &&other) = default;
	owned_cstrings &operator=(owned_cstrings &&other) = default;

	char **data()
	{
		return cstring_array.data();
	}

	[[nodiscard]] size_t size() const
	{
		return cstring_array.size();
	}

	~owned_cstrings()
	{
		for (const char *elem : cstring_array) {
			delete[] elem;
		}
	}
};
