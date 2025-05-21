#pragma once

#include <drogon/WebSocketController.h>
#include <drogon/PubSubService.h> // For broadcasting emulator updates
#include <set>
#include <mutex>

// Forward declaration if your emulator logic is in a separate class/compilation unit
// class YourEmulator;

// A struct to hold subscriber ID for cleanup
struct WsClientContext
{
    drogon::SubscriberID emulatorDataSubId;
    // You can add more client-specific data here if needed
    // For example, what kind of data this client is interested in.
};

namespace api
{
    namespace v1
    {
        class EmulatorWebSocket : public drogon::WebSocketController<EmulatorWebSocket>
        {
        public:
            // Constructor: Potentially get a reference to your emulator instance
            // EmulatorWebSocket(YourEmulator* emu) : emulator_(emu) {}

            void handleNewMessage(const drogon::WebSocketConnectionPtr &wsConnPtr,
                                  std::string &&message,
                                  const drogon::WebSocketMessageType &type) override;

            void handleConnectionClosed(const drogon::WebSocketConnectionPtr &wsConnPtr) override;

            void handleNewConnection(const drogon::HttpRequestPtr &req,
                                     const drogon::WebSocketConnectionPtr &wsConnPtr) override;

            void publishToSubscribers(const std::string &data);

            // Method for your emulator to call to send data to all connected clients
            // This uses Drogon's application-wide PubSubService
            static void broadcastEmulatorData(const std::string &data);

            static void registerEmulatorPubSubService();

            // Registration will be done automatically by Drogon
            // but only when this header is included in the .cpp file
            // Example:
            // main.cpp
            // #include "emulator_websocket.h" // Triggers auto-registration for WebSocket handlers
            WS_PATH_LIST_BEGIN
                // Define the WebSocket path. GET is standard for WebSocket upgrade requests.
                WS_PATH_ADD("/api/v1/websocket", drogon::Get);
            WS_PATH_LIST_END

        private:
            /// Member PubSubService
            drogon::PubSubService<std::string> _emulatorUpdatesService;

            // If you need direct interaction with an emulator instance from within the controller
            // YourEmulator* emulator_ = nullptr; // Example: Initialize in constructor

            // If you choose to manage connections directly without PubSub for broadcasting
            // (PubSub is generally preferred for broadcasting from external sources)
            // std::set<drogon::WebSocketConnectionPtr> clients_;
            // std::mutex clientsMutex_;

            // The NAME of the PubSub Service instance managed by drogon::app()
            inline static const std::string kEmulatorPubSubServiceName = "EmulatorUpdatesService";
            // The TOPIC within the above PubSub Service
            inline static const std::string kEmulatorDataTopic = "zx_emulator_updates_topic";
        };
    } // namespace v1
} // namespace api