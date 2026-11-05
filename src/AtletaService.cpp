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
    // configure settings service update handler
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

bool AtletaService::hasAtletaWithNumeral(int numeral)
{
    atleta_t dummy;
    return getAtletaByNumeral(numeral, dummy);
}

bool AtletaService::hasAnyAtleta()
{
    bool hasAny = false;
    this->read([&](Atleta &state) {
        hasAny = !state.atletas.empty();
    });
    return hasAny;
}

bool AtletaService::getAtletaByNumeral(int numeral, atleta_t &outAtleta)
{
    if (numeral <= 0)
    {
        return false;
    }
    bool found = false;
    this->read([&](Atleta &state) {
        for (const auto &atleta : state.atletas)
        {
            if (atleta.numeral == numeral)
            {
                outAtleta = atleta;
                found = true;
                break;
            }
        }
    });
    return found;
}

void AtletaService::onConfigUpdated()
{
}

void AtletaService::registerConfig()
{
}
