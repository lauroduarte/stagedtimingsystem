#include <RelogioService.h>

RelogioService::RelogioService(PsychicHttpServer *server,
                                     ESP32SvelteKit *sveltekit,
                                     FS *fs) : _httpEndpoint(Relogio::read,
                                                             Relogio::update,
                                                             this,
                                                             server,
                                                             RELOGIO_SETTINGS_ENDPOINT_PATH,
                                                             sveltekit->getSecurityManager(),
                                                             AuthenticationPredicates::IS_AUTHENTICATED)
{
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

void RelogioService::begin()
{
    _httpEndpoint.begin();
    onConfigUpdated();
}

void RelogioService::onConfigUpdated()
{
   // Nenhuma ação adicional no momento
}

void RelogioService::registerConfig()
{
    // Nenhuma configuração persistente registrada atualmente
    // sveltekit->addSettings(...);
}
