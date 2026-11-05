#ifndef CategoriaService_h
#define CategoriaService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <algorithm>
#include <vector>

#define CATEGORIA_SETTINGS_ENDPOINT_PATH "/rest/categoria"

typedef struct 
{
    int id;
    std::string categoria;
} categoria_t;


class Categoria
{
public:
    std::vector<categoria_t> categorias;

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

    static void read(Categoria &settings, JsonObject &root)
    {
        JsonArray categorias = root["categorias"].to<JsonArray>();
        for (const auto &categoria : settings.categorias)
        {
            JsonObject categoriaObj = categorias.add<JsonObject>();
            categoriaObj["id"] = categoria.id;
            categoriaObj["categoria"] = categoria.categoria;
        }
    }

    static StateUpdateResult update(JsonObject &root, Categoria &categoria)
    {
        if (!root["categorias"].is<JsonArray>())
        {
            return StateUpdateResult::UNCHANGED;
        }

        std::vector<categoria_t> newCategorias;
        for (JsonVariant item : root["categorias"].as<JsonArray>())
        {
            if (!item.is<JsonObject>())
            {
                return StateUpdateResult::ERROR;
            }

            JsonObject categoriaObj = item.as<JsonObject>();
            categoria_t newCategoria;
            newCategoria.id = categoriaObj["id"].as<int>();
            newCategoria.categoria = categoriaObj["categoria"].is<const char*>()
                                         ? trim(categoriaObj["categoria"].as<std::string>())
                                         : "";

            if (newCategoria.id <= 0 || newCategoria.categoria.empty())
            {
                return StateUpdateResult::ERROR;
            }

            bool duplicada = std::any_of(
                newCategorias.begin(),
                newCategorias.end(),
                [&](const categoria_t &existing) {
                    return existing.id == newCategoria.id || existing.categoria == newCategoria.categoria;
                }
            );
            if (duplicada)
            {
                return StateUpdateResult::ERROR;
            }

            newCategorias.push_back(newCategoria);
        }
        categoria.categorias = std::move(newCategorias);
        return StateUpdateResult::CHANGED;
    }
};

class CategoriaService : public StatefulService<Categoria>
{
public:
    CategoriaService(PsychicHttpServer *server,
                      ESP32SvelteKit *sveltekit, FS *fs);

    void begin();

private:
    FSPersistence<Categoria> _fsPersistence;
    HttpEndpoint<Categoria> _httpEndpoint;

    void registerConfig();
    void onConfigUpdated();
};

#endif
