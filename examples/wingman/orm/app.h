#pragma once
#include <optional>
#include <string>
#include <sqlite3.h>
#include "databaseactions.h"
#include "types.h"

class AppOrm {
private:
    sqlite3* db;
    DatabaseActions dbActions;

public:
    AppOrm(sqlite3* dbInstance)
        : db(dbInstance)
        , dbActions(dbInstance)
    {
    }

    void upsert(const AppItem& item);

    void remove(const std::string& name, const std::string& key);

    std::optional<AppItem> get(const std::string& name, std::optional<std::string> key = std::nullopt);
};
