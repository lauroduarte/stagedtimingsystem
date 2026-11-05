#include "RFIDService.h"

RFIDService::RFIDService(PsychicHttpServer *server,
                         ESP32SvelteKit *sveltekit,
                         SecurityManager *securityManager,
                         DisplayService *display,
                         FS *fs,
                         BuzzerService *buzzer,
                         uint8_t ssPin,
                         uint8_t rstPin,
                         int8_t sckPin,
                         int8_t misoPin,
                         int8_t mosiPin)
    : _server(server),
      _securityManager(securityManager),
      _display(display),
      _buzzer(buzzer),
      _webSocketServer(
          RFIDState::read,
          RFIDState::update,
          this,
          server,
          RFID_SERVICE_SOCKET_PATH,
          sveltekit->getSecurityManager(),
          AuthenticationPredicates::IS_AUTHENTICATED),
      _httpEndpoint(RFIDState::read,
                    RFIDState::update,
                    this,
                    server,
                    RFID_SERVICE_ENDPOINT_PATH,
                    sveltekit->getSecurityManager(),
                    AuthenticationPredicates::IS_AUTHENTICATED),
      _eventEndpoint(RFIDState::read,
                     RFIDState::update,
                     this,
                     sveltekit->getSocket(),
                     RFID_SERVICE_EVENT),
      _fsPersistence(RFIDState::read, RFIDState::update, this, sveltekit->getFS(), "/config/rfid.json"),
      _ssPin(ssPin),
      _rstPin(rstPin),
      _sckPin(sckPin),
      _misoPin(misoPin),
      _mosiPin(mosiPin),
      _rfid(_ssPin, MFRC522::UNUSED_PIN),
      _resetMode(false),
      _hardwareOk(false) {}

void RFIDService::begin()
{
    Serial.println("Initializing RFIDService...");

    // 1. Configura pinos CS e RST
    pinMode(_ssPin, OUTPUT);
    digitalWrite(_ssPin, HIGH);
    pinMode(_rstPin, OUTPUT);

    // 2. Pulso de Hardware Reset ativo no MFRC522
    pulseHardwareReset();
    gpio_set_drive_capability((gpio_num_t)_rstPin, GPIO_DRIVE_CAP_3);

    // 3. Inicializa barramento SPI (biblioteca não tocará no _rstPin pois passamos UNUSED_PIN)
    SPI.begin(_sckPin, _misoPin, _mosiPin, _ssPin);

    // 4. Configura registradores, ganho de antena estável (43dB) e liga o campo de RF
    configureReader();

    // 5. Exibe versao do chip
    _rfid.PCD_DumpVersionToSerial();

    byte tx = _rfid.PCD_ReadRegister(MFRC522::TxControlReg);
    byte cmd = _rfid.PCD_ReadRegister(MFRC522::CommandReg);
    Serial.printf("MFRC522 Inicial: TxControlReg=0x%02X (Antena %s), CommandReg=0x%02X\n",
                  tx, ((tx & 0x03) == 0x03) ? "LIGADA (OK)" : "DESLIGADA (FALHA)", cmd);

    checkHardwareStatus();
    if (_hardwareOk)
    {
        runSelfTest();
    }
    setupEndpoints();
    _httpEndpoint.begin();
    _eventEndpoint.begin();
    _webSocketServer.begin();
    _fsPersistence.readFromFS();
}

void RFIDService::pulseHardwareReset()
{
    // LOW por 25ms, depois HIGH por 50ms para estabilizar o oscilador do MFRC522
    digitalWrite(_rstPin, LOW);
    delay(25);
    digitalWrite(_rstPin, HIGH);
    delay(50);
}

void RFIDService::configureReader(bool enableAntenna)
{
    // Soft reset: restaura todos os registradores e desliga a antena
    _rfid.PCD_Reset();

    _rfid.PCD_WriteRegister(MFRC522::CommandReg, MFRC522::PCD_Idle);
    _rfid.PCD_WriteRegister(MFRC522::TxModeReg, 0x00);
    _rfid.PCD_WriteRegister(MFRC522::RxModeReg, 0x00);
    _rfid.PCD_WriteRegister(MFRC522::ModWidthReg, 0x26);
    _rfid.PCD_WriteRegister(MFRC522::TModeReg, 0x80);
    _rfid.PCD_WriteRegister(MFRC522::TPrescalerReg, 0xA9);
    _rfid.PCD_WriteRegister(MFRC522::TReloadRegH, 0x03);
    _rfid.PCD_WriteRegister(MFRC522::TReloadRegL, 0xE8);
    _rfid.PCD_WriteRegister(MFRC522::TxASKReg, 0x40);
    _rfid.PCD_WriteRegister(MFRC522::ModeReg, 0x3D);
    _rfid.PCD_WriteRegister(MFRC522::CWGsPReg, _txConductance);
    _rfid.PCD_SetAntennaGain(READER_RX_GAIN);
    if (enableAntenna)
    {
        _rfid.PCD_AntennaOn();
    }
}

bool RFIDService::isReaderConfigured()
{
    byte txControl = _rfid.PCD_ReadRegister(MFRC522::TxControlReg);
    byte rfCfg = _rfid.PCD_ReadRegister(MFRC522::RFCfgReg);
    return (txControl & TX_ANTENNA_MASK) == TX_ANTENNA_MASK &&
           (rfCfg & RX_GAIN_MASK) == READER_RX_GAIN;
}

bool RFIDService::isGainRetained()
{
    return (_rfid.PCD_ReadRegister(MFRC522::RFCfgReg) & RX_GAIN_MASK) == READER_RX_GAIN;
}

bool RFIDService::verifyRegisterWrite(MFRC522::PCD_Register reg, byte value)
{
    _rfid.PCD_WriteRegister(reg, value);
    return _rfid.PCD_ReadRegister(reg) == value;
}

void RFIDService::logSelfTestStage(const char *stage)
{
    Serial.printf("[RFID-SELFTEST] %-28s txCtrl=0x%02X rfCfg=0x%02X cmd=0x%02X cwGsP=0x%02X\n",
                  stage,
                  _rfid.PCD_ReadRegister(MFRC522::TxControlReg),
                  _rfid.PCD_ReadRegister(MFRC522::RFCfgReg),
                  _rfid.PCD_ReadRegister(MFRC522::CommandReg),
                  _rfid.PCD_ReadRegister(MFRC522::CWGsPReg));
}

bool RFIDService::antennaHoldsConfig(const char *label)
{
    _rfid.PCD_AntennaOn();
    delay(SELFTEST_HOLD_MS);
    logSelfTestStage(label);
    if (!isReaderConfigured())
    {
        return false;
    }

    // Transmissão real (REQA): é o pico de corrente do transmissor
    byte atqa[2];
    byte size = sizeof(atqa);
    _rfid.PICC_RequestA(atqa, &size);
    delay(SELFTEST_SETTLE_MS);
    logSelfTestStage("apos transmissao REQA");
    return isReaderConfigured();
}

void RFIDService::runSelfTest()
{
    Serial.println("[RFID-SELFTEST] Iniciando autoteste do MFRC522...");

    // 1. Integridade da escrita SPI, com antena desligada
    configureReader(false);
    bool spiWriteOk = verifyRegisterWrite(MFRC522::ModWidthReg, SELFTEST_PATTERN_A) &&
                      verifyRegisterWrite(MFRC522::ModWidthReg, SELFTEST_PATTERN_B);
    _rfid.PCD_WriteRegister(MFRC522::ModWidthReg, 0x26);

    // 2. Retenção da configuração sem campo de RF
    delay(SELFTEST_HOLD_MS);
    logSelfTestStage("antena OFF");
    bool holdsWithoutRf = isGainRetained();

    // 3. Retenção com antena ligada em potência nominal
    bool holdsNominal = spiWriteOk && holdsWithoutRf && antennaHoldsConfig("antena ON (potencia nominal)");

    // 4. Fallback: potência de transmissão reduzida (menor consumo, menor alcance)
    bool holdsLowPower = false;
    if (spiWriteOk && holdsWithoutRf && !holdsNominal)
    {
        _txConductance = CWGSP_LOW_POWER;
        configureReader(false);
        holdsLowPower = antennaHoldsConfig("antena ON (potencia reduzida)");
        if (!holdsLowPower)
        {
            _txConductance = CWGSP_NOMINAL;
        }
    }

    if (!spiWriteOk)
    {
        Serial.println("[RFID-SELFTEST] FALHA: escrita SPI nao confere. Verifique fios MOSI(23), SCK(18), SDA/SS(5) e GND.");
    }
    else if (!holdsWithoutRf)
    {
        Serial.println("[RFID-SELFTEST] FALHA: chip perde a configuracao mesmo com antena desligada. Verifique RST (GPIO4) e a alimentacao 3,3V/GND.");
    }
    else if (holdsNominal)
    {
        Serial.println("[RFID-SELFTEST] OK: leitor estavel em potencia nominal.");
    }
    else if (holdsLowPower)
    {
        Serial.println("[RFID-SELFTEST] ATENCAO: chip reinicia ao energizar a antena em potencia nominal. Operando em POTENCIA REDUZIDA (alcance menor). Alimentacao 3,3V do modulo insuficiente.");
    }
    else
    {
        Serial.println("[RFID-SELFTEST] FALHA: chip reinicia ao energizar a antena mesmo em potencia reduzida. Alimentacao 3,3V/GND do modulo deficiente ou modulo/antena danificados.");
    }

    configureReader(true);
}

bool RFIDService::ensureReaderReady()
{
    if (!_hardwareOk)
    {
        return false;
    }

    if (isReaderConfigured())
    {
        return true;
    }

    // Backoff exponencial: cada reconfiguração bloqueia a loopTask (~50ms de soft reset),
    // então resets repetidos não podem monopolizar o loop
    unsigned long nowMs = millis();
    if (_recoveryCount > 0 && nowMs - _lastRecoveryMs < _recoveryIntervalMs)
    {
        return false;
    }

    if (_recoveryCount > 0 && nowMs - _lastRecoveryMs < RECOVERY_STABLE_MS)
    {
        _recoveryIntervalMs = min(_recoveryIntervalMs * 2, RECOVERY_MAX_INTERVAL_MS);
    }
    else
    {
        _recoveryIntervalMs = RECOVERY_BASE_INTERVAL_MS;
    }
    _lastRecoveryMs = nowMs;
    _recoveryCount++;

    byte txControl = _rfid.PCD_ReadRegister(MFRC522::TxControlReg);
    byte rfCfg = _rfid.PCD_ReadRegister(MFRC522::RFCfgReg);
    Serial.printf("[RFID] Leitor perdeu a configuracao (#%lu: txCtrl=0x%02X rfCfg=0x%02X). Reconfigurando; proxima tentativa em >= %lums\n",
                  _recoveryCount, txControl, rfCfg, _recoveryIntervalMs);

    configureReader();
    return isReaderConfigured();
}

void RFIDService::setupEndpoints()
{
    _server->on(RFID_SERVICE_START_RESET, HTTP_GET, _securityManager->wrapRequest(std::bind(&RFIDService::handleStartReset, this, std::placeholders::_1), AuthenticationPredicates::IS_AUTHENTICATED));

    ESP_LOGV(SVK_TAG, "Registered GET endpoint: %s", RFID_SERVICE_START_RESET);

    _server->on(RFID_SERVICE_STOP_RESET, HTTP_GET, _securityManager->wrapRequest(std::bind(&RFIDService::handleStopReset, this, std::placeholders::_1), AuthenticationPredicates::IS_AUTHENTICATED));

    ESP_LOGV(SVK_TAG, "Registered GET endpoint: %s", RFID_SERVICE_STOP_RESET);
}

void RFIDService::stopResetMode()
{
    if (_resetMode)
    {
        Serial.println("Stopping RFID reset mode automatically");
        _resetMode = false;
        if (_display)
        {
            _display->clear();
        }
    }
}

int RFIDService::getCardNumber(const String &uid)
{
    int foundNumber = 0;
    this->read([&](RFIDState &state) {
        auto it = state._cardRegistry.find(uid);
        if (it != state._cardRegistry.end())
        {
            foundNumber = it->second;
        }
    });
    return foundNumber;
}

String RFIDService::getCardUid(int numero)
{
    String foundUid = "";
    if (numero <= 0)
    {
        return foundUid;
    }
    this->read([&](RFIDState &state) {
        for (const auto &entry : state._cardRegistry)
        {
            if (entry.second == numero)
            {
                foundUid = entry.first;
                break;
            }
        }
    });
    return foundUid;
}

esp_err_t RFIDService::handleStartReset(PsychicRequest *request)
{
    Serial.println("Starting RFID reset mode");

    if (_onResetStartedCallback)
    {
        _onResetStartedCallback();
    }

    _resetMode = true;

    int nextIndex = 1;
    this->update([&](RFIDState& state) {
        state = RFIDState(); // Reinicializa o estado com segurança de thread sob o mutex
        nextIndex = state.nextIndex;
        return StateUpdateResult::CHANGED; // Notifica StatefulService
    }, "handleStartReset");

    PsychicJsonResponse response = PsychicJsonResponse(request, false);

    JsonObject root = response.getRoot();

    root["status"] = "reset mode started";
    root["resetMode"] = true;
    root["nextIndex"] = nextIndex;

    if (_display)
    {
        _display->showResetCount(0);
    }

    return response.send();
}

esp_err_t RFIDService::handleStopReset(PsychicRequest *request)
{
    Serial.println("Stopping RFID reset mode");
    _resetMode = false;

    PsychicJsonResponse response = PsychicJsonResponse(request, false);

    JsonObject root = response.getRoot();

    root["status"] = "reset mode stopped";
    root["resetMode"] = false;

    JsonArray array = root["cards"].to<JsonArray>();

    this->read([&](RFIDState& state) {
        for (const auto &entry : state._cardRegistry)
        {
            JsonObject obj = array.add<JsonObject>();
            obj["uid"] = entry.first;
            obj["numero"] = entry.second;
        }
    });

    if (_display)
    {
        _display->clear();
    }

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

bool RFIDService::clearRFIDData(MFRC522::Uid *uid)
{
    MFRC522::MIFARE_Key key = getDefaultKey();
    bool allSuccess = true;

    // Ignorar setor 0 (UID e dados críticos do fabricante)
    for (byte sector = 1; sector < 16; sector++)
    {
        byte trailerBlock = (sector + 1) * 4 - 1;

        // Autentica o setor com até 2 tentativas para tolerar ruídos de RF
        MFRC522::StatusCode authStatus = MFRC522::STATUS_ERROR;
        for (int authRetry = 0; authRetry < 2; authRetry++)
        {
            authStatus = _rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, uid);
            if (authStatus == MFRC522::STATUS_OK)
            {
                break;
            }
            delay(10);
        }

        if (authStatus != MFRC522::STATUS_OK)
        {
            Serial.printf("Falha na autenticação do setor %d: %s\n", sector, _rfid.GetStatusCodeName(authStatus));
            allSuccess = false;
            break;
        }

        for (byte relBlock = 0; relBlock < 3; relBlock++)
        {
            byte absBlock = (sector * 4) + relBlock;
            byte buffer[16] = {0};

            // Escrita com retry (até 3 tentativas) e restauração do canal criptográfico
            bool blockWritten = false;
            for (int retry = 0; retry < 3; retry++)
            {
                if (retry > 0)
                {
                    delay(15);
                    // Re-autentica o trailer do setor para restabelecer o canal Crypto1 se desincronizado
                    _rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, trailerBlock, &key, uid);
                }

                MFRC522::StatusCode writeStatus = _rfid.MIFARE_Write(absBlock, buffer, 16);
                if (writeStatus == MFRC522::STATUS_OK)
                {
                    blockWritten = true;
                    break;
                }
                else
                {
                    Serial.printf("[RFID] Tentativa %d/%d falhou no bloco %d: %s\n",
                                  retry + 1, 3, absBlock, _rfid.GetStatusCodeName(writeStatus));
                }
            }

            if (!blockWritten)
            {
                Serial.printf("Falha definitiva ao escrever no bloco %d\n", absBlock);
                allSuccess = false;
                break;
            }
        }

        if (!allSuccess)
        {
            break;
        }
    }

    if (allSuccess)
    {
        Serial.println("Processo de limpeza concluído com sucesso");
    }
    else
    {
        Serial.println("Processo de limpeza falhou");
    }

    _rfid.PICC_HaltA();
    _rfid.PCD_StopCrypto1();
    return allSuccess;
}

void RFIDService::logDiagnostics()
{
    byte version = _rfid.PCD_ReadRegister(MFRC522::VersionReg);
    byte txControl = _rfid.PCD_ReadRegister(MFRC522::TxControlReg);
    byte rfCfg = _rfid.PCD_ReadRegister(MFRC522::RFCfgReg);
    byte command = _rfid.PCD_ReadRegister(MFRC522::CommandReg);
    bool antennaOn = (txControl & 0x03) == 0x03;

    Serial.printf("[RFID-DIAG] t=%lu reset=%d hwOk=%d ver=0x%02X txCtrl=0x%02X(antena %s) rfCfg=0x%02X cmd=0x%02X polls=%lu detect=%lu recoveries=%lu\n",
                  millis(), _resetMode, _hardwareOk, version, txControl, antennaOn ? "ON" : "OFF",
                  rfCfg, command, _pollCount, _detectCount, _recoveryCount);
}

void RFIDService::loop()
{
    yield(); // Permite que o ESP32 execute tarefas internas cooperativamente

    unsigned long nowMs = millis();
    if (nowMs - _lastDiagMs >= DIAG_INTERVAL_MS)
    {
        _lastDiagMs = nowMs;
        logDiagnostics();
    }

    if (!_hardwareOk)
    {
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck > 5000)
        {
            lastCheck = millis();
            pulseHardwareReset();
            configureReader();
            checkHardwareStatus(); // Tenta recuperar
        }
        return;
    }

    // Se cronometragem estiver ativa, cede a leitura para CronometrarService
    if (_isCronometrandoCallback && _isCronometrandoCallback())
    {
        return;
    }

    // Intervalo de polling de 60ms para evitar sobrecarga de RF e oscilação de alimentação
    static unsigned long lastPollMs = 0;
    if (nowMs - lastPollMs < 60)
    {
        return;
    }
    lastPollMs = nowMs;

    // Garante que o MFRC522 mantém a configuração (ele pode reiniciar por queda de tensão/ruído no RST)
    if (!ensureReaderReady())
    {
        return;
    }

    _pollCount++;
    if (!_rfid.PICC_IsNewCardPresent())
        return;
    _detectCount++;
    if (!_rfid.PICC_ReadCardSerial())
    {
        Serial.println("[RFID] Cartão detectado (ATQA) mas falha no ReadCardSerial/anticolisão");
        return;
    }

    if (_resetMode)
    {
        String uidStr = uidToString(&_rfid.uid);
        bool cardExists = false;
        int existingNumber = 0;

        this->read([&](RFIDState& state) {
            auto it = state._cardRegistry.find(uidStr);
            if (it != state._cardRegistry.end())
            {
                cardExists = true;
                existingNumber = it->second;
            }
        });

        // Sempre executa a limpeza física dos blocos do cartão e verifica o sucesso
        bool cleared = clearRFIDData(&_rfid.uid);
        if (!cleared)
        {
            Serial.printf("Falha ao limpar os blocos físicos do cartão %s. Operação abortada.\n", uidStr.c_str());
            if (_buzzer)
            {
                _buzzer->beepError();
            }
            if (_display)
            {
                _display->showError(2000);
            }
            return;
        }

        if (_buzzer)
        {
            _buzzer->beep(180);
        }

        if (cardExists)
        {
            Serial.printf("Card %s already exists with number %d (physical blocks re-cleared)\n", uidStr.c_str(), existingNumber);
            if (_display)
            {
                _display->showResetCount(existingNumber);
            }
        }
        else
        {
            this->update([&](RFIDState& state) {
                int assignedNumber = state.nextIndex++;
                state._cardRegistry[uidStr] = assignedNumber;
                Serial.printf("Card %s reset and stored as %d\n", uidStr.c_str(), assignedNumber);
                if (_display)
                {
                    _display->showResetCount(assignedNumber);
                }
                return StateUpdateResult::CHANGED; // notify StatefulService by returning CHANGED
            }, "loopRFIDService");
        }
    }
    else
    {
        // Standby / Diagnóstico: qualquer aproximação de cartão emite bipe e imprime UID na serial
        static unsigned long lastStandbyLog = 0;
        static String lastStandbyUid = "";
        String uidStr = uidToString(&_rfid.uid);
        if (uidStr != lastStandbyUid || millis() - lastStandbyLog > 1200)
        {
            lastStandbyUid = uidStr;
            lastStandbyLog = millis();
            Serial.printf("[RFID-STANDBY] Cartão detectado: %s\n", uidStr.c_str());
            if (_buzzer)
            {
                _buzzer->beep(120);
            }
        }
        _rfid.PICC_HaltA();
        _rfid.PCD_StopCrypto1();
    }
}

MFRC522::MIFARE_Key RFIDService::getDefaultKey()
{
    MFRC522::MIFARE_Key key;
    for (byte i = 0; i < 6; i++)
    {
        key.keyByte[i] = 0xFF;
    }
    return key;
}

bool RFIDService::readCardData(MFRC522 &rfid, byte startBlock, byte blockCount, String &outData, const MFRC522::MIFARE_Key *customKey)
{
    outData = "";
    outData.reserve(blockCount * 16);

    if (rfid.uid.size == 0)
    {
        Serial.println("Erro ao ler RFID: UID do cartão não disponível para autenticação.");
        return false;
    }

    MFRC522::MIFARE_Key key = (customKey != nullptr) ? *customKey : getDefaultKey();

    int currentSector = -1;
    byte blocksRead = 0;
    byte block = startBlock;

    while (blocksRead < blockCount)
    {
        if (block >= 64)
        {
            Serial.println("Aviso: Limite de blocos do cartão MIFARE Classic 1K atingido.");
            break;
        }

        // pula blocos de trailer de setor
        if ((block + 1) % 4 == 0)
        {
            block++;
            continue;
        }

        byte sector = block / 4;
        if (sector != currentSector)
        {
            byte trailerBlock = (sector * 4) + 3;
            MFRC522::StatusCode status = rfid.PCD_Authenticate(
                MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                trailerBlock,
                &key,
                &(rfid.uid)
            );

            if (status != MFRC522::STATUS_OK)
            {
                Serial.printf("Falha na autenticação do setor %d para leitura: %s\n", sector, rfid.GetStatusCodeName(status));
                return false;
            }
            currentSector = sector;
        }

        byte buffer[18];
        byte size = sizeof(buffer);
        MFRC522::StatusCode status = rfid.MIFARE_Read(block, buffer, &size);

        if (status != MFRC522::STATUS_OK)
        {
            Serial.printf("Erro ao ler bloco %d: %s\n", block, rfid.GetStatusCodeName(status));
            return false;
        }

        bool endOfString = false;
        for (int i = 0; i < 16; i++)
        {
            if (buffer[i] == 0)
            {
                endOfString = true;
                break; // fim da string
            }
            outData += (char)buffer[i];
        }

        if (endOfString)
        {
            break;
        }

        blocksRead++;
        block++;
    }

    return true;
}

String RFIDService::readCardData(MFRC522 &rfid, byte startBlock, byte blockCount, const MFRC522::MIFARE_Key *customKey)
{
    String result = "";
    readCardData(rfid, startBlock, blockCount, result, customKey);
    return result;
}

bool RFIDService::writeCardData(MFRC522 &rfid, byte startBlock, const String &data, const MFRC522::MIFARE_Key *customKey)
{
    if (rfid.uid.size == 0)
    {
        Serial.println("Erro ao gravar RFID: UID do cartão não disponível para autenticação.");
        return false;
    }

    const byte bytesPerBlock = 16;
    int dataLen = data.length();
    // Inclui sempre 1 byte extra para o terminador nulo '\0', garantindo leitura dinâmica segura
    int totalBytesWithNull = dataLen + 1;
    int blocksNeeded = (totalBytesWithNull + bytesPerBlock - 1) / bytesPerBlock;
    if (blocksNeeded == 0)
    {
        blocksNeeded = 1;
    }

    // [P0-04] Validação pré-escrita de capacidade no MIFARE 1K:
    // Calcula o bloco final real (pulando trailers de setor nos blocos 7, 11, ..., 63) ANTES de iniciar a escrita física
    byte testBlock = startBlock;
    byte dataBlocksFound = 0;
    while (dataBlocksFound < blocksNeeded)
    {
        if (testBlock >= 64)
        {
            Serial.printf("Erro: Capacidade do cartão MIFARE Classic 1K excedida (%d blocos necessários a partir do bloco %d).\n",
                          blocksNeeded, startBlock);
            return false;
        }
        if ((testBlock + 1) % 4 == 0)
        {
            testBlock++;
            continue;
        }
        dataBlocksFound++;
        if (dataBlocksFound < blocksNeeded)
        {
            testBlock++;
        }
    }

    if (testBlock > 62)
    {
        Serial.printf("Erro: Bloco final calculado (%d) excede o limite de dados úteis do cartão (bloco 62 = 720 B).\n", testBlock);
        return false;
    }

    MFRC522::MIFARE_Key key = (customKey != nullptr) ? *customKey : getDefaultKey();
    byte buffer[bytesPerBlock];
    int currentSector = -1;
    byte written = 0;
    byte block = startBlock;

    while (written < blocksNeeded)
    {
        if (block >= 64)
        {
            Serial.println("Erro: Limite de blocos do cartão MIFARE Classic 1K excedido durante escrita.");
            return false;
        }

        // pula blocos de trailer
        if ((block + 1) % 4 == 0)
        {
            block++;
            continue;
        }

        byte sector = block / 4;
        if (sector != currentSector)
        {
            byte trailerBlock = (sector * 4) + 3;
            MFRC522::StatusCode status = rfid.PCD_Authenticate(
                MFRC522::PICC_CMD_MF_AUTH_KEY_A,
                trailerBlock,
                &key,
                &(rfid.uid)
            );

            if (status != MFRC522::STATUS_OK)
            {
                Serial.printf("Falha na autenticação do setor %d para escrita: %s\n", sector, rfid.GetStatusCodeName(status));
                return false;
            }
            currentSector = sector;
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

bool RFIDService::readCardBlock(MFRC522 &rfid, byte block, byte buffer[16], const MFRC522::MIFARE_Key *customKey)
{
    if (block >= 64 || (block + 1) % 4 == 0)
    {
        return false; // Bloco fora da faixa do MIFARE 1K ou bloco trailer de setor
    }
    if (rfid.uid.size == 0)
    {
        return false;
    }

    MFRC522::MIFARE_Key key = (customKey != nullptr) ? *customKey : getDefaultKey();
    byte sector = block / 4;
    byte trailerBlock = (sector * 4) + 3;

    MFRC522::StatusCode status = rfid.PCD_Authenticate(
        MFRC522::PICC_CMD_MF_AUTH_KEY_A,
        trailerBlock,
        &key,
        &(rfid.uid)
    );

    if (status != MFRC522::STATUS_OK)
    {
        return false;
    }

    byte readBuf[18];
    byte size = sizeof(readBuf);
    status = rfid.MIFARE_Read(block, readBuf, &size);
    if (status != MFRC522::STATUS_OK)
    {
        return false;
    }

    memcpy(buffer, readBuf, 16);
    return true;
}

bool RFIDService::writeCardBlock(MFRC522 &rfid, byte block, const byte buffer[16], const MFRC522::MIFARE_Key *customKey)
{
    if (block >= 64 || (block + 1) % 4 == 0)
    {
        return false; // Bloco fora da faixa do MIFARE 1K ou bloco trailer de setor
    }
    if (rfid.uid.size == 0)
    {
        return false;
    }

    MFRC522::MIFARE_Key key = (customKey != nullptr) ? *customKey : getDefaultKey();
    byte sector = block / 4;
    byte trailerBlock = (sector * 4) + 3;

    for (int retry = 0; retry < 2; retry++)
    {
        MFRC522::StatusCode status = rfid.PCD_Authenticate(
            MFRC522::PICC_CMD_MF_AUTH_KEY_A,
            trailerBlock,
            &key,
            &(rfid.uid)
        );

        if (status != MFRC522::STATUS_OK)
        {
            if (retry == 0) { delay(5); continue; }
            return false;
        }

        status = rfid.MIFARE_Write(block, const_cast<byte*>(buffer), 16);
        if (status == MFRC522::STATUS_OK)
        {
            return true;
        }
        if (retry == 0) { delay(5); }
    }

    return false;
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
