#include "SyncService.h"
#include <ChunkPrinter.h>
#include <memory>
#include <new>

SyncService::SyncService(PsychicHttpServer *server,
                         ESP32SvelteKit *sveltekit,
                         ProvaService *provaService,
                         CategoriaService *categoriaService,
                         AtletaService *atletaService,
                         RFIDService *rfidService,
                         RelogioService *relogioService,
                         CronometrarService *cronometrarService,
                         CronometragemService *cronometragemService)
    : _server(server),
      _securityManager(sveltekit->getSecurityManager()),
      _provaService(provaService),
      _categoriaService(categoriaService),
      _atletaService(atletaService),
      _rfidService(rfidService),
      _relogioService(relogioService),
      _cronometrarService(cronometrarService),
      _cronometragemService(cronometragemService),
      _cacheMutex(xSemaphoreCreateMutex())
{
}

void SyncService::registerOptionsEndpoint(const char *uri)
{
#ifdef ENABLE_CORS
    _server->on(uri,
                HTTP_OPTIONS,
                _securityManager->wrapRequest(
                    [](PsychicRequest *request)
                    {
                        return request->reply(200);
                    },
                    AuthenticationPredicates::NONE_REQUIRED));
#endif
}

void SyncService::begin()
{
    if (_server && _server->maxRequestBodySize < SYNC_MAX_BODY_SIZE)
    {
        _server->maxRequestBodySize = SYNC_MAX_BODY_SIZE;
    }

    // Status e diagnóstico de conectividade entre estações
    registerOptionsEndpoint(SYNC_STATUS_ENDPOINT_PATH);
    _server->on(SYNC_STATUS_ENDPOINT_PATH,
                HTTP_GET,
                _securityManager->wrapRequest(
                    std::bind(&SyncService::handleGetStatus, this, std::placeholders::_1),
                    AuthenticationPredicates::NONE_REQUIRED));

    // Sincronização Inicial de Configuração e Relógio (RF-06, RF-07 / BL-10)
    registerOptionsEndpoint(SYNC_CONFIG_ENDPOINT_PATH);
    _server->on(SYNC_CONFIG_ENDPOINT_PATH,
                HTTP_GET,
                _securityManager->wrapRequest(
                    std::bind(&SyncService::handleGetConfig, this, std::placeholders::_1),
                    AuthenticationPredicates::NONE_REQUIRED));
    _server->on(SYNC_CONFIG_ENDPOINT_PATH,
                HTTP_POST,
                _securityManager->wrapCallback(
                    std::bind(&SyncService::handlePostConfig, this, std::placeholders::_1, std::placeholders::_2),
                    AuthenticationPredicates::NONE_REQUIRED));

    // Sincronização Final de Resultados (RF-12, RF-11 / BL-11)
    registerOptionsEndpoint(SYNC_RESULTADOS_ENDPOINT_PATH);
    _server->on(SYNC_RESULTADOS_ENDPOINT_PATH,
                HTTP_GET,
                _securityManager->wrapRequest(
                    std::bind(&SyncService::handleGetResultados, this, std::placeholders::_1),
                    AuthenticationPredicates::NONE_REQUIRED));
    _server->on(SYNC_RESULTADOS_ENDPOINT_PATH,
                HTTP_POST,
                _securityManager->wrapCallback(
                    std::bind(&SyncService::handlePostResultados, this, std::placeholders::_1, std::placeholders::_2),
                    AuthenticationPredicates::NONE_REQUIRED));

    // Consulta Pública Wi-Fi e Apuração Oficial (RF-13, RF-14 / BL-12, BL-13)
    registerOptionsEndpoint(RESULTADOS_PUBLICOS_ENDPOINT_PATH);
    _server->on(RESULTADOS_PUBLICOS_ENDPOINT_PATH,
                HTTP_GET,
                _securityManager->wrapRequest(
                    std::bind(&SyncService::handleGetResultadosPublicos, this, std::placeholders::_1),
                    AuthenticationPredicates::NONE_REQUIRED));

    // Reset dos dados da estação para iniciar nova prova do zero
    registerOptionsEndpoint(RESET_ESTACAO_ENDPOINT_PATH);
    _server->on(RESET_ESTACAO_ENDPOINT_PATH,
                HTTP_POST,
                _securityManager->wrapCallback(
                    std::bind(&SyncService::handlePostResetEstacao, this, std::placeholders::_1, std::placeholders::_2),
                    AuthenticationPredicates::IS_AUTHENTICATED));

    // Alias legado (/sync) para compatibilidade direta com URLs configuradas como http://<ip>/sync
    registerOptionsEndpoint(SYNC_LEGACY_ENDPOINT_PATH);
    _server->on(SYNC_LEGACY_ENDPOINT_PATH,
                HTTP_GET,
                _securityManager->wrapRequest(
                    std::bind(&SyncService::handleGetStatus, this, std::placeholders::_1),
                    AuthenticationPredicates::NONE_REQUIRED));
    _server->on(SYNC_LEGACY_ENDPOINT_PATH,
                HTTP_POST,
                _securityManager->wrapCallback(
                    std::bind(&SyncService::handlePostResultados, this, std::placeholders::_1, std::placeholders::_2),
                    AuthenticationPredicates::NONE_REQUIRED));

    // P0-01: Invalidação automática do cache de apuração quando houver alteração de estado
    if (_provaService)
    {
        _provaService->addUpdateHandler([this](const String &) {
            invalidarCacheApuracao();
        });
    }
    if (_categoriaService)
    {
        _categoriaService->addUpdateHandler([this](const String &) {
            invalidarCacheApuracao();
        });
    }
    if (_atletaService)
    {
        _atletaService->addUpdateHandler([this](const String &) {
            invalidarCacheApuracao();
        });
    }
    if (_rfidService)
    {
        _rfidService->addUpdateHandler([this](const String &) {
            invalidarCacheApuracao();
        });
    }
    if (_cronometrarService)
    {
        _cronometrarService->addUpdateHandler([this](const String &) {
            invalidarCacheApuracao();
        });
    }
    if (_cronometragemService)
    {
        _cronometragemService->addUpdateHandler([this](const String &) {
            invalidarCacheApuracao();
        });
    }
}

void SyncService::invalidarCacheApuracao()
{
    if (_cacheMutex)
    {
        xSemaphoreTake(_cacheMutex, portMAX_DELAY);
        _cacheApuracaoValido = false;
        _versaoApuracao++;
        xSemaphoreGive(_cacheMutex);
    }
    else
    {
        _cacheApuracaoValido = false;
        _versaoApuracao++;
    }
}

void SyncService::atualizarCacheApuracao(const std::string &criterioVoltas)
{
    Prova provaLocal;
    Cronometrar cronoLocal;
    if (_provaService)
    {
        _provaService->read([&](Prova &p) { provaLocal = p; });
    }
    if (_cronometrarService)
    {
        _cronometrarService->read([&](Cronometrar &cr) { cronoLocal = cr; });
    }

    if (_categoriaService && _atletaService && _rfidService && _cronometragemService)
    {
        _categoriaService->read([&](Categoria &cat) {
            _atletaService->read([&](Atleta &atl) {
                _rfidService->read([&](RFIDState &rfid) {
                    _cronometragemService->read([&](Cronometragem &crono) {
                        _cacheRelatorio = ApuracaoResultados::calcular(
                            provaLocal,
                            cat.categorias,
                            atl.atletas,
                            rfid._cardRegistry,
                            cronoLocal,
                            crono.cronometragens,
                            criterioVoltas);
                        _cacheCategorias = cat.categorias;
                    });
                });
            });
        });
    }

    _cacheProva = provaLocal;
    _cacheCronometrar = cronoLocal;
    _cacheCriterioVoltas = criterioVoltas;
    _cacheApuracaoValido = true;
}

esp_err_t SyncService::handleGetStatus(PsychicRequest *request)
{
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();

    DateTime now = Relogio::getNow();
    root["status"] = "online";
    root["versao"] = PROTOCOL_VERSION;
    root["relogio"] = Relogio::formatDateTime(now);
    root["unixtime"] = now.unixtime();

    if (_provaService)
    {
        _provaService->read([&](Prova &prova) {
            root["prova"] = prova.nome;
            root["tipoProva"] = prova.tipo.empty() ? "especial" : prova.tipo;
            root["modoVoltas"] = (prova.modoVoltas == "duas") ? "duas" : "unica";
            root["criterioVoltas"] = (prova.criterioVoltas == "melhor_volta") ? "melhor_volta" : "voltas_tempo";
            root["totalEstagios"] = prova.totalEstagios;
        });
    }

    if (_cronometrarService)
    {
        _cronometrarService->read([&](Cronometrar &cron) {
            root["cronometrando"] = cron.cronometrar;
            root["tipoEstacao"] = cron.tipo;
            root["especial"] = cron.especial;
            root["posicao"] = cron.posicao;
        });
    }

    int qtdCategorias = 0;
    if (_categoriaService)
    {
        _categoriaService->read([&](Categoria &cat) {
            qtdCategorias = static_cast<int>(cat.categorias.size());
        });
    }
    root["categorias"] = qtdCategorias;

    int qtdAtletas = 0;
    if (_atletaService)
    {
        _atletaService->read([&](Atleta &atl) {
            qtdAtletas = static_cast<int>(atl.atletas.size());
        });
    }
    root["atletas"] = qtdAtletas;

    int qtdCartoes = 0;
    if (_rfidService)
    {
        _rfidService->read([&](RFIDState &rfid) {
            qtdCartoes = static_cast<int>(rfid._cardRegistry.size());
        });
    }
    root["cartoes"] = qtdCartoes;

    if (_cronometragemService)
    {
        diagnostico_cronometragem_t diag = _cronometragemService->obterDiagnostico();
        root["registros"] = diag.totalRegistros;
        JsonObject diagObj = root["diagnostico"].to<JsonObject>();
        diagObj["totalRegistros"] = diag.totalRegistros;
        diagObj["concluidos"] = diag.concluidos;
        diagObj["pendentesChegada"] = diag.pendentesChegada;
        diagObj["semLargada"] = diag.semLargada;
        diagObj["invalidos"] = diag.invalidos;
    }

    return response.send();
}

esp_err_t SyncService::handleGetConfig(PsychicRequest *request)
{
    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject root = response.getRoot();

    root["versao"] = PROTOCOL_VERSION;

    DateTime now = Relogio::getNow();
    JsonObject relogioObj = root["relogio"].to<JsonObject>();
    relogioObj["datetime"] = Relogio::formatDateTime(now);
    relogioObj["unixtime"] = now.unixtime();

    if (_provaService)
    {
        JsonObject provaObj = root["prova"].to<JsonObject>();
        _provaService->read(provaObj, Prova::read);
    }

    if (_categoriaService)
    {
        _categoriaService->read(root, Categoria::read);
    }

    if (_atletaService)
    {
        _atletaService->read(root, Atleta::read);
    }

    if (_rfidService)
    {
        JsonObject rfidObj = root["rfid"].to<JsonObject>();
        _rfidService->read(rfidObj, RFIDState::read);
    }

    if (_cronometrarService)
    {
        JsonObject cronObj = root["cronometrar"].to<JsonObject>();
        _cronometrarService->read(cronObj, Cronometrar::read);
    }

    return response.send();
}

esp_err_t SyncService::handlePostConfig(PsychicRequest *request, JsonVariant &json)
{
    // P0-06: Liberação antecipada do buffer de corpo HTTP
    request->clearBody();

    if (!json.is<JsonObject>())
    {
        return request->reply(400, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Payload JSON inválido.\"}");
    }

    if (_cronometrarService && _cronometrarService->isCronometrando())
    {
        return request->reply(409, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Encerre a cronometragem ativa nesta estação antes de aplicar a sincronização inicial.\"}");
    }

    if (_rfidService && _rfidService->isResetMode())
    {
        _rfidService->stopResetMode();
    }

    JsonObject root = json.as<JsonObject>();

    bool provaSincronizada = false;
    int categoriasSincronizadas = 0;
    int atletasSincronizados = 0;
    int cartoesSincronizados = 0;
    bool relogioAjustado = false;

    // 1. Atualiza Prova (se presente e com campos válidos)
    if (root["prova"].is<JsonObject>() && _provaService)
    {
        JsonObject provaObj = root["prova"].as<JsonObject>();
        bool temNome = provaObj["nome"].is<const char*>() && !Prova::trim(provaObj["nome"].as<std::string>()).empty();
        if (temNome)
        {
            StateUpdateResult resProva = StateUpdateResult::UNCHANGED;
            _provaService->update([&](Prova &state) {
                resProva = Prova::update(provaObj, state);
                return resProva;
            }, "sync_config");

            if (resProva == StateUpdateResult::ERROR)
            {
                return request->reply(400, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Dados da Prova inválidos no pacote de sincronização.\"}");
            }
            provaSincronizada = true;

            // Alinha o modo da estação receptora ao modo da Prova sincronizada, preservando a posição local (inicio/fim)
            if (_cronometrarService)
            {
                Prova pState = _provaService->getProvaState();
                _cronometrarService->update([&](Cronometrar &cronState) {
                    bool changed = false;
                    if (!pState.tipo.empty() && cronState.tipo != pState.tipo)
                    {
                        cronState.tipo = pState.tipo;
                        changed = true;
                    }
                    if (!pState.modoVoltas.empty() && cronState.modoVoltas != pState.modoVoltas)
                    {
                        cronState.modoVoltas = pState.modoVoltas;
                        changed = true;
                    }
                    if (cronState.tipo == "especial" && !Cronometrar::isValidStageName(cronState.especial, pState.totalEstagios))
                    {
                        cronState.especial = "E1";
                        changed = true;
                    }
                    return changed ? StateUpdateResult::CHANGED : StateUpdateResult::UNCHANGED;
                }, "sync_config");
            }
        }
    }

    // 2. Atualiza Categorias (idempotente: substitui a lista completa sem duplicar)
    JsonVariant categoriasVar = root["categorias"];
    if (categoriasVar.is<JsonObject>() && categoriasVar["categorias"].is<JsonArray>())
    {
        categoriasVar = categoriasVar["categorias"];
    }
    if (categoriasVar.is<JsonArray>() && _categoriaService)
    {
        JsonObject catRoot = root["categorias"].is<JsonObject>() ? root["categorias"].as<JsonObject>() : root;

        StateUpdateResult resCat = StateUpdateResult::UNCHANGED;
        _categoriaService->update([&](Categoria &state) {
            resCat = Categoria::update(catRoot, state);
            if (resCat != StateUpdateResult::ERROR)
            {
                categoriasSincronizadas = static_cast<int>(state.categorias.size());
            }
            return resCat;
        }, "sync_config");

        if (resCat == StateUpdateResult::ERROR)
        {
            return request->reply(400, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Lista de categorias inválida no pacote de sincronização.\"}");
        }
    }

    // 3. Atualiza Atletas / Pilotos (idempotente: substitui a lista completa sem duplicar)
    JsonVariant atletasVar = root["atletas"];
    if (atletasVar.is<JsonObject>() && atletasVar["atletas"].is<JsonArray>())
    {
        atletasVar = atletasVar["atletas"];
    }
    if (atletasVar.is<JsonArray>() && _atletaService)
    {
        JsonObject atlRoot = root["atletas"].is<JsonObject>() ? root["atletas"].as<JsonObject>() : root;

        StateUpdateResult resAtl = StateUpdateResult::UNCHANGED;
        _atletaService->update([&](Atleta &state) {
            resAtl = Atleta::update(atlRoot, state);
            if (resAtl != StateUpdateResult::ERROR)
            {
                atletasSincronizados = static_cast<int>(state.atletas.size());
            }
            return resAtl;
        }, "sync_config");

        if (resAtl == StateUpdateResult::ERROR)
        {
            return request->reply(400, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Lista de atletas inválida no pacote de sincronização.\"}");
        }
    }

    // 4. Atualiza Registro de Cartões RFID (idempotente: reconstrói _cardRegistry sem duplicar)
    JsonVariant cardsVar = root["rfid"]["cards"];
    if (cardsVar.isNull() && root["cards"].is<JsonArray>())
    {
        cardsVar = root["cards"];
    }
    if (cardsVar.is<JsonArray>() && _rfidService)
    {
        JsonObject rfidRoot = root["rfid"].is<JsonObject>() ? root["rfid"].as<JsonObject>() : root;

        _rfidService->update([&](RFIDState &state) {
            StateUpdateResult resRfid = RFIDState::update(rfidRoot, state);
            cartoesSincronizados = static_cast<int>(state._cardRegistry.size());
            return resRfid;
        }, "sync_config");
    }

    // 5. Sincronização de Relógio RTC DS3231 + POSIX (RF-07 / BL-10)
    bool ajustarRelogio = root["ajustarRelogio"].is<bool>() ? root["ajustarRelogio"].as<bool>() : true;
    std::string datetimeStr = "";
    int32_t offsetSeconds = 0;

    if (root["relogio"].is<JsonObject>())
    {
        JsonObject relObj = root["relogio"].as<JsonObject>();
        if (relObj["datetime"].is<const char*>())
        {
            datetimeStr = relObj["datetime"].as<std::string>();
        }
        if (relObj["offsetSeconds"].is<int>())
        {
            offsetSeconds = relObj["offsetSeconds"].as<int>();
        }
    }
    else if (root["datetime"].is<const char*>())
    {
        datetimeStr = root["datetime"].as<std::string>();
    }
    if (root["offsetSeconds"].is<int>())
    {
        offsetSeconds = root["offsetSeconds"].as<int>();
    }

    if (ajustarRelogio && !datetimeStr.empty())
    {
        if (Relogio::applyDateTimeString(datetimeStr, offsetSeconds))
        {
            relogioAjustado = true;
            if (_relogioService)
            {
                std::string formattedNow = Relogio::formatDateTime(Relogio::getNow());
                _relogioService->update([&](Relogio &relState) {
                    relState.datetime = formattedNow;
                    return StateUpdateResult::CHANGED;
                }, "sync_config");
            }
        }
    }

    invalidarCacheApuracao();

    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject respRoot = response.getRoot();
    respRoot["status"] = "ok";
    respRoot["provaSincronizada"] = provaSincronizada;
    respRoot["categoriasSincronizadas"] = categoriasSincronizadas;
    respRoot["atletasSincronizados"] = atletasSincronizados;
    respRoot["cartoesSincronizados"] = cartoesSincronizados;
    respRoot["relogioAjustado"] = relogioAjustado;
    respRoot["horarioAtual"] = Relogio::formatDateTime(Relogio::getNow());

    return response.send();
}

esp_err_t SyncService::handleGetResultados(PsychicRequest *request)
{
    constexpr size_t STREAM_BUF_SIZE = 2048;
    uint8_t *buffer = static_cast<uint8_t *>(malloc(STREAM_BUF_SIZE));
    if (!buffer)
    {
        return request->reply(500, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Sem memoria para exportar resultados.\"}");
    }

    PsychicResponse response(request);
    response.setContentType(JSON_MIMETYPE);
    response.sendHeaders();

    ChunkPrinter dest(&response, buffer, STREAM_BUF_SIZE);

    char headerBuf[64];
    std::snprintf(headerBuf, sizeof(headerBuf), "{\"versao\":%d", PROTOCOL_VERSION);
    dest.print(headerBuf);

    {
        DateTime now = Relogio::getNow();
        JsonDocument doc;
        JsonObject relogioObj = doc.to<JsonObject>();
        relogioObj["datetime"] = Relogio::formatDateTime(now);
        relogioObj["unixtime"] = now.unixtime();
        dest.print(",\"relogio\":");
        serializeJson(doc, dest);
    }

    if (_provaService)
    {
        JsonDocument doc;
        JsonObject provaObj = doc.to<JsonObject>();
        _provaService->read(provaObj, Prova::read);
        dest.print(",\"prova\":");
        serializeJson(doc, dest);
    }

    if (_cronometrarService)
    {
        JsonDocument doc;
        JsonObject estacaoObj = doc.to<JsonObject>();
        _cronometrarService->read(estacaoObj, Cronometrar::read);
        dest.print(",\"estacao\":");
        serializeJson(doc, dest);
    }

    if (_cronometragemService)
    {
        diagnostico_cronometragem_t diag{};
        dest.print(",\"cronometragem\":");
        _cronometragemService->read([&](Cronometragem &state) {
            Cronometragem::serializarListaToStream(state.cronometragens, dest);
            diag = Cronometragem::diagnosticarLista(state.cronometragens);
        });

        JsonDocument doc;
        JsonObject diagObj = doc.to<JsonObject>();
        diagObj["totalRegistros"] = diag.totalRegistros;
        diagObj["concluidos"] = diag.concluidos;
        diagObj["pendentesChegada"] = diag.pendentesChegada;
        diagObj["semLargada"] = diag.semLargada;
        diagObj["invalidos"] = diag.invalidos;
        dest.print(",\"diagnostico\":");
        serializeJson(doc, dest);
    }

    dest.write('}');
    dest.flush();

    esp_err_t err = response.finishChunking();
    free(buffer);
    return err;
}

esp_err_t SyncService::handlePostResultados(PsychicRequest *request, JsonVariant &json)
{
    // P0-06: Liberação antecipada do buffer do corpo HTTP
    request->clearBody();

    if (!json.is<JsonObject>())
    {
        return request->reply(400, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Payload JSON inválido.\"}");
    }

    if (!_cronometragemService)
    {
        return request->reply(500, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Serviço de cronometragem indisponível.\"}");
    }

    JsonObject root = json.as<JsonObject>();

    JsonArray registrosArr;
    bool temArray = false;
    if (root["cronometragem"].is<JsonArray>())
    {
        registrosArr = root["cronometragem"].as<JsonArray>();
        temArray = true;
    }
    else if (root["cronometragens"].is<JsonArray>())
    {
        registrosArr = root["cronometragens"].as<JsonArray>();
        temArray = true;
    }
    else if (root["passagens"].is<JsonArray>())
    {
        registrosArr = root["passagens"].as<JsonArray>();
        temArray = true;
    }

    std::vector<cronometragem_t> remotos;
    if (temArray)
    {
        remotos.reserve(registrosArr.size());
        for (JsonVariant itemVar : registrosArr)
        {
            if (!itemVar.is<JsonObject>())
            {
                continue;
            }
            JsonObject itemObj = itemVar.as<JsonObject>();
            cronometragem_t item;
            if (!Cronometragem::parseJsonItem(itemObj, item))
            {
                continue;
            }

            // Enriquecimento cruzado com o registro de cartões RFID (se necessário)
            if (item.numeral <= 0 && !item.uid.empty() && _rfidService)
            {
                int resolvedNum = _rfidService->getCardNumber(item.uid.c_str());
                if (resolvedNum > 0)
                {
                    item.numeral = resolvedNum;
                }
            }
            else if (item.uid.empty() && item.numeral > 0 && _rfidService)
            {
                String resolvedUid = _rfidService->getCardUid(item.numeral);
                if (!resolvedUid.isEmpty())
                {
                    item.uid = resolvedUid.c_str();
                }
            }

            remotos.push_back(item);
        }
    }

    bool sobrescreverConflitos = root["sobrescreverConflitos"].is<bool>() && root["sobrescreverConflitos"].as<bool>();

    // P0-06: Libera a árvore JSON de entrada antes de executar merge e persistência
    json = JsonVariant();

    resultado_merge_sync_t res = _cronometragemService->consolidarRegistros(remotos, sobrescreverConflitos);

    invalidarCacheApuracao();

    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject respRoot = response.getRoot();
    respRoot["status"] = "ok";
    respRoot["recebidos"] = res.recebidos;
    respRoot["inseridos"] = res.inseridos;
    respRoot["conciliados"] = res.conciliados;
    respRoot["duplicadosIgnorados"] = res.duplicadosIgnorados;
    respRoot["conflitosDetectados"] = res.conflitosDetectados;
    respRoot["totalConsolidado"] = res.diagnostico.totalRegistros;

    JsonObject diagObj = respRoot["diagnostico"].to<JsonObject>();
    diagObj["totalRegistros"] = res.diagnostico.totalRegistros;
    diagObj["concluidos"] = res.diagnostico.concluidos;
    diagObj["pendentesChegada"] = res.diagnostico.pendentesChegada;
    diagObj["semLargada"] = res.diagnostico.semLargada;
    diagObj["invalidos"] = res.diagnostico.invalidos;

    JsonArray conflitosArr = respRoot["conflitos"].to<JsonArray>();
    for (const auto &c : res.conflitos)
    {
        JsonObject cObj = conflitosArr.add<JsonObject>();
        cObj["uid"] = c.uid;
        cObj["numeral"] = c.numeral;
        cObj["especial"] = c.especial;
        cObj["campo"] = c.campo;
        cObj["valorLocal"] = c.valorLocal;
        cObj["valorRemoto"] = c.valorRemoto;
    }

    return response.send();
}

esp_err_t SyncService::handleGetResultadosPublicos(PsychicRequest *request)
{
    constexpr size_t STREAM_BUF_SIZE = 2048;
    uint8_t *buffer = static_cast<uint8_t *>(malloc(STREAM_BUF_SIZE));
    if (!buffer)
    {
        return request->reply(500, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Estouro de memória ao apurar resultados.\"}");
    }

    std::string criterioVoltasOverride;
    if (request->hasParam("criterioVoltas"))
    {
        PsychicWebParameter *param = request->getParam("criterioVoltas");
        if (param)
        {
            criterioVoltasOverride = param->value().c_str();
        }
    }

    bool incluirRaw = request->hasParam("includeRaw") && request->getParam("includeRaw")->value() == "true";

    // P0-01: Proteção por mutex e uso do cache de apuração para zerar triplicação em RAM
    if (_cacheMutex)
    {
        xSemaphoreTake(_cacheMutex, portMAX_DELAY);
    }

    if (!_cacheApuracaoValido || _cacheCriterioVoltas != criterioVoltasOverride)
    {
        atualizarCacheApuracao(criterioVoltasOverride);
    }

    DateTime now = Relogio::getNow();
    std::string relogioIso = Relogio::formatDateTime(now);
    uint32_t unixtime = now.unixtime();

    PsychicResponse response(request);
    response.setContentType(JSON_MIMETYPE);
    response.sendHeaders();

    ChunkPrinter dest(&response, buffer, STREAM_BUF_SIZE);

    if (incluirRaw && _cronometragemService)
    {
        _cronometragemService->read([&](Cronometragem &crono) {
            ApuracaoResultados::serializarRelatorioPublicoToStream(
                dest,
                _cacheProva,
                _cacheCategorias,
                _cacheCronometrar,
                relogioIso,
                unixtime,
                _cacheRelatorio,
                crono.cronometragens,
                true);
        });
    }
    else
    {
        static const std::deque<cronometragem_t> emptyDeque;
        ApuracaoResultados::serializarRelatorioPublicoToStream(
            dest,
            _cacheProva,
            _cacheCategorias,
            _cacheCronometrar,
            relogioIso,
            unixtime,
            _cacheRelatorio,
            emptyDeque,
            false);
    }

    if (_cacheMutex)
    {
        xSemaphoreGive(_cacheMutex);
    }

    dest.flush();
    esp_err_t err = response.finishChunking();
    free(buffer);
    return err;
}

esp_err_t SyncService::handlePostResetEstacao(PsychicRequest *request, JsonVariant &json)
{
    JsonObject root;
    if (json.is<JsonObject>())
    {
        root = json.as<JsonObject>();
    }

    // Por padrão (se não especificado ou se vazio), reseta tudo para nova prova
    bool limparCronometragem = root["limparCronometragem"].is<bool>() ? root["limparCronometragem"].as<bool>() : true;
    bool limparProva = root["limparProva"].is<bool>() ? root["limparProva"].as<bool>() : true;
    bool limparAtletas = root["limparAtletas"].is<bool>() ? root["limparAtletas"].as<bool>() : true;
    bool limparCategorias = root["limparCategorias"].is<bool>() ? root["limparCategorias"].as<bool>() : true;
    bool limparRfidCards = root["limparRfidCards"].is<bool>() ? root["limparRfidCards"].as<bool>() : true;
    bool pararCronometragem = root["pararCronometragem"].is<bool>() ? root["pararCronometragem"].as<bool>() : true;

    // 1. Interrompe a cronometragem se estiver ativa ou se solicitado
    if (pararCronometragem && _cronometrarService)
    {
        _cronometrarService->stopCronometragem();
    }

    // 2. Interrompe modo reset do RFID se estiver ativo
    if (_rfidService && _rfidService->isResetMode())
    {
        _rfidService->stopResetMode();
    }

    // 3. Limpa tempos/cronometragens registrados
    if (limparCronometragem && _cronometragemService)
    {
        _cronometragemService->update([](Cronometragem &state) {
            state.cronometragens.clear();
            return StateUpdateResult::CHANGED;
        }, "reset_estacao");
    }

    // 4. Limpa cartões RFID cadastrados na estação
    if (limparRfidCards && _rfidService)
    {
        _rfidService->update([](RFIDState &state) {
            state._cardRegistry.clear();
            state.nextIndex = 1;
            return StateUpdateResult::CHANGED;
        }, "reset_estacao");
    }

    // 5. Limpa atletas / pilotos
    if (limparAtletas && _atletaService)
    {
        _atletaService->update([](Atleta &state) {
            state.atletas.clear();
            return StateUpdateResult::CHANGED;
        }, "reset_estacao");
    }

    // 6. Limpa categorias
    if (limparCategorias && _categoriaService)
    {
        _categoriaService->update([](Categoria &state) {
            state.categorias.clear();
            return StateUpdateResult::CHANGED;
        }, "reset_estacao");
    }

    // 7. Limpa dados da Prova
    if (limparProva && _provaService)
    {
        _provaService->update([](Prova &state) {
            state.nome = "";
            state.data = "";
            state.local = "";
            state.tipo = "especial";
            state.modoVoltas = "unica";
            state.criterioVoltas = "voltas_tempo";
            state.totalEstagios = Prova::DEFAULT_ESTAGIOS;
            return StateUpdateResult::CHANGED;
        }, "reset_estacao");
    }

    invalidarCacheApuracao();

    PsychicJsonResponse response = PsychicJsonResponse(request, false);
    JsonObject respRoot = response.getRoot();
    respRoot["status"] = "sucesso";
    respRoot["mensagem"] = "Dados da estação resetados com sucesso para nova prova.";
    JsonObject detalhes = respRoot["detalhes"].to<JsonObject>();
    detalhes["cronometragemLimpa"] = limparCronometragem;
    detalhes["provaLimpa"] = limparProva;
    detalhes["atletasLimpos"] = limparAtletas;
    detalhes["categoriasLimpas"] = limparCategorias;
    detalhes["rfidCardsLimpos"] = limparRfidCards;

    return response.send();
}
