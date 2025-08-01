#ifndef CronometrarService_h
#define CronometrarService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <MFRC522.h>
#include "BuzzerService.h"
#include "DisplayService.h"
#include "CronometragemService.h"

#define CRONOMETRAR_SETTINGS_ENDPOINT_PATH "/rest/cronometrar"

    
class Cronometrar
{
public:
    bool cronometrar;
    std::string tipo; // 'especial' | 'laps'
    std::string especial; // 'E1' | 'E2' | 'E3' | 'E4' | 'E5' | 'E6' | 'E7' | 'E8' | 'E9' | 'E10'
    std::string posicao; // 'inicio' | 'fim'

    static void read(Cronometrar &settings, JsonObject &root)
    {
        root["cronometrar"] = settings.cronometrar;
        root["tipo"] = settings.tipo;
        root["especial"] = settings.especial;
        root["posicao"] = settings.posicao;
    }

    static StateUpdateResult update(JsonObject &root, Cronometrar &cronometrar)
    {
        if (!root.containsKey("cronometrar") || !root.containsKey("tipo") ||
            !root.containsKey("especial") || !root.containsKey("posicao"))
        {
            return StateUpdateResult::ERROR;
        }

        if(root["cronometrar"] == cronometrar.cronometrar)
        {
            return StateUpdateResult::UNCHANGED;
        }

        cronometrar.cronometrar = root["cronometrar"];
        cronometrar.tipo = root["tipo"].as<std::string>();
        cronometrar.especial = root["especial"].as<std::string>();
        cronometrar.posicao = root["posicao"].as<std::string>();

        return StateUpdateResult::CHANGED;
    }
};

class CronometrarService : public StatefulService<Cronometrar>
{
public:
    CronometrarService(
                    PsychicHttpServer *server,
                    ESP32SvelteKit *sveltekit, 
                    FS *fs, 
                    MFRC522 *rfid,
                    BuzzerService *buzzer, 
                    DisplayService *display);

    void begin();

private:
    FSPersistence<Cronometrar> _fsPersistence;
    HttpEndpoint<Cronometrar> _httpEndpoint;
    CronometragemService _cronometragemService;
    DisplayService *_display;
    PsychicMqttClient *_mqttClient;
    MFRC522 _rfid;
    BuzzerService *_buzzer;

    bool _cronometrar = false;

    void loop();
    void registerConfig();
    void onConfigUpdated();
};

#endif
