#include "lamarzocco_machine.h"
#include "config.h"
#include "boiler_display.h"
#include <ArduinoJson.h>

LaMarzoccoMachine* LaMarzoccoMachine::_instance = nullptr;

LaMarzoccoMachine::LaMarzoccoMachine(LaMarzoccoClient& client, LaMarzoccoWebSocket& websocket)
    : _client(client), _websocket(websocket), _power_state(false) {
    _instance = this;
    _websocket.set_message_callback(_websocket_message_handler);
}

void LaMarzoccoMachine::_websocket_message_handler(const String& message) {
    if (_instance) {
        // Parse JSON message with large buffer for La Marzocco messages (can be 2-3KB)
        JsonDocument doc;
        
        Serial.println("\n========== WEBSOCKET MESSAGE RECEIVED ==========");
        Serial.print("Message length: ");
        Serial.print(message.length());
        Serial.println(" bytes");
        
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
        
        // Variables to store extracted data
        const char* machine_status = nullptr;
        const char* machine_mode = nullptr;
        const char* coffee_boiler_status = nullptr;
        int64_t coffee_ready_time = 0;
        const char* steam_boiler_status = nullptr;
        int64_t steam_ready_time = 0;
        
        // Parse widgets array to extract boiler and machine status
        if (doc.containsKey("widgets")) {
            JsonArray widgets = doc["widgets"].as<JsonArray>();
            Serial.print("Widgets count: ");
            Serial.println(widgets.size());
            
            for (JsonVariant widget : widgets) {
                const char* code = widget["code"];
                if (!code) continue;
                
                // Extract machine status
                if (strcmp(code, "CMMachineStatus") == 0) {
                    Serial.println("✓ Found CMMachineStatus widget");
                    JsonObject output = widget["output"].as<JsonObject>();
                    
                    machine_status = output["status"];
                    machine_mode = output["mode"];
                    
                    if (machine_status) {
                        _instance->_power_state = (strcmp(machine_status, "PoweredOn") == 0);
                        Serial.print("📊 Machine status: ");
                        Serial.print(machine_status);
                        if (machine_mode) {
                            Serial.print(" (mode: ");
                            Serial.print(machine_mode);
                            Serial.print(")");
                        }
                        Serial.println();
                    }
                }
                // Extract coffee boiler status and ready time
                else if (strcmp(code, "CMCoffeeBoiler") == 0) {
                    Serial.println("☕ Found CMCoffeeBoiler widget");
                    JsonObject output = widget["output"].as<JsonObject>();
                    
                    coffee_boiler_status = output["status"];
                    if (output.containsKey("readyStartTime") && !output["readyStartTime"].isNull()) {
                        coffee_ready_time = output["readyStartTime"].as<long long>();
                    }
                    
                    Serial.print("  Status: ");
                    Serial.print(coffee_boiler_status ? coffee_boiler_status : "null");
                    Serial.print(", ReadyStartTime: ");
                    Serial.println((long long)coffee_ready_time);
                }
                // Extract steam boiler status and ready time
                else if (strcmp(code, "CMSteamBoilerLevel") == 0) {
                    Serial.println("♨️  Found CMSteamBoilerLevel widget");
                    JsonObject output = widget["output"].as<JsonObject>();
                    
                    steam_boiler_status = output["status"];
                    if (output.containsKey("readyStartTime") && !output["readyStartTime"].isNull()) {
                        steam_ready_time = output["readyStartTime"].as<long long>();
                    }
                    
                    Serial.print("  Status: ");
                    Serial.print(steam_boiler_status ? steam_boiler_status : "null");
                    Serial.print(", ReadyStartTime: ");
                    Serial.println((long long)steam_ready_time);
                }
            }
        }
        
        // Update boiler displays if we have machine status
        if (machine_status) {
            Serial.println("\n🔄 Updating boiler displays...");
            
            // If machine is OFF or StandBy, use that for both boilers
            if (strcmp(machine_status, "Off") == 0 || strcmp(machine_status, "StandBy") == 0) {
                boiler_display_update(BOILER_COFFEE, machine_status, 
                                     coffee_boiler_status ? coffee_boiler_status : "Off", 
                                     coffee_ready_time);
                boiler_display_update(BOILER_STEAM, machine_status, 
                                     steam_boiler_status ? steam_boiler_status : "Off", 
                                     steam_ready_time);
            } else {
                // Machine is ON, update each boiler independently
                if (coffee_boiler_status) {
                    boiler_display_update(BOILER_COFFEE, machine_status, 
                                         coffee_boiler_status, coffee_ready_time);
                }
                
                if (steam_boiler_status) {
                    boiler_display_update(BOILER_STEAM, machine_status, 
                                         steam_boiler_status, steam_ready_time);
                }
            }
        } else {
            Serial.println("⚠ No machine status found, skipping boiler updates");
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
        
        Serial.println("===============================================\n");
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

