#include <CronometrarService.h>
#include "RFIDService.h"
#include "RelogioService.h"
#include "CronometragemService.h"

CronometrarService::CronometrarService(
                    PsychicHttpServer *server,
                    ESP32SvelteKit *sveltekit, 
                    FS *fs, 
                    MFRC522 *rfid,
                    BuzzerService *buzzer, 
                    DisplayService *display) : 
                                _httpEndpoint(Cronometrar::read,
                                             Cronometrar::update,
                                             this,
                                             server,
                                             CRONOMETRAR_SETTINGS_ENDPOINT_PATH,
                                             sveltekit->getSecurityManager(),
                                             AuthenticationPredicates::IS_AUTHENTICATED),
                               _fsPersistence(Cronometrar::read, Cronometrar::update, this, sveltekit->getFS(), "/config/cronometrar.json"),
                               _rfid(SS_PIN, RST_PIN),
                               _buzzer(),
                               _display(),
                               _cronometragemService(server, sveltekit, fs)
{
    // configure settings service update handler to update LED state
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

void CronometrarService::begin()
{
    // Inicializar SPI e leitor RFID.
    SPI.begin();
    _rfid.PCD_Init();
    _httpEndpoint.begin();
    _cronometragemService.begin();
    _fsPersistence.readFromFS();
    onConfigUpdated();
}

void CronometrarService::loop()
{
    if (!_cronometrar || !_rfid.PICC_IsNewCardPresent() || !_rfid.PICC_ReadCardSerial())
    {
        // Se o cronometrar não estiver ativo, não processa o loop do RFID.
        return;
    }

    if (_state.posicao == "inicio")
    {

        if (_buzzer)
        {
            _buzzer->beepTone(1000, 150); // beep mais agudo na largada
        }
        if (_display)
        {
            _display->showGoBlinking(2000, 400); // pisca "GO" por 2 segundos
        }
    }

    if (_state.posicao == "fim")
    {
        // Após gravar horário de fim:
        if (_buzzer)
        {
            _buzzer->beepTone(1000, 300); // beep mais grave e longo na chegada
        }
        if(_display)
        {
            _display->showFin(2000); // mostra "FIN" por 2 segundos
        }
    }

    String uid = RFIDService::uidToString(&_rfid.uid);
    Serial.println("Cartão detectado: " + uid);

    // 2. Lê dados do cartão (começando do bloco 4, por exemplo)
    String rawData = RFIDService::readCardData(_rfid, 4, 10); // lê 10 blocos
    DynamicJsonDocument doc(512);
    deserializeJson(doc, rawData);

    // 3. Atualiza histórico com novo horário
    String especial = String(_state.especial.c_str());
    String posicao = String(_state.posicao.c_str());

    DateTime now = Relogio::getRTC().now(); // se você já tiver um RTC global
    char buffer[10];
    sprintf(buffer, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());

    doc["uid"] = uid;
    doc["especial"][especial][posicao] = buffer;

    // 4. Serializa de volta e escreve no cartão
    String novoJson;
    serializeJson(doc, novoJson);
    RFIDService::writeCardData(_rfid, 4, novoJson); // sobrescreve os mesmos blocos

    // 5. Atualiza o estado do serviço
    _cronometragemService.registrarTempo({.uid = uid.c_str(),
                                          .especial = especial.c_str(),
                                          .inicio = posicao == "inicio" ? buffer : "",
                                          .fim = posicao == "fim" ? buffer : ""});
}

void CronometrarService::onConfigUpdated()
{
    _cronometrar = _state.cronometrar;
}

void CronometrarService::registerConfig()
{
}
