#pragma once
#include <string>
#include <map>
#include <vector>
#include <memory>
#include <optional>
#include <filesystem>
#include <regex>
#include <spdlog/spdlog.h>
#include <sqlite3.h>
#include "./json.hpp"
#include "./types.h"

namespace wingman {
	namespace fs = std::filesystem;

	static  void initializeColumnIndices(sqlite3 *dbInstance, const char *tableName, std::map<std::string, int> &columnIndices)
	{
		if (dbInstance == nullptr)
			throw std::runtime_error("ItemActions not initialized");

		const std::string sql = "PRAGMA table_info(" + std::string(tableName) + ")";
		sqlite3_stmt *stmt;
		const int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
		if (rc != SQLITE_OK)
			throw std::runtime_error("(initializeColumnIndices) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

		int index = 0;
		while (sqlite3_step(stmt) == SQLITE_ROW) {
			std::string columnName = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
			const int columnIndex = index++;
			columnIndices[columnName] = columnIndex;
		}

		sqlite3_finalize(stmt);

		if (columnIndices.empty())
			throw std::runtime_error(std::format("(initializeColumnIndices) No columns found in table: {}", tableName));
	}

	/**
	* @brief Get the a mapping of parameter names to indices in a prepared statement.
	*
	* @param stmt
	* @return std::map<std::string, int>
	*/
	static std::map<std::string, int> getBindingIndices(sqlite3_stmt *stmt)
	{
		std::map<std::string, int> parameterIndices;

		// Get the number of SQL parameters in the prepared statement
		const int paramCount = sqlite3_bind_parameter_count(stmt);
		for (int i = 1; i <= paramCount; ++i) { // Indexing starts at 1 for parameters
			if (const char *paramName = sqlite3_bind_parameter_name(stmt, i))
				parameterIndices[paramName] = i;
		}

		return parameterIndices;
	}

	/**
	* @brief Get the a mapping of parameter names to in a SQL statement.
	* @details This function prepares the SQL statement and then calls getIndices(sqlite3_stmt* stmt).
	*/
	static std::map<std::string, int> getBindingIndices(sqlite3 *dbInstance, const std::string &sql)
	{
		sqlite3_stmt *stmt;
		const int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
		if (rc != SQLITE_OK)
			throw std::runtime_error("Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

		std::map<std::string, int> parameterIndices = getBindingIndices(stmt);

		sqlite3_finalize(stmt);
		return parameterIndices;
	}

	/**
	* @brief Get the a mapping of parameter names to in a SQL statement.
	* @details This function prepares the SQL statement and then calls getIndices(sqlite3_stmt* stmt).
	* @param columnIndices A map of column names to indices in the table.
	*/
	//static std::string makeSelect(const std::map<std::string, int> &columnIndices)
	//{
	//	std::string sql = "SELECT ";
	//	for (const auto &columnName : columnIndices | std::views::keys)
	//		sql += columnName + ", ";
	//	sql.pop_back();
	//	sql.pop_back();
	//	return sql;
	//}

	static std::map<std::string, int> getStmtColumnIndices(sqlite3_stmt *stmt)
	{
		std::map<std::string, int> indices;
		const int columnCount = sqlite3_column_count(stmt);
		for (int i = 0; i < columnCount; ++i) {
			const char *columnName = sqlite3_column_name(stmt, i);
			indices[columnName] = i;
		}
		return indices;
	}

	class DatabaseActions {
		sqlite3 *dbInstance;

	public:
		DatabaseActions(sqlite3 *dbInstance)
			: dbInstance(dbInstance)
		{}

		TableInfo getTableInfo(const std::string &tableName) const
		{
			if (dbInstance == nullptr)
				throw std::runtime_error("ORM not initialized");

			const std::string sql = "PRAGMA table_info(" + tableName + ");";
			sqlite3_stmt *stmt = nullptr;

			if (sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK)
				throw std::runtime_error("Failed to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

			TableInfo info;
			info.name = tableName;

			while (sqlite3_step(stmt) == SQLITE_ROW) {
				TableColumnInfo colInfo;
				colInfo.cid = sqlite3_column_int(stmt, 0);
				colInfo.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
				colInfo.type = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
				colInfo.notnull = sqlite3_column_int(stmt, 3);
				colInfo.dflt_value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
				colInfo.pk = sqlite3_column_int(stmt, 5);

				info.columns[colInfo.name] = colInfo;
			}

			sqlite3_finalize(stmt);
			return info;
		}

		void createDownloadsTable() const
		{
			if (dbInstance == nullptr) {
				throw std::runtime_error("(createDownloadsTable) Database not initialized: " + std::string(sqlite3_errmsg(dbInstance)));
			}

			const std::string sql = "CREATE TABLE IF NOT EXISTS downloads ("
				"modelRepo TEXT NOT NULL, "
				"filePath TEXT NOT NULL, "
				"status TEXT DEFAULT 'idle' NOT NULL, "
				"totalBytes INTEGER DEFAULT 0 NOT NULL, "
				"downloadedBytes INTEGER DEFAULT 0 NOT NULL, "
				"downloadSpeed TEXT, "
				"progress REAL DEFAULT 0.0 NOT NULL, "
				"error TEXT, "
				"created INTEGER DEFAULT (unixepoch('now')) NOT NULL, "
				"updated INTEGER DEFAULT (unixepoch('now')) NOT NULL, "
				"PRIMARY KEY (modelRepo, filePath)"
				")";

			char *errorMsg = nullptr;
			const int rc   = sqlite3_exec(dbInstance, sql.c_str(), nullptr, nullptr, &errorMsg);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(createDownloadsTable) Exception: " + std::string(sqlite3_errmsg(dbInstance)));
			spdlog::debug("(createDownloadsTable) Downloads table created.");
		}

		void createWingmanTable() const
		{
			if (dbInstance == nullptr)
				throw std::runtime_error("(createWingmanTable)Database not initialized: " + std::string(sqlite3_errmsg(dbInstance)));

			const std::string sql = "CREATE TABLE IF NOT EXISTS wingman ("
				"alias TEXT NOT NULL, "
				"status TEXT DEFAULT 'idle' NOT NULL, "
				"modelRepo TEXT NOT NULL, "
				"filePath TEXT NOT NULL, "
				"force INTEGER DEFAULT 0 NOT NULL, "
				"error TEXT, "
				"created INTEGER DEFAULT (unixepoch('now')) NOT NULL, "
				"updated INTEGER DEFAULT (unixepoch('now')) NOT NULL, "
				"PRIMARY KEY (alias)"
				")";

			char *errorMsg = nullptr;
			const int rc   = sqlite3_exec(dbInstance, sql.c_str(), nullptr, nullptr, &errorMsg);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(createWingmanTable) Exception: " + std::string(sqlite3_errmsg(dbInstance)));
			spdlog::debug("(createWingmanTable) Wingman table created.");
		}

		void createAppTable() const
		{
			if (dbInstance == nullptr)
				throw std::runtime_error("(createAppTable) Database not initialized");

			const std::string sql = "CREATE TABLE IF NOT EXISTS app ("
				"name TEXT NOT NULL, "
				"key TEXT NOT NULL, "
				"value TEXT, "
				"enabled INTEGER DEFAULT 1 NOT NULL, "
				"created INTEGER DEFAULT (unixepoch('now')) NOT NULL, "
				"updated INTEGER DEFAULT (unixepoch('now')) NOT NULL, "
				"PRIMARY KEY (name, key)"
				")";

			char *errorMsg = nullptr;
			const int rc   = sqlite3_exec(dbInstance, sql.c_str(), nullptr, nullptr, &errorMsg);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(createAppTable) Exception: " + std::string(sqlite3_errmsg(dbInstance)));
			spdlog::debug("(createAppTable) App table created.");
		}
	};

	class AppItemActions {
		sqlite3 *dbInstance;
		std::map<std::string, int> selectColumns;

		static std::optional<AppItem> getSome(sqlite3_stmt *stmt)
		{
			if (sqlite3_step(stmt) == SQLITE_ROW) {
				AppItem item;
				std::map<std::string, int> columnIndices = getStmtColumnIndices(stmt);
				if (columnIndices.contains("name"))
					item.name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["name"]));
				if (columnIndices.contains("key"))
					item.key = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["key"]));
				if (columnIndices.contains("value"))
					item.value = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["value"]));
				if (columnIndices.contains("enabled"))
					item.enabled = sqlite3_column_int(stmt, columnIndices["enabled"]);
				if (columnIndices.contains("created"))
					item.created = sqlite3_column_int64(stmt, columnIndices["created"]);
				if (columnIndices.contains("updated"))
					item.updated = sqlite3_column_int64(stmt, columnIndices["updated"]);
				return item;
			}
			return std::nullopt;
		}

	public:
		AppItemActions(sqlite3 *dbInstance)
			: dbInstance(dbInstance)
		{
			initializeColumnIndices(dbInstance, "app", selectColumns);
		}

		std::optional<AppItem> get(const std::string &name, const std::optional<std::string> &key = std::nullopt) const
		{
			if (dbInstance == nullptr)
				throw std::runtime_error("ItemActions not initialized");

			const std::string dbKey = key.has_value() ? key.value() : "default";
			// create a select statement made up of the column indices
			//std::string sql = "SELECT name, key, value, enabled, created, updated FROM app WHERE name = $name AND key = $key";
			const std::string sql = "SELECT * FROM app WHERE name = $name AND key = $key";
			sqlite3_stmt *stmt;

			const int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(read) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

			auto bindingIndices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, bindingIndices["$name"], name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, bindingIndices["$key"], dbKey.c_str(), -1, SQLITE_STATIC);

			auto item = getSome(stmt);

			sqlite3_finalize(stmt);
			return item;
		}

		void set(const AppItem &item)
		{
			if (dbInstance == nullptr) {
				throw std::runtime_error("ItemActions not initialized");
			}
			//std::string sql = "UPDATE app SET value = $value, enabled = $enabled, updated = $updated WHERE name = $name AND key = $key";
			// check if item exists, if not insert, else update
			const auto existingItem = get(item.name, item.key);
			std::string sql;
			bool insert = false;
			if (existingItem.has_value()) {
				//sql = "UPDATE app SET"
				//    " value = $value"
				//    ", enabled = $enabled"
				//    ", updated = $updated"
				//    " WHERE name = $name AND key = $key";
				sql = "UPDATE app SET";
				for (const auto &columnName : selectColumns | std::views::keys) {
					if (columnName == "created") {
						continue;
					}
					sql += " " + columnName + " = $" + columnName + ",";
				}
				sql.pop_back();
				sql += " WHERE name = $name AND key = $key";
			} else {
				insert = true;
				sql = "INSERT INTO app (";
				for (const auto &columnName : selectColumns | std::views::keys) {
					sql += columnName + ",";
				}
				sql.pop_back();
				sql += ") VALUES (";
				for (const auto &columnName : selectColumns | std::views::keys) {
					sql += "$" + columnName + ",";
				}
				sql.pop_back();
				sql += ")";
			}
			sqlite3_stmt *stmt;

			int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK) {
				throw std::runtime_error("(update) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));
			}

			auto indices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, indices["$value"], item.value.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt, indices["$enabled"], item.enabled);
			if (insert) {
				sqlite3_bind_int64(stmt, indices["$created"], item.created);
			}
			sqlite3_bind_int64(stmt, indices["$updated"], item.updated);
			sqlite3_bind_text(stmt, indices["$name"], item.name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, indices["$key"], item.key.c_str(), -1, SQLITE_STATIC);

			rc = sqlite3_step(stmt);
			if (rc != SQLITE_DONE) {
				throw std::runtime_error("(update) Failed to update record: " + std::string(sqlite3_errmsg(dbInstance)));
			}

			sqlite3_finalize(stmt);
		}

		void remove(const std::string &name, const std::string &key) const
		{
			if (dbInstance == nullptr)
				throw std::runtime_error("ItemActions not initialized");

			const std::string sql = "DELETE FROM app WHERE name = $name AND key = $key";
			sqlite3_stmt *stmt;

			int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(remove) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

			auto indices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, indices["$name"], name.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, indices["$key"], key.c_str(), -1, SQLITE_STATIC);

			rc = sqlite3_step(stmt);
			if (rc != SQLITE_DONE)
				throw std::runtime_error("(remove) Failed to delete record: " + std::string(sqlite3_errmsg(dbInstance)));

			sqlite3_finalize(stmt);
		}

		static nlohmann::json toJson(const AppItem &item)
		{
			nlohmann::json j;
			j["name"] = item.name;
			j["key"] = item.key;
			j["value"] = item.value;
			j["enabled"] = item.enabled;
			j["created"] = item.created;
			j["updated"] = item.updated;

			return j;
		}

		static AppItem fromJson(const nlohmann::json &j)
		{
			AppItem item;
			item.name = j["name"];
			item.key = j["key"];
			item.value = j["value"];
			item.enabled = j["enabled"];
			item.created = j["created"];
			item.updated = j["updated"];

			return item;
		}
	};

	class DownloadItemActions {
	private:
		sqlite3 *dbInstance;
		std::map<std::string, int> columnIndices;
		inline static fs::path downloadsDirectory;

		std::vector<DownloadItemStatus> activeDownloadStatuses = { DownloadItemStatus::queued, DownloadItemStatus::downloading };

		//std::vector<DownloadItem> getSome(sqlite3_stmt *stmt)
		//{
		//	std::vector<DownloadItem> items;

		//	while (sqlite3_step(stmt) == SQLITE_ROW) {
		//		DownloadItem item;
		//		item.modelRepo = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["modelRepo"]));
		//		item.filePath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["filePath"]));
		//		item.status = DownloadItem::toStatus(reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["status"])));
		//		item.totalBytes = sqlite3_column_int(stmt, columnIndices["totalBytes"]);
		//		item.downloadedBytes = sqlite3_column_int(stmt, columnIndices["downloadedBytes"]);
		//		item.downloadSpeed = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["downloadSpeed"]));
		//		item.progress = sqlite3_column_double(stmt, columnIndices["progress"]);
		//		item.error = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["error"]));
		//		item.created = sqlite3_column_int64(stmt, columnIndices["created"]);
		//		item.updated = sqlite3_column_int64(stmt, columnIndices["updated"]);
		//		items.push_back(item);
		//	}

		//	return items;
		//}
		// rewrite above function to match the design of getSome in AppItem
		std::vector<DownloadItem> getSome(sqlite3_stmt *stmt)
		{
			std::vector<DownloadItem> items;
			std::map<std::string, int> indices = getStmtColumnIndices(stmt);
			while (sqlite3_step(stmt) == SQLITE_ROW) {
				DownloadItem item;
				if (indices.contains("modelRepo"))
					item.modelRepo = reinterpret_cast<const char *>(sqlite3_column_text(stmt, indices["modelRepo"]));
				if (indices.contains("filePath"))
					item.filePath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, indices["filePath"]));
				if (indices.contains("status"))
					item.status = DownloadItem::toStatus(reinterpret_cast<const char *>(sqlite3_column_text(stmt, indices["status"])));
				if (indices.contains("totalBytes"))
					item.totalBytes = sqlite3_column_int(stmt, indices["totalBytes"]);
				if (indices.contains("downloadedBytes"))
					item.downloadedBytes = sqlite3_column_int(stmt, indices["downloadedBytes"]);
				if (indices.contains("downloadSpeed"))
					item.downloadSpeed = reinterpret_cast<const char *>(sqlite3_column_text(stmt, indices["downloadSpeed"]));
				if (indices.contains("progress"))
					item.progress = sqlite3_column_double(stmt, indices["progress"]);
				if (indices.contains("error"))
					item.error = reinterpret_cast<const char *>(sqlite3_column_text(stmt, indices["error"]));
				if (indices.contains("created"))
					item.created = sqlite3_column_int64(stmt, indices["created"]);
				if (indices.contains("updated"))
					item.updated = sqlite3_column_int64(stmt, indices["updated"]);
				items.push_back(item);
			}

			return items;
		}

	public:
		DownloadItemActions(sqlite3 *dbInstance, const fs::path &downloadsDirectory)
			: dbInstance(dbInstance)
		{
			initializeColumnIndices(dbInstance, "downloads", columnIndices);
			DownloadItemActions::downloadsDirectory = downloadsDirectory;
			fs::create_directories(downloadsDirectory);
			initializeColumnIndices(dbInstance, "downloads", columnIndices);
		}

		std::optional<DownloadItem> get(const std::string &modelRepo, const std::string &filePath)
		{
			if (dbInstance == nullptr) {
				throw std::runtime_error("ItemActions not initialized");
			}

			std::string sql = "SELECT * FROM downloads WHERE modelRepo = $modelRepo AND filePath = $filePath";
			//auto sql = makeSelect(columnIndices);
			//sql += " FROM downloads WHERE modelRepo = $modelRepo AND filePath = $filePath";
			sqlite3_stmt *stmt;

			int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK) {
				throw std::runtime_error("(read) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));
			}

			auto indices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, indices["$modelRepo"], modelRepo.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, indices["$filePath"], filePath.c_str(), -1, SQLITE_STATIC);

			auto items = getSome(stmt);
			sqlite3_finalize(stmt);

			if (!items.empty())
				return items[0];
			return std::nullopt;
		}

		std::vector<DownloadItem> getAll()
		{
			if (dbInstance == nullptr) {
				throw std::runtime_error("ItemActions not initialized");
			}

			std::vector<DownloadItem> items;
			const std::string sql = "SELECT * FROM downloads";
			//auto sql = makeSelect(columnIndices);
			//sql += " FROM downloads";
			sqlite3_stmt *stmt;

			const int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK) {
				throw std::runtime_error("(getAll) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));
			}
			auto some = getSome(stmt);
			sqlite3_finalize(stmt);
			return some;
		}

		std::vector<DownloadItem> getAllByStatus(DownloadItemStatus status)
		{
			if (dbInstance == nullptr) {
				throw std::runtime_error("ItemActions not initialized");
			}

			std::vector<DownloadItem> items;
			const std::string sql = "SELECT * FROM downloads WHERE status = $status";
			sqlite3_stmt *stmt;

			const int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK) {
				throw std::runtime_error("(getAllByStatus) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));
			}

			auto indices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, indices["$status"], DownloadItem::toString(status).c_str(), -1, SQLITE_STATIC);

			auto item = getSome(stmt);
			sqlite3_finalize(stmt);
			return item;
		}

		// a function that returns the next queued item by oldest created date
		std::optional<DownloadItem> getNextQueued()
		{
			if (dbInstance == nullptr)
				throw std::runtime_error("ItemActions not initialized");

			std::vector<DownloadItem> items;
			const std::string sql = "SELECT * FROM downloads WHERE status = 'queued' ORDER BY created ASC LIMIT 1";
			sqlite3_stmt *stmt;

			const int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(getNextDownloadItem) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

			auto item = getSome(stmt);
			sqlite3_finalize(stmt);
			if (!item.empty()) {
				return item[0];
			}

			return std::nullopt;
		}

		void set(const DownloadItem &item)
		{
			if (dbInstance == nullptr) {
				throw std::runtime_error("ItemActions not initialized");
			}

			// check if item exists, if not insert, else update
			auto existingItem = get(item.modelRepo, item.filePath);
			std::string sql;
			bool insert = false;
			if (existingItem.has_value()) {
				//sql = "UPDATE downloads SET"
				//    " status = $status"
				//    ", totalBytes = $totalBytes"
				//    ", downloadedBytes = $downloadBytes"
				//    ", downloadSpeed = $downloadSpeed"
				//    ", progress = $progress"
				//    ", error = $error"
				//    ", updated = $updated"
				//    " WHERE modelRepo = $modelRepo AND filePath = $filePath";
				sql = "UPDATE downloads SET";
				for (const auto &columnName : columnIndices | std::views::keys) {
					if (columnName == "created") {
						continue;
					}
					sql += " " + columnName + " = $" + columnName + ",";
				}
				sql.pop_back();
				sql += " WHERE modelRepo = $modelRepo AND filePath = $filePath";
			} else {
				insert = true;
				sql = "INSERT INTO downloads (";
				for (const auto &columnName : columnIndices | std::views::keys) {
					sql += columnName + ",";
				}
				sql.pop_back();
				sql += ") VALUES (";
				for (const auto &columnName : columnIndices | std::views::keys) {
					sql += "$" + columnName + ",";
				}
				sql.pop_back();
				sql += ")";
			}
			sqlite3_stmt *stmt;

			int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(update) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

			auto indices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, indices["$status"], DownloadItem::toString(item.status).c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt, indices["$totalBytes"], item.totalBytes);
			sqlite3_bind_int(stmt, indices["$downloadBytes"], item.downloadedBytes);
			sqlite3_bind_text(stmt, indices["$downloadSpeed"], item.downloadSpeed.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_double(stmt, indices["$progress"], item.progress);
			sqlite3_bind_text(stmt, indices["$error"], item.error.c_str(), -1, SQLITE_STATIC);
			if (insert) {
				sqlite3_bind_int64(stmt, indices["$created"], item.created);
			}
			sqlite3_bind_int64(stmt, indices["$updated"], item.updated);
			sqlite3_bind_text(stmt, indices["$modelRepo"], item.modelRepo.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, indices["$filePath"], item.filePath.c_str(), -1, SQLITE_STATIC);

			rc = sqlite3_step(stmt);
			if (rc != SQLITE_DONE)
				throw std::runtime_error("(update) Failed to update record: " + std::string(sqlite3_errmsg(dbInstance)));

			sqlite3_finalize(stmt);
		}

		void remove(const std::string &modelRepo, const std::string &filePath) const
		{
			if (dbInstance == nullptr)
				throw std::runtime_error("ItemActions not initialized");

			const std::string sql = "DELETE FROM downloads WHERE modelRepo = $modelRepo AND filePath = $filePath";
			sqlite3_stmt *stmt;

			int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(remove) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

			auto indices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, indices["$modelRepo"], modelRepo.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, indices["$filePath"], filePath.c_str(), -1, SQLITE_STATIC);

			rc = sqlite3_step(stmt);
			if (rc != SQLITE_DONE) {
				throw std::runtime_error("(remove) Failed to delete record: " + std::string(sqlite3_errmsg(dbInstance)));
			}

			sqlite3_finalize(stmt);
		}

		static nlohmann::json toJson(const DownloadItem &item)
		{
			nlohmann::json j;
			j["modelRepo"] = item.modelRepo;
			j["filePath"] = item.filePath;
			j["status"] = DownloadItem::toString(item.status);
			j["totalBytes"] = item.totalBytes;
			j["downloadedBytes"] = item.downloadedBytes;
			j["downloadSpeed"] = item.downloadSpeed;
			j["progress"] = item.progress;
			j["error"] = item.error;
			j["created"] = item.created;
			j["updated"] = item.updated;

			return j;
		}

		static DownloadItem fromJson(const nlohmann::json &j)
		{
			DownloadItem item;
			item.modelRepo = j["modelRepo"];
			item.filePath = j["filePath"];
			item.status = DownloadItem::toStatus(j["status"]);
			item.totalBytes = j["totalBytes"];
			item.downloadedBytes = j["downloadedBytes"];
			item.downloadSpeed = j["downloadSpeed"];
			item.progress = j["progress"];
			item.error = j["error"];
			item.created = j["created"];
			item.updated = j["updated"];

			return item;
		}

		void reset() const
		{
			if (dbInstance == nullptr)
				throw std::runtime_error("ORM not initialized");

			spdlog::debug("(reset) Resetting downloads table");

			// mark all idle,downloading and error items as queued
			// delete other items

			std::string sql = "UPDATE downloads SET status = 'queued', progress = 0, downloadedBytes = 0, totalBytes = 0, downloadSpeed = '' WHERE status = 'downloading' OR status = 'error' or status = 'idle'";

			char *errorMsg = nullptr;
			int rc = sqlite3_exec(dbInstance, sql.c_str(), nullptr, nullptr, &errorMsg);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(reset) Exception: " + std::string(sqlite3_errmsg(dbInstance)));

			sql = "DELETE FROM downloads WHERE status = 'complete' OR status = 'cancelled' OR status = 'unknown'";
			errorMsg = nullptr;
			rc = sqlite3_exec(dbInstance, sql.c_str(), nullptr, nullptr, &errorMsg);
			if (rc != SQLITE_OK) {
				throw std::runtime_error("(reset) Exception: " + std::string(sqlite3_errmsg(dbInstance)));
			}
			spdlog::debug("(reset) Downloads table reset.");
		}

		static std::string getDownloadItemFileName(const std::string &modelRepo, const std::string &filePath)
		{
			return safeDownloadItemName(modelRepo, filePath);
		}

		static bool isDownloaded(const std::string &modelRepo, const std::string &filePath);

		static DownloadedFileInfo getDownloadedFileInfo(const std::string &modelRepo, const std::string &filePath);

		static std::vector<std::string> getModelFiles()
		{
			std::vector<std::string> files;

			for (const auto &entry : std::filesystem::directory_iterator(downloadsDirectory)) {
				if (entry.is_regular_file()) {
					files.push_back(entry.path().filename().string());
				}
			}

			return files;
		}

		static std::vector<DownloadedFileInfo> getDownloadedFileInfos()
		{
			std::vector<DownloadedFileInfo> fileInfos;
			auto modelFiles = getModelFiles();

			for (const auto &file : modelFiles) {
				auto name = safeDownloadItemNameToModelRepo(file);
				if (!name) {
					spdlog::debug("Skipping file: " + file + " because it's not a downloaded model file.");
					continue;
				}
				fileInfos.push_back(getDownloadedFileInfo(name->modelRepo, name->filePath));
			}

			return fileInfos;
		}

		static std::string safeDownloadItemName(const std::string &modelRepo, const std::string &filePath)
		{
			std::regex slashRegex("\\/");
			std::string result = std::regex_replace(modelRepo, slashRegex, "[-]");
			return result + "[=]" + filePath;
		}

		static std::optional<DownloadItemName> safeDownloadItemNameToModelRepo(const std::string &name)
		{
			if (name.find("[-]") == std::string::npos || name.find("[=]") == std::string::npos) {
				return {};
			}

			size_t pos = name.find("[=]");
			std::string modelRepoPart = name.substr(0, pos);
			std::string filePathPart = name.substr(pos + 3);

			std::regex dashRegex("\\[-\\]");
			modelRepoPart = std::regex_replace(modelRepoPart, dashRegex, "/");

			return { DownloadItemName { modelRepoPart, filePathPart } }; // Return the struct and true flag indicating success.
		}

		static std::string getDownloadItemFilePath(const std::string &modelRepo, const std::string &filePath)
		{
			std::filesystem::path path = downloadsDirectory / safeDownloadItemName(modelRepo, filePath);
			return path.string();
		}
	};

	class WingmanItemActions {
	private:
		sqlite3 *dbInstance;
		std::map<std::string, int> columnIndices;
		fs::path modelsDir;

		std::vector<WingmanItem> getSome(sqlite3_stmt *stmt)
		{
			std::vector<WingmanItem> items;

			while (sqlite3_step(stmt) == SQLITE_ROW) {
				WingmanItem item;
				item.alias = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["alias"]));
				item.status = WingmanItem::toStatus(reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["status"])));
				item.modelRepo = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["modelRepo"]));
				item.filePath = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["filePath"]));
				item.force = sqlite3_column_int(stmt, columnIndices["force"]);
				item.error = reinterpret_cast<const char *>(sqlite3_column_text(stmt, columnIndices["error"]));
				item.created = sqlite3_column_int64(stmt, columnIndices["created"]);
				item.updated = sqlite3_column_int64(stmt, columnIndices["updated"]);
				items.push_back(item);
			}

			return items;
		}

	public:
		WingmanItemActions(sqlite3 *dbInstance, fs::path modelsDir)
			: dbInstance(dbInstance)
			, modelsDir(modelsDir)
		{
			initializeColumnIndices(dbInstance, "wingman", columnIndices);
		}

		std::optional<WingmanItem> get(const std::string &alias)
		{
			if (dbInstance == nullptr)
				throw std::runtime_error("ItemActions not initialized");

			const std::string sql = "SELECT * FROM wingman WHERE alias = $alias";
			sqlite3_stmt *stmt;

			const int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(read) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

			auto indices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, indices["$alias"], alias.c_str(), -1, SQLITE_STATIC);

			auto items = getSome(stmt);

			sqlite3_finalize(stmt);

			if (!items.empty())
				return items[0];
			return std::nullopt;
		}

		void set(const WingmanItem &item)
		{
			if (dbInstance == nullptr) {
				throw std::runtime_error("ItemActions not initialized");
			}

			//std::string sql = "UPDATE wingman SET status = $status, modelRepo = $modelRepo, filePath = $filePath, force = $force, error = $error, updated = $updated "
			//	"WHERE alias = $alias";
			// check if item exists, if not insert, else update
			auto existingItem = get(item.alias);
			std::string sql;
			bool insert = false;
			if (existingItem.has_value()) {
				//sql = "UPDATE wingman SET"
				//    " status = $status"
				//    ", modelRepo = $modelRepo"
				//    ", filePath = $filePath"
				//    ", force = $force"
				//    ", error = $error"
				//    ", updated = $updated"
				//    " WHERE alias = $alias";
				sql = "UPDATE wingman SET";
				for (const auto &columnName : columnIndices | std::views::keys) {
					if (columnName == "created") {
						continue;
					}
					sql += " " + columnName + " = $" + columnName + ",";
				}
				sql.pop_back();
				sql += " WHERE alias = $alias";
			} else {
				insert = true;
				sql = "INSERT INTO wingman (";
				for (const auto &columnName : columnIndices | std::views::keys) {
					sql += columnName + ",";
				}
				sql.pop_back();
				sql += ") VALUES (";
				for (const auto &columnName : columnIndices | std::views::keys) {
					sql += "$" + columnName + ",";
				}
				sql.pop_back();
				sql += ")";
			}
			sqlite3_stmt *stmt;

			int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK) {
				throw std::runtime_error("(update) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));
			}

			auto indices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, indices["$status"], WingmanItem::toString(item.status).c_str(), -1, nullptr);
			sqlite3_bind_text(stmt, indices["$modelRepo"], item.modelRepo.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_text(stmt, indices["$filePath"], item.filePath.c_str(), -1, SQLITE_STATIC);
			sqlite3_bind_int(stmt, indices["$force"], item.force);
			sqlite3_bind_text(stmt, indices["$error"], item.error.c_str(), -1, SQLITE_STATIC);
			if (insert)
				sqlite3_bind_int64(stmt, indices["$created"], item.created);
			sqlite3_bind_int64(stmt, indices["$updated"], item.updated);
			sqlite3_bind_text(stmt, indices["$alias"], item.alias.c_str(), -1, SQLITE_STATIC);

			rc = sqlite3_step(stmt);
			if (rc != SQLITE_DONE)
				throw std::runtime_error("(update) Failed to update record: " + std::string(sqlite3_errmsg(dbInstance)));

			sqlite3_finalize(stmt);
		}

		void remove(const std::string &alias) const
		{
			if (dbInstance == nullptr) {
				throw std::runtime_error("ItemActions not initialized");
			}

			const std::string sql = "DELETE FROM wingman WHERE alias = $alias";
			sqlite3_stmt *stmt;

			int rc = sqlite3_prepare_v2(dbInstance, sql.c_str(), -1, &stmt, nullptr);
			if (rc != SQLITE_OK)
				throw std::runtime_error("(remove) Unable to prepare statement: " + std::string(sqlite3_errmsg(dbInstance)));

			auto indices = getBindingIndices(stmt);
			sqlite3_bind_text(stmt, indices["$alias"], alias.c_str(), -1, SQLITE_STATIC);

			rc = sqlite3_step(stmt);
			if (rc != SQLITE_DONE)
				throw std::runtime_error("(remove) Failed to delete record: " + std::string(sqlite3_errmsg(dbInstance)));

			sqlite3_finalize(stmt);
		}

		static nlohmann::json toJson(const WingmanItem &item)
		{
			nlohmann::json j;
			j["alias"] = item.alias;
			j["status"] = WingmanItem::toString(item.status);
			j["modelRepo"] = item.modelRepo;
			j["filePath"] = item.filePath;
			j["force"] = item.force;
			j["error"] = item.error;
			j["created"] = item.created;
			j["updated"] = item.updated;

			return j;
		}

		static WingmanItem fromJson(const nlohmann::json &j)
		{
			WingmanItem item;
			item.alias = j["alias"];
			item.status = WingmanItem::toStatus(j["status"]);
			item.modelRepo = j["modelRepo"];
			item.filePath = j["filePath"];
			item.force = j["force"];
			item.error = j["error"];
			item.created = j["created"];
			item.updated = j["updated"];

			return item;
		}
	};

	class ItemActionsFactory {
		sqlite3 *db;
		fs::path wingmanHome;
		fs::path dataDir;
		fs::path modelsDir;
		fs::path dbPath;

		const std::string SERVER_NAME = "orm.Sqlite";
		bool initialized;

		void openDatabase()
		{
			spdlog::debug("(openDatabase) Opening database {}...", dbPath.string());
			if (db != nullptr) {
				throw std::runtime_error("(openDatabase) Database is already opened.");
			}
			int rc = sqlite3_open(dbPath.string().c_str(), &db);
			if (rc != SQLITE_OK) {
				throw std::runtime_error("(openDatabase) Failed to open database: " + std::string(sqlite3_errmsg(db)));
			}
			spdlog::debug("(openDatabase) Database opened.");
		}

		void initializeDatabase()
		{
			spdlog::debug("(initializeDatabase) Initializing database...");

			if (initialized) {
				throw std::runtime_error("(initializeDatabase) ORM already initialized: " + std::string(sqlite3_errmsg(db)));
			}

			spdlog::debug("(initializeDatabase) DATA_DIR: {}", dataDir.string());

			// Ensure the directory exists
			spdlog::debug("(initializeDatabase) Ensuring DATA_DIR '{}' exists...", dataDir.string());
			fs::create_directories(dataDir);
			spdlog::trace("(initializeDatabase) DATA_DIR exists...");
			spdlog::debug("(initializeDatabase) Ensuring MODELS_DIR '{}' exists...", modelsDir.string());
			fs::create_directories(modelsDir);
			spdlog::trace("(initializeDatabase) MODELS_DIR exists...");

			openDatabase();

			initialized = true;
		}

	public:
		std::shared_ptr<AppItemActions> pAppItemActions;
		std::shared_ptr<DownloadItemActions> pDownloadItemItemActions;
		std::shared_ptr<WingmanItemActions> pWingmanItemItemActions;

		/**
		* @brief Construct a new Item Actions Factory object
		*
		* @param baseDirectory - The base directory for the application. If not provided, `$HOME/.wingman` is used.
		*
		*/
		ItemActionsFactory(std::optional<const fs::path> baseDirectory = std::nullopt)
			: initialized(false)
			, db(nullptr)
		{
			fs::path baseDir = fs::path(baseDirectory.value_or(get_wingman_home()));
			wingmanHome = baseDir;
			dataDir = wingmanHome / "data";
			modelsDir = wingmanHome / "models";
			dbPath = wingmanHome / dataDir / "wingman.db";

			// spdlog levels:
			// trace = SPDLOG_LEVEL_TRACE,
			// debug = SPDLOG_LEVEL_DEBUG,
			// info = SPDLOG_LEVEL_INFO,
			// warn = SPDLOG_LEVEL_WARN,
			// err = SPDLOG_LEVEL_ERROR,
			// critical = SPDLOG_LEVEL_CRITICAL,
			// off = SPDLOG_LEVEL_OFF,
			// For convenience, spdlog creates a default global logger (to stdout, colored and multithreaded).
			//  It can be used easily by calling spdlog::info(..), spdlog::debug(..), etc directly.
			//  It's instance can be replaced to any other logger (shared_ptr):
			//   spdlog::set_default_logger(some_other_logger);
			spdlog::info("Starting ItemActions...");
			initializeDatabase();

			auto dbActions = DatabaseActions(db);

			dbActions.createDownloadsTable();
			dbActions.createWingmanTable();
			dbActions.createAppTable();

			pAppItemActions = std::make_shared<AppItemActions>(db);
			pDownloadItemItemActions = std::make_shared<DownloadItemActions>(db, modelsDir);
			pWingmanItemItemActions = std::make_shared<WingmanItemActions>(db, modelsDir);
		}

		~ItemActionsFactory()
		{
			if (db != nullptr) {
				sqlite3_close(db);
				db = nullptr;
			}
			pAppItemActions = nullptr;
			pDownloadItemItemActions = nullptr;
			pWingmanItemItemActions = nullptr;
		}

		std::shared_ptr <AppItemActions> app()
		{
			return pAppItemActions;
		}

		std::shared_ptr <DownloadItemActions> download()
		{
			return pDownloadItemItemActions;
		}

		std::shared_ptr <WingmanItemActions> wingman()
		{
			return pWingmanItemItemActions;
		}
	};

	inline bool DownloadItemActions::isDownloaded(const std::string &modelRepo, const std::string &filePath)
	{
		ItemActionsFactory ormFactory;
		auto item = ormFactory.download()->get(modelRepo, filePath);

		// If it's in the database and marked as complete, then it's downloaded.
		if (item.has_value() && item->status == DownloadItemStatus::complete) {
			return true;
		}

		// If it's not in the database, we check the file system.
		return std::filesystem::exists(downloadsDirectory / filePath);
	}

	inline DownloadedFileInfo DownloadItemActions::getDownloadedFileInfo(const std::string &modelRepo, const std::string &filePath)
	{
		ItemActionsFactory itemActionsFactory;
		auto item = itemActionsFactory.download()->get(modelRepo, filePath);

		DownloadedFileInfo fileInfo;
		fileInfo.filePath = filePath;
		fileInfo.modelRepo = modelRepo;

		// If the item is in the database, fetch its information.
		if (item.has_value()) {
			fileInfo.totalBytes = item->totalBytes;
			fileInfo.downloadedBytes = item->downloadedBytes;
		}

		auto safeFileName = safeDownloadItemName(modelRepo, filePath);
		auto path = downloadsDirectory / filePath;
		// Getting the size directly from the file system.
		fileInfo.fileSizeOnDisk = std::filesystem::file_size(downloadsDirectory / fs::path(filePath));

		return fileInfo;
	}

	inline nlohmann::json DownloadServerAppItem::toJson(const DownloadServerAppItem &downloadServerAppItem)
	{
		nlohmann::json j;
		j["isa"] = downloadServerAppItem.isa;
		j["status"] = toString(downloadServerAppItem.status);
		if (downloadServerAppItem.currentDownload.has_value()) {
			j["currentDownload"] = DownloadItemActions::toJson(downloadServerAppItem.currentDownload.value());
		}
		if (downloadServerAppItem.error.has_value()) {
			j["error"] = downloadServerAppItem.error.value();
		}
		j["created"] = downloadServerAppItem.created;
		j["updated"] = downloadServerAppItem.updated;
		return j;
	}

	inline DownloadServerAppItem DownloadServerAppItem::fromJson(const nlohmann::json &j)
	{
		DownloadServerAppItem downloadServerAppItem;
		downloadServerAppItem.status = DownloadServerAppItem::toStatus(j["status"].get<std::string>());
		if (j.contains("currentDownload")) {
			auto currentDownload = DownloadItemActions::fromJson(j["currentDownload"]);
			//downloadServerAppItem.currentDownload = DownloadItemActions::fromJson(j["currentDownload"]);
		}
		if (j.contains("error")) {
			downloadServerAppItem.error = j["error"].get<std::string>();
		}
		downloadServerAppItem.created = j["created"].get<long long>();
		downloadServerAppItem.updated = j["updated"].get<long long>();
		return downloadServerAppItem;
	}

} // namespace wingman
