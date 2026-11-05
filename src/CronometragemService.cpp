#include <CronometragemService.h>
#include <esp_log.h>

CronometragemService::CronometragemService(PsychicHttpServer *server,
                                           ESP32SvelteKit *sveltekit,
                                           FS *fs) : _server(server),
                                                     _securityManager(sveltekit->getSecurityManager()),
                                                     _fs(fs ? fs : sveltekit->getFS()),
                                                     _fsPersistence(
                                                         Cronometragem::read,
                                                         Cronometragem::update,
                                                         this,
                                                         fs ? fs : sveltekit->getFS(),
                                                         CRONOMETRAGEM_LEGACY_JSON_PATH),
                                                     _httpEndpoint(Cronometragem::read,
                                                                   Cronometragem::update,
                                                                   this,
                                                                   server,
                                                                   CRONOMETRAGEM_SETTINGS_ENDPOINT_PATH,
                                                                   sveltekit->getSecurityManager(),
                                                                   AuthenticationPredicates::IS_AUTHENTICATED),
                                                     _fsMutex(xSemaphoreCreateRecursiveMutex())
{
    // P0-02: Desativa a persistência automática monolítica do FSPersistence para cronometragem
    // evitando reescrita de arquivo completo O(N^2) a cada passagem RFID.
    _fsPersistence.disableUpdateHandler();

    // Registra handler para eventos de atualização administrativa/sync/reset
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(originId); },
                     false);
}

void CronometragemService::begin()
{
#ifdef ENABLE_CORS
    _server->on(CRONOMETRAGEM_SETTINGS_ENDPOINT_PATH,
                HTTP_OPTIONS,
                _securityManager->wrapRequest(
                    [](PsychicRequest *request)
                    {
                        return request->reply(200);
                    },
                    AuthenticationPredicates::NONE_REQUIRED));
#endif

    _server->on(CRONOMETRAGEM_SETTINGS_ENDPOINT_PATH,
                HTTP_GET,
                _securityManager->wrapRequest(
                    [this](PsychicRequest *request)
                    {
                        return handleGetCronometragem(request);
                    },
                    AuthenticationPredicates::IS_AUTHENTICATED));

    _server->on(CRONOMETRAGEM_SETTINGS_ENDPOINT_PATH,
                HTTP_POST,
                _securityManager->wrapCallback(
                    [this](PsychicRequest *request, JsonVariant &json)
                    {
                        return handlePostCronometragem(request, json);
                    },
                    AuthenticationPredicates::IS_AUTHENTICATED));

    readFromFS();
    onConfigUpdated("begin");
}

esp_err_t CronometragemService::sendCronometragemStream(PsychicRequest *request)
{
    constexpr size_t STREAM_BUF_SIZE = 2048;
    uint8_t *buffer = static_cast<uint8_t *>(malloc(STREAM_BUF_SIZE));
    if (!buffer)
    {
        return request->reply(500, "application/json", "{\"status\":\"erro\",\"mensagem\":\"Sem memoria para streaming.\"}");
    }

    PsychicResponse response(request);
    response.setContentType(JSON_MIMETYPE);
    response.sendHeaders();

    ChunkPrinter dest(&response, buffer, STREAM_BUF_SIZE);
    dest.print("{\"cronometragem\":");
    this->read([&](Cronometragem &state) {
        Cronometragem::serializarListaToStream(state.cronometragens, dest);
    });
    dest.write('}');
    dest.flush();

    esp_err_t err = response.finishChunking();
    free(buffer);
    return err;
}

esp_err_t CronometragemService::handleGetCronometragem(PsychicRequest *request)
{
    return sendCronometragemStream(request);
}

esp_err_t CronometragemService::handlePostCronometragem(PsychicRequest *request, JsonVariant &json)
{
    request->clearBody();

    if (!json.is<JsonObject>())
    {
        return request->reply(400);
    }

    JsonObject jsonObject = json.as<JsonObject>();
    StateUpdateResult outcome = this->updateWithoutPropagation(jsonObject, Cronometragem::update);
    json = JsonVariant();

    if (outcome == StateUpdateResult::ERROR)
    {
        return request->reply(400);
    }
    else if (outcome == StateUpdateResult::CHANGED)
    {
        this->callUpdateHandlers(HTTP_ENDPOINT_ORIGIN_ID);
    }

    return sendCronometragemStream(request);
}

void CronometragemService::onConfigUpdated(const String &originId)
{
    // "cronometragem" é persistido imediatamente via appendRegistroToFS
    // "sync_resultados" já executa saveToFS() explicitamente ao final de consolidarRegistros()
    // "boot_load", "migration", "begin" são operações de carga/inicialização
    if (originId == "cronometragem" || originId == "sync_resultados" || originId == "boot_load" || originId == "migration" || originId == "begin")
    {
        return;
    }

    // Para requisições HTTP ("/rest/cronometragem") ou reset da estação:
    // Reescrita atômica completa do arquivo (.tmp + rename)
    saveToFS();
}

void CronometragemService::registerConfig()
{
}

bool CronometragemService::jaRegistrado(const std::string &uid, const std::string &especial, const std::string &posicao)
{
    bool encontrado = false;
    this->read([&](Cronometragem &state) {
        for (const auto &item : state.cronometragens)
        {
            // P2-02: Filtro rápido por especial antes de comparar UID
            if (item.especial == especial && item.uid == uid)
            {
                if (posicao == "inicio" && !item.inicio.empty())
                {
                    encontrado = true;
                    break;
                }
                if (posicao == "fim" && !item.fim.empty())
                {
                    encontrado = true;
                    break;
                }
            }
        }
    });
    return encontrado;
}

bool CronometragemService::obterHorarioInicio(const std::string &uid, const std::string &especial, std::string &outInicio)
{
    bool encontrado = false;
    outInicio = "";
    this->read([&](Cronometragem &state) {
        for (const auto &item : state.cronometragens)
        {
            if (item.especial == especial && item.uid == uid && !item.inicio.empty())
            {
                outInicio = item.inicio.c_str();
                encontrado = true;
                break;
            }
        }
    });
    return encontrado;
}

bool CronometragemService::obterUltimaPassagemVoltaUnica(const std::string &uid, std::string &outUltimoHorario, int &outProximaVolta)
{
    bool encontrado = false;
    int maxVolta = -1;
    outUltimoHorario = "";
    outProximaVolta = 0;

    this->read([&](Cronometragem &state) {
        for (const auto &item : state.cronometragens)
        {
            bool isLapItem = (item.tipo == "laps") || (!item.especial.empty() && item.especial[0] == 'L');
            if (item.uid == uid && isLapItem)
            {
                if (item.volta >= maxVolta)
                {
                    maxVolta = item.volta;
                    encontrado = true;
                    if (item.volta == 0)
                    {
                        outUltimoHorario = item.inicio.c_str();
                        outProximaVolta = 1;
                    }
                    else
                    {
                        outUltimoHorario = !item.fim.empty() ? item.fim.c_str() : item.inicio.c_str();
                        outProximaVolta = item.volta + 1;
                    }
                }
            }
        }
    });

    return encontrado;
}

bool CronometragemService::obterEstadoVoltaDuasEstacoes(const std::string &uid, int &outUltimaVolta, bool &outTemLargadaAberta, std::string &outHorarioLargadaAberta)
{
    bool encontrado = false;
    int maxVolta = 0;
    outUltimaVolta = 0;
    outTemLargadaAberta = false;
    outHorarioLargadaAberta = "";

    this->read([&](Cronometragem &state) {
        for (const auto &item : state.cronometragens)
        {
            bool isLapItem = (item.tipo == "laps") || (!item.especial.empty() && item.especial[0] == 'L');
            if (item.uid == uid && isLapItem && item.volta >= 1)
            {
                if (item.volta >= maxVolta)
                {
                    maxVolta = item.volta;
                    encontrado = true;
                    outUltimaVolta = item.volta;
                    outTemLargadaAberta = !item.inicio.empty() && item.fim.empty();
                    outHorarioLargadaAberta = outTemLargadaAberta ? item.inicio.c_str() : "";
                }
            }
        }
    });

    return encontrado;
}

void CronometragemService::registrarTempo(const cronometragem_t &novo)
{
    cronometragem_t registro = novo;
    Cronometragem::preencherDerivados(registro);

    cronometragem_t consolidado;
    bool alterou = false;

    // Atualiza estado em memória
    update([&](Cronometragem &state) {
        alterou = Cronometragem::aplicarRegistro(state.cronometragens, registro, &consolidado);
        return alterou ? StateUpdateResult::CHANGED : StateUpdateResult::UNCHANGED;
    }, "cronometragem");

    // P0-02: Se o estado foi alterado, faz persistência append-only em < 5 ms com 0 B de heap
    if (alterou)
    {
        appendRegistroToFS(consolidado);
    }
}

namespace {
struct FsRecursiveLockGuard {
    SemaphoreHandle_t mtx;
    explicit FsRecursiveLockGuard(SemaphoreHandle_t m) : mtx(m) {
        if (mtx) xSemaphoreTakeRecursive(mtx, portMAX_DELAY);
    }
    ~FsRecursiveLockGuard() {
        if (mtx) xSemaphoreGiveRecursive(mtx);
    }
};
} // namespace

bool CronometragemService::appendRegistroToFS(const cronometragem_t &reg)
{
    if (!_fs)
    {
        return false;
    }

    FsRecursiveLockGuard lock(_fsMutex);

    if (!_fs->exists("/config"))
    {
        _fs->mkdir("/config");
    }

    File file = _fs->open(CRONOMETRAGEM_JSONL_PATH, "a");
    if (!file)
    {
        ESP_LOGE("CronoFS", "Erro ao abrir %s para append", CRONOMETRAGEM_JSONL_PATH);
        return false;
    }

    char buf[256];
    int len = Cronometragem::formatItemJson(reg, buf, sizeof(buf), true);

    if (len <= 0 || static_cast<size_t>(len) >= sizeof(buf))
    {
        file.close();
        return false;
    }

    size_t written = file.write(reinterpret_cast<const uint8_t*>(buf), len);
    file.flush();
    file.close();

    return written == static_cast<size_t>(len);
}

bool CronometragemService::saveToFS()
{
    if (!_fs)
    {
        return false;
    }

    FsRecursiveLockGuard lock(_fsMutex);

    if (!_fs->exists("/config"))
    {
        _fs->mkdir("/config");
    }

    String tmpPath = String(CRONOMETRAGEM_JSONL_PATH) + ".tmp";
    File file = _fs->open(tmpPath.c_str(), "w");
    if (!file)
    {
        ESP_LOGE("CronoFS", "Erro ao abrir %s para escrita atomica", tmpPath.c_str());
        return false;
    }

    char buf[256];
    bool ok = true;

    this->read([&](Cronometragem &state) {
        for (const auto &reg : state.cronometragens)
        {
            int len = Cronometragem::formatItemJson(reg, buf, sizeof(buf), true);

            if (len > 0 && static_cast<size_t>(len) < sizeof(buf))
            {
                if (file.write(reinterpret_cast<const uint8_t*>(buf), len) != static_cast<size_t>(len))
                {
                    ok = false;
                    break;
                }
            }
        }
    });

    file.flush();
    file.close();

    if (!ok)
    {
        _fs->remove(tmpPath.c_str());
        ESP_LOGE("CronoFS", "Falha na gravacao atomica dos registros em %s", tmpPath.c_str());
        return false;
    }

    // Atomic rename
    if (!_fs->rename(tmpPath.c_str(), CRONOMETRAGEM_JSONL_PATH))
    {
        _fs->remove(CRONOMETRAGEM_JSONL_PATH);
        if (!_fs->rename(tmpPath.c_str(), CRONOMETRAGEM_JSONL_PATH))
        {
            _fs->remove(tmpPath.c_str());
            ESP_LOGE("CronoFS", "Falha ao renomear %s para %s", tmpPath.c_str(), CRONOMETRAGEM_JSONL_PATH);
            return false;
        }
    }

    return true;
}

bool CronometragemService::readFromFS()
{
    if (!_fs)
    {
        return false;
    }

    FsRecursiveLockGuard lock(_fsMutex);

    String jsonlPath = CRONOMETRAGEM_JSONL_PATH;
    String tmpPath = jsonlPath + ".tmp";

    // 1. Se o arquivo principal não existir mas houver .tmp, restaura o .tmp
    if (!_fs->exists(jsonlPath.c_str()) && _fs->exists(tmpPath.c_str()))
    {
        ESP_LOGW("CronoFS", "Arquivo %s ausente. Restaurando a partir de %s", jsonlPath.c_str(), tmpPath.c_str());
        _fs->rename(tmpPath.c_str(), jsonlPath.c_str());
    }

    // 2. Se existir o arquivo JSONL, lê linha a linha
    if (_fs->exists(jsonlPath.c_str()))
    {
        File file = _fs->open(jsonlPath.c_str(), "r");
        if (file)
        {
            size_t validos = 0;
            size_t ignorados = 0;

            this->update([&](Cronometragem &state) {
                state.cronometragens.clear();

                while (file.available())
                {
                    String line = file.readStringUntil('\n');
                    line.trim();
                    if (line.length() == 0)
                    {
                        continue;
                    }

                    JsonDocument doc;
                    DeserializationError err = deserializeJson(doc, line);
                    if (err == DeserializationError::Ok && doc.is<JsonObject>())
                    {
                        cronometragem_t item;
                        if (Cronometragem::parseJsonItem(doc.as<JsonObject>(), item))
                        {
                            Cronometragem::aplicarRegistro(state.cronometragens, item);
                            validos++;
                        }
                        else
                        {
                            ignorados++;
                        }
                    }
                    else
                    {
                        // Linha corrompida ou truncada (ex: queda de energia durante append da última linha)
                        ignorados++;
                        ESP_LOGW("CronoFS", "Linha ignorada no boot: %s", line.c_str());
                    }
                }
                return StateUpdateResult::UNCHANGED;
            }, "boot_load");

            file.close();

            if (_fs->exists(tmpPath.c_str()))
            {
                _fs->remove(tmpPath.c_str());
            }

            ESP_LOGI("CronoFS", "Boot: Carregados %u registros validos (%u linhas ignoradas)", validos, ignorados);

            // Se existia também arquivo legado não-migrado, arquiva-o
            if (_fs->exists(CRONOMETRAGEM_LEGACY_JSON_PATH))
            {
                String legacyBak = String(CRONOMETRAGEM_LEGACY_JSON_PATH) + ".migrated";
                _fs->rename(CRONOMETRAGEM_LEGACY_JSON_PATH, legacyBak.c_str());
            }

            return true;
        }
    }

    // 3. Caso não exista .jsonl, verifica compatibilidade de migração com /config/cronometragem.json legado
    if (_fs->exists(CRONOMETRAGEM_LEGACY_JSON_PATH))
    {
        ESP_LOGI("CronoFS", "Detectado arquivo legado %s. Iniciando migracao...", CRONOMETRAGEM_LEGACY_JSON_PATH);
        File legacyFile = _fs->open(CRONOMETRAGEM_LEGACY_JSON_PATH, "r");
        if (legacyFile)
        {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, legacyFile);
            legacyFile.close();

            if (err == DeserializationError::Ok && doc.is<JsonObject>())
            {
                JsonObject root = doc.as<JsonObject>();
                this->update([&](Cronometragem &state) {
                    Cronometragem::update(root, state);
                    return StateUpdateResult::UNCHANGED;
                }, "migration");

                // Salva os dados migrados no formato JSONL
                saveToFS();

                String legacyBak = String(CRONOMETRAGEM_LEGACY_JSON_PATH) + ".migrated";
                _fs->rename(CRONOMETRAGEM_LEGACY_JSON_PATH, legacyBak.c_str());

                ESP_LOGI("CronoFS", "Migracao concluida com sucesso para %s", CRONOMETRAGEM_JSONL_PATH);
                return true;
            }
            else
            {
                String legacyBak = String(CRONOMETRAGEM_LEGACY_JSON_PATH) + ".bak";
                _fs->rename(CRONOMETRAGEM_LEGACY_JSON_PATH, legacyBak.c_str());
                ESP_LOGE("CronoFS", "Arquivo legado corrompido, preservado em %s", legacyBak.c_str());
            }
        }
    }

    // 4. Se não existe arquivo nenhum, inicia limpo
    return true;
}

resultado_merge_sync_t CronometragemService::consolidarRegistros(const std::vector<cronometragem_t> &remotos, bool sobrescreverConflitos)
{
    resultado_merge_sync_t resultado{};
    bool alterou = false;
    update([&](Cronometragem &state) {
        alterou = Cronometragem::mesclarLista(state.cronometragens, remotos, resultado, sobrescreverConflitos);
        return alterou ? StateUpdateResult::CHANGED : StateUpdateResult::UNCHANGED;
    }, "sync_resultados");
    if (alterou)
    {
        saveToFS();
    }
    return resultado;
}

resultado_merge_sync_t CronometragemService::consolidarRegistros(const std::deque<cronometragem_t> &remotos, bool sobrescreverConflitos)
{
    resultado_merge_sync_t resultado{};
    bool alterou = false;
    update([&](Cronometragem &state) {
        alterou = Cronometragem::mesclarLista(state.cronometragens, remotos, resultado, sobrescreverConflitos);
        return alterou ? StateUpdateResult::CHANGED : StateUpdateResult::UNCHANGED;
    }, "sync_resultados");
    if (alterou)
    {
        saveToFS();
    }
    return resultado;
}

diagnostico_cronometragem_t CronometragemService::obterDiagnostico()
{
    diagnostico_cronometragem_t diag{};
    this->read([&](Cronometragem &state) {
        diag = Cronometragem::diagnosticarLista(state.cronometragens);
    });
    return diag;
}
