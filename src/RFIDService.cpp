#include "RFIDService.h"

RFIDService::RFIDService(PsychicHttpServer *server,
                         ESP32SvelteKit *sveltekit,
                         SecurityManager *securityManager)
    : _server(server), 
    _securityManager(securityManager), 
    _rfid(SS_PIN, RST_PIN), 
    _webSocketServer(
        RFIDState::read,
        RFIDState::update, 
        this, 
        server, 
        RFID_SERVICE_SOCKET_PATH, 
        sveltekit->getSecurityManager(), 
        AuthenticationPredicates::IS_AUTHENTICATED) {}

void RFIDService::begin() {
    SPI.begin();
    _rfid.PCD_Init();
    setupEndpoints();
    _webSocketServer.begin();
}

void RFIDService::setupEndpoints() {
    _server->on(RFID_SERVICE_START_RESET, HTTP_GET, _securityManager->wrapRequest(std::bind(&RFIDService::handleStartReset, this, std::placeholders::_1),
                                              AuthenticationPredicates::IS_AUTHENTICATED));

    ESP_LOGV(SVK_TAG, "Registered GET endpoint: %s", RFID_SERVICE_START_RESET);

    _server->on(RFID_SERVICE_STOP_RESET, HTTP_GET, _securityManager->wrapRequest(std::bind(&RFIDService::handleStopReset, this, std::placeholders::_1),
                                            AuthenticationPredicates::IS_AUTHENTICATED));

    ESP_LOGV(SVK_TAG, "Registered GET endpoint: %s", RFID_SERVICE_STOP_RESET);
}

esp_err_t RFIDService::handleStartReset(PsychicRequest *request) {
    _resetMode = true;
    _state._cardRegistry.clear();
    _nextIndex = 1;

    PsychicJsonResponse response = PsychicJsonResponse(request, false);    

    JsonObject root = response.getRoot();

    root["status"] = "reset mode started";
    root["nextIndex"] = _nextIndex;

    return response.send();
}

esp_err_t RFIDService::handleStopReset(PsychicRequest *request) {
    _resetMode = false;

    PsychicJsonResponse response = PsychicJsonResponse(request, false);   

    JsonObject root = response.getRoot();

    JsonArray array = root[""].to<JsonArray>();

    for (const auto &entry : _state._cardRegistry) {
        JsonObject obj = array.createNestedObject();
        obj["uid"] = entry.first;
        obj["numero"] = entry.second;
    }

    return response.send();
}

String RFIDService::uidToString(MFRC522::Uid *uid) {
    char buffer[32] = {0};
    for (byte i = 0; i < uid->size; i++) {
        sprintf(buffer + i * 2, "%02X", uid->uidByte[i]);
    }
    return String(buffer);
}

void RFIDService::clearRFIDData(MFRC522::Uid *uid) {
    for (byte block = 1; block < 63; block++) {
        byte buffer[16] = {0};
        _rfid.MIFARE_Write(block, buffer, 16);
    }
}

void RFIDService::loop() {
    if (!_resetMode || !_rfid.PICC_IsNewCardPresent() || !_rfid.PICC_ReadCardSerial()) {
        return;
    }

    String uidStr = uidToString(&_rfid.uid);

    if (_state._cardRegistry.find(uidStr) == _state._cardRegistry.end()) {
        clearRFIDData(&_rfid.uid);
        _state._cardRegistry[uidStr] = _nextIndex++;
        Serial.printf("Card %s reset and stored as %d\n", uidStr.c_str(), _state._cardRegistry[uidStr]);
    }

    _rfid.PICC_HaltA();
}