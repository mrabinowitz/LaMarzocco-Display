#include "lamarzocco_machine.h"
#include "config.h"
#include <ArduinoJson.h>

LaMarzoccoMachine* LaMarzoccoMachine::_instance = nullptr;

LaMarzoccoMachine::LaMarzoccoMachine(LaMarzoccoClient& client, LaMarzoccoWebSocket& websocket)
    : _client(client), _websocket(websocket), _power_state(false) {
    _instance = this;
    _websocket.set_message_callback(_websocket_message_handler);
}

void LaMarzoccoMachine::_websocket_message_handler(const String& message) {
    if (_instance) {
        // Always print incoming message to serial
        Serial.println("\n========== WEBSOCKET MESSAGE RECEIVED ==========");
        Serial.print("Message length: ");
        Serial.print(message.length());
        Serial.println(" bytes");
        Serial.println(message);
        Serial.println("===============================================\n");
        
        // Parse JSON message with large buffer for La Marzocco messages (can be 2-3KB)
        // Allocate 4KB to handle large messages with all widgets
        JsonDocument doc;
        
        Serial.println("Parsing JSON...");
        DeserializationError error = deserializeJson(doc, message);
        
        if (error) {
            Serial.print("❌ JSON parse error: ");
            Serial.println(error.c_str());
            Serial.print("Error code: ");
            Serial.println((int)error.code());
            
            if (error == DeserializationError::NoMemory) {
                Serial.println("💡 JSON document is too large - increase buffer size");
            }
            return;
        }
        
        Serial.println("✓ JSON parsed successfully");
        
        // Look for power/mode information in widgets array
        if (doc.containsKey("widgets")) {
            Serial.println("Found 'widgets' key");
            JsonArray widgets = doc["widgets"].as<JsonArray>();
            Serial.print("Widgets count: ");
            Serial.println(widgets.size());
            
            for (JsonVariant widget : widgets) {
                const char* code = widget["code"];
                if (code && strcmp(code, "CMMachineStatus") == 0) {
                    Serial.println("✓ Found CMMachineStatus widget");
                    
                    JsonObject output = widget["output"].as<JsonObject>();
                    
                    // Get status
                    const char* status = output["status"];
                    if (status) {
                        _instance->_power_state = (strcmp(status, "PoweredOn") == 0);
                        Serial.print("📊 Machine status: ");
                        Serial.println(status);
                        Serial.print("🔋 Power state: ");
                        Serial.println(_instance->_power_state ? "ON" : "OFF");
                    } else {
                        Serial.println("⚠ Status field not found");
                    }
                    
                    // Get mode
                    const char* mode = output["mode"];
                    if (mode) {
                        Serial.print("⚙️  Machine mode: ");
                        Serial.println(mode);
                    }
                    
                    break;
                }
            }
        } else {
            Serial.println("⚠ No 'widgets' key found in JSON");
        }
        
        // Check for command responses
        if (doc.containsKey("commands")) {
            JsonArray commands = doc["commands"].as<JsonArray>();
            if (commands.size() > 0) {
                Serial.println("\n📋 Command responses:");
                for (JsonVariant cmd : commands) {
                    const char* id = cmd["id"];
                    const char* status = cmd["status"];
                    if (id && status) {
                        Serial.print("  ✓ Command ");
                        Serial.print(id);
                        Serial.print(": ");
                        Serial.println(status);
                    }
                }
            }
        }
        
        Serial.println();
    }
}

bool LaMarzoccoMachine::set_power(bool enabled) {
    String serial = _client.get_serial_number();
    if (serial.length() == 0) {
        debugln("Serial number not set");
        return false;
    }
    
    JsonDocument request;
    request["mode"] = enabled ? "BrewingMode" : "StandBy";
    
    JsonDocument response;
    bool success = _client.api_call("POST", 
                                     "/things/" + serial + "/command/CoffeeMachineChangeMode",
                                     &request, &response);
    
    if (success) {
        _power_state = enabled;
        debug("Power set to: ");
        debugln(enabled ? "ON" : "OFF");
    } else {
        debugln("Failed to set power");
    }
    
    return success;
}

bool LaMarzoccoMachine::toggle_power() {
    return set_power(!_power_state);
}

bool LaMarzoccoMachine::connect_websocket() {
    if (is_websocket_connected()) {
        return true;  // Already connected
    }
    
    String serial = _client.get_serial_number();
    if (serial.length() == 0) {
        debugln("Serial number not set");
        return false;
    }
    
    return _websocket.connect(serial);
}

bool LaMarzoccoMachine::is_websocket_connected() const {
    return _websocket.is_connected();
}

void LaMarzoccoMachine::disconnect_websocket() {
    _websocket.disconnect();
}

void LaMarzoccoMachine::loop() {
    // Call websocket loop regularly - this is critical for connection
    _websocket.loop();
}

