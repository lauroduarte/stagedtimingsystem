#ifndef SyncService_h
#define SyncService_h

#include <HttpEndpoint.h>
#include <ESP32SvelteKit.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "ProvaService.h"
#include "CategoriaService.h"
#include "AtletaService.h"
#include "RFIDService.h"
#include "RelogioService.h"
#include "CronometrarService.h"
#include "CronometragemService.h"
#include "ApuracaoResultados.h"

#define SYNC_STATUS_ENDPOINT_PATH "/rest/sync/status"
#define SYNC_CONFIG_ENDPOINT_PATH "/rest/sync/config"
#define SYNC_RESULTADOS_ENDPOINT_PATH "/rest/sync/resultados"
#define RESULTADOS_PUBLICOS_ENDPOINT_PATH "/rest/resultados-publicos"
#define RESET_ESTACAO_ENDPOINT_PATH "/rest/estacao/reset"
#define SYNC_LEGACY_ENDPOINT_PATH "/sync"

class SyncService
{
public:
    static constexpr int PROTOCOL_VERSION = 1;
    static constexpr unsigned long SYNC_MAX_BODY_SIZE = 32 * 1024;

    SyncService(PsychicHttpServer *server,
                ESP32SvelteKit *sveltekit,
                ProvaService *provaService,
                CategoriaService *categoriaService,
                AtletaService *atletaService,
                RFIDService *rfidService,
                RelogioService *relogioService,
                CronometrarService *cronometrarService,
                CronometragemService *cronometragemService);

    void begin();
    void invalidarCacheApuracao();

private:
    PsychicHttpServer *_server;
    SecurityManager *_securityManager;
    ProvaService *_provaService;
    CategoriaService *_categoriaService;
    AtletaService *_atletaService;
    RFIDService *_rfidService;
    RelogioService *_relogioService;
    CronometrarService *_cronometrarService;
    CronometragemService *_cronometragemService;

    // P0-01: Cache de apuração protegido por mutex e invalidado por dirty flag
    SemaphoreHandle_t _cacheMutex = nullptr;
    bool _cacheApuracaoValido = false;
    relatorio_apuracao_t _cacheRelatorio;
    std::string _cacheCriterioVoltas;
    Prova _cacheProva;
    Cronometrar _cacheCronometrar;
    std::vector<categoria_t> _cacheCategorias;
    uint32_t _versaoApuracao = 0;

    void atualizarCacheApuracao(const std::string &criterioVoltas);
    void registerOptionsEndpoint(const char *uri);
    esp_err_t handleGetStatus(PsychicRequest *request);
    esp_err_t handleGetConfig(PsychicRequest *request);
    esp_err_t handlePostConfig(PsychicRequest *request, JsonVariant &json);
    esp_err_t handleGetResultados(PsychicRequest *request);
    esp_err_t handlePostResultados(PsychicRequest *request, JsonVariant &json);
    esp_err_t handleGetResultadosPublicos(PsychicRequest *request);
    esp_err_t handlePostResetEstacao(PsychicRequest *request, JsonVariant &json);
};

#endif

