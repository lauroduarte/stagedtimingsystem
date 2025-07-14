#include <CategoriaService.h>

CategoriaService::CategoriaService(PsychicHttpServer *server,
                                     ESP32SvelteKit *sveltekit,
                                     FS *fs) : _httpEndpoint(Categoria::read,
                                                             Categoria::update,
                                                             this,
                                                             server,
                                                             CATEGORIA_SETTINGS_ENDPOINT_PATH,
                                                             sveltekit->getSecurityManager(),
                                                             AuthenticationPredicates::IS_AUTHENTICATED),
                                                _fsPersistence(Categoria::read, Categoria::update, this, sveltekit->getFS(), "/config/categoria.json")
{
    // configure led to be output
    pinMode(LED_BUILTIN, OUTPUT);

    // configure settings service update handler to update LED state
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

void CategoriaService::begin()
{
    _httpEndpoint.begin();
    _fsPersistence.readFromFS();
    onConfigUpdated();
}

void CategoriaService::onConfigUpdated()
{
}

void CategoriaService::registerConfig()
{
}
