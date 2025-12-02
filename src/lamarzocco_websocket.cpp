#include "lamarzocco_websocket.h"
#include "config.h"
#include <ArduinoJson.h>
#include <esp_random.h>

static const char* WS_BASE_URL = "lion.lamarzocco.io";  // Same base URL for both REST API and WebSocket

LaMarzoccoWebSocket* LaMarzoccoWebSocket::_instance = nullptr;

LaMarzoccoWebSocket::LaMarzoccoWebSocket(LaMarzoccoClient& client) 
    : _client(client), _connected(false), _message_callback(nullptr) {
    _instance = this;
    _ws.onEvent(_ws_event_handler);
    _ws.setReconnectInterval(5000);  // 5 second reconnect interval
    // Don't call beginSSL here - wait until connect() is called
}

LaMarzoccoWebSocket::~LaMarzoccoWebSocket() {
    // Clear instance pointer first to prevent callbacks from accessing this object
    if (_instance == this) {
        _instance = nullptr;
    }
    
    // Disconnect and cleanup
    disconnect();
    
    // Clear the event handler to prevent any pending callbacks
    // Note: WebSocketsClient doesn't have a clear event handler method,
    // but setting instance to null above should prevent callbacks from accessing this object
}

void LaMarzoccoWebSocket::_ws_event_handler(WStype_t type, uint8_t* payload, size_t length) {
    // Safety check: ensure instance is valid before accessing
    if (_instance == nullptr) {
        return;  // Instance has been destroyed, ignore callback
    }
    
    // Call the instance's event handler
    // The instance will do additional safety checks
    _instance->_handle_websocket_event(type, payload, length);
}

String LaMarzoccoWebSocket::_encode_stomp_message(const String& command, const String& headers, const String& body) {
    // STOMP frame format matching Python exactly:
    // COMMAND\n
    // header1:value1\n
    // header2:value2\n
    // \n
    // BODY\x00
    
    String message = command + "\n";
    
    // Add headers (should already have \n at end of each line)
    message += headers;
    
    // Ensure headers section ends with exactly one \n
    if (!headers.endsWith("\n")) {
        message += "\n";
    }
    
    // Add blank line separator (this creates the \n\n between headers and body)
    message += "\n";
    
    // Add body if present
    if (body.length() > 0) {
        message += body;
    }
    
    // Add null terminator
    message += '\x00';
    
    return message;
}

bool LaMarzoccoWebSocket::_decode_stomp_message(const String& message, String& command, String& headers, String& body) {
    // STOMP frame format: COMMAND\nHEADERS\n\nBODY\x00
    // Example:
    // MESSAGE\n
    // destination:/ws/...\n
    // content-type:application/json\n
    // \n
    // {"json":"data"}\x00
    
    debug("Decoding message, length: ");
    debugln(message.length());
    
    // Find the double newline that separates headers from body
    int header_end = message.indexOf("\n\n");
    if (header_end < 0) {
        debugln("❌ No header separator (\\n\\n) found");
        return false;
    }
    
    debug("Header end position: ");
    debugln(header_end);
    
    // Find the first newline (end of command)
    int command_end = message.indexOf("\n");
    if (command_end < 0 || command_end > header_end) {
        debugln("❌ No command separator found");
        return false;
    }
    
    // Extract command
    command = message.substring(0, command_end);
    command.trim();
    debug("Command: ");
    debugln(command);
    
    // Extract headers (between first \n and \n\n)
    if (command_end + 1 < header_end) {
        headers = message.substring(command_end + 1, header_end);
    } else {
        headers = "";
    }
    
    // Extract body (after \n\n until \x00 or end of string)
    int body_start = header_end + 2;  // Skip the \n\n
    debug("Body starts at position: ");
    debugln(body_start);
    
    // Find null terminator
    int body_end = message.indexOf('\x00', body_start);
    if (body_end < 0) {
        // No null terminator found, use end of string
        body_end = message.length();
        debug("No null terminator, using message length: ");
        debugln(body_end);
    } else {
        debug("Null terminator at position: ");
        debugln(body_end);
    }
    
    // Extract body
    if (body_end > body_start) {
        body = message.substring(body_start, body_end);
        debug("Body extracted, length: ");
        debugln(body.length());
    } else {
        body = "";
        debugln("⚠ Body is empty (body_end <= body_start)");
    }
    
    return true;
}

void LaMarzoccoWebSocket::_handle_websocket_event(WStype_t type, uint8_t* payload, size_t length) {
    // Safety check: ensure we're still the active instance
    if (_instance != this) {
        return;  // This object is no longer the active instance, ignore event
    }
    
    switch (type) {
        case WStype_DISCONNECTED:
            {
                debug("WebSocket disconnected");
                if (payload && length > 0) {
                    debug(" - reason: ");
                    debugln((char*)payload);
                } else {
                    debugln("");
                }
                _connected = false;
            }
            break;
            
        case WStype_CONNECTED:
            {
                // Additional safety check: ensure we're still the active instance
                if (_instance != this) {
                    debugln("WARNING: WebSocket connected but instance changed, ignoring");
                    return;
                }
                
                debugln("*** WebSocket TCP connection established ***");
                debugln("WebSocket handshake complete, sending STOMP CONNECT...");
                
                // Use cached token (fetched before connecting)
                if (_cached_token.length() == 0) {
                    debugln("ERROR: Cached access token is empty!");
                    _ws.disconnect();
                    break;
                }
                
                debug("Using cached token, length: ");
                debugln(_cached_token.length());
                
                // Build STOMP CONNECT headers exactly as Python diagnostic does
                // Python format: host:lion.lamarzocco.io, accept-version:1.2,1.1,1.0, heart-beat:0,0, Authorization:Bearer token
                String connect_headers = "host:" + String(WS_BASE_URL) + "\n";
                connect_headers += "accept-version:1.2,1.1,1.0\n";
                connect_headers += "heart-beat:0,0\n";
                connect_headers += "Authorization:Bearer " + _cached_token + "\n";
                
                String connect_msg = _encode_stomp_message("CONNECT", connect_headers);
                
                debugln("Sending STOMP CONNECT:");
                debugln("--- RAW CONNECT MESSAGE ---");
                // Print with visible control characters for debugging
                for (size_t i = 0; i < connect_msg.length() && i < 500; i++) {
                    char c = connect_msg[i];
                    if (c == '\n') Serial.print("\\n\n");
                    else if (c == '\x00') Serial.print("\\x00");
                    else Serial.print(c);
                }
                Serial.println();
                debugln("--- END MESSAGE ---");
                
                // Send the STOMP CONNECT message immediately
                bool sent = _ws.sendTXT(connect_msg);
                if (sent) {
                    debugln("✓ STOMP CONNECT sent successfully");
                    debugln("Waiting for server CONNECTED response...");
                } else {
                    debugln("✗ ERROR: Failed to send STOMP CONNECT!");
                    _ws.disconnect();
                }
            }
            break;
            
        case WStype_TEXT:
            {
                String message = String((char*)payload, length);
                debug("*** WebSocket TEXT message received (");
                debug(length);
                debugln(" bytes) ***");
                debugln("Raw message:");
                debugln(message);
                
                // Try to decode as STOMP message
                String command, headers, body;
                if (_decode_stomp_message(message, command, headers, body)) {
                    debug("✓ Decoded STOMP - command: ");
                    debugln(command);
                    if (headers.length() > 0) {
                        debug("Headers: ");
                        debugln(headers);
                    }
                    if (body.length() > 0) {
                        debug("Body: ");
                        debugln(body);
                    }
                    
                    if (command == "CONNECTED") {
                        debugln("*** ✓✓✓ STOMP CONNECTED - Server accepted! ✓✓✓ ***");
                        
                        // Generate subscription ID (pre-allocate to avoid fragmentation)
                        _subscription_id = LaMarzoccoAuth::generate_uuid();
                        if (_subscription_id.length() == 0) {
                            debugln("ERROR: Failed to generate subscription ID!");
                            _ws.disconnect();
                            break;
                        }
                        
                        // Subscribe to dashboard immediately - match Python format exactly
                        // Build headers carefully to minimize memory allocations
                        String subscribe_headers;
                        subscribe_headers.reserve(256);  // Pre-allocate
                        subscribe_headers = "destination:/ws/sn/";
                        subscribe_headers += _serial_number;
                        subscribe_headers += "/dashboard\n";
                        subscribe_headers += "ack:auto\n";
                        subscribe_headers += "id:";
                        subscribe_headers += _subscription_id;
                        subscribe_headers += "\n";
                        subscribe_headers += "content-length:0\n";  // No space after colon
                        
                        String subscribe_msg = _encode_stomp_message("SUBSCRIBE", subscribe_headers);
                        
                        debugln("Sending SUBSCRIBE to dashboard:");
                        debugln("--- RAW SUBSCRIBE MESSAGE ---");
                        // Print with visible control characters
                        for (size_t i = 0; i < subscribe_msg.length(); i++) {
                            char c = subscribe_msg[i];
                            if (c == '\n') Serial.print("\\n\n");
                            else if (c == '\x00') Serial.print("\\x00");
                            else Serial.print(c);
                        }
                        Serial.println();
                        debugln("--- END MESSAGE ---");
                        _ws.sendTXT(subscribe_msg);
                        _connected = true;
                        debugln("*** ✓✓✓ WebSocket fully connected and subscribed! ✓✓✓ ***");
                        debugln("*** Subscription ID: " + _subscription_id + " ***");
                        debugln("*** Topic: /ws/sn/" + _serial_number + "/dashboard ***");
                        debugln("*** Ready to receive messages from machine... ***");
                        debugln("*** Waiting for messages... ***");
                    } else if (command == "MESSAGE") {
                        // Received a message from the server - call callback with JSON body
                        debugln("✓✓✓ Received MESSAGE frame from server ✓✓✓");
                        debug("Message body length: ");
                        debugln(body.length());
                        if (_message_callback) {
                            debugln("Calling message callback...");
                            _message_callback(body);  // Body contains the JSON
                            debugln("Callback completed.");
                        } else {
                            debugln("⚠ WARNING: No message callback registered!");
                        }
                    } else if (command == "ERROR") {
                        debug("*** ✗ STOMP ERROR - Server rejected request ✗ ***");
                        debug("Error headers: ");
                        debugln(headers);
                        debug("Error body: ");
                        debugln(body);
                        // Don't auto-disconnect - let user see the error
                    } else {
                        debug("? Unknown STOMP command: ");
                        debugln(command);
                        debug("Full message: ");
                        debugln(message);
                    }
                } else {
                    // Not a valid STOMP message
                    debugln("✗ Failed to decode as STOMP message");
                    debugln("Raw bytes (first 100):");
                    for (size_t i = 0; i < length && i < 100; i++) {
                        char c = payload[i];
                        if (c >= 32 && c < 127) {
                            debug(c);
                        } else if (c == '\n') {
                            debug("\\n");
                        } else if (c == '\r') {
                            debug("\\r");
                        } else if (c == '\x00') {
                            debug("\\0");
                        } else {
                            debug("[");
                            debug((int)c);
                            debug("]");
                        }
                    }
                    debugln("");
                }
            }
            break;
            
        case WStype_ERROR:
            debug("WebSocket error: ");
            if (payload && length > 0) {
                debugln((char*)payload);
            } else {
                debugln("Unknown error");
            }
            _connected = false;
            break;
            
        case WStype_PONG:
            debugln("WebSocket pong received");
            break;
            
        case WStype_PING:
            debugln("WebSocket ping received");
            break;
            
        case WStype_BIN:
            debugln("WebSocket binary message received");
            break;
            
        default:
            {
                debug("WebSocket unknown event type: ");
                debugln((int)type);
            }
            break;
    }
}

bool LaMarzoccoWebSocket::connect(const String& serial_number) {
    _serial_number = serial_number;
    
    debug("WebSocket connect() called for serial: ");
    debugln(_serial_number);
    
    // Ensure we have access token before connecting and CACHE IT
    // This is critical - we cannot safely call _client methods from the WebSocket callback
    if (!_client.get_access_token()) {
        debugln("ERROR: Failed to get access token for websocket");
        return false;
    }
    
    _cached_token = _client.get_access_token_string();
    if (_cached_token.length() == 0) {
        debugln("ERROR: Access token is empty!");
        return false;
    }
    
    debug("Access token cached, length: ");
    debugln(_cached_token.length());
    
    // Disconnect if already connected
    if (_connected) {
        debugln("Disconnecting existing connection...");
        disconnect();
        // Don't use delay() - let the loop handle disconnection
    }
    
    // Get installation key headers for HTTP upgrade request
    // The Python code sends these headers in the HTTP WebSocket upgrade request
    InstallationKey key;
    if (!_client.get_installation_key(key)) {
        debugln("ERROR: Failed to get installation key!");
        return false;
    }
    
    String installation_id, timestamp, nonce, signature;
    debugln("🔐 Generating request signature (heavy crypto work)...");
    LaMarzoccoAuth::generate_extra_request_headers(key, installation_id, timestamp, nonce, signature);
    
    if (signature.length() == 0) {
        debugln("ERROR: Failed to generate signature!");
        return false;
    }
    
    debugln("✓ Signature generated successfully");
    yield();  // Give system time to recover after heavy crypto
    
    debugln("📋 WEBSOCKET CONNECTION HEADERS:");
    debugln("  X-App-Installation-Id: " + installation_id);
    debugln("  X-Timestamp: " + timestamp);
    debugln("  X-Nonce: " + nonce);
    debugln("  X-Request-Signature: " + signature);
    debugln("");
    
    // CRITICAL: Installation key headers MUST be sent in HTTP WebSocket upgrade request
    // WebSocketsClient library adds NEW_LINE after extraHeaders, so don't add trailing \r\n
    // Format: "Header1: Value1\r\nHeader2: Value2" (no trailing \r\n on last header)
    // Build headers carefully with reserve to avoid fragmentation
    String extraHeaders;
    extraHeaders.reserve(400);  // Pre-allocate to avoid fragmentation
    extraHeaders = "X-App-Installation-Id: ";
    extraHeaders += installation_id;
    extraHeaders += "\r\nX-Timestamp: ";
    extraHeaders += timestamp;
    extraHeaders += "\r\nX-Nonce: ";
    extraHeaders += nonce;
    extraHeaders += "\r\nX-Request-Signature: ";
    extraHeaders += signature;  // No \r\n on last header
    
    debug("Setting extra headers for HTTP upgrade (length=");
    debug(extraHeaders.length());
    debugln(")");
    
    // Set extra headers BEFORE calling beginSSL
    // This is critical - without these headers, server will reject connection
    _ws.setExtraHeaders(extraHeaders.c_str());
    debugln("✓ Extra headers configured");
    
    // Connect to websocket (beginSSL handles SSL automatically)
    debugln("🚀 Starting WebSocket connection...");
    debug("📡 URL: wss://");
    debug(WS_BASE_URL);
    debugln("/ws/connect");
    
    // Make sure we're not already trying to connect
    _ws.disconnect();
    delay(100);  // Small delay to ensure disconnect completes
    
    debugln("Calling beginSSL()...");
    _ws.beginSSL(WS_BASE_URL, 443, "/ws/connect");
    debugln("✓ beginSSL() returned");
    
    debugln("⏳ WebSocket connection initiated, waiting for handshake...");
    debugln("💡 You should see messages when power state changes!");
    debugln("");
    
    return true;
}

void LaMarzoccoWebSocket::disconnect() {
    // Save connected state before clearing it
    bool was_connected = _connected;
    
    // Set connected to false first to prevent callbacks from trying to use the connection
    _connected = false;
    
    // Disable auto-reconnect to prevent callbacks after disconnect
    _ws.setReconnectInterval(0);  // 0 = disable auto-reconnect
    
    // Try to unsubscribe if we have a subscription and were connected
    if (was_connected && _subscription_id.length() > 0) {
        // Try to send unsubscribe message
        // The WebSocket library should handle this gracefully
        String unsubscribe_headers = "id:" + _subscription_id + "\n";
        String unsubscribe_msg = _encode_stomp_message("UNSUBSCRIBE", unsubscribe_headers);
        _ws.sendTXT(unsubscribe_msg);
        // Give it a moment to send
        delay(50);
    }
    
    // Disconnect the WebSocket (this is safe to call even if already disconnected)
    _ws.disconnect();
    
    // Clear subscription ID and cached token
    _subscription_id = "";
    _cached_token = "";
    
    // Re-enable reconnect interval for future connections
    _ws.setReconnectInterval(5000);
}

void LaMarzoccoWebSocket::loop() {
    // This must be called regularly for websocket to work
    _ws.loop();
}

void LaMarzoccoWebSocket::set_message_callback(void (*callback)(const String& message)) {
    _message_callback = callback;
}

