#ifndef ApuracaoResultados_h
#define ApuracaoResultados_h

#include <ArduinoJson.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "AtletaService.h"
#include "CategoriaService.h"
#include "CronometragemService.h"
#include "CronometrarService.h"
#include "ProvaService.h"
#include "RFIDService.h"
#include "FixedString.h"

struct etapa_apurada_t {
    int32_t duracaoSegundos = -1;
    int16_t indice = 0;                  // 1..N
    int16_t posicaoEtapa = 0;
    FixedString<6> codigo;               // "E1".."E10" ou "L1".."LN"
    FixedString<9> inicio;
    FixedString<9> fim;
    FixedString<9> duracao;
    FixedString<12> status = "pendente"; // "OK" | "largada" | "sem_largada" | "invalido" | "pendente"
    FixedString<10> gapEtapa;
};

static_assert(sizeof(etapa_apurada_t) <= 64, "etapa_apurada_t muito grande");

struct piloto_apurado_t {
    int numeral = 0;
    std::string nome;
    std::string equipe;
    int categoriaId = 0;
    std::string categoriaNome;
    FixedString<12> uid;

    FixedString<13> situacao = "DNS"; // "CLASSIFICADO" | "EM_CURSO" | "DNF" | "DNS"
    int etapasConcluidas = 0;
    int etapasEsperadas = 0;
    int voltasConcluidas = 0;

    int32_t tempoTotalSegundos = -1;
    FixedString<9> tempoTotal;

    int32_t melhorVoltaSegundos = -1;
    FixedString<9> melhorVolta;
    int32_t ultimaEtapaSegundos = -1;

    int posicaoGeral = 0;
    FixedString<10> gapGeral;

    int posicaoCategoria = 0;
    FixedString<10> gapCategoria;

    FixedString<9> horarioLargadaInicial; // Para L0 no modo voltas de estação única
    std::vector<etapa_apurada_t> etapas;
};

struct relatorio_apuracao_t {
    std::string modoProva = "especial";          // "especial" | "laps"
    std::string modoVoltas = "unica";            // "unica" | "duas"
    std::string criterioVoltas = "voltas_tempo"; // "voltas_tempo" | "melhor_volta"
    int totalEstagiosConfigurados = 5;
    int totalEtapasDisputadas = 0;
    bool resultadoOficial = false;
    std::string statusResultado = "aguardando";  // "oficial" | "parcial" | "aguardando"
    diagnostico_cronometragem_t diagnostico;
    std::vector<std::string> codigosEtapas;
    std::vector<piloto_apurado_t> classificacao;
};

class ApuracaoResultados {
public:
    static constexpr const char *MODO_ESPECIAL = "especial";
    static constexpr const char *MODO_LAPS = "laps";
    static constexpr const char *CRITERIO_VOLTAS_TEMPO = "voltas_tempo";
    static constexpr const char *CRITERIO_MELHOR_VOLTA = "melhor_volta";

    static constexpr const char *SITUACAO_CLASSIFICADO = "CLASSIFICADO";
    static constexpr const char *SITUACAO_EM_CURSO = "EM_CURSO";
    static constexpr const char *SITUACAO_DNF = "DNF";
    static constexpr const char *SITUACAO_DNS = "DNS";

    static std::string formatGapSeconds(int32_t diffSeconds)
    {
        if (diffSeconds <= 0)
        {
            return "+00:00:00";
        }
        return "+" + Cronometragem::formatDuration(diffSeconds);
    }

    static int prioridadeSituacao(const std::string &situacao)
    {
        if (situacao == SITUACAO_CLASSIFICADO)
        {
            return 0;
        }
        if (situacao == SITUACAO_EM_CURSO)
        {
            return 1;
        }
        if (situacao == SITUACAO_DNF)
        {
            return 2;
        }
        return 3; // DNS
    }

    static int extrairIndiceCodigo(const char *codigo, char prefixoEsperado)
    {
        if (!codigo || std::strlen(codigo) < 2 || codigo[0] != prefixoEsperado)
        {
            return -1;
        }
        return std::atoi(codigo + 1);
    }

    static int extrairIndiceCodigo(const std::string &codigo, char prefixoEsperado)
    {
        return extrairIndiceCodigo(codigo.c_str(), prefixoEsperado);
    }

    template <typename Container = std::deque<cronometragem_t>>
    static __attribute__((noinline)) relatorio_apuracao_t calcular(
        const Prova &prova,
        const std::vector<categoria_t> &categorias,
        const std::vector<atleta_t> &atletas,
        const std::map<String, int> &cardRegistry,
        const Cronometrar &cronometrarState,
        const Container &registrosBrutos,
        const std::string &criterioVoltasOverride = "")
    {
        relatorio_apuracao_t relatorio;

        // 1. Determina o modo efetivo da prova e critério de voltas
        std::string modo = !prova.tipo.empty() ? prova.tipo : cronometrarState.tipo.c_str();
        if (modo != MODO_ESPECIAL && modo != MODO_LAPS)
        {
            modo = MODO_ESPECIAL;
        }
        // Se não há prova salva mas todos os registros são voltas ('L'), respeita os registros
        if (prova.nome.empty() && !registrosBrutos.empty())
        {
            bool temLap = false;
            bool temEspecial = false;
            for (const auto &r : registrosBrutos)
            {
                if (r.tipo == MODO_LAPS || (!r.especial.empty() && r.especial[0] == 'L'))
                {
                    temLap = true;
                }
                else if (!r.especial.empty() && r.especial[0] == 'E')
                {
                    temEspecial = true;
                }
            }
            if (temLap && !temEspecial)
            {
                modo = MODO_LAPS;
            }
        }

        relatorio.modoProva = modo;
        relatorio.modoVoltas = (prova.modoVoltas == "duas" || cronometrarState.modoVoltas == "duas") ? "duas" : "unica";

        std::string crit = !criterioVoltasOverride.empty()
                               ? criterioVoltasOverride
                               : (!prova.criterioVoltas.empty() ? prova.criterioVoltas : CRITERIO_VOLTAS_TEMPO);
        relatorio.criterioVoltas = (crit == CRITERIO_MELHOR_VOLTA) ? CRITERIO_MELHOR_VOLTA : CRITERIO_VOLTAS_TEMPO;

        int totalEstagiosCfg = (prova.totalEstagios >= Prova::MIN_ESTAGIOS && prova.totalEstagios <= Prova::MAX_ESTAGIOS)
                                   ? prova.totalEstagios
                                   : Prova::DEFAULT_ESTAGIOS;
        relatorio.totalEstagiosConfigurados = totalEstagiosCfg;

        // 2. Mapas auxiliares de Categorias e Cartões RFID
        std::map<int, std::string> mapaCategorias;
        for (const auto &cat : categorias)
        {
            mapaCategorias[cat.id] = cat.categoria;
        }

        std::map<int, std::string> numeralParaUid;
        std::map<std::string, int> uidParaNumeral;
        for (const auto &kv : cardRegistry)
        {
            std::string uidStr = kv.first.c_str();
            int numero = kv.second;
            if (!uidStr.empty() && numero > 0)
            {
                uidParaNumeral[uidStr] = numero;
                numeralParaUid[numero] = uidStr;
            }
        }

        // 3. P0-01: Opera diretamente sobre registrosBrutos por const& sem cópia redundante
        relatorio.diagnostico = Cronometragem::diagnosticarLista(registrosBrutos);

        // 4. Determina se o resultado é Oficial, Parcial ou Aguardando
        if (relatorio.diagnostico.totalRegistros == 0)
        {
            relatorio.resultadoOficial = false;
            relatorio.statusResultado = "aguardando";
        }
        else if (cronometrarState.cronometrar || relatorio.diagnostico.pendentesChegada > 0 || relatorio.diagnostico.concluidos == 0)
        {
            relatorio.resultadoOficial = false;
            relatorio.statusResultado = "parcial";
        }
        else
        {
            relatorio.resultadoOficial = true;
            relatorio.statusResultado = "oficial";
        }

        // 5. Constrói a lista base de pilotos (cadastrados + avulsos que tenham registro na cronometragem)
        std::vector<piloto_apurado_t> pilotos;
        std::map<int, size_t> idxPorNumeral;
        std::map<std::string, size_t> idxPorUid;

        for (const auto &atl : atletas)
        {
            piloto_apurado_t p;
            p.numeral = atl.numeral;
            p.nome = atl.nome;
            p.equipe = atl.equipe;
            p.categoriaId = atl.categoriaId;
            auto itCat = mapaCategorias.find(atl.categoriaId);
            p.categoriaNome = (itCat != mapaCategorias.end()) ? itCat->second : "Sem Categoria";
            auto itUid = numeralParaUid.find(atl.numeral);
            if (itUid != numeralParaUid.end())
            {
                p.uid = itUid->second;
            }

            size_t idx = pilotos.size();
            pilotos.push_back(p);
            if (p.numeral > 0)
            {
                idxPorNumeral[p.numeral] = idx;
            }
            if (!p.uid.empty())
            {
                idxPorUid[p.uid.c_str()] = idx;
            }
        }

        // Inclui eventuais cartões/numerais com passagens registradas mas sem cadastro em AtletaService
        for (const auto &reg : registrosBrutos)
        {
            bool jaExiste = false;
            int num = reg.numeral;
            std::string u = reg.uid.c_str();
            if (num <= 0 && !u.empty())
            {
                auto it = uidParaNumeral.find(u);
                if (it != uidParaNumeral.end())
                {
                    num = it->second;
                }
            }
            else if (u.empty() && num > 0)
            {
                auto it = numeralParaUid.find(num);
                if (it != numeralParaUid.end())
                {
                    u = it->second;
                }
            }

            if (num > 0 && idxPorNumeral.count(num) > 0)
            {
                jaExiste = true;
            }
            else if (!u.empty() && idxPorUid.count(u) > 0)
            {
                jaExiste = true;
            }

            if (!jaExiste)
            {
                piloto_apurado_t avulso;
                avulso.numeral = num;
                avulso.uid = u.c_str();
                if (avulso.numeral > 0)
                {
                    char bufNome[48];
                    std::snprintf(bufNome, sizeof(bufNome), "Piloto #%d", avulso.numeral);
                    avulso.nome = bufNome;
                }
                else
                {
                    avulso.nome = "Cartão " + u;
                }
                avulso.equipe = "Avulso";
                avulso.categoriaId = 0;
                avulso.categoriaNome = "Sem Categoria";

                size_t idx = pilotos.size();
                pilotos.push_back(avulso);
                if (avulso.numeral > 0)
                {
                    idxPorNumeral[avulso.numeral] = idx;
                }
                if (!avulso.uid.empty())
                {
                    idxPorUid[avulso.uid.c_str()] = idx;
                }
            }
        }

        if (modo == MODO_ESPECIAL)
        {
            apurarModoEstagios(relatorio, pilotos, idxPorNumeral, idxPorUid, registrosBrutos, cronometrarState.cronometrar);
        }
        else
        {
            apurarModoVoltas(relatorio, pilotos, idxPorNumeral, idxPorUid, registrosBrutos, cronometrarState.cronometrar);
        }

        relatorio.classificacao = std::move(pilotos);
        return relatorio;
    }

    template <typename Container = std::deque<cronometragem_t>>
    static __attribute__((noinline)) void serializarRelatorioPublico(
        JsonObject &root,
        const Prova &prova,
        const std::vector<categoria_t> &categorias,
        const Cronometrar &estacao,
        const std::string &relogioIso,
        uint32_t unixtime,
        const relatorio_apuracao_t &relatorio,
        const Container &registros,
        bool incluirCronometragem = false)
    {
        root["versao"] = 1;
        root["resultadoOficial"] = relatorio.resultadoOficial;
        root["statusResultado"] = relatorio.statusResultado;
        root["modoProva"] = relatorio.modoProva;
        root["modoVoltas"] = relatorio.modoVoltas;
        root["criterioVoltas"] = relatorio.criterioVoltas;
        root["totalEstagiosConfigurados"] = relatorio.totalEstagiosConfigurados;
        root["totalEtapasDisputadas"] = relatorio.totalEtapasDisputadas;

        JsonObject relObj = root["relogio"].to<JsonObject>();
        relObj["datetime"] = relogioIso;
        relObj["unixtime"] = unixtime;

        JsonObject provaObj = root["prova"].to<JsonObject>();
        provaObj["nome"] = prova.nome;
        provaObj["data"] = prova.data;
        provaObj["local"] = prova.local;
        provaObj["tipo"] = relatorio.modoProva;
        provaObj["modoVoltas"] = relatorio.modoVoltas;
        provaObj["criterioVoltas"] = relatorio.criterioVoltas;
        provaObj["totalEstagios"] = relatorio.totalEstagiosConfigurados;

        JsonObject estacaoObj = root["estacao"].to<JsonObject>();
        estacaoObj["cronometrar"] = estacao.cronometrar;
        estacaoObj["tipo"] = estacao.tipo;
        estacaoObj["modoVoltas"] = estacao.modoVoltas;
        estacaoObj["especial"] = estacao.especial;
        estacaoObj["posicao"] = estacao.posicao;

        JsonObject diagObj = root["diagnostico"].to<JsonObject>();
        diagObj["totalRegistros"] = relatorio.diagnostico.totalRegistros;
        diagObj["concluidos"] = relatorio.diagnostico.concluidos;
        diagObj["pendentesChegada"] = relatorio.diagnostico.pendentesChegada;
        diagObj["semLargada"] = relatorio.diagnostico.semLargada;
        diagObj["invalidos"] = relatorio.diagnostico.invalidos;

        JsonArray etapasArr = root["codigosEtapas"].to<JsonArray>();
        for (const auto &cod : relatorio.codigosEtapas)
        {
            etapasArr.add(cod);
        }

        JsonArray catArr = root["categorias"].to<JsonArray>();
        for (const auto &c : categorias)
        {
            JsonObject cObj = catArr.add<JsonObject>();
            cObj["id"] = c.id;
            cObj["categoria"] = c.categoria;
        }

        JsonArray classifArr = root["classificacao"].to<JsonArray>();
        for (const auto &p : relatorio.classificacao)
        {
            JsonObject pObj = classifArr.add<JsonObject>();
            pObj["numeral"] = p.numeral;
            pObj["nome"] = p.nome;
            pObj["equipe"] = p.equipe;
            pObj["categoriaId"] = p.categoriaId;
            pObj["categoriaNome"] = p.categoriaNome;
            pObj["uid"] = p.uid;
            pObj["situacao"] = p.situacao;
            pObj["etapasConcluidas"] = p.etapasConcluidas;
            pObj["etapasEsperadas"] = p.etapasEsperadas;
            pObj["voltasConcluidas"] = p.voltasConcluidas;
            pObj["tempoTotalSegundos"] = p.tempoTotalSegundos;
            pObj["tempoTotal"] = p.tempoTotal;
            pObj["melhorVoltaSegundos"] = p.melhorVoltaSegundos;
            pObj["melhorVolta"] = p.melhorVolta;
            pObj["posicaoGeral"] = p.posicaoGeral;
            pObj["gapGeral"] = p.gapGeral;
            pObj["posicaoCategoria"] = p.posicaoCategoria;
            pObj["gapCategoria"] = p.gapCategoria;
            pObj["horarioLargadaInicial"] = p.horarioLargadaInicial;

            JsonArray etArr = pObj["etapas"].to<JsonArray>();
            if (p.etapas.empty() && !relatorio.codigosEtapas.empty())
            {
                for (size_t i = 0; i < relatorio.codigosEtapas.size(); ++i)
                {
                    JsonObject etObj = etArr.add<JsonObject>();
                    etObj["codigo"] = relatorio.codigosEtapas[i];
                    etObj["indice"] = static_cast<int>(i + 1);
                    etObj["inicio"] = "";
                    etObj["fim"] = "";
                    etObj["duracao"] = "";
                    etObj["duracaoSegundos"] = -1;
                    etObj["status"] = "pendente";
                    etObj["posicaoEtapa"] = 0;
                    etObj["gapEtapa"] = "";
                }
            }
            else
            {
                for (const auto &et : p.etapas)
                {
                    JsonObject etObj = etArr.add<JsonObject>();
                    etObj["codigo"] = et.codigo;
                    etObj["indice"] = et.indice;
                    etObj["inicio"] = et.inicio;
                    etObj["fim"] = et.fim;
                    etObj["duracao"] = et.duracao;
                    etObj["duracaoSegundos"] = et.duracaoSegundos;
                    etObj["status"] = et.status;
                    etObj["posicaoEtapa"] = et.posicaoEtapa;
                    etObj["gapEtapa"] = et.gapEtapa;
                }
            }
        }

        JsonArray cronArr = root["cronometragem"].to<JsonArray>();
        if (incluirCronometragem)
        {
            for (const auto &reg : registros)
            {
                JsonObject cObj = cronArr.add<JsonObject>();
                Cronometragem::serializarItem(reg, cObj);
            }
        }
    }

    static void serializarEtapaToStream(
        const char *codigo,
        int indice,
        const char *inicio,
        const char *fim,
        const char *duracao,
        int32_t duracaoSegundos,
        const char *status,
        int posicaoEtapa,
        const char *gapEtapa,
        Print &out)
    {
        char buf[240];
        int len = std::snprintf(
            buf,
            sizeof(buf),
            "{\"codigo\":\"%s\",\"indice\":%d,\"inicio\":\"%s\",\"fim\":\"%s\",\"duracao\":\"%s\",\"duracaoSegundos\":%ld,\"status\":\"%s\",\"posicaoEtapa\":%d,\"gapEtapa\":\"%s\"}",
            codigo ? codigo : "",
            indice,
            inicio ? inicio : "",
            fim ? fim : "",
            duracao ? duracao : "",
            static_cast<long>(duracaoSegundos),
            status ? status : "pendente",
            posicaoEtapa,
            gapEtapa ? gapEtapa : "");
        if (len > 0 && static_cast<size_t>(len) < sizeof(buf))
        {
            out.write(reinterpret_cast<const uint8_t *>(buf), static_cast<size_t>(len));
        }
    }

    template <typename Container = std::deque<cronometragem_t>>
    static __attribute__((noinline)) void serializarRelatorioPublicoToStream(
        Print &out,
        const Prova &prova,
        const std::vector<categoria_t> &categorias,
        const Cronometrar &estacao,
        const std::string &relogioIso,
        uint32_t unixtime,
        const relatorio_apuracao_t &relatorio,
        const Container &registros,
        bool incluirCronometragem = false)
    {
        out.print("{\"versao\":1,\"resultadoOficial\":");
        out.print(relatorio.resultadoOficial ? "true" : "false");

        {
            JsonDocument doc;
            out.print(",\"statusResultado\":");
            doc.set(relatorio.statusResultado);
            serializeJson(doc, out);

            out.print(",\"modoProva\":");
            doc.set(relatorio.modoProva);
            serializeJson(doc, out);

            out.print(",\"modoVoltas\":");
            doc.set(relatorio.modoVoltas);
            serializeJson(doc, out);

            out.print(",\"criterioVoltas\":");
            doc.set(relatorio.criterioVoltas);
            serializeJson(doc, out);
        }

        char numBuf[96];
        std::snprintf(numBuf,
                      sizeof(numBuf),
                      ",\"totalEstagiosConfigurados\":%d,\"totalEtapasDisputadas\":%d",
                      relatorio.totalEstagiosConfigurados,
                      relatorio.totalEtapasDisputadas);
        out.print(numBuf);

        {
            JsonDocument doc;
            JsonObject relObj = doc.to<JsonObject>();
            relObj["datetime"] = relogioIso;
            relObj["unixtime"] = unixtime;
            out.print(",\"relogio\":");
            serializeJson(doc, out);
        }

        {
            JsonDocument doc;
            JsonObject provaObj = doc.to<JsonObject>();
            provaObj["nome"] = prova.nome;
            provaObj["data"] = prova.data;
            provaObj["local"] = prova.local;
            provaObj["tipo"] = relatorio.modoProva;
            provaObj["modoVoltas"] = relatorio.modoVoltas;
            provaObj["criterioVoltas"] = relatorio.criterioVoltas;
            provaObj["totalEstagios"] = relatorio.totalEstagiosConfigurados;
            out.print(",\"prova\":");
            serializeJson(doc, out);
        }

        {
            JsonDocument doc;
            JsonObject estacaoObj = doc.to<JsonObject>();
            estacaoObj["cronometrar"] = estacao.cronometrar;
            estacaoObj["tipo"] = estacao.tipo;
            estacaoObj["modoVoltas"] = estacao.modoVoltas;
            estacaoObj["especial"] = estacao.especial;
            estacaoObj["posicao"] = estacao.posicao;
            out.print(",\"estacao\":");
            serializeJson(doc, out);
        }

        {
            JsonDocument doc;
            JsonObject diagObj = doc.to<JsonObject>();
            diagObj["totalRegistros"] = relatorio.diagnostico.totalRegistros;
            diagObj["concluidos"] = relatorio.diagnostico.concluidos;
            diagObj["pendentesChegada"] = relatorio.diagnostico.pendentesChegada;
            diagObj["semLargada"] = relatorio.diagnostico.semLargada;
            diagObj["invalidos"] = relatorio.diagnostico.invalidos;
            out.print(",\"diagnostico\":");
            serializeJson(doc, out);
        }

        {
            JsonDocument doc;
            JsonArray etapasArr = doc.to<JsonArray>();
            for (const auto &cod : relatorio.codigosEtapas)
            {
                etapasArr.add(cod);
            }
            out.print(",\"codigosEtapas\":");
            serializeJson(doc, out);
        }

        {
            JsonDocument doc;
            JsonArray catArr = doc.to<JsonArray>();
            for (const auto &c : categorias)
            {
                JsonObject cObj = catArr.add<JsonObject>();
                cObj["id"] = c.id;
                cObj["categoria"] = c.categoria;
            }
            out.print(",\"categorias\":");
            serializeJson(doc, out);
        }

        out.print(",\"classificacao\":[");
        bool firstPilot = true;
        for (const auto &p : relatorio.classificacao)
        {
            if (!firstPilot)
            {
                out.write(',');
            }
            firstPilot = false;

            JsonDocument pDoc;
            JsonObject pObj = pDoc.to<JsonObject>();
            pObj["numeral"] = p.numeral;
            pObj["nome"] = p.nome;
            pObj["equipe"] = p.equipe;
            pObj["categoriaId"] = p.categoriaId;
            pObj["categoriaNome"] = p.categoriaNome;
            pObj["uid"] = p.uid.c_str();
            pObj["situacao"] = p.situacao.c_str();
            pObj["etapasConcluidas"] = p.etapasConcluidas;
            pObj["etapasEsperadas"] = p.etapasEsperadas;
            pObj["voltasConcluidas"] = p.voltasConcluidas;
            pObj["tempoTotalSegundos"] = p.tempoTotalSegundos;
            pObj["tempoTotal"] = p.tempoTotal.c_str();
            pObj["melhorVoltaSegundos"] = p.melhorVoltaSegundos;
            pObj["melhorVolta"] = p.melhorVolta.c_str();
            pObj["posicaoGeral"] = p.posicaoGeral;
            pObj["gapGeral"] = p.gapGeral.c_str();
            pObj["posicaoCategoria"] = p.posicaoCategoria;
            pObj["gapCategoria"] = p.gapCategoria.c_str();
            pObj["horarioLargadaInicial"] = p.horarioLargadaInicial.c_str();

            char pBuf[768];
            size_t pLen = serializeJson(pDoc, pBuf, sizeof(pBuf));
            if (pLen >= 2 && pBuf[pLen - 1] == '}')
            {
                out.write(reinterpret_cast<const uint8_t *>(pBuf), pLen - 1);
                out.print(",\"etapas\":[");
                if (p.etapas.empty() && !relatorio.codigosEtapas.empty())
                {
                    for (size_t i = 0; i < relatorio.codigosEtapas.size(); ++i)
                    {
                        if (i > 0)
                        {
                            out.write(',');
                        }
                        serializarEtapaToStream(
                            relatorio.codigosEtapas[i].c_str(),
                            static_cast<int>(i + 1),
                            "",
                            "",
                            "",
                            -1,
                            "pendente",
                            0,
                            "",
                            out);
                    }
                }
                else
                {
                    for (size_t i = 0; i < p.etapas.size(); ++i)
                    {
                        if (i > 0)
                        {
                            out.write(',');
                        }
                        const auto &et = p.etapas[i];
                        serializarEtapaToStream(
                            et.codigo.c_str(),
                            et.indice,
                            et.inicio.c_str(),
                            et.fim.c_str(),
                            et.duracao.c_str(),
                            et.duracaoSegundos,
                            et.status.c_str(),
                            et.posicaoEtapa,
                            et.gapEtapa.c_str(),
                            out);
                    }
                }
                out.print("]}");
            }
            else
            {
                serializeJson(pDoc, out);
            }
        }
        out.write(']');

        out.print(",\"cronometragem\":");
        if (incluirCronometragem)
        {
            Cronometragem::serializarListaToStream(registros, out);
        }
        else
        {
            out.print("[]");
        }
        out.write('}');
    }

private:
    static bool localizarPilotoIndex(
        const cronometragem_t &reg,
        const std::map<int, size_t> &idxPorNumeral,
        const std::map<std::string, size_t> &idxPorUid,
        size_t &outIdx)
    {
        if (reg.numeral > 0)
        {
            auto it = idxPorNumeral.find(reg.numeral);
            if (it != idxPorNumeral.end())
            {
                outIdx = it->second;
                return true;
            }
        }
        if (!reg.uid.empty())
        {
            auto it = idxPorUid.find(reg.uid.c_str());
            if (it != idxPorUid.end())
            {
                outIdx = it->second;
                return true;
            }
        }
        return false;
    }

    static void calcularPosicoesEGapsPorEtapa(std::vector<piloto_apurado_t> &pilotos, size_t qtdEtapas)
    {
        for (size_t eIdx = 0; eIdx < qtdEtapas; ++eIdx)
        {
            std::vector<size_t> concluintes;
            for (size_t pIdx = 0; pIdx < pilotos.size(); ++pIdx)
            {
                if (eIdx < pilotos[pIdx].etapas.size() &&
                    pilotos[pIdx].etapas[eIdx].status == "OK" &&
                    pilotos[pIdx].etapas[eIdx].duracaoSegundos >= 0)
                {
                    concluintes.push_back(pIdx);
                }
            }

            std::sort(concluintes.begin(), concluintes.end(), [&](size_t a, size_t b) {
                int32_t ta = pilotos[a].etapas[eIdx].duracaoSegundos;
                int32_t tb = pilotos[b].etapas[eIdx].duracaoSegundos;
                if (ta != tb)
                {
                    return ta < tb;
                }
                return pilotos[a].numeral < pilotos[b].numeral;
            });

            if (!concluintes.empty())
            {
                int32_t melhorTempoEtapa = pilotos[concluintes[0]].etapas[eIdx].duracaoSegundos;
                for (size_t rank = 0; rank < concluintes.size(); ++rank)
                {
                    size_t pIdx = concluintes[rank];
                    pilotos[pIdx].etapas[eIdx].posicaoEtapa = static_cast<int>(rank + 1);
                    int32_t diff = pilotos[pIdx].etapas[eIdx].duracaoSegundos - melhorTempoEtapa;
                    pilotos[pIdx].etapas[eIdx].gapEtapa = formatGapSeconds(diff);
                }
            }
        }
    }

    template <typename Container = std::deque<cronometragem_t>>
    static __attribute__((noinline)) void apurarModoEstagios(
        relatorio_apuracao_t &relatorio,
        std::vector<piloto_apurado_t> &pilotos,
        const std::map<int, size_t> &idxPorNumeral,
        const std::map<std::string, size_t> &idxPorUid,
        const Container &registros,
        bool cronometrandoAtivo)
    {
        // Descobre o maior estágio E1..EN que possui algum registro real
        int maxEstagioComRegistro = 0;
        for (const auto &reg : registros)
        {
            int idx = extrairIndiceCodigo(reg.especial.c_str(), 'E');
            if (idx > maxEstagioComRegistro && idx <= Prova::MAX_ESTAGIOS)
            {
                maxEstagioComRegistro = idx;
            }
        }

        int etapasDisputadas = (maxEstagioComRegistro > 0) ? maxEstagioComRegistro : relatorio.totalEstagiosConfigurados;
        int etapasExibidas = std::max(relatorio.totalEstagiosConfigurados, maxEstagioComRegistro);
        relatorio.totalEtapasDisputadas = (maxEstagioComRegistro > 0) ? maxEstagioComRegistro : 0;

        for (int i = 1; i <= etapasExibidas; ++i)
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "E%d", i);
            relatorio.codigosEtapas.push_back(buf);
        }

        // Inicializa os slots de estágios para cada piloto
        for (auto &p : pilotos)
        {
            p.etapasEsperadas = etapasDisputadas;
            p.etapas.resize(etapasExibidas);
            for (int i = 0; i < etapasExibidas; ++i)
            {
                p.etapas[i].codigo = relatorio.codigosEtapas[i].c_str();
                p.etapas[i].indice = i + 1;
                p.etapas[i].status = "pendente";
            }
        }

        // Preenche os registros nos estágios de cada piloto
        for (const auto &reg : registros)
        {
            int stageNum = extrairIndiceCodigo(reg.especial.c_str(), 'E');
            if (stageNum < 1 || stageNum > etapasExibidas)
            {
                continue;
            }
            size_t pIdx = 0;
            if (!localizarPilotoIndex(reg, idxPorNumeral, idxPorUid, pIdx))
            {
                continue;
            }

            etapa_apurada_t &et = pilotos[pIdx].etapas[stageNum - 1];
            et.inicio = reg.inicio.c_str();
            et.fim = reg.fim.c_str();
            et.duracao = reg.duracao();
            et.duracaoSegundos = reg.duracaoSegundos;
            et.status = reg.status.c_str();
        }

        // Calcula posição e gap dentro de cada estágio
        calcularPosicoesEGapsPorEtapa(pilotos, static_cast<size_t>(etapasExibidas));

        // Consolida totais e situação de cada piloto
        for (auto &p : pilotos)
        {
            int concluidasDisputadas = 0;
            int concluidasTotais = 0;
            bool temAlgumRegistro = false;
            bool temFalhaOuIncompleto = false;
            bool temLargadaAberta = false;
            int32_t somaSegundos = 0;
            int32_t ultimoEstagioSeg = -1;

            for (int i = 0; i < etapasExibidas; ++i)
            {
                const auto &et = p.etapas[i];
                if (et.status != "pendente")
                {
                    temAlgumRegistro = true;
                }

                if (et.status == "OK" && et.duracaoSegundos >= 0)
                {
                    concluidasTotais++;
                    if (i < etapasDisputadas)
                    {
                        concluidasDisputadas++;
                    }
                    somaSegundos += et.duracaoSegundos;
                    ultimoEstagioSeg = et.duracaoSegundos;
                }
                else if (et.status == "sem_largada" || et.status == "invalido")
                {
                    temFalhaOuIncompleto = true;
                }
                else if (et.status == "largada")
                {
                    temLargadaAberta = true;
                }
            }

            p.etapasConcluidas = concluidasTotais;
            p.ultimaEtapaSegundos = ultimoEstagioSeg;

            if (!temAlgumRegistro)
            {
                p.situacao = SITUACAO_DNS;
                p.tempoTotalSegundos = -1;
                p.tempoTotal = "";
                p.etapas.clear();
                p.etapas.shrink_to_fit();
            }
            else if (maxEstagioComRegistro > 0 &&
                     concluidasDisputadas == etapasDisputadas &&
                     !temFalhaOuIncompleto &&
                     !temLargadaAberta)
            {
                p.situacao = SITUACAO_CLASSIFICADO;
                p.tempoTotalSegundos = somaSegundos;
                p.tempoTotal = Cronometragem::formatDuration(somaSegundos);
            }
            else
            {
                if (cronometrandoAtivo && temLargadaAberta && !temFalhaOuIncompleto)
                {
                    p.situacao = SITUACAO_EM_CURSO;
                }
                else
                {
                    p.situacao = SITUACAO_DNF;
                }
                p.tempoTotalSegundos = (concluidasTotais > 0) ? somaSegundos : -1;
                p.tempoTotal = (concluidasTotais > 0) ? Cronometragem::formatDuration(somaSegundos) : "";
            }
        }

        // Ordena classificação geral no Modo Estágios (DEC-09 / TA-15)
        std::sort(pilotos.begin(), pilotos.end(), [&](const piloto_apurado_t &a, const piloto_apurado_t &b) {
            int prioA = prioridadeSituacao(a.situacao);
            int prioB = prioridadeSituacao(b.situacao);
            if (prioA != prioB)
            {
                return prioA < prioB;
            }

            if (a.situacao == SITUACAO_CLASSIFICADO)
            {
                if (a.tempoTotalSegundos != b.tempoTotalSegundos)
                {
                    return a.tempoTotalSegundos < b.tempoTotalSegundos;
                }
                // Desempate: melhor tempo no último estágio disputado, recuando até E1
                for (int i = static_cast<int>( std::min(a.etapas.size(), b.etapas.size()) ) - 1; i >= 0; --i)
                {
                    int32_t ta = a.etapas[i].duracaoSegundos;
                    int32_t tb = b.etapas[i].duracaoSegundos;
                    if (ta >= 0 && tb >= 0 && ta != tb)
                    {
                        return ta < tb;
                    }
                }
                return a.numeral < b.numeral;
            }

            if (a.situacao == SITUACAO_EM_CURSO || a.situacao == SITUACAO_DNF)
            {
                if (a.etapasConcluidas != b.etapasConcluidas)
                {
                    return a.etapasConcluidas > b.etapasConcluidas;
                }
                if (a.tempoTotalSegundos >= 0 && b.tempoTotalSegundos >= 0 && a.tempoTotalSegundos != b.tempoTotalSegundos)
                {
                    return a.tempoTotalSegundos < b.tempoTotalSegundos;
                }
                return a.numeral < b.numeral;
            }

            return a.numeral < b.numeral;
        });

        // Atribui posições e gaps (Geral e por Categoria)
        atribuirPosicoesEGapsEstagios(pilotos);
    }

    static void atribuirPosicoesEGapsEstagios(std::vector<piloto_apurado_t> &pilotos)
    {
        int posGeral = 1;
        int32_t tempoLiderGeral = -1;
        std::map<int, int> posPorCat;
        std::map<int, int32_t> liderPorCat;

        for (auto &p : pilotos)
        {
            if (p.situacao == SITUACAO_CLASSIFICADO)
            {
                p.posicaoGeral = posGeral++;
                if (tempoLiderGeral < 0)
                {
                    tempoLiderGeral = p.tempoTotalSegundos;
                }
                p.gapGeral = formatGapSeconds(p.tempoTotalSegundos - tempoLiderGeral);

                int catRank = ++posPorCat[p.categoriaId];
                p.posicaoCategoria = catRank;
                if (liderPorCat.find(p.categoriaId) == liderPorCat.end())
                {
                    liderPorCat[p.categoriaId] = p.tempoTotalSegundos;
                }
                p.gapCategoria = formatGapSeconds(p.tempoTotalSegundos - liderPorCat[p.categoriaId]);
            }
            else
            {
                p.posicaoGeral = 0;
                p.posicaoCategoria = 0;
                p.gapGeral = (p.situacao == SITUACAO_DNS) ? "DNS" : (p.situacao == SITUACAO_EM_CURSO ? "EM CURSO" : "DNF");
                p.gapCategoria = p.gapGeral;
            }
        }
    }

    template <typename Container = std::deque<cronometragem_t>>
    static __attribute__((noinline)) void apurarModoVoltas(
        relatorio_apuracao_t &relatorio,
        std::vector<piloto_apurado_t> &pilotos,
        const std::map<int, size_t> &idxPorNumeral,
        const std::map<std::string, size_t> &idxPorUid,
        const Container &registros,
        bool cronometrandoAtivo)
    {
        int maxVoltaRegistrada = 0;
        for (const auto &reg : registros)
        {
            int v = reg.volta;
            if (v <= 0 && !reg.especial.empty() && reg.especial[0] == 'L')
            {
                v = extrairIndiceCodigo(reg.especial.c_str(), 'L');
            }
            if (v > maxVoltaRegistrada)
            {
                maxVoltaRegistrada = v;
            }
        }

        relatorio.totalEtapasDisputadas = maxVoltaRegistrada;
        for (int v = 1; v <= maxVoltaRegistrada; ++v)
        {
            char buf[8];
            std::snprintf(buf, sizeof(buf), "L%d", v);
            relatorio.codigosEtapas.push_back(buf);
        }

        for (auto &p : pilotos)
        {
            p.etapasEsperadas = maxVoltaRegistrada;
            p.etapas.resize(maxVoltaRegistrada);
            for (int v = 0; v < maxVoltaRegistrada; ++v)
            {
                p.etapas[v].codigo = relatorio.codigosEtapas[v].c_str();
                p.etapas[v].indice = v + 1;
                p.etapas[v].status = "pendente";
            }
        }

        std::vector<bool> temRegistroPorPiloto(pilotos.size(), false);
        for (const auto &reg : registros)
        {
            size_t pIdx = 0;
            if (!localizarPilotoIndex(reg, idxPorNumeral, idxPorUid, pIdx))
            {
                continue;
            }
            temRegistroPorPiloto[pIdx] = true;

            int v = reg.volta;
            if (v <= 0 && !reg.especial.empty() && reg.especial[0] == 'L')
            {
                v = extrairIndiceCodigo(reg.especial.c_str(), 'L');
            }

            if (v == 0 || reg.especial == "L0")
            {
                pilotos[pIdx].horarioLargadaInicial = reg.inicio.c_str();
                continue;
            }

            if (v >= 1 && v <= maxVoltaRegistrada)
            {
                etapa_apurada_t &et = pilotos[pIdx].etapas[v - 1];
                et.codigo = reg.especial.c_str();
                et.inicio = reg.inicio.c_str();
                et.fim = reg.fim.c_str();
                et.duracao = reg.duracao();
                et.duracaoSegundos = reg.duracaoSegundos;
                et.status = reg.status.c_str();
            }
        }

        calcularPosicoesEGapsPorEtapa(pilotos, static_cast<size_t>(maxVoltaRegistrada));

        for (size_t idx = 0; idx < pilotos.size(); ++idx)
        {
            auto &p = pilotos[idx];
            int voltasOk = 0;
            int32_t somaSegundos = 0;
            int32_t melhorVoltaSeg = -1;
            int32_t ultimaVoltaSeg = -1;
            bool temLargadaAberta = !p.horarioLargadaInicial.empty();

            for (const auto &et : p.etapas)
            {
                if (et.status == "OK" && et.duracaoSegundos >= 0)
                {
                    voltasOk++;
                    somaSegundos += et.duracaoSegundos;
                    ultimaVoltaSeg = et.duracaoSegundos;
                    if (melhorVoltaSeg < 0 || et.duracaoSegundos < melhorVoltaSeg)
                    {
                        melhorVoltaSeg = et.duracaoSegundos;
                    }
                }
                else if (et.status == "largada")
                {
                    temLargadaAberta = true;
                }
            }

            p.voltasConcluidas = voltasOk;
            p.etapasConcluidas = voltasOk;
            p.ultimaEtapaSegundos = ultimaVoltaSeg;

            if (!temRegistroPorPiloto[idx])
            {
                p.situacao = SITUACAO_DNS;
                p.tempoTotalSegundos = -1;
                p.tempoTotal = "";
                p.melhorVoltaSegundos = -1;
                p.melhorVolta = "";
                p.etapas.clear();
                p.etapas.shrink_to_fit();
            }
            else if (voltasOk > 0)
            {
                p.situacao = SITUACAO_CLASSIFICADO;
                p.tempoTotalSegundos = somaSegundos;
                p.tempoTotal = Cronometragem::formatDuration(somaSegundos);
                p.melhorVoltaSegundos = melhorVoltaSeg;
                p.melhorVolta = Cronometragem::formatDuration(melhorVoltaSeg);
            }
            else
            {
                p.situacao = (cronometrandoAtivo && temLargadaAberta) ? SITUACAO_EM_CURSO : SITUACAO_DNF;
                p.tempoTotalSegundos = -1;
                p.tempoTotal = "";
                p.melhorVoltaSegundos = -1;
                p.melhorVolta = "";
            }
        }

        const bool usarMelhorVolta = (relatorio.criterioVoltas == CRITERIO_MELHOR_VOLTA);

        std::sort(pilotos.begin(), pilotos.end(), [&](const piloto_apurado_t &a, const piloto_apurado_t &b) {
            int prioA = prioridadeSituacao(a.situacao);
            int prioB = prioridadeSituacao(b.situacao);
            if (prioA != prioB)
            {
                return prioA < prioB;
            }

            if (a.situacao == SITUACAO_CLASSIFICADO)
            {
                if (usarMelhorVolta)
                {
                    if (a.melhorVoltaSegundos != b.melhorVoltaSegundos)
                    {
                        return a.melhorVoltaSegundos < b.melhorVoltaSegundos;
                    }
                    if (a.voltasConcluidas != b.voltasConcluidas)
                    {
                        return a.voltasConcluidas > b.voltasConcluidas;
                    }
                    if (a.tempoTotalSegundos != b.tempoTotalSegundos)
                    {
                        return a.tempoTotalSegundos < b.tempoTotalSegundos;
                    }
                }
                else
                {
                    if (a.voltasConcluidas != b.voltasConcluidas)
                    {
                        return a.voltasConcluidas > b.voltasConcluidas;
                    }
                    if (a.tempoTotalSegundos != b.tempoTotalSegundos)
                    {
                        return a.tempoTotalSegundos < b.tempoTotalSegundos;
                    }
                    if (a.ultimaEtapaSegundos >= 0 && b.ultimaEtapaSegundos >= 0 && a.ultimaEtapaSegundos != b.ultimaEtapaSegundos)
                    {
                        return a.ultimaEtapaSegundos < b.ultimaEtapaSegundos;
                    }
                    if (a.melhorVoltaSegundos >= 0 && b.melhorVoltaSegundos >= 0 && a.melhorVoltaSegundos != b.melhorVoltaSegundos)
                    {
                        return a.melhorVoltaSegundos < b.melhorVoltaSegundos;
                    }
                }
                return a.numeral < b.numeral;
            }

            return a.numeral < b.numeral;
        });

        atribuirPosicoesEGapsVoltas(pilotos, usarMelhorVolta);
    }

    static std::string calcularTextoGapVolta(
        const piloto_apurado_t &piloto,
        int voltasLider,
        int32_t tempoTotalLider,
        int32_t melhorVoltaLider,
        bool usarMelhorVolta)
    {
        if (usarMelhorVolta)
        {
            return formatGapSeconds(piloto.melhorVoltaSegundos - melhorVoltaLider);
        }

        int diffVoltas = voltasLider - piloto.voltasConcluidas;
        if (diffVoltas <= 0)
        {
            return formatGapSeconds(piloto.tempoTotalSegundos - tempoTotalLider);
        }
        char buf[32];
        std::snprintf(buf, sizeof(buf), "-%d %s", diffVoltas, (diffVoltas == 1) ? "volta" : "voltas");
        return std::string(buf);
    }

    static void atribuirPosicoesEGapsVoltas(std::vector<piloto_apurado_t> &pilotos, bool usarMelhorVolta)
    {
        int posGeral = 1;
        int voltasLiderGeral = -1;
        int32_t tempoLiderGeral = -1;
        int32_t melhorVoltaLiderGeral = -1;

        std::map<int, int> posPorCat;
        std::map<int, int> voltasLiderCat;
        std::map<int, int32_t> tempoLiderCat;
        std::map<int, int32_t> melhorVoltaLiderCat;

        for (auto &p : pilotos)
        {
            if (p.situacao == SITUACAO_CLASSIFICADO)
            {
                p.posicaoGeral = posGeral++;
                if (voltasLiderGeral < 0)
                {
                    voltasLiderGeral = p.voltasConcluidas;
                    tempoLiderGeral = p.tempoTotalSegundos;
                    melhorVoltaLiderGeral = p.melhorVoltaSegundos;
                }
                p.gapGeral = calcularTextoGapVolta(p, voltasLiderGeral, tempoLiderGeral, melhorVoltaLiderGeral, usarMelhorVolta);

                int catRank = ++posPorCat[p.categoriaId];
                p.posicaoCategoria = catRank;
                if (voltasLiderCat.find(p.categoriaId) == voltasLiderCat.end())
                {
                    voltasLiderCat[p.categoriaId] = p.voltasConcluidas;
                    tempoLiderCat[p.categoriaId] = p.tempoTotalSegundos;
                    melhorVoltaLiderCat[p.categoriaId] = p.melhorVoltaSegundos;
                }
                p.gapCategoria = calcularTextoGapVolta(
                    p,
                    voltasLiderCat[p.categoriaId],
                    tempoLiderCat[p.categoriaId],
                    melhorVoltaLiderCat[p.categoriaId],
                    usarMelhorVolta);
            }
            else
            {
                p.posicaoGeral = 0;
                p.posicaoCategoria = 0;
                p.gapGeral = (p.situacao == SITUACAO_DNS) ? "DNS" : (p.situacao == SITUACAO_EM_CURSO ? "EM CURSO" : "DNF");
                p.gapCategoria = p.gapGeral;
            }
        }
    }
};

#endif

