#ifndef CronometragemService_h
#define CronometragemService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>


#define CRONOMETRAGEM_SETTINGS_ENDPOINT_PATH "/rest/cronometragem"

struct cronometragem_t {
    std::string uid;
    std::string especial;
    std::string inicio;
    std::string fim;
};
    
class Cronometragem
{
public:
    std::vector<cronometragem_t> cronometragens;

    static void read(Cronometragem &settings, JsonObject &root)
    {
        root.createNestedArray("cronometragem");
        for (const auto &cronometragem : settings.cronometragens)
        {
            JsonObject cronometragemObj = root["cronometragem"].createNestedObject("cronometragem");
            cronometragemObj["uid"] = cronometragem.uid;
            cronometragemObj["especial"] = cronometragem.especial;
            cronometragemObj["inicio"] = cronometragem.inicio;
            cronometragemObj["fim"] = cronometragem.fim;
        }
    }

    static StateUpdateResult update(JsonObject &root, Cronometragem &cronometragem)
    {

        return StateUpdateResult::UNCHANGED;
    }
};

class CronometragemService : public StatefulService<Cronometragem>
{
public:
    CronometragemService(PsychicHttpServer *server,
                      ESP32SvelteKit *sveltekit, FS *fs);

    void begin();
    void registrarTempo(const cronometragem_t &novo);

private:
    FSPersistence<Cronometragem> _fsPersistence;
    HttpEndpoint<Cronometragem> _httpEndpoint;
    PsychicMqttClient *_mqttClient;

    void salvar();
    void registerConfig();
    void onConfigUpdated();
};

#endif
