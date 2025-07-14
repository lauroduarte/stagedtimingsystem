#ifndef RFIDService_h
#define RFIDService_h

#include <Arduino.h>
#include <vector>
#include <map>
#include <MFRC522.h>
#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <WebSocketServer.h>

#define RST_PIN 22 // ajuste conforme necessário
#define SS_PIN  21

#define RFID_SERVICE_START_RESET "/rest/rfid/start_reset"
#define RFID_SERVICE_STOP_RESET "/rest/rfid/stop_reset"
#define RFID_SERVICE_SOCKET_PATH "/ws/rfid"


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
                SecurityManager *securityManager);

    void begin();
    void loop();

private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    WebSocketServer<RFIDState> _webSocketServer;

    MFRC522 _rfid;
    bool _resetMode = false;
    
    int _nextIndex = 1;

    void setupEndpoints();
    esp_err_t handleStartReset(PsychicRequest *request);
    esp_err_t handleStopReset(PsychicRequest *request);
    String uidToString(MFRC522::Uid *uid);
    void clearRFIDData(MFRC522::Uid *uid);
};

#endif