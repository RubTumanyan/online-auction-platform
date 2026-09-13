#pragma once

#include "models/AuctionLifecycle.h"

#include <filesystem>
#include <vector>

namespace auction::services
{
class AuctionCloseService final
{
  public:
    explicit AuctionCloseService(std::filesystem::path databasePath);
    std::vector<models::ClosedLot> closeExpired() const;

  private:
    std::filesystem::path databasePath_;
};
}
