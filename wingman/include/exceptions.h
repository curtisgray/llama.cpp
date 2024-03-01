
#pragma once
namespace wingman {
	class SilentException : public std::exception {
	public:
		SilentException() noexcept
		{}

		virtual const char *what() const noexcept
		{
			return "";
		}
	};
} // namespace wingman
