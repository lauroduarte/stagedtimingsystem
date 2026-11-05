#ifndef RFIDService_h
#define RFIDService_h

#include <Arduino.h>
#include <vector>
#include <map>
#include <functional>
#include <MFRC522.h>
#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <WebSocketServer.h>
#include <EventEndpoint.h>
#include <DisplayService.h>
#include <BuzzerService.h>
#include <SPI.h>

// Definições de hardware para barramento VSPI e MFRC522 no ESP32
#define RST_PIN 4  // Reset / Power down
#define SS_PIN  5  // Slave Select (SDA / SS)
#define SCK_PIN 18 // Serial Clock (SCK)
#define MISO_PIN 19 // Master In Slave Out (MISO)
#define MOSI_PIN 23 // Master Out Slave In (MOSI)

#define RFID_SERVICE_START_RESET "/rest/rfid/start_reset"
#define RFID_SERVICE_STOP_RESET "/rest/rfid/stop_reset"
#define RFID_SERVICE_ENDPOINT_PATH "/rest/rfid"
#define RFID_SERVICE_SOCKET_PATH "/ws/rfid"
#define RFID_SERVICE_EVENT "rfid"


class RFIDState{
    public:
        std::map<String, int> _cardRegistry;
        int nextIndex = 1;

    static void read(RFIDState &state, JsonObject &json) {
        JsonArray cards = json["cards"].to<JsonArray>();
        for (const auto &entry : state._cardRegistry) {
            JsonObject card = cards.add<JsonObject>();
            card["uid"] = entry.first;
            card["numero"] = entry.second;
        }
        json["nextIndex"] = state.nextIndex;
    }

    static StateUpdateResult update(JsonObject &json, RFIDState &state) {
        state._cardRegistry.clear();
        int maxNumero = 0;
        if(json["cards"].is<JsonArray>())
        {
            for(JsonVariant card : json["cards"].as<JsonArray>())
            {
                if(card.is<JsonObject>())
                {
                    String uid = card["uid"].as<String>();
                    int numero = card["numero"];
                    if (!uid.isEmpty() && numero > 0)
                    {
                        state._cardRegistry[uid] = numero;
                        if (numero > maxNumero)
                        {
                            maxNumero = numero;
                        }
                    }
                }
            }
            state.nextIndex = maxNumero + 1;
            return StateUpdateResult::CHANGED;
        } else {
            state.nextIndex = 1;
            return StateUpdateResult::UNCHANGED;
        }
    }

};

class RFIDService : public StatefulService<RFIDState> {
public:
    static constexpr uint8_t DEFAULT_RST_PIN = RST_PIN;
    static constexpr uint8_t DEFAULT_SS_PIN = SS_PIN;
    static constexpr int8_t DEFAULT_SCK_PIN = SCK_PIN;
    static constexpr int8_t DEFAULT_MISO_PIN = MISO_PIN;
    static constexpr int8_t DEFAULT_MOSI_PIN = MOSI_PIN;
    static constexpr byte FIRST_DATA_BLOCK = 4;
    static constexpr byte MAX_DATA_BLOCKS = 45;

    RFIDService(PsychicHttpServer *server, 
                ESP32SvelteKit *sveltekit,
                SecurityManager *securityManager,
                DisplayService *display,
                FS *fs,
                BuzzerService *buzzer = nullptr,
                uint8_t ssPin = DEFAULT_SS_PIN,
                uint8_t rstPin = DEFAULT_RST_PIN,
                int8_t sckPin = DEFAULT_SCK_PIN,
                int8_t misoPin = DEFAULT_MISO_PIN,
                int8_t mosiPin = DEFAULT_MOSI_PIN);
    static MFRC522::MIFARE_Key getDefaultKey();
    static String uidToString(MFRC522::Uid *uid);
    static bool readCardData(MFRC522 &rfid, byte startBlock, byte blockCount, String &outData, const MFRC522::MIFARE_Key *customKey = nullptr);
    static String readCardData(MFRC522 &rfid, byte startBlock, byte blockCount, const MFRC522::MIFARE_Key *customKey = nullptr);
    static bool writeCardData(MFRC522 &rfid, byte startBlock, const String &data, const MFRC522::MIFARE_Key *customKey = nullptr);
    static bool readCardBlock(MFRC522 &rfid, byte block, byte buffer[16], const MFRC522::MIFARE_Key *customKey = nullptr);
    static bool writeCardBlock(MFRC522 &rfid, byte block, const byte buffer[16], const MFRC522::MIFARE_Key *customKey = nullptr);

    void checkHardwareStatus();
    bool isHardwareOk() const { return _hardwareOk; }

    // Verifica se o MFRC522 mantém a configuração (antena ligada + ganho) e, se tiver
    // voltado ao padrão de fábrica, reconfigura. Retorna true se o leitor está pronto.
    bool ensureReaderReady();

    void begin();
    void loop();
    MFRC522* getRFID() { return &_rfid; }

    bool isResetMode() const { return _resetMode; }
    void stopResetMode();
    void setOnResetStartedCallback(std::function<void()> callback) { _onResetStartedCallback = callback; }
    void setIsCronometrandoCallback(std::function<bool()> callback) { _isCronometrandoCallback = callback; }

    int getCardNumber(const String &uid);
    String getCardUid(int numero);

    uint8_t getSsPin() const { return _ssPin; }
    uint8_t getRstPin() const { return _rstPin; }
    int8_t getSckPin() const { return _sckPin; }
    int8_t getMisoPin() const { return _misoPin; }
    int8_t getMosiPin() const { return _mosiPin; }

private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    DisplayService *_display;
    BuzzerService *_buzzer;
    WebSocketServer<RFIDState> _webSocketServer;
    HttpEndpoint<RFIDState> _httpEndpoint;
    EventEndpoint<RFIDState> _eventEndpoint;
    FSPersistence<RFIDState> _fsPersistence;

    uint8_t _ssPin;
    uint8_t _rstPin;
    int8_t _sckPin;
    int8_t _misoPin;
    int8_t _mosiPin;

    MFRC522 _rfid;
    volatile bool _resetMode = false; // alterado pela task HTTP, lido no loop()
    bool _hardwareOk = false;
    std::function<void()> _onResetStartedCallback = nullptr;
    std::function<bool()> _isCronometrandoCallback = nullptr;

    static constexpr unsigned long DIAG_INTERVAL_MS = 5000;
    unsigned long _lastDiagMs = 0;
    unsigned long _pollCount = 0;
    unsigned long _detectCount = 0;
    unsigned long _recoveryCount = 0;
    unsigned long _lastRecoveryMs = 0;
    void logDiagnostics();

    static constexpr MFRC522::PCD_RxGain READER_RX_GAIN = MFRC522::RxGain_43dB;
    static constexpr byte RX_GAIN_MASK = 0x70;      // RFCfgReg[6:4]
    static constexpr byte TX_ANTENNA_MASK = 0x03;   // TxControlReg: Tx1RFEn | Tx2RFEn
    static constexpr byte CWGSP_NOMINAL = 0x20;     // CWGsPReg: valor de reset (potência nominal do transmissor)
    static constexpr byte CWGSP_LOW_POWER = 0x08;   // CWGsPReg: potência reduzida (menor pico de corrente)
    static constexpr unsigned long RECOVERY_BASE_INTERVAL_MS = 250;
    static constexpr unsigned long RECOVERY_MAX_INTERVAL_MS = 8000;
    static constexpr unsigned long RECOVERY_STABLE_MS = 10000; // sem resets por esse tempo => backoff zerado
    static constexpr unsigned long SELFTEST_HOLD_MS = 100;
    static constexpr unsigned long SELFTEST_SETTLE_MS = 5;
    static constexpr byte SELFTEST_PATTERN_A = 0x55;
    static constexpr byte SELFTEST_PATTERN_B = 0x2A;

    byte _txConductance = CWGSP_NOMINAL;
    unsigned long _recoveryIntervalMs = RECOVERY_BASE_INTERVAL_MS;

    void pulseHardwareReset();
    void configureReader(bool enableAntenna = true);
    bool isReaderConfigured();
    bool isGainRetained();
    bool verifyRegisterWrite(MFRC522::PCD_Register reg, byte value);
    bool antennaHoldsConfig(const char *label);
    void logSelfTestStage(const char *stage);
    void runSelfTest();

    void setupEndpoints();
    esp_err_t handleStartReset(PsychicRequest *request);
    esp_err_t handleStopReset(PsychicRequest *request);
    
    bool clearRFIDData(MFRC522::Uid *uid);
};

#endif
