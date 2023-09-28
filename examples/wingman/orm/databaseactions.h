#pragma once
#include <string>
#include <sqlite3.h>

struct TableInfo;

class DatabaseActions {
private:
    sqlite3* db;

public:
    DatabaseActions(sqlite3* dbInstance);

    TableInfo getTableInfo(const std::string& tableName);

    void createDownloadsTable();

    void createWingmanTable();

    void createAppTable();
};
