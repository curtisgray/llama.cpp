#pragma once
#include <string>
#include <sqlite3.h>
struct WingmanItem;
class DatabaseActions;
class WingmanItemOrm {
private:
    sqlite3* db;
    DatabaseActions dbActions;
    std::string modelsDirectory;

public:
    WingmanItemOrm(sqlite3* dbInstance, std::string& modelsDirectory);

    void upsert(const WingmanItem& item);

    void remove(const std::string& alias);

    std::optional<WingmanItem> get(const std::string& alias);

    std::vector<WingmanItem> getAll();

    void reset();
};
