#include <RelogioService.h>

RelogioService::RelogioService(PsychicHttpServer *server,
                               ESP32SvelteKit *sveltekit,
                               FS *fs)
    : _server(server),
      _sveltekit(sveltekit)
{
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

void RelogioService::begin()
{
    Relogio::getRTC();

#ifdef ENABLE_CORS
    _server->on(RELOGIO_SETTINGS_ENDPOINT_PATH,
                HTTP_OPTIONS,
                _sveltekit->getSecurityManager()->wrapRequest(
                    [](PsychicRequest *request)
                    {
                        return request->reply(200);
                    },
                    AuthenticationPredicates::NONE_REQUIRED));
#endif

    _server->on(RELOGIO_SETTINGS_ENDPOINT_PATH,
                HTTP_GET,
                _sveltekit->getSecurityManager()->wrapRequest(
                    [this](PsychicRequest *request)
                    {
                        return handleGetRelogio(request);
                    },
                    AuthenticationPredicates::IS_AUTHENTICATED));

    _server->on(RELOGIO_SETTINGS_ENDPOINT_PATH,
                HTTP_POST,
                _sveltekit->getSecurityManager()->wrapCallback(
                    [this](PsychicRequest *request, JsonVariant &json)
                    {
                        return handlePostRelogio(request, json);
                    },
                    AuthenticationPredicates::IS_AUTHENTICATED));

    onConfigUpdated();
}

esp_err_t RelogioService::handleGetRelogio(PsychicRequest *request)
{
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject jsonObject = response.getRoot();
    this->read(jsonObject, Relogio::read);
    return response.send();
}

esp_err_t RelogioService::handlePostRelogio(PsychicRequest *request, JsonVariant &json)
{
    // Bloqueia alteração do relógio se a cronometragem estiver ativa (P3-02)
    if (_isCronometrandoCallback && _isCronometrandoCallback())
    {
        return request->reply(409, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Não é permitido alterar o relógio durante cronometragem ativa.\"}");
    }

    if (!json.is<JsonObject>())
    {
        return request->reply(400);
    }

    JsonObject jsonObject = json.as<JsonObject>();
    StateUpdateResult outcome = this->updateWithoutPropagation(jsonObject, Relogio::update);

    if (outcome == StateUpdateResult::ERROR)
    {
        return request->reply(400);
    }
    else if (outcome == StateUpdateResult::CHANGED)
    {
        this->callUpdateHandlers("http");
    }

    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject respObj = response.getRoot();
    this->read(respObj, Relogio::read);
    return response.send();
}

void RelogioService::loop()
{
    // Aferição periódica não-bloqueante na borda de segundo do DS3231 a cada 15 minutos (900.000 ms) (P0-05)
    static unsigned long lastSyncMs = 0;
    static unsigned long lastEdgePollMs = 0;
    static unsigned long edgeStartMs = 0;
    static bool waitingEdge = false;
    static uint8_t initialSec = 0;

    // Nunca ressincroniza durante cronometragem ativa para evitar salto de subsegundo no meio de passagens RFID
    if (_isCronometrandoCallback && _isCronometrandoCallback())
    {
        waitingEdge = false;
        return;
    }

    unsigned long nowMs = millis();

    if (!waitingEdge)
    {
        if (nowMs - lastSyncMs >= 900000UL)
        {
            lastSyncMs = nowMs;
            if (!Relogio::isHardwarePresent())
            {
                return;
            }
            SemaphoreHandle_t mtx = Relogio::getI2CMutex();
            if (mtx && xSemaphoreTake(mtx, pdMS_TO_TICKS(20)) == pdTRUE)
            {
                DateTime dt = Relogio::getRTC().now();
                xSemaphoreGive(mtx);
                initialSec = dt.second();
                edgeStartMs = nowMs;
                lastEdgePollMs = nowMs;
                waitingEdge = true;
            }
        }
        return;
    }

    if (nowMs - edgeStartMs > 1200UL)
    {
        waitingEdge = false;
        return;
    }

    if (nowMs - lastEdgePollMs >= 15UL)
    {
        lastEdgePollMs = nowMs;
        SemaphoreHandle_t mtx = Relogio::getI2CMutex();
        if (mtx && xSemaphoreTake(mtx, pdMS_TO_TICKS(20)) == pdTRUE)
        {
            DateTime dt = Relogio::getRTC().now();
            xSemaphoreGive(mtx);
            if (dt.second() != initialSec)
            {
                Relogio::syncSystemTime(dt);
                waitingEdge = false;
            }
        }
    }
}

void RelogioService::onConfigUpdated()
{
    // Nenhuma ação adicional no momento
}

void RelogioService::registerConfig()
{
    // Nenhuma configuração persistente registrada atualmente
}
