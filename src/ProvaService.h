#ifndef ProvaService_h
#define ProvaService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <algorithm>
#include <cctype>

#define PROVA_SETTINGS_ENDPOINT_PATH "/rest/prova"

class Prova
{
public:
    static constexpr int MIN_ESTAGIOS = 1;
    static constexpr int MAX_ESTAGIOS = 10;
    static constexpr int DEFAULT_ESTAGIOS = 5;

    std::string nome;
    std::string data;
    std::string local;
    std::string tipo = "especial";              // 'especial' | 'laps'
    std::string modoVoltas = "unica";           // 'unica' | 'duas' (quando tipo == 'laps')
    std::string criterioVoltas = "voltas_tempo"; // 'voltas_tempo' | 'melhor_volta'
    int totalEstagios = DEFAULT_ESTAGIOS;

    static std::string trim(const std::string &str)
    {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
        {
            return "";
        }
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    static void read(Prova &settings, JsonObject &root)
    {
        root["nome"] = settings.nome;
        root["data"] = settings.data;
        root["local"] = settings.local;
        root["tipo"] = settings.tipo.empty() ? "especial" : settings.tipo;
        root["modoVoltas"] = (settings.modoVoltas == "duas") ? "duas" : "unica";
        root["criterioVoltas"] = (settings.criterioVoltas == "melhor_volta") ? "melhor_volta" : "voltas_tempo";
        root["totalEstagios"] = (settings.totalEstagios >= MIN_ESTAGIOS && settings.totalEstagios <= MAX_ESTAGIOS)
                                    ? settings.totalEstagios
                                    : DEFAULT_ESTAGIOS;
    }

    static StateUpdateResult update(JsonObject &root, Prova &prova)
    {
        if (root.isNull() || root.size() == 0)
        {
            return StateUpdateResult::UNCHANGED;
        }

        std::string newNome = root["nome"].is<const char*>() ? trim(root["nome"].as<std::string>()) : "";
        std::string newData = root["data"].is<const char*>() ? trim(root["data"].as<std::string>()) : "";
        std::string newLocal = root["local"].is<const char*>() ? trim(root["local"].as<std::string>()) : "";

        if (newNome.empty() || newData.empty() || newLocal.empty())
        {
            return StateUpdateResult::ERROR;
        }

        std::string newTipo = "especial";
        if (!root["tipo"].isNull())
        {
            if (!root["tipo"].is<const char*>())
            {
                return StateUpdateResult::ERROR;
            }
            newTipo = trim(root["tipo"].as<std::string>());
            if (newTipo != "especial" && newTipo != "laps")
            {
                return StateUpdateResult::ERROR;
            }
        }

        std::string newModoVoltas = prova.modoVoltas.empty() ? "unica" : prova.modoVoltas;
        if (!root["modoVoltas"].isNull())
        {
            if (!root["modoVoltas"].is<const char*>())
            {
                return StateUpdateResult::ERROR;
            }
            newModoVoltas = trim(root["modoVoltas"].as<std::string>());
            if (newModoVoltas != "unica" && newModoVoltas != "duas")
            {
                return StateUpdateResult::ERROR;
            }
        }

        std::string newCriterioVoltas = (prova.criterioVoltas == "melhor_volta") ? "melhor_volta" : "voltas_tempo";
        if (!root["criterioVoltas"].isNull())
        {
            if (!root["criterioVoltas"].is<const char*>())
            {
                return StateUpdateResult::ERROR;
            }
            newCriterioVoltas = trim(root["criterioVoltas"].as<std::string>());
            if (newCriterioVoltas != "voltas_tempo" && newCriterioVoltas != "melhor_volta")
            {
                return StateUpdateResult::ERROR;
            }
        }

        int newTotalEstagios = DEFAULT_ESTAGIOS;
        if (!root["totalEstagios"].isNull())
        {
            newTotalEstagios = root["totalEstagios"].as<int>();
            if (newTotalEstagios < MIN_ESTAGIOS || newTotalEstagios > MAX_ESTAGIOS)
            {
                return StateUpdateResult::ERROR;
            }
        }

        if (prova.nome == newNome &&
            prova.data == newData &&
            prova.local == newLocal &&
            prova.tipo == newTipo &&
            prova.modoVoltas == newModoVoltas &&
            prova.criterioVoltas == newCriterioVoltas &&
            prova.totalEstagios == newTotalEstagios)
        {
            return StateUpdateResult::UNCHANGED;
        }

        prova.nome = newNome;
        prova.data = newData;
        prova.local = newLocal;
        prova.tipo = newTipo;
        prova.modoVoltas = newModoVoltas;
        prova.criterioVoltas = newCriterioVoltas;
        prova.totalEstagios = newTotalEstagios;

        return StateUpdateResult::CHANGED;
    }
};

class ProvaService : public StatefulService<Prova>
{
public:
    ProvaService(PsychicHttpServer *server,
                 ESP32SvelteKit *sveltekit, FS *fs);

    void begin();

    bool isConfigured()
    {
        bool configured = false;
        this->read([&](Prova &state) {
            configured = !state.nome.empty() && !state.data.empty() && !state.local.empty();
        });
        return configured;
    }

    Prova getProvaState()
    {
        Prova copy;
        this->read([&](Prova &state) {
            copy = state;
        });
        return copy;
    }

private:
    FSPersistence<Prova> _fsPersistence;
    HttpEndpoint<Prova> _httpEndpoint;

    void registerConfig();
    void onConfigUpdated();
};

#endif
