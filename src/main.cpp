#include <drogon/drogon.h>

#include "runtime/RuntimePaths.h"
#include "database/Database.h"
#include "realtime/AuctionEvents.h"
#include "services/AuctionCloseService.h"
#include "services/AuthBidService.h"
#include "security/HttpSecurity.h"

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
        LOG_INFO << "[STARTUP] Reading configuration from " << configPath.string();
        drogon::app().loadConfigFile(configPath.string());
        auction::security::registerHttpSecurity();
        const auto& databaseConfig = drogon::app().getCustomConfig()["database"];
        const auto databasePath = std::filesystem::u8path(
            databaseConfig.get("path", "runtime/auction.sqlite3").asString());
        auction::database::Connection database(databasePath);
        auction::database::initialize(database, "database");
        auction::services::AuthService(databasePath).ensureDemoUsers();
        LOG_INFO << "[STARTUP] Database ready: " << std::filesystem::absolute(databasePath).string();
        const auto closeExpired = [databasePath] {
            try
            {
                const auto closed = auction::services::AuctionCloseService(databasePath).closeExpired();
                for (const auto& item : closed)
                {
                    const auto winner = item.winnerUsername.value_or("none");
                    LOG_INFO << "[CLOSE] Lot " << item.lotId << " closed; winner=" << winner
                             << "; finalPrice=" << item.currentPrice;
                    auction::realtime::AuctionEventHub::instance().publish(
                        item.lotId, auction::realtime::lotClosedEvent(item));
                }
                if (!closed.empty())
                    LOG_INFO << "[CLOSE] Closing pass finished; " << closed.size()
                             << " lot(s) closed";
            }
            catch (const std::exception& error)
            {
                LOG_ERROR << "[CLOSE] Auction closing pass failed: " << error.what();
            }
        };
drogon::app().registerBeginningAdvice([closeExpired] {
            closeExpired();
            drogon::app().getLoop()->runEvery(1.0, closeExpired);
        });
        LOG_INFO << "[STARTUP] Auction server starting; version=1.0 qsize=0";
        drogon::app().run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Server startup failed: " << error.what() << std::endl;
        return 1;
    }
}
