#include "RFIDService.h"

RFIDService::RFIDService(PsychicHttpServer *server,
                         ESP32SvelteKit *sveltekit,
                         SecurityManager *securityManager, DisplayService *display)
    : _server(server), 
    _securityManager(securityManager), 
    _rfid(SS_PIN, RST_PIN),
    _eventEndpoint(RFIDState::read,
                  RFIDState::update,
                  this,
                  sveltekit->getSocket(),
                  RFID_SERVICE_EVENT),
    _httpEndpoint(RFIDState::read,
                  RFIDState::update,
                  this,
                  server,
                  RFID_SERVICE_ENDPOINT_PATH,
                  sveltekit->getSecurityManager(),
                  AuthenticationPredicates::IS_AUTHENTICATED),
    _webSocketServer(
        RFIDState::read,
        RFIDState::update, 
        this, 
        server, 
        RFID_SERVICE_SOCKET_PATH, 
        sveltekit->getSecurityManager(), 
        AuthenticationPredicates::IS_AUTHENTICATED),
    _display() {}

void RFIDService::begin() {
    SPI.begin();
    _rfid.PCD_Init();
    delay(4);
    _rfid.PCD_DumpVersionToSerial();
    setupEndpoints();
    _httpEndpoint.begin();
    _eventEndpoint.begin();
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
    if(millis() % 5000 == 0) {
        Serial.printf("RFIDService loop running, reset mode: %s\n", _resetMode);
    }
    if (!_resetMode || !_rfid.PICC_IsNewCardPresent() || !_rfid.PICC_ReadCardSerial()) {
        return;
    }

    _display->showResetCount(_nextIndex);

    String uidStr = uidToString(&_rfid.uid);

    if (_state._cardRegistry.find(uidStr) == _state._cardRegistry.end()) {
        clearRFIDData(&_rfid.uid);
        _state._cardRegistry[uidStr] = _nextIndex++;
        Serial.printf("Card %s reset and stored as %d\n", uidStr.c_str(), _state._cardRegistry[uidStr]);
    }

    _rfid.PICC_HaltA();
}

String RFIDService::readCardData(MFRC522 &rfid, byte startBlock, byte blockCount) {
    String result = "";

    byte blocksRead = 0;
    byte block = startBlock;

    while (blocksRead < blockCount) {
        // pula blocos de trailer de setor
        if ((block + 1) % 4 == 0) {
            block++;
            continue;
        }

        byte buffer[18];
        byte size = sizeof(buffer);
        MFRC522::StatusCode status = rfid.MIFARE_Read(block, buffer, &size);

        if (status != MFRC522::STATUS_OK) {
            Serial.printf("Erro ao ler bloco %d: %s\n", block, rfid.GetStatusCodeName(status));
            break;
        }

        for (int i = 0; i < 16; i++) {
            if (buffer[i] == 0) break; // fim da string
            result += (char)buffer[i];
        }

        blocksRead++;
        block++;
    }

    return result;
}

bool RFIDService::writeCardData(MFRC522 &rfid, byte startBlock, const String &data) {
    const byte bytesPerBlock = 16;
    byte buffer[bytesPerBlock];
    int dataLen = data.length();
    int blocksNeeded = (dataLen + bytesPerBlock - 1) / bytesPerBlock;

    byte written = 0;
    byte block = startBlock;

    while (written < blocksNeeded) {
        // pula blocos de trailer
        if ((block + 1) % 4 == 0) {
            block++;
            continue;
        }

        memset(buffer, 0, bytesPerBlock);
        for (int j = 0; j < bytesPerBlock; j++) {
            int index = written * bytesPerBlock + j;
            if (index < dataLen) {
                buffer[j] = data[index];
            } else {
                buffer[j] = 0;
            }
        }

        MFRC522::StatusCode status = rfid.MIFARE_Write(block, buffer, bytesPerBlock);
        if (status != MFRC522::STATUS_OK) {
            Serial.printf("Erro ao escrever bloco %d: %s\n", block, rfid.GetStatusCodeName(status));
            return false;
        }

        written++;
        block++;
    }

    return true;
}