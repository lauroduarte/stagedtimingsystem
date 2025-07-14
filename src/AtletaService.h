#ifndef AtletaService_h
#define AtletaService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>


#define ATLETA_SETTINGS_ENDPOINT_PATH "/rest/atleta"

typedef struct 
{
    std::string nome;
    std::string equipe;
    int numeral;
    int categoriaId; // Assuming categoriaId is an integer, adjust as necessary
} atleta_t;
    
class Atleta
{
public:
    std::vector<atleta_t> atletas;
    std::string nome;
    std::string equipe;
    int numeral;
    int categoriaId;

    static void read(Atleta &settings, JsonObject &root)
    {
        JsonArray atletas = root["atletas"].to<JsonArray>();
        for (const auto &atleta : settings.atletas)
        {
            JsonObject atletaObj = atletas.add<JsonObject>();
            atletaObj["nome"] = atleta.nome;
            atletaObj["equipe"] = atleta.equipe;
            atletaObj["numeral"] = atleta.numeral;
            atletaObj["categoriaId"] = atleta.categoriaId;
        }
    }

    static StateUpdateResult update(JsonObject &root, Atleta &atleta)
    {
        atleta.atletas.clear();
        if(root["atletas"].is<JsonArray>())
        {
            for(JsonVariant atletaVariant : root["atletas"].as<JsonArray>())
            {
                if(atletaVariant.is<JsonObject>())
                {
                    JsonObject atletaObj = atletaVariant.as<JsonObject>();
                    atleta_t newAtleta;
                    newAtleta.nome = atletaObj["nome"].as<std::string>();
                    newAtleta.equipe = atletaObj["equipe"].as<std::string>();
                    newAtleta.numeral = atletaObj["numeral"].as<int>();
                    newAtleta.categoriaId = atletaObj["categoriaId"].as<int>();
                    atleta.atletas.push_back(newAtleta);
                }
            }
            return StateUpdateResult::CHANGED;

        }else{
            return StateUpdateResult::UNCHANGED;
        }
        
    }
};

class AtletaService : public StatefulService<Atleta>
{
public:
    AtletaService(PsychicHttpServer *server,
                      ESP32SvelteKit *sveltekit, FS *fs);

    void begin();

private:
    FSPersistence<Atleta> _fsPersistence;
    HttpEndpoint<Atleta> _httpEndpoint;
    PsychicMqttClient *_mqttClient;

    void registerConfig();
    void onConfigUpdated();
};

#endif
