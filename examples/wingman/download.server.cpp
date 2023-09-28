#include "orm.h"
#include <chrono>
#include <iostream>
#include <json.hpp>
#include <thread>
#include "orm.hpp"

class DownloadServer {
private:
    OrmFactory& orm;
    bool isRunning = true;
    const std::string SERVER_NAME = "DownloadServer";
    const int QUEUE_CHECK_INTERVAL = 1000; // Assuming 1000ms as in TypeScript
    
    DownloadServerAppItem newDownloadServerAppItem()
    {
        DownloadServerAppItem item;
        item.isa = "DownloadServer";
        item.status = "idle";
        item.created = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        item.updated = item.created;
        return item;
    }

public:
    DownloadServer(OrmFactory& OrmFactory)
        : orm(OrmFactory)
    {
    }

    void startDownload(const std::string& modelRepo, const std::string& filePath, bool overwrite)
    {
        // Implement the logic for downloading the file from the model repo
        // and saving it to the local file system.
    }

    void updateServerStatus(const std::string& status, std::optional<DownloadItem> downloadItem, std::optional<std::string> error = std::nullopt)
    {
        auto ao = orm.appOrm();
        auto appItem = ao->get(SERVER_NAME).value_or(AppItem { SERVER_NAME, "default", "{}", 1, 0, 0 });
        // .get(SERVER_NAME).value_or(AppItem { SERVER_NAME, "default", "{}", 1, 0, 0 });
        // auto appItem = orm.appOrm().get(SERVER_NAME).value_or(AppItem { SERVER_NAME, "default", "{}", 1, 0, 0 });

        DownloadServerAppItem item = fromJson(nlohmann::json::parse(appItem.value));
        item.status = status;
        if (error.has_value()) {
            item.error = error;
        }
        if (downloadItem.has_value()) {
            item.currentDownload = downloadItem;
        }
        auto j = toJson(item);
        appItem.value = j.dump();
        orm.appOrm().upsert(appItem);
    }

    void initializeServerStatus()
    {
        orm.appOrm().upsert({ SERVER_NAME, "default", data });

        // Check for orphaned downloads and clean up
        auto downloads = orm.downloadOrm().getAll();
        for (const auto& download : downloads) {
            if (download.status == DownloadItemStatus::complete) {
                // Use static method to check if the download file exists in the file system
                if (!DownloadItemOrm::isDownloaded(download.modelRepo, download.filePath)) {
                    // Use static method to delete the download item file
                    std::string filePath = DownloadItemOrm::getDownloadItemFilePath(download.modelRepo, download.filePath);
                    fs::remove(filePath); // Assuming you have 'namespace fs = std::filesystem;'
                    orm.downloadOrm().remove(download.modelRepo, download.filePath);
                }
            }
        }
        orm.downloadOrm().reset();
    }

    void run()
    {
        logger.debug(SERVER_NAME + ": Download server started.");

        initializeServerStatus();

        while (isRunning) {
            updateServerStatus("ready");
            logger.trace(SERVER_NAME + ": Checking for queued downloads...");
            auto nextProgress = orm.downloadOrm().getAll(); // Assuming a method to get next download item
            if (!nextProgress.empty()) {
                const auto& currentItem = nextProgress.front();
                const std::string modelName = currentItem.modelRepo + "/" + currentItem.filePath;

                logger.info(SERVER_NAME + ": Processing download of " + modelName + "...");

                if (currentItem.status == DownloadItemStatus::queued) {
                    // Update status to downloading
                    DownloadItem updatedItem = currentItem;
                    updatedItem.status = DownloadItemStatus::downloading;
                    orm.downloadOrm().upsert(updatedItem);
                    updateServerStatus("preparing", &updatedItem);

                    logger.debug(SERVER_NAME + ": (main) calling startDownload " + modelName + "...");
                    try {
                        startDownload(updatedItem.modelRepo, updatedItem.filePath, true);
                    } catch (const std::exception& e) {
                        logger.error(SERVER_NAME + ": (main) Exception (startDownload): " + std::string(e.what()));
                        updateServerStatus("error", &updatedItem, e.what());
                    }
                    logger.info(SERVER_NAME + ": Download of " + modelName + " complete.");
                    updateServerStatus("ready");
                }
            }

            logger.trace(SERVER_NAME + ": Waiting " + std::to_string(QUEUE_CHECK_INTERVAL) + "ms...");
            std::this_thread::sleep_for(std::chrono::milliseconds(QUEUE_CHECK_INTERVAL));
        }
        updateServerStatus("stopping");
    }

    void stop()
    {
        isRunning = false;
    }
};

// Usage:
// ILogger loggerInstance;
// DownloadServer server(loggerInstance);
// std::thread serverThread(&DownloadServer::run, &server);
// ...
// server.stop();
// serverThread.join();
