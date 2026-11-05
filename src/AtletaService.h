#ifndef AtletaService_h
#define AtletaService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <algorithm>
#include <vector>

#define ATLETA_SETTINGS_ENDPOINT_PATH "/rest/atleta"

typedef struct 
{
    std::string nome;
    std::string equipe;
    int numeral;
    int categoriaId;
} atleta_t;
    
class Atleta
{
public:
    std::vector<atleta_t> atletas;

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
        if (!root["atletas"].is<JsonArray>())
        {
            return StateUpdateResult::UNCHANGED;
        }

        std::vector<atleta_t> novosAtletas;
        for (JsonVariant atletaVariant : root["atletas"].as<JsonArray>())
        {
            if (!atletaVariant.is<JsonObject>())
            {
                return StateUpdateResult::ERROR;
            }

            JsonObject atletaObj = atletaVariant.as<JsonObject>();
            atleta_t newAtleta;
            newAtleta.nome = atletaObj["nome"].is<const char*>() ? atletaObj["nome"].as<std::string>() : "";
            newAtleta.equipe = atletaObj["equipe"].is<const char*>() ? atletaObj["equipe"].as<std::string>() : "";
            newAtleta.numeral = atletaObj["numeral"].as<int>();
            newAtleta.categoriaId = atletaObj["categoriaId"].as<int>();

            if (newAtleta.nome.empty() || newAtleta.equipe.empty() || newAtleta.numeral <= 0 || newAtleta.categoriaId <= 0)
            {
                return StateUpdateResult::ERROR;
            }

            bool numeralDuplicado = std::any_of(
                novosAtletas.begin(),
                novosAtletas.end(),
                [&](const atleta_t &existing) { return existing.numeral == newAtleta.numeral; }
            );
            if (numeralDuplicado)
            {
                return StateUpdateResult::ERROR;
            }

            novosAtletas.push_back(newAtleta);
        }

        atleta.atletas = std::move(novosAtletas);
        return StateUpdateResult::CHANGED;
    }
};

class AtletaService : public StatefulService<Atleta>
{
public:
    AtletaService(PsychicHttpServer *server,
                  ESP32SvelteKit *sveltekit,
                  FS *fs);

    void begin();
    bool hasAtletaWithNumeral(int numeral);
    bool getAtletaByNumeral(int numeral, atleta_t &outAtleta);
    bool hasAnyAtleta();

private:
    FSPersistence<Atleta> _fsPersistence;
    HttpEndpoint<Atleta> _httpEndpoint;

    void registerConfig();
    void onConfigUpdated();
};

#endif
