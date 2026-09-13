#include <drogon/drogon.h>

#include "runtime/RuntimePaths.h"
#include "database/Database.h"
#include "services/AuthBidService.h"

#include <exception>
#include <iostream>

int main(int argc, char* argv[])
{
    if (argc > 2)
    {
        std::cerr << "Usage: auction_server [config-path]\n";
        return 1;
    }

    try
    {
        const auto configPath = auction::prepareRuntime(argc == 2 ? argv[1] : nullptr);
        drogon::app().loadConfigFile(configPath.string());
        const auto& databaseConfig = drogon::app().getCustomConfig()["database"];
        const auto databasePath = std::filesystem::u8path(
            databaseConfig.get("path", "runtime/auction.sqlite3").asString());
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
        auction::services::AuthService(databasePath).ensureDemoUsers();
        std::cout << "Database ready: " << std::filesystem::absolute(databasePath).string() << std::endl;
        drogon::app().run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Server startup failed: " << error.what() << std::endl;
        return 1;
    }
}
