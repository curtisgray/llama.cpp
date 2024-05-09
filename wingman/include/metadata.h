#pragma once
#include <optional>
#include <string>
#include <vector>
#include "json.hpp"
#include "orm.h"

namespace wingman {
	struct ChatTemplate {
		std::string alias;
		std::string name; // This is the proper name for displaying in a user interface
		std::string description;

		ChatTemplate(const std::string &alias, const std::string &name, const std::string &description)
			: alias(alias), name(name), description(description) {}
		ChatTemplate() = default;
		ChatTemplate(const ChatTemplate &) = default;
		ChatTemplate(ChatTemplate &&) = default;
		ChatTemplate &operator=(const ChatTemplate &) = default;
		ChatTemplate &operator=(ChatTemplate &&) = default;
	};
	NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ChatTemplate, alias, name, description)

	std::vector<ChatTemplate> GetSupportedChatTemplates();
	std::optional<ChatTemplate> ExtractChatTemplate(const std::string &modelFilePath);
	std::optional<nlohmann::json> ExtractModelMetadata(const std::string &modelFilePath);
	std::optional<ChatTemplate> GetChatTemplate(const std::string &modelRepo, const std::string &filePath,
		orm::ItemActionsFactory &actionsFactory);
	std::optional<nlohmann::json> GetModelMetadata(const std::string &modelRepo, const std::string &filePath,
		orm::ItemActionsFactory &actionsFactory);
	ChatTemplate ParseChatTemplate(const std::string &tmpl);
	std::optional<nlohmann::json> GetModelInfo(const std::string &modelRepo, const std::string &filePath, orm::ItemActionsFactory &actionsFactory);
} // namespace wingman
