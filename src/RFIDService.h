#ifndef RFIDService_h
#define RFIDService_h

#include <Arduino.h>
#include <vector>
#include <map>
#include <MFRC522.h>
#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <WebSocketServer.h>
#include <EventEndpoint.h>
#include <DisplayService.h>
#include <SPI.h>

#define RST_PIN 4 // ajuste conforme necessário
#define SS_PIN  5
#define MOSI_PIN 18
#define MISO_PIN 19
#define SCK_PIN 23

#define RFID_SERVICE_START_RESET "/rest/rfid/start_reset"
#define RFID_SERVICE_STOP_RESET "/rest/rfid/stop_reset"
#define RFID_SERVICE_ENDPOINT_PATH "/rest/rfid"
#define RFID_SERVICE_SOCKET_PATH "/ws/rfid"
#define RFID_SERVICE_EVENT "rfid"


class RFIDState{
    public:
        std::map<String, int> _cardRegistry;

    static void read(RFIDState &state, JsonObject &json) {
        for (const auto &entry : state._cardRegistry) {
            json[entry.first] = entry.second;
        }
    }

    static StateUpdateResult update(JsonObject &json, RFIDState &state) {
        if (!json.containsKey("uid") || !json.containsKey("numero")) {
            return StateUpdateResult::ERROR;
        }

        String uid = json["uid"].as<String>();
        int numero = json["numero"];

        if (state._cardRegistry.find(uid) != state._cardRegistry.end()) {
            state._cardRegistry[uid] = numero;
            return StateUpdateResult::UNCHANGED;
        } else {
            state._cardRegistry[uid] = numero;
            return StateUpdateResult::CHANGED;
        }
    }

};

class RFIDService : public StatefulService<RFIDState> {
public:
    RFIDService(PsychicHttpServer *server, 
                ESP32SvelteKit *sveltekit,
                SecurityManager *securityManager, DisplayService *display);
    static String uidToString(MFRC522::Uid *uid);
    static String readCardData(MFRC522 &rfid, byte startBlock, byte blockCount);
    static bool writeCardData(MFRC522 &rfid, byte startBlock, const String &data);

    void checkHardwareStatus();

    void begin();
    void loop();

private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    DisplayService *_display;
    WebSocketServer<RFIDState> _webSocketServer;
    HttpEndpoint<RFIDState> _httpEndpoint;
    EventEndpoint<RFIDState> _eventEndpoint;

    MFRC522 _rfid;
    bool _resetMode = false;
    bool _hardwareOk;
    
    int _nextIndex = 1;
    void setupEndpoints();
    esp_err_t handleStartReset(PsychicRequest *request);
    esp_err_t handleStopReset(PsychicRequest *request);
    
    void clearRFIDData(MFRC522::Uid *uid);
};

#endif