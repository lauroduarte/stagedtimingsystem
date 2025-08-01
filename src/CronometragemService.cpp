#include <CronometragemService.h>

CronometragemService::CronometragemService(PsychicHttpServer *server,
                                           ESP32SvelteKit *sveltekit,
                                           FS *fs) : _httpEndpoint(Cronometragem::read,
                                                                   Cronometragem::update,
                                                                   this,
                                                                   server,
                                                                   CRONOMETRAGEM_SETTINGS_ENDPOINT_PATH,
                                                                   sveltekit->getSecurityManager(),
                                                                   AuthenticationPredicates::IS_AUTHENTICATED),
                                                     _fsPersistence(
                                                         Cronometragem::read,
                                                         Cronometragem::update,
                                                         this,
                                                         sveltekit->getFS(),
                                                         "/config/cronometragem.json")
                                                    
{

    // configure settings service update handler to update LED state
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

void CronometragemService::begin()
{
    _httpEndpoint.begin();
    _fsPersistence.readFromFS();
    onConfigUpdated();
}

void CronometragemService::onConfigUpdated()
{
}

void CronometragemService::registerConfig()
{
}

void CronometragemService::registrarTempo(const cronometragem_t &novo)
{
    // Adiciona o novo cronometragem ao vetor
    update([&](Cronometragem &state) {
        state.cronometragens.push_back(novo);
        return StateUpdateResult::CHANGED;
    }, "cronometragem");
    _fsPersistence.writeToFS();
}
