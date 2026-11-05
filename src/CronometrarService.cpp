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
                    DisplayService *display,
                    CronometragemService *cronometragemService,
                    RFIDService *rfidService,
                    AtletaService *atletaService,
                    ProvaService *provaService) : 
                               _fsPersistence(Cronometrar::read, Cronometrar::update, this, sveltekit->getFS(), "/config/cronometrar.json"),
                               _httpEndpoint(Cronometrar::read,
                                             std::bind(&CronometrarService::updateFromHttp, this, std::placeholders::_1, std::placeholders::_2),
                                             this,
                                             server,
                                             CRONOMETRAR_SETTINGS_ENDPOINT_PATH,
                                             sveltekit->getSecurityManager(),
                                             AuthenticationPredicates::IS_AUTHENTICATED),
                               _cronometragemService(cronometragemService),
                               _rfidService(rfidService),
                               _atletaService(atletaService),
                               _provaService(provaService),
                               _display(display),
                               _rfid(rfid),
                               _buzzer(buzzer),
                               _cronometrar(false),
                               _lastReadUid(""),
                               _lastReadTimeMs(0)
{
    // configure settings service update handler
    addUpdateHandler([&](const String &originId)
                     { onConfigUpdated(); },
                     false);
}

StateUpdateResult CronometrarService::updateFromHttp(JsonObject &root, Cronometrar &cronometrar)
{
    bool requestingStart = root["cronometrar"].is<bool>() && root["cronometrar"].as<bool>();
    if (requestingStart)
    {
        if (_provaService && !_provaService->isConfigured())
        {
            Serial.println("Bloqueado início da cronometragem: Prova ainda não configurada.");
            return StateUpdateResult::ERROR;
        }
        if (_atletaService && !_atletaService->hasAnyAtleta())
        {
            Serial.println("Bloqueado início da cronometragem: Nenhum atleta cadastrado.");
            return StateUpdateResult::ERROR;
        }
        if (_provaService && _provaService->isConfigured())
        {
            Prova prova = _provaService->getProvaState();
            std::string reqTipo = root["tipo"].is<const char*>() ? root["tipo"].as<std::string>() : cronometrar.tipo;
            if (!prova.tipo.empty() && reqTipo != prova.tipo)
            {
                Serial.println("Bloqueado início da cronometragem: Tipo diverge do modo configurado na Prova.");
                return StateUpdateResult::ERROR;
            }
            if (reqTipo == "especial")
            {
                std::string reqEspecial = root["especial"].is<const char*>() ? root["especial"].as<std::string>() : cronometrar.especial;
                if (!Cronometrar::isValidStageName(reqEspecial, prova.totalEstagios))
                {
                    Serial.printf("Bloqueado início da cronometragem: Estágio %s fora do limite da prova (%d estágios).\n",
                                  reqEspecial.c_str(), prova.totalEstagios);
                    return StateUpdateResult::ERROR;
                }
            }
        }
    }

    return Cronometrar::update(root, cronometrar);
}

void CronometrarService::begin()
{
    _httpEndpoint.begin();
    _fsPersistence.readFromFS();
    onConfigUpdated();
}

void CronometrarService::stopCronometragem()
{
    if (_cronometrar)
    {
        Serial.println("Stopping cronometragem automatically due to RFID reset mode");
        this->update([&](Cronometrar &state) {
            if (!state.cronometrar)
            {
                return StateUpdateResult::UNCHANGED;
            }
            state.cronometrar = false;
            return StateUpdateResult::CHANGED;
        }, "stopCronometragem");
    }
}

String CronometrarService::extrairHorarioCartao(const JsonDocument &doc, const String &especial, const String &posicao)
{
    const char *compactPosKey = (posicao == "inicio") ? "i" : "f";

    // 1. Verifica formato compacto: {"u":"...","e":{"E1":{"i":"HH:MM:SS","f":"HH:MM:SS"}}}
    JsonVariantConst compactVal = doc["e"][especial][compactPosKey];
    if (compactVal.is<const char*>())
    {
        String val = compactVal.as<String>();
        if (!val.isEmpty())
        {
            return val;
        }
    }

    // 2. Fallback para formato legado verboso: {"uid":"...","especial":{"E1":{"inicio":"HH:MM:SS","fim":"HH:MM:SS"}}}
    JsonVariantConst legacyVal = doc["especial"][especial][posicao];
    if (legacyVal.is<const char*>())
    {
        return legacyVal.as<String>();
    }

    return "";
}

void CronometrarService::atualizarHorarioCartaoCompacto(JsonDocument &doc, const String &uid, const String &especial, const String &posicao, const String &horario)
{
    // Migra chaves legadas ("especial" -> "e", "uid" -> "u") se existirem no cartão
    if (doc["especial"].is<JsonObject>())
    {
        JsonObject legacyEspeciais = doc["especial"].as<JsonObject>();
        for (JsonPair kv : legacyEspeciais)
        {
            const char *stageKey = kv.key().c_str();
            JsonObject stageObj = kv.value().as<JsonObject>();
            if (stageObj["inicio"].is<const char*>())
            {
                doc["e"][stageKey]["i"] = stageObj["inicio"].as<const char*>();
            }
            if (stageObj["fim"].is<const char*>())
            {
                doc["e"][stageKey]["f"] = stageObj["fim"].as<const char*>();
            }
        }
        doc.remove("especial");
    }
    if (!doc["uid"].isNull())
    {
        doc.remove("uid");
    }

    const char *compactPosKey = (posicao == "inicio") ? "i" : "f";
    doc["u"] = uid;
    doc["e"][especial][compactPosKey] = horario;
}

int CronometrarService::obterQuantidadePassagensVoltaUnicaCartao(const JsonDocument &doc)
{
    JsonVariantConst lapsVar = doc["l"];
    if (!lapsVar.is<JsonArrayConst>())
    {
        return 0;
    }
    return static_cast<int>(lapsVar.as<JsonArrayConst>().size());
}

String CronometrarService::extrairUltimaPassagemVoltaUnicaCartao(const JsonDocument &doc)
{
    JsonVariantConst lapsVar = doc["l"];
    if (!lapsVar.is<JsonArrayConst>())
    {
        return "";
    }
    JsonArrayConst arr = lapsVar.as<JsonArrayConst>();
    if (arr.size() == 0)
    {
        return "";
    }
    JsonVariantConst lastVal = arr[arr.size() - 1];
    if (lastVal.is<const char*>())
    {
        return lastVal.as<String>();
    }
    return "";
}

void CronometrarService::adicionarPassagemVoltaUnicaCartao(JsonDocument &doc, const String &uid, const String &horario)
{
    if (!doc["uid"].isNull())
    {
        doc.remove("uid");
    }
    doc["u"] = uid;
    JsonArray arr = doc["l"].is<JsonArray>() ? doc["l"].as<JsonArray>() : doc["l"].to<JsonArray>();
    arr.add(horario);
}

void CronometrarService::processarPassagemEstagio(const String &uid, int cardNumber, JsonDocument &doc, const Cronometrar &cfg, const DateTime &now, const String &horarioAtual, bool isBinaryCard, const RFIDCard::Header *existingHeader)
{
    String especial = String(cfg.especial.c_str());
    String posicao = String(cfg.posicao.c_str());

    // 4. Proteção contra sobrescrita do mesmo (uid, especial, posicao) (BL-06 / DEC-04)
    String horarioExistenteCartao = extrairHorarioCartao(doc, especial, posicao);
    bool jaRegistradoEstacao = _cronometragemService && _cronometragemService->jaRegistrado(uid.c_str(), especial.c_str(), posicao.c_str());
    if (!horarioExistenteCartao.isEmpty() || jaRegistradoEstacao)
    {
        Serial.printf("Leitura repetida bloqueada para UID %s em %s (%s). Horário original preservado.\n",
                      uid.c_str(), especial.c_str(), posicao.c_str());
        if (_buzzer)
        {
            _buzzer->beepWarning();
        }
        _lastReadUid = uid;
        _lastReadTimeMs = millis();
        _rfid->PICC_HaltA();
        _rfid->PCD_StopCrypto1();
        return;
    }

    // 5. Horário já capturado no milissegundo de detecção física RF (P0-05)
    String inicioEstagio = "";
    String fimEstagio = "";
    bool chegadaSemLargada = false;

    if (posicao == "inicio")
    {
        inicioEstagio = horarioAtual;
    }
    else
    {
        fimEstagio = horarioAtual;
        inicioEstagio = extrairHorarioCartao(doc, especial, "inicio");
        if (inicioEstagio.isEmpty() && _cronometragemService)
        {
            std::string inicioLocal;
            if (_cronometragemService->obterHorarioInicio(uid.c_str(), especial.c_str(), inicioLocal))
            {
                inicioEstagio = inicioLocal.c_str();
            }
        }
        if (inicioEstagio.isEmpty())
        {
            chegadaSemLargada = true;
            Serial.printf("Alerta: Cartão %s registrado na chegada de %s SEM horário de largada prévio!\n",
                          uid.c_str(), especial.c_str());
        }
    }

    // 6. Atualiza documento compacto e grava no cartão ANTES de confirmar ou persistir (BL-04 / DEF-04)
    atualizarHorarioCartaoCompacto(doc, uid, especial, posicao, horarioAtual);

    bool writeOk = false;
    int stageNum = (especial.length() >= 2 && especial[0] == 'E') ? atoi(especial.c_str() + 1) : 1;

    if (isBinaryCard && stageNum >= 1 && stageNum <= RFIDCard::MAX_RECORDS)
    {
        uint8_t recIdx = static_cast<uint8_t>(stageNum - 1);
        uint8_t blk = RFIDCard::recordIndexToBlock(recIdx);

        RFIDCard::Record rec{};
        rec.id = static_cast<uint16_t>(stageNum);
        rec.tsInicio = RFIDCard::timeStringToSeconds(inicioEstagio);
        rec.tsFim = RFIDCard::timeStringToSeconds(fimEstagio);
        rec.flags = 0;
        if (!inicioEstagio.isEmpty()) rec.flags |= RFIDCard::FLAG_START;
        if (!fimEstagio.isEmpty()) rec.flags |= RFIDCard::FLAG_FINISH;
        rec.msInicio = 0;
        rec.msFim = 0;
        rec.crc8 = RFIDCard::calcCRC8(reinterpret_cast<const uint8_t*>(&rec), 15);

        bool writeRecOk = RFIDService::writeCardBlock(*_rfid, blk, reinterpret_cast<const byte*>(&rec));

        bool writeHdrOk = false;
        if (writeRecOk)
        {
            bool headerAlreadyCoversStage = (existingHeader != nullptr &&
                                             existingHeader->mode == RFIDCard::MODE_ESTAGIO &&
                                             existingHeader->recordCount >= static_cast<uint8_t>(stageNum));
            if (headerAlreadyCoversStage)
            {
                // Cabeçalho já está inicializado e cobre este estágio (ex: chegada de estágio): grava apenas 1 bloco (~15 ms)
                writeHdrOk = true;
            }
            else
            {
                RFIDCard::Header hdr{};
                hdr.magic0 = RFIDCard::MAGIC_0;
                hdr.magic1 = RFIDCard::MAGIC_1;
                hdr.version = RFIDCard::FORMAT_VERSION;
                hdr.mode = RFIDCard::MODE_ESTAGIO;
                for (int b = 0; b < 4 && b < _rfid->uid.size; b++)
                {
                    hdr.uidBytes[b] = _rfid->uid.uidByte[b];
                }
                uint8_t prevCount = (existingHeader != nullptr && existingHeader->mode == RFIDCard::MODE_ESTAGIO)
                                        ? existingHeader->recordCount
                                        : 0;
                hdr.recordCount = (static_cast<uint8_t>(stageNum) > prevCount)
                                      ? static_cast<uint8_t>(stageNum)
                                      : prevCount;
                hdr.crc8 = RFIDCard::calcCRC8(reinterpret_cast<const uint8_t*>(&hdr), 15);
                writeHdrOk = RFIDService::writeCardBlock(*_rfid, RFIDCard::HEADER_BLOCK, reinterpret_cast<const byte*>(&hdr));
            }
        }
        writeOk = writeRecOk && writeHdrOk;
    }
    else
    {
        String novoJson;
        serializeJson(doc, novoJson);

        if (novoJson.length() + 1 > 720)
        {
            Serial.printf("Erro: JSON excede o limite físico do cartão MIFARE 1K (%d > 720 B). Abortado.\n", novoJson.length() + 1);
            if (_buzzer)
            {
                _buzzer->beepError();
            }
            if (_display)
            {
                _display->showError(2000);
            }
            _rfid->PICC_HaltA();
            _rfid->PCD_StopCrypto1();
            return;
        }

        writeOk = RFIDService::writeCardData(*_rfid, RFIDService::FIRST_DATA_BLOCK, novoJson);
    }
    _rfid->PICC_HaltA();
    _rfid->PCD_StopCrypto1();

    if (!writeOk)
    {
        Serial.printf("Falha ao gravar no cartão %s! Passagem NÃO registrada.\n", uid.c_str());
        if (_buzzer)
        {
            _buzzer->beepError();
        }
        if (_display)
        {
            _display->showError(2000);
        }
        return;
    }

    // 7. Gravação confirmada no cartão: ativa janela de debounce e persiste registro na estação
    _lastReadUid = uid;
    _lastReadTimeMs = millis();

    if (_cronometragemService)
    {
        cronometragem_t novoRegistro;
        novoRegistro.uid = uid.c_str();
        novoRegistro.numeral = cardNumber;
        novoRegistro.tipo = "especial";
        novoRegistro.especial = especial.c_str();
        novoRegistro.volta = 0;
        novoRegistro.inicio = inicioEstagio.c_str();
        novoRegistro.fim = fimEstagio.c_str();
        _cronometragemService->registrarTempo(novoRegistro);
    }

    // 8. Aciona feedback sonoro e visual SOMENTE após gravação confirmada (BL-04 / BL-05)
    if (posicao == "inicio")
    {
        if (_buzzer)
        {
            _buzzer->beepTone(1000, 150); // bipe curto de largada confirmada
        }
        if (_display)
        {
            _display->showGoBlinking(2000, 400); // pisca "GO" por 2 segundos
        }
    }
    else if (posicao == "fim")
    {
        if (chegadaSemLargada)
        {
            if (_buzzer)
            {
                _buzzer->beepWarning(); // alerta duplo para chegada sem largada
            }
            if (_display)
            {
                _display->showWarn(2000); // exibe "E-L" (Estágio sem Largada)
            }
        }
        else
        {
            if (_buzzer)
            {
                _buzzer->beepTone(1000, 300); // bipe de chegada confirmada
            }
            if (_display)
            {
                _display->showFin(2000); // mostra "FIN" por 2 segundos
            }
        }
    }
}

void CronometrarService::processarPassagemVoltaUnica(const String &uid, int cardNumber, JsonDocument &doc, const Cronometrar &cfg, const DateTime &now, const String &horarioAtual, bool isBinaryCard)
{
    int passagensCartao = obterQuantidadePassagensVoltaUnicaCartao(doc);
    String ultimoHorario = extrairUltimaPassagemVoltaUnicaCartao(doc);

    std::string ultimoHorarioLocal;
    int proximaVoltaLocal = 0;
    bool temPassagemLocal = _cronometragemService &&
                            _cronometragemService->obterUltimaPassagemVoltaUnica(uid.c_str(), ultimoHorarioLocal, proximaVoltaLocal);

    int voltaAtual = 0;
    if (passagensCartao == 0 && !temPassagemLocal)
    {
        voltaAtual = 0; // 1ª leitura: Largada / Referência inicial (L0)
    }
    else if (passagensCartao > 0)
    {
        voltaAtual = passagensCartao;
        if (temPassagemLocal && proximaVoltaLocal > voltaAtual && !ultimoHorarioLocal.empty())
        {
            voltaAtual = proximaVoltaLocal;
            ultimoHorario = ultimoHorarioLocal.c_str();
        }
    }
    else
    {
        voltaAtual = proximaVoltaLocal;
        ultimoHorario = ultimoHorarioLocal.c_str();
        if (!ultimoHorario.isEmpty())
        {
            adicionarPassagemVoltaUnicaCartao(doc, uid, ultimoHorario);
        }
    }

    int minIntervaloSeg = (cfg.intervaloMinVoltaSegundos >= Cronometrar::MIN_INTERVALO_VOLTA_SEG)
                              ? cfg.intervaloMinVoltaSegundos
                              : Cronometrar::DEFAULT_INTERVALO_VOLTA_SEG;

    // Se já houve referência inicial, valida intervalo mínimo entre passagens (debounce de volta)
    if (voltaAtual >= 1 && !ultimoHorario.isEmpty())
    {
        int32_t segDecorridos = -1;
        std::string fmt;
        if (Cronometragem::calcularDuracao(ultimoHorario.c_str(), horarioAtual.c_str(), segDecorridos, fmt))
        {
            if (segDecorridos < minIntervaloSeg)
            {
                Serial.printf("Leitura de volta (estação única) bloqueada para UID %s: decorridos %ds (mínimo %ds).\n",
                              uid.c_str(), static_cast<int>(segDecorridos), minIntervaloSeg);
                if (_buzzer)
                {
                    _buzzer->beepWarning();
                }
                _lastReadUid = uid;
                _lastReadTimeMs = millis();
                _rfid->PICC_HaltA();
                _rfid->PCD_StopCrypto1();
                return;
            }
        }
    }

    if (voltaAtual >= RFIDCard::MAX_RECORDS)
    {
        Serial.printf("Erro: Limite máximo de passagens do cartão MIFARE (%d passagens) atingido!\n", RFIDCard::MAX_RECORDS);
        if (_buzzer)
        {
            _buzzer->beepError();
        }
        if (_display)
        {
            _display->showError(2000);
        }
        _rfid->PICC_HaltA();
        _rfid->PCD_StopCrypto1();
        return;
    }

    adicionarPassagemVoltaUnicaCartao(doc, uid, horarioAtual);

    bool writeOk = false;
    if (isBinaryCard)
    {
        uint8_t recIdx = static_cast<uint8_t>(voltaAtual);
        uint8_t blk = RFIDCard::recordIndexToBlock(recIdx);

        RFIDCard::Record rec{};
        rec.id = static_cast<uint16_t>(voltaAtual);
        rec.flags = RFIDCard::FLAG_START;
        rec.tsInicio = RFIDCard::timeStringToSeconds(horarioAtual);
        rec.tsFim = 0;
        rec.msInicio = 0;
        rec.msFim = 0;
        rec.crc8 = RFIDCard::calcCRC8(reinterpret_cast<const uint8_t*>(&rec), 15);

        bool writeRecOk = RFIDService::writeCardBlock(*_rfid, blk, reinterpret_cast<const byte*>(&rec));

        bool writeHdrOk = false;
        if (writeRecOk)
        {
            RFIDCard::Header hdr{};
            hdr.magic0 = RFIDCard::MAGIC_0;
            hdr.magic1 = RFIDCard::MAGIC_1;
            hdr.version = RFIDCard::FORMAT_VERSION;
            hdr.mode = RFIDCard::MODE_VOLTA_UNICA;
            for (int b = 0; b < 4 && b < _rfid->uid.size; b++)
            {
                hdr.uidBytes[b] = _rfid->uid.uidByte[b];
            }
            hdr.recordCount = static_cast<uint8_t>(voltaAtual + 1);
            hdr.crc8 = RFIDCard::calcCRC8(reinterpret_cast<const uint8_t*>(&hdr), 15);
            writeHdrOk = RFIDService::writeCardBlock(*_rfid, RFIDCard::HEADER_BLOCK, reinterpret_cast<const byte*>(&hdr));
        }
        writeOk = writeRecOk && writeHdrOk;
    }
    else
    {
        String novoJson;
        serializeJson(doc, novoJson);

        if (novoJson.length() + 1 > 720)
        {
            Serial.printf("Erro: JSON excede o limite físico do cartão MIFARE 1K (%d > 720 B). Abortado.\n", novoJson.length() + 1);
            if (_buzzer)
            {
                _buzzer->beepError();
            }
            if (_display)
            {
                _display->showError(2000);
            }
            _rfid->PICC_HaltA();
            _rfid->PCD_StopCrypto1();
            return;
        }

        writeOk = RFIDService::writeCardData(*_rfid, RFIDService::FIRST_DATA_BLOCK, novoJson);
    }
    _rfid->PICC_HaltA();
    _rfid->PCD_StopCrypto1();

    if (!writeOk)
    {
        Serial.printf("Falha ao gravar volta no cartão %s! Passagem NÃO registrada.\n", uid.c_str());
        if (_buzzer)
        {
            _buzzer->beepError();
        }
        if (_display)
        {
            _display->showError(2000);
        }
        return;
    }

    _lastReadUid = uid;
    _lastReadTimeMs = millis();

    String codigoVolta = "L" + String(voltaAtual);
    if (_cronometragemService)
    {
        cronometragem_t novoRegistro;
        novoRegistro.uid = uid.c_str();
        novoRegistro.numeral = cardNumber;
        novoRegistro.tipo = "laps";
        novoRegistro.modoVoltas = "unica";
        novoRegistro.especial = codigoVolta.c_str();
        novoRegistro.volta = voltaAtual;
        if (voltaAtual == 0)
        {
            novoRegistro.inicio = horarioAtual.c_str();
            novoRegistro.fim = "";
        }
        else
        {
            novoRegistro.inicio = ultimoHorario.c_str();
            novoRegistro.fim = horarioAtual.c_str();
        }
        _cronometragemService->registrarTempo(novoRegistro);
    }

    if (voltaAtual == 0)
    {
        Serial.printf("Modo Voltas (Estação Única) — Largada (L0) registrada para piloto #%d (%s) às %s.\n",
                      cardNumber, uid.c_str(), horarioAtual.c_str());
        if (_buzzer)
        {
            _buzzer->beepTone(1000, 150);
        }
        if (_display)
        {
            _display->showGoBlinking(2000, 400);
        }
    }
    else
    {
        Serial.printf("Modo Voltas (Estação Única) — Volta %d (L%d) registrada para piloto #%d (%s): %s -> %s.\n",
                      voltaAtual, voltaAtual, cardNumber, uid.c_str(), ultimoHorario.c_str(), horarioAtual.c_str());
        if (_buzzer)
        {
            _buzzer->beepTone(1000, 300);
        }
        if (_display)
        {
            _display->showLap(static_cast<uint16_t>(voltaAtual), 2000);
        }
    }
}

void CronometrarService::processarPassagemVoltaDuasEstacoes(const String &uid, int cardNumber, JsonDocument &doc, const Cronometrar &cfg, const DateTime &now, const String &horarioAtual, bool isBinaryCard)
{
    String posicao = String(cfg.posicao.c_str());

    if (!doc["uid"].isNull())
    {
        doc.remove("uid");
    }
    doc["u"] = uid;

    JsonArray voltasArr = doc["v"].is<JsonArray>() ? doc["v"].as<JsonArray>() : doc["v"].to<JsonArray>();
    int qtdVoltasCartao = static_cast<int>(voltasArr.size());

    int ultimaVoltaLocal = 0;
    bool temLargadaAbertaLocal = false;
    std::string horarioLargadaAbertaLocal;
    bool temEstadoLocal = _cronometragemService &&
                          _cronometragemService->obterEstadoVoltaDuasEstacoes(uid.c_str(), ultimaVoltaLocal, temLargadaAbertaLocal, horarioLargadaAbertaLocal);

    int minIntervaloSeg = (cfg.intervaloMinVoltaSegundos >= Cronometrar::MIN_INTERVALO_VOLTA_SEG)
                              ? cfg.intervaloMinVoltaSegundos
                              : Cronometrar::DEFAULT_INTERVALO_VOLTA_SEG;

    if (posicao == "inicio")
    {
        // Verifica se a última volta no cartão já está aberta (possui "i" e não possui "f")
        bool ultimaVoltaCartaoAberta = false;
        if (qtdVoltasCartao > 0)
        {
            JsonObject ultimaObj = voltasArr[qtdVoltasCartao - 1].as<JsonObject>();
            String ini = ultimaObj["i"].is<const char*>() ? ultimaObj["i"].as<String>() : "";
            String fim = ultimaObj["f"].is<const char*>() ? ultimaObj["f"].as<String>() : "";
            ultimaVoltaCartaoAberta = !ini.isEmpty() && fim.isEmpty();
        }

        if (ultimaVoltaCartaoAberta || (temEstadoLocal && temLargadaAbertaLocal && ultimaVoltaLocal >= qtdVoltasCartao))
        {
            int voltaAberta = ultimaVoltaCartaoAberta ? qtdVoltasCartao : ultimaVoltaLocal;
            Serial.printf("Leitura repetida de largada bloqueada para UID %s na Volta %d (L%d). Aguardando chegada da volta.\n",
                          uid.c_str(), voltaAberta, voltaAberta);
            if (_buzzer)
            {
                _buzzer->beepWarning();
            }
            _lastReadUid = uid;
            _lastReadTimeMs = millis();
            _rfid->PICC_HaltA();
            _rfid->PCD_StopCrypto1();
            return;
        }

        int baseVoltas = (temEstadoLocal && ultimaVoltaLocal > qtdVoltasCartao) ? ultimaVoltaLocal : qtdVoltasCartao;
        int novaVolta = baseVoltas + 1;
        if (novaVolta > RFIDCard::MAX_RECORDS)
        {
            Serial.printf("Erro: Limite máximo de voltas do cartão MIFARE (%d voltas) atingido!\n", RFIDCard::MAX_RECORDS);
            if (_buzzer)
            {
                _buzzer->beepError();
            }
            if (_display)
            {
                _display->showError(2000);
            }
            _rfid->PICC_HaltA();
            _rfid->PCD_StopCrypto1();
            return;
        }

        while (static_cast<int>(voltasArr.size()) < novaVolta - 1)
        {
            voltasArr.add<JsonObject>();
        }
        JsonObject novaVoltaObj = voltasArr.add<JsonObject>();
        novaVoltaObj["i"] = horarioAtual;

        bool writeOk = false;
        if (isBinaryCard)
        {
            // Gravação binária atômica por bloco (16 bytes) — Etapa 6 (P0-04)
            uint8_t recIdx = static_cast<uint8_t>(novaVolta - 1);
            uint8_t blk = RFIDCard::recordIndexToBlock(recIdx);

            RFIDCard::Record rec{};
            rec.id = static_cast<uint16_t>(novaVolta);
            rec.flags = RFIDCard::FLAG_START;
            rec.tsInicio = RFIDCard::timeStringToSeconds(horarioAtual);
            rec.tsFim = 0;
            rec.msInicio = 0;
            rec.msFim = 0;
            rec.crc8 = RFIDCard::calcCRC8(reinterpret_cast<const uint8_t*>(&rec), 15);

            bool writeRecOk = RFIDService::writeCardBlock(*_rfid, blk, reinterpret_cast<const byte*>(&rec));

            bool writeHdrOk = false;
            if (writeRecOk)
            {
                RFIDCard::Header hdr{};
                hdr.magic0 = RFIDCard::MAGIC_0;
                hdr.magic1 = RFIDCard::MAGIC_1;
                hdr.version = RFIDCard::FORMAT_VERSION;
                hdr.mode = RFIDCard::MODE_VOLTA_DUAS;
                for (int b = 0; b < 4 && b < _rfid->uid.size; b++)
                {
                    hdr.uidBytes[b] = _rfid->uid.uidByte[b];
                }
                hdr.recordCount = static_cast<uint8_t>(novaVolta);
                hdr.crc8 = RFIDCard::calcCRC8(reinterpret_cast<const uint8_t*>(&hdr), 15);
                writeHdrOk = RFIDService::writeCardBlock(*_rfid, RFIDCard::HEADER_BLOCK, reinterpret_cast<const byte*>(&hdr));
            }
            writeOk = writeRecOk && writeHdrOk;
        }
        else
        {
            String novoJson;
            serializeJson(doc, novoJson);

            if (novoJson.length() + 1 > 720)
            {
                Serial.printf("Erro: JSON excede o limite físico do cartão MIFARE 1K (%d > 720 B). Abortado.\n", novoJson.length() + 1);
                if (_buzzer)
                {
                    _buzzer->beepError();
                }
                if (_display)
                {
                    _display->showError(2000);
                }
                _rfid->PICC_HaltA();
                _rfid->PCD_StopCrypto1();
                return;
            }

            writeOk = RFIDService::writeCardData(*_rfid, RFIDService::FIRST_DATA_BLOCK, novoJson);
        }

        _rfid->PICC_HaltA();
        _rfid->PCD_StopCrypto1();

        if (!writeOk)
        {
            Serial.printf("Falha ao gravar largada da volta %d no cartão %s! Passagem NÃO registrada.\n", novaVolta, uid.c_str());
            if (_buzzer)
            {
                _buzzer->beepError();
            }
            if (_display)
            {
                _display->showError(2000);
            }
            return;
        }

        _lastReadUid = uid;
        _lastReadTimeMs = millis();

        String codigoVolta = "L" + String(novaVolta);
        if (_cronometragemService)
        {
            cronometragem_t novoRegistro;
            novoRegistro.uid = uid.c_str();
            novoRegistro.numeral = cardNumber;
            novoRegistro.tipo = "laps";
            novoRegistro.modoVoltas = "duas";
            novoRegistro.especial = codigoVolta.c_str();
            novoRegistro.volta = novaVolta;
            novoRegistro.inicio = horarioAtual.c_str();
            novoRegistro.fim = "";
            _cronometragemService->registrarTempo(novoRegistro);
        }

        Serial.printf("Modo Voltas (2 Estações) — Largada da Volta %d (L%d) registrada para piloto #%d (%s) às %s.\n",
                      novaVolta, novaVolta, cardNumber, uid.c_str(), horarioAtual.c_str());
        if (_buzzer)
        {
            _buzzer->beepTone(1000, 150);
        }
        if (_display)
        {
            _display->showGoBlinking(2000, 400);
        }
        return;
    }

    // Estação de Chegada da Volta (posicao == "fim")
    int voltaAtual = 0;
    String inicioVolta = "";
    bool chegadaSemLargada = false;

    if (qtdVoltasCartao > 0)
    {
        JsonObject ultimaObj = voltasArr[qtdVoltasCartao - 1].as<JsonObject>();
        String iniCartao = ultimaObj["i"].is<const char*>() ? ultimaObj["i"].as<String>() : "";
        String fimCartao = ultimaObj["f"].is<const char*>() ? ultimaObj["f"].as<String>() : "";

        if (!iniCartao.isEmpty() && fimCartao.isEmpty())
        {
            voltaAtual = qtdVoltasCartao;
            inicioVolta = iniCartao;
        }
        else if (temEstadoLocal && temLargadaAbertaLocal && ultimaVoltaLocal > qtdVoltasCartao)
        {
            voltaAtual = ultimaVoltaLocal;
            inicioVolta = horarioLargadaAbertaLocal.c_str();
            JsonObject novaObj = voltasArr.add<JsonObject>();
            novaObj["i"] = inicioVolta;
        }
        else
        {
            // Última volta no cartão já possui horário de chegada ("f") e nenhuma nova largada foi aberta
            Serial.printf("Leitura repetida na chegada bloqueada para UID %s: Volta %d (L%d) já finalizada.\n",
                          uid.c_str(), qtdVoltasCartao, qtdVoltasCartao);
            if (_buzzer)
            {
                _buzzer->beepWarning();
            }
            _lastReadUid = uid;
            _lastReadTimeMs = millis();
            _rfid->PICC_HaltA();
            _rfid->PCD_StopCrypto1();
            return;
        }
    }
    else
    {
        if (temEstadoLocal && temLargadaAbertaLocal)
        {
            voltaAtual = ultimaVoltaLocal;
            inicioVolta = horarioLargadaAbertaLocal.c_str();
        }
        else
        {
            voltaAtual = (temEstadoLocal ? ultimaVoltaLocal : 0) + 1;
            chegadaSemLargada = true;
            Serial.printf("Alerta: Cartão %s registrado na chegada da Volta %d (L%d) SEM largada prévia!\n",
                          uid.c_str(), voltaAtual, voltaAtual);
        }
        JsonObject novaObj = voltasArr.add<JsonObject>();
        if (!inicioVolta.isEmpty())
        {
            novaObj["i"] = inicioVolta;
        }
    }

    String codigoVolta = "L" + String(voltaAtual);
    if (_cronometragemService && _cronometragemService->jaRegistrado(uid.c_str(), codigoVolta.c_str(), "fim"))
    {
        Serial.printf("Leitura repetida bloqueada na chegada para UID %s em %s.\n", uid.c_str(), codigoVolta.c_str());
        if (_buzzer)
        {
            _buzzer->beepWarning();
        }
        _lastReadUid = uid;
        _lastReadTimeMs = millis();
        _rfid->PICC_HaltA();
        _rfid->PCD_StopCrypto1();
        return;
    }

    if (!inicioVolta.isEmpty())
    {
        int32_t segDecorridos = -1;
        std::string fmt;
        if (Cronometragem::calcularDuracao(inicioVolta.c_str(), horarioAtual.c_str(), segDecorridos, fmt))
        {
            if (segDecorridos < minIntervaloSeg)
            {
                Serial.printf("Chegada de volta bloqueada para UID %s: decorridos %ds desde a largada (mínimo %ds).\n",
                              uid.c_str(), static_cast<int>(segDecorridos), minIntervaloSeg);
                if (_buzzer)
                {
                    _buzzer->beepWarning();
                }
                _lastReadUid = uid;
                _lastReadTimeMs = millis();
                _rfid->PICC_HaltA();
                _rfid->PCD_StopCrypto1();
                return;
            }
        }
    }

    // Atualiza horário de chegada ("f") na volta atual no cartão
    JsonObject voltaAlvoObj = voltasArr[voltasArr.size() - 1].as<JsonObject>();
    voltaAlvoObj["f"] = horarioAtual;

    bool writeOk = false;
    if (isBinaryCard && voltaAtual >= 1 && voltaAtual <= RFIDCard::MAX_RECORDS)
    {
        // Gravação binária atômica de EXATAMENTE 1 BLOCO de 16 bytes (~15 ms) — Etapa 6 (P0-04)
        uint8_t recIdx = static_cast<uint8_t>(voltaAtual - 1);
        uint8_t blk = RFIDCard::recordIndexToBlock(recIdx);

        RFIDCard::Record rec{};
        rec.id = static_cast<uint16_t>(voltaAtual);
        rec.flags = RFIDCard::FLAG_COMPLETE;
        rec.tsInicio = RFIDCard::timeStringToSeconds(inicioVolta);
        rec.tsFim = RFIDCard::timeStringToSeconds(horarioAtual);
        rec.msInicio = 0;
        rec.msFim = 0;
        rec.crc8 = RFIDCard::calcCRC8(reinterpret_cast<const uint8_t*>(&rec), 15);

        writeOk = RFIDService::writeCardBlock(*_rfid, blk, reinterpret_cast<const byte*>(&rec));
    }
    else
    {
        String novoJson;
        serializeJson(doc, novoJson);

        if (novoJson.length() + 1 > 720)
        {
            Serial.printf("Erro: JSON excede o limite físico do cartão MIFARE 1K (%d > 720 B). Abortado.\n", novoJson.length() + 1);
            if (_buzzer)
            {
                _buzzer->beepError();
            }
            if (_display)
            {
                _display->showError(2000);
            }
            _rfid->PICC_HaltA();
            _rfid->PCD_StopCrypto1();
            return;
        }

        writeOk = RFIDService::writeCardData(*_rfid, RFIDService::FIRST_DATA_BLOCK, novoJson);
    }

    _rfid->PICC_HaltA();
    _rfid->PCD_StopCrypto1();

    if (!writeOk)
    {
        Serial.printf("Falha ao gravar chegada da volta %d no cartão %s! Passagem NÃO registrada.\n", voltaAtual, uid.c_str());
        if (_buzzer)
        {
            _buzzer->beepError();
        }
        if (_display)
        {
            _display->showError(2000);
        }
        return;
    }

    _lastReadUid = uid;
    _lastReadTimeMs = millis();

    if (_cronometragemService)
    {
        cronometragem_t novoRegistro;
        novoRegistro.uid = uid.c_str();
        novoRegistro.numeral = cardNumber;
        novoRegistro.tipo = "laps";
        novoRegistro.modoVoltas = "duas";
        novoRegistro.especial = codigoVolta.c_str();
        novoRegistro.volta = voltaAtual;
        novoRegistro.inicio = inicioVolta.c_str();
        novoRegistro.fim = horarioAtual.c_str();
        _cronometragemService->registrarTempo(novoRegistro);
    }

    if (chegadaSemLargada)
    {
        if (_buzzer)
        {
            _buzzer->beepWarning();
        }
        if (_display)
        {
            _display->showWarn(2000);
        }
    }
    else
    {
        Serial.printf("Modo Voltas (2 Estações) — Chegada da Volta %d (L%d) registrada para piloto #%d (%s): %s -> %s.\n",
                      voltaAtual, voltaAtual, cardNumber, uid.c_str(), inicioVolta.c_str(), horarioAtual.c_str());
        if (_buzzer)
        {
            _buzzer->beepTone(1000, 300);
        }
        if (_display)
        {
            _display->showLap(static_cast<uint16_t>(voltaAtual), 2000);
        }
    }
}

void CronometrarService::loop()
{
    if (!_cronometrar || (_rfidService && _rfidService->isResetMode()) || !_rfid)
    {
        // Se o cronometrar não estiver ativo ou estiver em modo reset, não processa o loop do RFID.
        return;
    }

    if (_rfidService && !_rfidService->isHardwareOk())
    {
        static unsigned long lastWarn = 0;
        if (millis() - lastWarn > 5000)
        {
            lastWarn = millis();
            Serial.println("Aviso [Cronometrar]: Leitor RFID não detectado ou com falha de comunicação.");
        }
        return;
    }

    static unsigned long lastCronPoll = 0;
    unsigned long nowMs = millis();
    if (nowMs - lastCronPoll < 60)
    {
        return;
    }
    lastCronPoll = nowMs;

    // Reconfigura o leitor caso o MFRC522 tenha reiniciado espontaneamente
    if (_rfidService && !_rfidService->ensureReaderReady())
    {
        return;
    }

    if (!_rfid->PICC_IsNewCardPresent() || !_rfid->PICC_ReadCardSerial())
    {
        return;
    }

    // P0-05: Captura imediata do timestamp no exato milissegundo de detec�o f�sica do cart�o
    DateTime detectNow = Relogio::getNow();
    char bufferHorario[10];
    snprintf(bufferHorario, sizeof(bufferHorario), "%02d:%02d:%02d",
             detectNow.hour(), detectNow.minute(), detectNow.second());
    String horarioDetectado(bufferHorario);

    // P1-03: Snapshot thread-safe do estado para evitar data races com o httpd
    Cronometrar cfg;
    this->read([&](Cronometrar &state) {
        cfg = state;
    });

    String uid = RFIDService::uidToString(&_rfid->uid);

    // 1. Debounce temporal rápido por UID (evita leituras repetidas enquanto o cartão permanece no leitor)
    nowMs = millis();
    if (_lastReadUid == uid && (nowMs - _lastReadTimeMs) < DEBOUNCE_UID_MS)
    {
        _lastReadTimeMs = nowMs;
        _rfid->PICC_HaltA();
        _rfid->PCD_StopCrypto1();
        return;
    }

    Serial.println("Cartão detectado: " + uid);

    // 2. Valida vínculo: RFIDCard.numero == Atleta.numeral
    int cardNumber = 0;
    atleta_t atletaVinculado{};
    if (_rfidService && _atletaService)
    {
        cardNumber = _rfidService->getCardNumber(uid);
        if (cardNumber <= 0 || !_atletaService->getAtletaByNumeral(cardNumber, atletaVinculado))
        {
            Serial.printf("Cartão %s (nº %d) rejeitado: não preparado no Reset ou sem piloto cadastrado com numeral correspondente.\n", uid.c_str(), cardNumber);
            if (_buzzer)
            {
                _buzzer->beepError();
            }
            if (_display)
            {
                _display->showError(2000);
            }
            _lastReadUid = uid;
            _lastReadTimeMs = millis();
            _rfid->PICC_HaltA();
            _rfid->PCD_StopCrypto1();
            return;
        }
        Serial.printf("Piloto identificado: #%d - %s\n", atletaVinculado.numeral, atletaVinculado.nome.c_str());
    }

    // 3. Lê o primeiro bloco de dados (Bloco 4) para identificar o formato do cartão (Etapa 6 - P0-04)
    byte block4[16] = {0};
    bool readBlock4Ok = RFIDService::readCardBlock(*_rfid, RFIDService::FIRST_DATA_BLOCK, block4);
    if (!readBlock4Ok)
    {
        Serial.printf("Falha física na leitura do bloco inicial do cartão %s. Aproxime novamente.\n", uid.c_str());
        if (_buzzer)
        {
            _buzzer->beepError();
        }
        if (_display)
        {
            _display->showError(2000);
        }
        _rfid->PICC_HaltA();
        _rfid->PCD_StopCrypto1();
        return;
    }

    JsonDocument doc;
    bool isBinaryCard = false;
    bool hasValidBinaryHeader = false;

    RFIDCard::Header cardHeader;
    memcpy(&cardHeader, block4, 16);

    if (RFIDCard::isValidHeader(cardHeader))
    {
        // Cartão no formato binário compacto (Etapa 6 - P0-04)
        isBinaryCard = true;
        hasValidBinaryHeader = true;
        std::vector<RFIDCard::Record> records;
        records.reserve(cardHeader.recordCount);
        bool recordsOk = true;

        for (uint8_t i = 0; i < cardHeader.recordCount; i++)
        {
            uint8_t blk = RFIDCard::recordIndexToBlock(i);
            byte recBuf[16];
            if (!RFIDService::readCardBlock(*_rfid, blk, recBuf))
            {
                Serial.printf("Falha física ao ler registro %d (bloco %d) do cartão %s.\n", i, blk, uid.c_str());
                recordsOk = false;
                break;
            }
            // Ignora blocos ainda virgens (0x00), ex.: estágio anterior pulado (largou em E2 sem passar em E1)
            if (RFIDCard::isBlank(recBuf))
            {
                continue;
            }
            RFIDCard::Record rec;
            memcpy(&rec, recBuf, 16);
            if (!RFIDCard::isValidRecord(rec))
            {
                Serial.printf("Erro de CRC no registro %d (bloco %d) do cartão %s. Dados corrompidos!\n", i, blk, uid.c_str());
                recordsOk = false;
                break;
            }
            if (rec.flags == 0)
            {
                continue;
            }
            records.push_back(rec);
        }

        if (!recordsOk)
        {
            Serial.printf("Erro: Leitura de registros corrompida no cartão %s. Abortando para proteger histórico.\n", uid.c_str());
            if (_buzzer)
            {
                _buzzer->beepError();
            }
            if (_display)
            {
                _display->showError(2000);
            }
            _rfid->PICC_HaltA();
            _rfid->PCD_StopCrypto1();
            return;
        }

        RFIDCard::decodeBinaryCardToJson(uid, cardHeader, records, doc);
    }
    else if (RFIDCard::isBlank(block4))
    {
        // Cartão virgem / recém-resetado (todos os bytes 0x00 no bloco 4)
        isBinaryCard = true;
        doc.clear();
    }
    else if (block4[0] == '{')
    {
        // Cartão legado no formato JSON: lê blocos restantes
        String rawData = "";
        bool readOk = RFIDService::readCardData(*_rfid, RFIDService::FIRST_DATA_BLOCK, RFIDService::MAX_DATA_BLOCKS, rawData);
        if (!readOk)
        {
            Serial.printf("Falha física na leitura de dados do cartão %s. Aproxime novamente.\n", uid.c_str());
            if (_buzzer)
            {
                _buzzer->beepError();
            }
            if (_display)
            {
                _display->showError(2000);
            }
            _rfid->PICC_HaltA();
            _rfid->PCD_StopCrypto1();
            return;
        }

        if (!rawData.isEmpty())
        {
            DeserializationError err = deserializeJson(doc, rawData);
            if (err != DeserializationError::Ok)
            {
                // [P0-04] NUNCA executar doc.clear() sobre dados corrompidos/truncados!
                Serial.printf("Erro: JSON no cartão %s corrompido ou truncado (%s). Abortando para proteger histórico anterior!\n",
                              uid.c_str(), err.c_str());
                if (_buzzer)
                {
                    _buzzer->beepError();
                }
                if (_display)
                {
                    _display->showError(2000);
                }
                _rfid->PICC_HaltA();
                _rfid->PCD_StopCrypto1();
                return;
            }
        }
        isBinaryCard = false;
    }
    else
    {
        // Bloco 4 não é binário válido, não é virgem e não começa com '{': corrompido!
        Serial.printf("Erro: Cartão %s possui formato não reconhecido no bloco 4. Abortando para proteger dados.\n", uid.c_str());
        if (_buzzer)
        {
            _buzzer->beepError();
        }
        if (_display)
        {
            _display->showError(2000);
        }
        _rfid->PICC_HaltA();
        _rfid->PCD_StopCrypto1();
        return;
    }

    if (cfg.tipo == "laps")
    {
        if (cfg.modoVoltas == "duas")
        {
            processarPassagemVoltaDuasEstacoes(uid, cardNumber, doc, cfg, detectNow, horarioDetectado, isBinaryCard);
        }
        else
        {
            processarPassagemVoltaUnica(uid, cardNumber, doc, cfg, detectNow, horarioDetectado, isBinaryCard);
        }
    }
    else
    {
        processarPassagemEstagio(uid, cardNumber, doc, cfg, detectNow, horarioDetectado, isBinaryCard, hasValidBinaryHeader ? &cardHeader : nullptr);
    }
}

void CronometrarService::onConfigUpdated()
{
    bool prev = _cronometrar;
    Cronometrar currentState;
    this->read([&](Cronometrar &state) {
        currentState = state;
    });
    _cronometrar = currentState.cronometrar;
    if (_cronometrar != prev)
    {
        Serial.printf("Cronometragem %s (tipo=%s, especial=%s, posicao=%s)\n",
                      _cronometrar ? "INICIADA" : "ENCERRADA",
                      currentState.tipo.c_str(),
                      currentState.especial.c_str(),
                      currentState.posicao.c_str());
    }
    if (_cronometrar && _rfidService && _rfidService->isResetMode())
    {
        _rfidService->stopResetMode();
    }
}

void CronometrarService::registerConfig()
{
}
