#include <drogon/drogon.h>

#include "runtime/RuntimePaths.h"

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
        drogon::app().loadConfigFile(configPath.string()).run();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Server startup failed: " << error.what() << '\n';
        return 1;
    }
}
