#include <AtletaService.h>

AtletaService::AtletaService(PsychicHttpServer *server,
                                     ESP32SvelteKit *sveltekit,
                                     FS *fs) : _httpEndpoint(Atleta::read,
                                                             Atleta::update,
                                                             this,
                                                             server,
                                                             ATLETA_SETTINGS_ENDPOINT_PATH,
                                                             sveltekit->getSecurityManager(),
                                                             AuthenticationPredicates::IS_AUTHENTICATED),
                                                _fsPersistence(Atleta::read, Atleta::update, this, sveltekit->getFS(), "/config/atleta.json")
{
    // configure led to be output
    pinMode(LED_BUILTIN, OUTPUT);

    // configure settings service update handler to update LED state
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

void AtletaService::begin()
{
    _httpEndpoint.begin();
    _fsPersistence.readFromFS();
    onConfigUpdated();
}

void AtletaService::onConfigUpdated()
{
}

void AtletaService::registerConfig()
{
}
