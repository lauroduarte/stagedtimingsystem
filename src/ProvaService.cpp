#include <ProvaService.h>

ProvaService::ProvaService(PsychicHttpServer *server,
                                     ESP32SvelteKit *sveltekit,
                                     FS *fs) : _httpEndpoint(Prova::read,
                                                             Prova::update,
                                                             this,
                                                             server,
                                                             PROVA_SETTINGS_ENDPOINT_PATH,
                                                             sveltekit->getSecurityManager(),
                                                             AuthenticationPredicates::IS_AUTHENTICATED),
                                                _fsPersistence(Prova::read, Prova::update, this, sveltekit->getFS(), "/config/prova.json")
{
    // configure led to be output
    pinMode(LED_BUILTIN, OUTPUT);

    // configure settings service update handler to update LED state
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

void ProvaService::begin()
{
    _httpEndpoint.begin();
    _fsPersistence.readFromFS();
    onConfigUpdated();
}

void ProvaService::onConfigUpdated()
{
}

void ProvaService::registerConfig()
{
}
