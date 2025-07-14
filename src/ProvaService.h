#ifndef ProvaService_h
#define ProvaService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>


#define PROVA_SETTINGS_ENDPOINT_PATH "/rest/prova"

    
class Prova
{
public:
    std::string nome;
    std::string data;
    std::string local;

    static void read(Prova &settings, JsonObject &root)
    {
        root["nome"] = settings.nome;
        root["data"] = settings.data;
        root["local"] = settings.local;
    }

    static StateUpdateResult update(JsonObject &root, Prova &prova)
    {
        prova.nome = root["nome"].as<std::string>();
        prova.data = root["data"].as<std::string>();
        prova.local = root["local"].as<std::string>();

        return StateUpdateResult::CHANGED;
    }
};

class ProvaService : public StatefulService<Prova>
{
public:
    ProvaService(PsychicHttpServer *server,
                      ESP32SvelteKit *sveltekit, FS *fs);

    void begin();

private:
    FSPersistence<Prova> _fsPersistence;
    HttpEndpoint<Prova> _httpEndpoint;
    PsychicMqttClient *_mqttClient;

    void registerConfig();
    void onConfigUpdated();
};

#endif
