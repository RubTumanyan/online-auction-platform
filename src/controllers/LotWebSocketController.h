#pragma once

#include <drogon/WebSocketController.h>

namespace auction
{
class LotWebSocketController final : public drogon::WebSocketController<LotWebSocketController>
{
  public:
    void handleNewMessage(const drogon::WebSocketConnectionPtr&, std::string&&,
                          const drogon::WebSocketMessageType&) override;
    void handleNewConnection(const drogon::HttpRequestPtr&,
                             const drogon::WebSocketConnectionPtr&) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr&) override;

    WS_PATH_LIST_BEGIN
    WS_ADD_PATH_VIA_REGEX("^/ws/lots/[1-9][0-9]*$", drogon::Get);
    WS_PATH_LIST_END
};
}
