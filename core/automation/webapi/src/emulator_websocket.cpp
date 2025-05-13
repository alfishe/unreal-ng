#include "emulator_websocket.h"
#include <drogon/HttpAppFramework.h> // For drogon::app() and LOG_*

// If kEmulatorDataTopic is not C++17 inline static in .h:
// const std::string EmulatorWebSocket::kEmulatorDataTopic = "zx_emulator_updates";

void EmulatorWebSocket::handleNewMessage(const drogon::WebSocketConnectionPtr &wsConnPtr,
                                         std::string &&message,
                                         const drogon::WebSocketMessageType &type)
{
    switch (type)
    {
        case drogon::WebSocketMessageType::Text:
            LOG_INFO << "Received WebSocket message from client: " << message;
            // Process commands for the emulator.
            // For example, you might parse the message and interact with your emulator.
            // Then, you could send a specific response back to this client:
            // wsConnPtr->send("Command received: " + message);
            // Or if it's a command that should update all, call:
            // EmulatorWebSocket::broadcastEmulatorData("{\"status\":\"command_processed\"}");

            // For now, just acknowledge the receipt
            wsConnPtr->send("ACK: " + message);
            break;

        case drogon::WebSocketMessageType::Binary:
            LOG_INFO << "Received binary WebSocket data (length: " << message.length() << ")";
            wsConnPtr->send("Binary data received.");
            break;

        case drogon::WebSocketMessageType::Ping:
            LOG_DEBUG << "Received Ping from client";
            // Drogon handles sending Pong automatically
            break;

        case drogon::WebSocketMessageType::Pong:
            LOG_DEBUG << "Received Pong from client";
            break;

        case drogon::WebSocketMessageType::Close:
            LOG_INFO << "Client initiated WebSocket close";
            // Connection will be closed by Drogon after this handler.
            break;

        default:
            LOG_WARN << "Received unknown WebSocket message type";
            break;
    }
}

void EmulatorWebSocket::handleConnectionClosed(const drogon::WebSocketConnectionPtr &wsConnPtr)
{
    LOG_INFO << "WebSocket connection closed";
    if (wsConnPtr->hasContext())
    {
        // Get the context we stored
        auto &clientCtx = wsConnPtr->getContextRef<WsClientContext>();

        // Unsubscribe from our member PubSubService using the stored ID and topic
        this->_emulatorUpdatesService.unsubscribe(kEmulatorDataTopic, clientCtx.emulatorDataSubId);

        LOG_DEBUG << "Client unsubscribed from topic: " << kEmulatorDataTopic;
    }
}

void EmulatorWebSocket::handleNewConnection(const drogon::HttpRequestPtr &req,
                                            const drogon::WebSocketConnectionPtr &wsConnPtr)
{
    LOG_INFO << "New WebSocket connection established from " << req->getPeerAddr().toIpPort();
    wsConnPtr->send("Welcome to ZX Spectrum Emulator WebSocket!");

    // Create a context for this connection
    auto clientCtx = std::make_shared<WsClientContext>();

    // Subscribe this new connection to our member PubSubService on the fixed topic
    clientCtx->emulatorDataSubId = this->_emulatorUpdatesService.subscribe(
            kEmulatorDataTopic,
            [wsConnPtr](const std::string & /*topic*/, const std::string &message) {
                // This lambda is called when a message is published to kEmulatorDataTopic
                // Check if the connection is still valid before sending
                if (wsConnPtr && wsConnPtr->connected())
                {
                    wsConnPtr->send(message);
                }
            });

    // Store the context (containing the subscription ID) on the connection
    wsConnPtr->setContext(clientCtx);

    LOG_DEBUG << "Client subscribed to topic '" << kEmulatorDataTopic
              << "' with subscription ID: " << clientCtx->emulatorDataSubId;
}

// Non-static method that uses the member PubSubService
void EmulatorWebSocket::publishToSubscribers(const std::string &data)
{
    this->_emulatorUpdatesService.publish(kEmulatorDataTopic, data);
    LOG_TRACE << "Published data to topic '" << kEmulatorDataTopic << "': " << data.substr(0, 50) << "...";
}

// Static method for external code (like the emulator) to call
void EmulatorWebSocket::broadcastEmulatorData(const std::string &data)
{
    // Retrieve the singleton instance of this controller registered with the framework.
    // The template argument is the class name of the controller.
    EmulatorWebSocket* controllerInstance = drogon::app.getController<EmulatorWebSocket>();

    if (controllerInstance)
    {
        // Call the non-static publish method on the retrieved instance
        controllerInstance->publishToSubscribers(data);
    }
    else
    {
        LOG_ERROR << "EmulatorWebSocket controller instance not found. Cannot broadcast emulator data.";
        // This might happen if the controller wasn't registered or the app is shutting down.
    }
}