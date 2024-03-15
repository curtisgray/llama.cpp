
#pragma once
namespace wingman {
	class SilentException : public std::exception {
	public:
		SilentException() noexcept
		{}

		const char *what() const noexcept override
		{
			return "";
		}
	};
	class ModelLoadingException final : public std::exception {
	public:
		ModelLoadingException() noexcept
		{}

		const char *what() const noexcept override
		{
			return "Wingman exited with error code 1024. There was an error loading the model.";
		}
	};
} // namespace wingman
