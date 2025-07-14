#ifndef CategoriaService_h
#define CategoriaService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>


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
    int id;
    std::string categoria;

    static void read(Categoria &settings, JsonObject &root)
    {
        root.createNestedArray("categorias");
        for (const auto &categoria : settings.categorias)
        {
            JsonObject categoriaObj = root["categorias"].createNestedObject();
            categoriaObj["id"] = categoria.id;
            categoriaObj["categoria"] = categoria.categoria;
        }
    }

    static StateUpdateResult update(JsonObject &root, Categoria &categoria)
    {
        if (!root.containsKey("categorias"))
        {
            return StateUpdateResult::UNCHANGED;
        }else {
            JsonArray categoriasArray = root["categorias"];
            if (categoriasArray.isNull())
            {
                return StateUpdateResult::ERROR;
            }

            std::vector<categoria_t> newCategorias;
            for (const auto &item : categoriasArray)
            {
                if (item.is<JsonObject>())
                {
                    JsonObject categoriaObj = item.as<JsonObject>();
                    categoria_t newCategoria;
                    newCategoria.id = categoriaObj["id"];
                    newCategoria.categoria = categoriaObj["categoria"].as<std::string>();
                    newCategorias.push_back(newCategoria);
                }
            }
            categoria.categorias = newCategorias;
        }
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
    PsychicMqttClient *_mqttClient;

    void registerConfig();
    void onConfigUpdated();
};

#endif
