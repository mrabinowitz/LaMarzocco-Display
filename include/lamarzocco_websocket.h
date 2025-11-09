#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include "lamarzocco_client.h"

class LaMarzoccoWebSocket {
public:
    LaMarzoccoWebSocket(LaMarzoccoClient& client);
    ~LaMarzoccoWebSocket();
    
    // Connect to websocket
    bool connect(const String& serial_number);
    
    // Disconnect
    void disconnect();
    
    // Check if connected
    bool is_connected() const { return _connected; }
    
    // Loop (call in main loop or task)
    void loop();
    
    // Set callback for incoming messages
    void set_message_callback(void (*callback)(const String& message));
    
private:
    LaMarzoccoClient& _client;
    WebSocketsClient _ws;
    bool _connected;
    String _serial_number;
    String _subscription_id;
    String _cached_token;  // Cache token before connecting to avoid accessing client in callback
    void (*_message_callback)(const String& message);
    
    // STOMP protocol helpers
    String _encode_stomp_message(const String& command, const String& headers, const String& body = "");
    bool _decode_stomp_message(const String& message, String& command, String& headers, String& body);
    void _handle_websocket_event(WStype_t type, uint8_t* payload, size_t length);
    static void _ws_event_handler(WStype_t type, uint8_t* payload, size_t length);
    static LaMarzoccoWebSocket* _instance;
};

