
#include "wingman.orm.design.hpp"

void start()
{
    ILogger loggerInstance;
    DownloadServer server(loggerInstance);
    std::thread serverThread(&DownloadServer::run, &server);
    server.stop();
    serverThread.join();
}