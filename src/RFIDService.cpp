#include "RFIDService.h"

RFIDService::RFIDService(PsychicHttpServer *server,
                         ESP32SvelteKit *sveltekit,
                         SecurityManager *securityManager, DisplayService *display, FS *fs)
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
      _display(),
      _fsPersistence(RFIDState::read, RFIDState::update, this, sveltekit->getFS(), "/config/rfid.json") {}

void RFIDService::begin()
{
    Serial.println("Initializing RFIDService...");
    SPI.begin(MOSI_PIN, MISO_PIN, SCK_PIN, SS_PIN); // MOSI, MISO, SCK, SS
    _rfid.PCD_Init();
    delay(4);
    _rfid.PCD_DumpVersionToSerial();
    checkHardwareStatus();
    setupEndpoints();
    _httpEndpoint.begin();
    _eventEndpoint.begin();
    _webSocketServer.begin();
    _fsPersistence.readFromFS();
}

void RFIDService::setupEndpoints()
{
    _server->on(RFID_SERVICE_START_RESET, HTTP_GET, _securityManager->wrapRequest(std::bind(&RFIDService::handleStartReset, this, std::placeholders::_1), AuthenticationPredicates::IS_AUTHENTICATED));

    ESP_LOGV(SVK_TAG, "Registered GET endpoint: %s", RFID_SERVICE_START_RESET);

    _server->on(RFID_SERVICE_STOP_RESET, HTTP_GET, _securityManager->wrapRequest(std::bind(&RFIDService::handleStopReset, this, std::placeholders::_1), AuthenticationPredicates::IS_AUTHENTICATED));

    ESP_LOGV(SVK_TAG, "Registered GET endpoint: %s", RFID_SERVICE_STOP_RESET);
}

esp_err_t RFIDService::handleStartReset(PsychicRequest *request)
{
    Serial.println("Starting RFID reset mode");
    _resetMode = true;
    // Create a new RFIDState instance
    _state = RFIDState();

    this->update([&](RFIDState& _state) {
        _state._cardRegistry = std::map<String, int>(); // clear existing registry
        return StateUpdateResult::CHANGED; // notify StatefulService by returning CHANGED
    }, "handleStartReset");

    PsychicJsonResponse response = PsychicJsonResponse(request, false);

    JsonObject root = response.getRoot();

    root["status"] = "reset mode started";
    root["nextIndex"] = _state.nextIndex;

    return response.send();
}

esp_err_t RFIDService::handleStopReset(PsychicRequest *request)
{
    Serial.println("Stopping RFID reset mode");
    _resetMode = false;

    PsychicJsonResponse response = PsychicJsonResponse(request, false);

    JsonObject root = response.getRoot();

    JsonArray array = root[""].to<JsonArray>();

    this->read([&](RFIDState& state) {
        for (const auto &entry : state._cardRegistry)
        {
            JsonObject obj = array.createNestedObject();
            obj["uid"] = entry.first;
            obj["numero"] = entry.second;
        }
    });

    return response.send();
}

String RFIDService::uidToString(MFRC522::Uid *uid)
{
    char buffer[32] = {0};
    for (byte i = 0; i < uid->size; i++)
    {
        sprintf(buffer + i * 2, "%02X", uid->uidByte[i]);
    }
    return String(buffer);
}

void RFIDService::clearRFIDData(MFRC522::Uid *uid)
{
    MFRC522::MIFARE_Key key;

    // Chave padrão (FFFFFFFFFFFF)
    for (byte i = 0; i < 6; i++)
    {
        key.keyByte[i] = 0xFF;
    }

    // Ignorar setor 0 (UID e dados críticos)
    for (byte sector = 1; sector < 16; sector++)
    {
        byte trailerBlock = (sector + 1) * 4 - 1;

        if (_rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, uid) != MFRC522::STATUS_OK)
        {
            Serial.printf("Falha na autenticação do setor %d\n", sector);
            continue;
        }

        for (byte relBlock = 0; relBlock < 3; relBlock++)
        {
            byte absBlock = (sector * 4) + relBlock;
            byte buffer[16] = {0};

            if (_rfid.MIFARE_Write(absBlock, buffer, 16) != MFRC522::STATUS_OK)
            {
                Serial.printf("Falha ao escrever no bloco %d\n", absBlock);
            }
            else
            {
                Serial.printf("Bloco %d limpo com sucesso\n", absBlock);
            }
        }
    }

    Serial.println("Processo de limpeza concluído");
    _rfid.PICC_HaltA();
    _rfid.PCD_StopCrypto1();
}

void RFIDService::loop()
{
    delay(10); // Libera tempo para outras tarefas
    yield();   // Permite que o ESP32 execute tarefas internas
    if (!_hardwareOk)
    {
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck > 5000)
        {
            lastCheck = millis();
            checkHardwareStatus(); // Tenta recuperar
        }
        return;
    }

    // if (!_resetMode)
    //     return;
    if (!_rfid.PICC_IsNewCardPresent())
        return;
    if (!_rfid.PICC_ReadCardSerial())
        return;

    // _display->showResetCount(_nextIndex);

    if (_resetMode)
    {

        String uidStr = uidToString(&_rfid.uid);

        this->read([&](RFIDState& state) {
            if (state._cardRegistry.find(uidStr) != state._cardRegistry.end())
            {
                Serial.printf("Card %s already exists with number %d\n", uidStr.c_str(), state._cardRegistry[uidStr]);
            } else{
                clearRFIDData(&_rfid.uid);
                this->update([&](RFIDState& _state) {
                    _state._cardRegistry[uidStr] = _state.nextIndex++;
                    Serial.printf("Card %s reset and stored as %d\n", uidStr.c_str(), _state._cardRegistry[uidStr]);
                    return StateUpdateResult::CHANGED; // notify StatefulService by returning CHANGED
                }, "loopRFIDService");
            }
        });
    }
    else
    {
        _rfid.PICC_DumpToSerial(&(_rfid.uid));
    }

    _rfid.PICC_HaltA();
    _rfid.PCD_StopCrypto1();
}

String RFIDService::readCardData(MFRC522 &rfid, byte startBlock, byte blockCount)
{
    String result = "";

    byte blocksRead = 0;
    byte block = startBlock;

    while (blocksRead < blockCount)
    {
        // pula blocos de trailer de setor
        if ((block + 1) % 4 == 0)
        {
            block++;
            continue;
        }

        byte buffer[18];
        byte size = sizeof(buffer);
        MFRC522::StatusCode status = rfid.MIFARE_Read(block, buffer, &size);

        if (status != MFRC522::STATUS_OK)
        {
            Serial.printf("Erro ao ler bloco %d: %s\n", block, rfid.GetStatusCodeName(status));
            break;
        }

        for (int i = 0; i < 16; i++)
        {
            if (buffer[i] == 0)
                break; // fim da string
            result += (char)buffer[i];
        }

        blocksRead++;
        block++;
    }

    return result;
}

bool RFIDService::writeCardData(MFRC522 &rfid, byte startBlock, const String &data)
{
    const byte bytesPerBlock = 16;
    byte buffer[bytesPerBlock];
    int dataLen = data.length();
    int blocksNeeded = (dataLen + bytesPerBlock - 1) / bytesPerBlock;

    byte written = 0;
    byte block = startBlock;

    while (written < blocksNeeded)
    {
        // pula blocos de trailer
        if ((block + 1) % 4 == 0)
        {
            block++;
            continue;
        }

        memset(buffer, 0, bytesPerBlock);
        for (int j = 0; j < bytesPerBlock; j++)
        {
            int index = written * bytesPerBlock + j;
            if (index < dataLen)
            {
                buffer[j] = data[index];
            }
            else
            {
                buffer[j] = 0;
            }
        }

        MFRC522::StatusCode status = rfid.MIFARE_Write(block, buffer, bytesPerBlock);
        if (status != MFRC522::STATUS_OK)
        {
            Serial.printf("Erro ao escrever bloco %d: %s\n", block, rfid.GetStatusCodeName(status));
            return false;
        }

        written++;
        block++;
    }

    return true;
}

void RFIDService::checkHardwareStatus()
{
    // Versão CORRETA para ler o registro de versão
    byte version = _rfid.PCD_ReadRegister(MFRC522::VersionReg); // Usando a primeira sobrecarga

    Serial.print("Versão do MFRC522: 0x");
    Serial.println(version, HEX);

    if (version == 0x00 || version == 0xFF)
    {
        Serial.println("ERRO: Módulo não detectado - verifique conexões!");
        _hardwareOk = false;
    }
    else
    {
        Serial.println("Módulo detectado com sucesso");
        _hardwareOk = true;
    }
}