#ifndef CronometrarService_h
#define CronometrarService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <MFRC522.h>
#include "BuzzerService.h"
#include "DisplayService.h"
#include "CronometragemService.h"
#include "RFIDService.h"
#include "RFIDCardLayout.h"
#include "AtletaService.h"
#include "ProvaService.h"

#define CRONOMETRAR_SETTINGS_ENDPOINT_PATH "/rest/cronometrar"

    
class Cronometrar
{
public:
    static constexpr int MIN_INTERVALO_VOLTA_SEG = 3;
    static constexpr int MAX_INTERVALO_VOLTA_SEG = 3600;
    static constexpr int DEFAULT_INTERVALO_VOLTA_SEG = 10;

    bool cronometrar = false;
    std::string tipo = "especial";      // 'especial' | 'laps'
    std::string modoVoltas = "unica";   // 'unica' | 'duas' (quando tipo == 'laps')
    std::string especial = "E1";        // 'E1' .. 'E10'
    std::string posicao = "inicio";     // 'inicio' | 'fim'
    int intervaloMinVoltaSegundos = DEFAULT_INTERVALO_VOLTA_SEG;

    static bool isValidStageName(const std::string &estagio, int maxEstagios = Prova::MAX_ESTAGIOS)
    {
        if (estagio.size() < 2 || estagio[0] != 'E')
        {
            return false;
        }
        int num = std::atoi(estagio.c_str() + 1);
        return num >= Prova::MIN_ESTAGIOS && num <= maxEstagios;
    }

    static void read(Cronometrar &settings, JsonObject &root)
    {
        root["cronometrar"] = settings.cronometrar;
        root["tipo"] = settings.tipo;
        root["modoVoltas"] = (settings.modoVoltas == "duas") ? "duas" : "unica";
        root["especial"] = settings.especial;
        root["posicao"] = settings.posicao;
        root["intervaloMinVoltaSegundos"] =
            (settings.intervaloMinVoltaSegundos >= MIN_INTERVALO_VOLTA_SEG &&
             settings.intervaloMinVoltaSegundos <= MAX_INTERVALO_VOLTA_SEG)
                ? settings.intervaloMinVoltaSegundos
                : DEFAULT_INTERVALO_VOLTA_SEG;
    }

    static StateUpdateResult update(JsonObject &root, Cronometrar &cronometrar)
    {
        if (root.isNull() || root.size() == 0)
        {
            return StateUpdateResult::UNCHANGED;
        }

        if (!root["cronometrar"].is<bool>() || !root["tipo"].is<const char*>())
        {
            return StateUpdateResult::ERROR;
        }

        bool newCronometrar = root["cronometrar"].as<bool>();
        std::string newTipo = root["tipo"].as<std::string>();
        if (newTipo != "especial" && newTipo != "laps")
        {
            return StateUpdateResult::ERROR;
        }

        std::string newModoVoltas = cronometrar.modoVoltas.empty() ? "unica" : cronometrar.modoVoltas;
        if (!root["modoVoltas"].isNull())
        {
            if (!root["modoVoltas"].is<const char*>())
            {
                return StateUpdateResult::ERROR;
            }
            newModoVoltas = root["modoVoltas"].as<std::string>();
            if (newModoVoltas != "unica" && newModoVoltas != "duas")
            {
                return StateUpdateResult::ERROR;
            }
        }

        std::string newEspecial = root["especial"].is<const char*>()
                                      ? root["especial"].as<std::string>()
                                      : cronometrar.especial;
        std::string newPosicao = root["posicao"].is<const char*>()
                                     ? root["posicao"].as<std::string>()
                                     : ((newTipo == "laps" && newModoVoltas == "unica") ? "inicio" : "");

        if (newTipo == "especial")
        {
            if (!isValidStageName(newEspecial) || (newPosicao != "inicio" && newPosicao != "fim"))
            {
                return StateUpdateResult::ERROR;
            }
        }
        else // newTipo == "laps"
        {
            if (newModoVoltas == "duas")
            {
                if (newPosicao != "inicio" && newPosicao != "fim")
                {
                    return StateUpdateResult::ERROR;
                }
            }
            else if (newPosicao.empty())
            {
                newPosicao = "inicio";
            }
        }

        int newIntervaloMinVolta = cronometrar.intervaloMinVoltaSegundos;
        if (!root["intervaloMinVoltaSegundos"].isNull())
        {
            newIntervaloMinVolta = root["intervaloMinVoltaSegundos"].as<int>();
            if (newIntervaloMinVolta < MIN_INTERVALO_VOLTA_SEG || newIntervaloMinVolta > MAX_INTERVALO_VOLTA_SEG)
            {
                return StateUpdateResult::ERROR;
            }
        }

        if (cronometrar.cronometrar == newCronometrar &&
            cronometrar.tipo == newTipo &&
            cronometrar.modoVoltas == newModoVoltas &&
            cronometrar.especial == newEspecial &&
            cronometrar.posicao == newPosicao &&
            cronometrar.intervaloMinVoltaSegundos == newIntervaloMinVolta)
        {
            return StateUpdateResult::UNCHANGED;
        }

        cronometrar.cronometrar = newCronometrar;
        cronometrar.tipo = newTipo;
        cronometrar.modoVoltas = newModoVoltas;
        cronometrar.especial = newEspecial;
        cronometrar.posicao = newPosicao;
        cronometrar.intervaloMinVoltaSegundos = newIntervaloMinVolta;

        return StateUpdateResult::CHANGED;
    }
};

class CronometrarService : public StatefulService<Cronometrar>
{
public:
    static constexpr unsigned long DEBOUNCE_UID_MS = 3000;

    CronometrarService(
                    PsychicHttpServer *server,
                    ESP32SvelteKit *sveltekit, 
                    FS *fs, 
                    MFRC522 *rfid,
                    BuzzerService *buzzer, 
                    DisplayService *display,
                    CronometragemService *cronometragemService = nullptr,
                    RFIDService *rfidService = nullptr,
                    AtletaService *atletaService = nullptr,
                    ProvaService *provaService = nullptr);

    void begin();
    void loop();
    void stopCronometragem();
    bool isCronometrando() const { return _cronometrar; }

    static String extrairHorarioCartao(const JsonDocument &doc, const String &especial, const String &posicao);
    static void atualizarHorarioCartaoCompacto(JsonDocument &doc, const String &uid, const String &especial, const String &posicao, const String &horario);

    // Helpers de cartão RFID para o Modo Voltas (RF-10 / BL-08)
    static int obterQuantidadePassagensVoltaUnicaCartao(const JsonDocument &doc);
    static String extrairUltimaPassagemVoltaUnicaCartao(const JsonDocument &doc);
    static void adicionarPassagemVoltaUnicaCartao(JsonDocument &doc, const String &uid, const String &horario);

private:
    FSPersistence<Cronometrar> _fsPersistence;
    HttpEndpoint<Cronometrar> _httpEndpoint;
    CronometragemService *_cronometragemService;
    RFIDService *_rfidService;
    AtletaService *_atletaService;
    ProvaService *_provaService;
    DisplayService *_display;
    MFRC522 *_rfid;
    BuzzerService *_buzzer;

    bool _cronometrar = false;
    String _lastReadUid = "";
    unsigned long _lastReadTimeMs = 0;

    void processarPassagemEstagio(const String &uid, int cardNumber, JsonDocument &doc, const Cronometrar &cfg, const DateTime &now, const String &horarioAtual, bool isBinaryCard, const RFIDCard::Header *existingHeader = nullptr);
    void processarPassagemVoltaUnica(const String &uid, int cardNumber, JsonDocument &doc, const Cronometrar &cfg, const DateTime &now, const String &horarioAtual, bool isBinaryCard);
    void processarPassagemVoltaDuasEstacoes(const String &uid, int cardNumber, JsonDocument &doc, const Cronometrar &cfg, const DateTime &now, const String &horarioAtual, bool isBinaryCard);

    StateUpdateResult updateFromHttp(JsonObject &root, Cronometrar &cronometrar);
    void registerConfig();
    void onConfigUpdated();
};

#endif
