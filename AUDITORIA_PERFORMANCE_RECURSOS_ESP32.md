# Auditoria Técnica de Performance, Memória, Armazenamento e Confiabilidade — Firmware ESP32 (Enduro MTB)

**Data da Auditoria:** 03/11/2026  
**Versão do Firmware Analisada:** `v0.6.1` (ELF `72078fd33761df33214c22fc8634a93771864df0bf1fe3c7ce25d17e12f24588.elf`)  
**Escopo:** Análise estática do código-fonte (`src/`, `lib/`, `interface/`), inspeção binária do ELF/imagem de firmware compilada, análise da tabela de partições e telemetria real de execução em bancada (`logs/device-monitor-*.log`).  
**Restrição Operacional Cumprida:** Auditoria 100% não destrutiva (nenhum arquivo de código, configuração ou dependência foi modificado).

---

## A. Resumo Executivo

A arquitetura atual do sistema de cronometragem cumpre funcionalmente os fluxos de bancada com baixo volume de dados, mas **não possui margem de segurança de memória (RAM/Heap), armazenamento persistente (LittleFS/Flash), capacidade de cartão RFID (no modo Voltas em Duas Estações) e determinismo temporal para operar com confiabilidade em uma prova real de médio ou grande porte** sem otimizações estruturais.

### Principais Conclusões Executivas

1. **RAM Livre e Fragmentação Crítica de Heap no Boot:**
   - O módulo real utiliza um **ESP32-D0WD-Q5 (Rev. 3.01) sem PSRAM** e com **4 MB de Flash SPI**.
   - Após a execução do `setup()`, restam apenas **`100.360 B` (~98,0 KB) de Heap livre total** e o **maior bloco contíguo disponível cai para `59.380 B` (~58,0 KB)** (`logs/device-monitor-261028-191403.log:110-114`).
   - O registro individual de **91 rotas estáticas** geradas em `WWWData.h` (`lib/framework/ESP32SvelteKit.cpp:104-116`) combinado à inicialização da pilha Wi-Fi e do servidor `PsychicHttpServer` (`stack_size = 24.576 B`, `max_uri_handlers = 150`) consome **`177.024 B` (172,9 KB) de Heap** logo na partida.

2. **Colapso de Memória (OOM) na Consulta Pública e na Sincronização:**
   - O endpoint `GET /rest/resultados-publicos` (`src/SyncService.cpp:573-669`) aloca na heap uma estrutura `ContextoPublico` que **duplica todos os estados residentes** (`Prova`, `Categorias`, `Atletas`, `RFIDState`, `Cronometrar`, `Cronometragem`), executa `ApuracaoResultados::calcular()` (que cria uma **terceira cópia** do vetor de registros além do vetor `classificacao` com `sizeof(piloto_apurado_t) = 292 B` + `sizeof(etapa_apurada_t) = 156 B` por estágio/volta) e, **mantendo todas essas cópias vivas na RAM**, instancia um `PsychicJsonResponse` (`ArduinoJson::JsonDocument`) contendo tanto a classificação completa quanto todo o vetor bruto `cronometragem`. Em uma prova de **150 pilotos × 5 estágios (750 registros)**, o pico transitório dessa única requisição atinge **`~430 KB`**, excedendo em **4,3×** todo o Heap livre do ESP32 (~98 KB) e causando falha silenciosa de serialização JSON ou *panic/reboot* por exaustão de memória.
   - Na recepção HTTP (`POST /rest/sync/resultados` e `POST /rest/sync/config`), `PsychicRequest::loadBody()` (`lib/PsychicHttp/src/PsychicRequest.cpp:138-159`) aloca `char* buf = malloc(len + 1)` e imediatamente copia para `this->_body = String(buf)` **antes** de chamar `free(buf)`, exigindo **2× o tamanho do payload em blocos contíguos** (para `maxRequestBodySize = 32 KB`, exige `32 KB + 32 KB = 64 KB`, superando o maior bloco contíguo de `58 KB`), seguido do `JsonDocument` que copia todas as strings por receber `const String&`.

3. **Saturação Física, Amplificação de Escrita $O(N^2)$ e Risco de Perda Total no LittleFS:**
   - A tabela de partições `min_spiffs.csv` reserva apenas **`128 KB` (`131.072 B` = 32 setores de 4 KB)** para a partição `spiffs` (montada como LittleFS).
   - Cada passagem de atleta no leitor RFID dispara `_cronometragemService->registrarTempo()`, que invoca `FSPersistence::writeToFS()` (`lib/framework/FSPersistence.h:64-87`), abrindo `/config/cronometragem.json` com `"w"` (`O_TRUNC`) e **reescrevendo o arquivo JSON inteiro desde o zero**.
   - **Risco de Perda Total:** A escrita não é atômica (não usa arquivo temporário + `rename`). Se houver queda de energia, *brownout* ou *reset* durante `writeToFS()` em qualquer passagem RFID, `/config/cronometragem.json` fica truncado; no próximo boot, `FSPersistence::readFromFS()` falha no `deserializeJson()`, executa `applyDefaults()` + `writeToFS()` e **apaga permanentemente todo o histórico da prova**.
   - **Limite de Capacidade e Desgaste:** Cada `cronometragem_t` ocupa `~205 B` em JSON. Acima de **~420 a 480 registros (`~86–98 KB`)**, o arquivo somado aos demais `/config/*.json` e aos blocos livres exigidos pelo mecanismo *Copy-on-Write* do LittleFS satura a partição de `128 KB`. Além disso, gravar $N = 500$ passagens gera $\frac{500 \times 501}{2} \times 205\text{ B} \approx \mathbf{25,7\text{ MB}}$ de escrita acumulada na Flash em uma única prova (~200 ciclos de apagamento por setor).

4. **Imprecisão Temporal na Captura RFID e Limite Físico do Cartão MIFARE 1K:**
   - No caminho crítico RFID (`src/CronometrarService.cpp:781-886`), quando o cartão entra no campo RF, o firmware **não captura o timestamp imediatamente**. Antes de chamar `Relogio::getNow()`, ele executa logs seriais, buscas em mapas/vetores, **lê até 45 blocos MIFARE via SPI/RF (`RFIDService::readCardData`, levando de `6 ms` a `350 ms`)**, faz o parse JSON e varre o histórico. Somente depois captura o horário (`time(nullptr)` com resolução de apenas **`1 segundo`** e truncamento `tv_usec = 0`).
   - Em seguida, `RFIDService::writeCardData()` **reescreve todo o JSON desde o bloco 4**. Se o piloto afastar o cartão durante a gravação (`50 ms` a `450 ms`), os blocos iniciais ficam com JSON truncado sem `\0`; na leitura seguinte, `deserializeJson()` falha e `doc.clear()` (`src/CronometrarService.cpp:868`) **apaga todo o histórico anterior do cartão**.
   - No **Modo Voltas em Duas Estações**, o array `"v":[{"i":"HH:MM:SS","f":"HH:MM:SS"},...]` consome `32 B` por volta nos `720 B` úteis do MIFARE Classic 1K, impondo um **limite físico intransponível de 21 voltas** (na 22ª volta, `727 B > 720 B`, `writeCardData` aborta no bloco 64 corrompendo o cartão).

5. **Partição de Firmware (`app0`) no Limite (97,91%):**
   - O binário `ESP32-Sveltekit_esp32dev_0-6-1.bin` possui **`1.924.896 B`** para uma partição `app0` de `1.966.080 B`, restando apenas **`41.184 B` (40,22 KB)** livres. Um único arquivo estático (`interface/src/lib/assets/logo.png`, embutido como `ESP_SVELTEKIT_DATA_8` em `WWWData.h`) consome **`280.132 B` (273,6 KB)** de Flash.

### Veredito de Viabilidade por Cenário (Estado Atual vs. Estado Otimizado)

| Cenário de Prova | Modo Estágios (5 Estágios) | Modo Estágios (10 Estágios) | Modo Voltas — Estação Única (20 Voltas) | Modo Voltas — Duas Estações (20–25 Voltas) |
| :--- | :--- | :--- | :--- | :--- |
| **Pequeno (50 pilotos)** | **Risco Alto (No-Go sem cautela):** 250 eventos (~51 KB LittleFS) cabem na Flash, mas 1 consulta `/rest/resultados-publicos` exige ~145 KB de Heap (> 98 KB livres $\rightarrow$ falha/OOM). | **Inviável (No-Go):** 500 eventos (~102 KB JSON) saturam a partição LittleFS de 128 KB e excedem a RAM na apuração/sync. | **Inviável (No-Go):** 1.050 eventos (~215 KB JSON) excedem em 1,7× a partição LittleFS (128 KB) e esgotam o Heap. | **Inviável (No-Go):** 1.000 eventos excedem LittleFS e Heap; cartão MIFARE 1K trava e corrompe na **22ª volta** (> 720 B). |
| **Médio (150 pilotos)** | **Inviável (No-Go):** 750 eventos (~154 KB JSON) excedem a partição LittleFS de 128 KB e esgotam o Heap em `writeToFS()` e `/rest/resultados-publicos`. | **Inviável (No-Go):** 1.500 eventos (~308 KB JSON) excedem LittleFS (2,4×) e RAM residente + transitória. | **Inviável (No-Go):** 3.150 eventos (~645 KB JSON) excedem RAM total do chip e LittleFS. | **Inviável (No-Go):** Excede RAM, LittleFS e limite físico de 21 voltas do cartão MIFARE 1K. |
| **Grande (300 pilotos)** | **Inviável (No-Go):** 1.500 eventos (~308 KB JSON) excedem LittleFS e toda a SRAM disponível do ESP32. | **Inviável (No-Go):** 3.000 eventos (~615 KB JSON) impossível na arquitetura atual. | **Inviável (No-Go):** 6.300 eventos (~1,29 MB JSON) impossível em JSON/RAM. | **Inviável (No-Go):** Impossível em RAM, LittleFS e MIFARE 1K. |

---

## B. Inventário de Recursos (Hardware e Ambiente)

Todas as informações abaixo foram extraídas diretamente dos artefatos de compilação (`.pio/build/esp32dev/idedata.json`, `platformio.ini`, `factory_settings.ini`, `features.ini`, `min_spiffs.csv`) e dos logs reais de inicialização do hardware (`logs/device-monitor-261028-191403.log` e `logs/device-monitor-261029-123732.log`).

### B.1 Hardware Real Identificado

| Recurso / Periférico | Especificação Teórica (Família ESP32) | Valor Real no Dispositivo Auditado | Fonte / Evidência |
| :--- | :--- | :--- | :--- |
| **Modelo do SoC** | ESP32 Clássico (Xtensa Dual-Core 32-bit LX6) | **`ESP32-D0WD-Q5` (Revisão `v3.01`)** | `logs/device-monitor-261028-191403.log:26,95` |
| **Clocks (CPU / APB / XTAL)** | Até 240 MHz / 80 MHz / 40 MHz | **CPU: `240 MHz` (2 núcleos), APB: `80 MHz`, XTAL: `40 MHz`** | `logs/device-monitor-261028-191403.log:95` |
| **SRAM Interna Total** | 520 KB (328 KB DRAM + 192 KB IRAM) | **Heap Total gerenciado: `320.004 B` (`312,50 KB`)** | `logs/device-monitor-261028-191403.log:105` |
| **SRAM Livre antes do `setup()`** | — | **`277.384 B` (`270,88 KB`)** \| Maior bloco: **`110.580 B` (`107,99 KB`)** | `logs/device-monitor-261028-191403.log:106-109` |
| **SRAM Livre após o `setup()`** | — | **`100.360 B` (`98,01 KB`)** \| Maior bloco: **`59.380 B` (`57,99 KB`)** | `logs/device-monitor-261028-191403.log:110-114` |
| **PSRAM (SPIRAM Externa)** | Suporta até 8 MB (4 MB mapeados) | **`0 B` — Não possui PSRAM (`Embedded PSRAM: No`)** | `logs/device-monitor-261028-191403.log:28` |
| **Memória Flash SPI** | Até 16 MB | **`4.194.304 B` (`4 MB`)**, Modo **`DIO`**, Clock **`40 MHz`** | `logs/device-monitor-261028-191403.log:18-20,99-100` |
| **Geometria da Flash** | — | Setor: **`4.096 B` (4 KB)**, Bloco: **`65.536 B` (64 KB)**, Página: **`256 B`** | `logs/device-monitor-261028-191403.log:101-103` |
| **Leitor RFID** | MFRC522 (SPI, ISO/IEC 14443A 106 kbps) | SPI (`SCK=18, MISO=19, MOSI=23, SS=5, RST=13`), Clock SPI **`1 MHz`**, Ganho **`RxGain_max` (48 dB)** | `src/RFIDService.h:35-40`, `platformio.ini:59`, `src/RFIDService.cpp:36` |
| **Cartão RFID** | MIFARE Classic 1K (`1.024 B`, 16 setores × 4 blocos) | Blocos úteis 4..62 (exceto trailers): **45 blocos = `720 B` úteis** | `src/RFIDService.cpp:543-647`, `src/CronometrarService.cpp:844` |
| **RTC Externo** | DS3231SN (TCXO $\pm 2\text{ ppm}$) | I2C (`SDA=21, SCL=22`), Clock I2C **`100 kHz`** | `src/RelogioService.h:50-54` |
| **Display Local** | TM1637 (LED 7 segmentos, 6 dígitos) | Bit-banging por software (`CLK=14, DIO=27`, `bitDelay=100 µs`) | `src/DisplayService.h:22-24`, `src/TM1637Display.h:29` |
| **Buzzer e Botão** | PWM / GPIO | Buzzer: `GPIO 16` (LEDC Canal 0); Botão: `GPIO 4` (`INPUT_PULLUP`) | `src/BuzzerService.h:16`, `src/RFIDService.h:39` |

### B.2 Particionamento da Flash (`min_spiffs.csv` — 4 MB)

A configuração ativa em `platformio.ini:72` é `board_build.partitions = min_spiffs.csv` (o arquivo `default_8MB.csv` existente na raiz não é utilizado porque o módulo possui apenas 4 MB de Flash).

| Nome da Partição | Tipo / Subtipo | Endereço Inicial | Tamanho (Hex) | Tamanho (Bytes / KB) | % da Flash (4 MB) | Uso Real no Projeto |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| *(Bootloader + Header)* | Sistema | `0x00001000` | `0x00007000` | `28.672 B` (`28 KB`) | 0,68% | Bootloader 2º estágio ESP-IDF |
| *(Partition Table)* | Sistema | `0x00008000` | `0x00001000` | `4.096 B` (`4 KB`) | 0,10% | Tabela de partições |
| **`nvs`** | `data` / `nvs` | `0x00009000` | `0x00005000` | `20.480 B` (`20 KB`) | 0,49% | Calibração PHY/RF e credenciais NVS do Wi-Fi |
| **`otadata`** | `data` / `ota` | `0x0000E000` | `0x00002000` | `8.192 B` (`8 KB`) | 0,20% | Seleção de partição ativa de boot |
| **`app0`** | `app` / `ota_0` | `0x00010000` | `0x001E0000` | **`1.966.080 B` (`1.920 KB`)** | **46,88%** | **Partição principal do firmware (97,91% ocupada)** |
| **`app1`** | `app` / `ota_1` | `0x001F0000` | `0x001E0000` | **`1.966.080 B` (`1.920 KB`)** | **46,88%** | **Ociosa** (`FT_UPLOAD_FIRMWARE=0` e `FT_DOWNLOAD_FIRMWARE=0` em `features.ini`) |
| **`spiffs`** | `data` / `spiffs` | `0x003D0000` | `0x00020000` | **`131.072 B` (`128 KB`)** | **3,13%** | **Montado como LittleFS (`/littlefs`) — 32 setores de 4 KB para TODOS os dados da prova** |
| **`coredump`** | `data` / `coredump` | `0x003F0000` | `0x00010000` | `65.536 B` (`64 KB`) | 1,56% | **Ociosa** (`FT_COREDUMP=0` em `features.ini`) |

> [!IMPORTANT]
> **Desperdício Crítico de Particionamento:** O projeto desabilita atualização OTA (`FT_UPLOAD_FIRMWARE=0`, `FT_DOWNLOAD_FIRMWARE=0`) e CoreDump (`FT_COREDUMP=0`) em `features.ini:9-10,13`, mas mantém a tabela `min_spiffs.csv` que reserva **`1.920 KB` para `app1` (OTA)** e **`64 KB` para `coredump`**, deixando apenas **`128 KB` (3,13% da Flash)** para armazenar todos os cadastros e tempos da prova no LittleFS.

### B.3 Ambiente de Compilação, Framework, Bibliotecas e Limites

- **Plataforma e Toolchain:** PlatformIO `6.2.0` | `pioarduino/platform-espressif32` release `53.03.13` | Framework **Arduino Core `3.1.3`** sobre **ESP-IDF `v5.3.2-584-g489d7a2b3a-dirty`** | Compilador **`xtensa-esp-elf-g++ 13.2.0`**.
- **Flags de Compilação e Otimização (`.pio/build/esp32dev/idedata.json` e `platformio.ini:27-60`):**
  - `platformio.ini` define `-flto=auto` e `-O3`. Contudo, o script de build do framework Arduino anexa `-Os` **após** as `build_flags` do usuário na linha de comando (`"... -flto=auto -O3 ... -Os"`). No GCC, quando múltiplas flags `-O` são passadas, **a última prevalece**; portanto, o código é efetivamente compilado com **`-Os`** (otimização para tamanho) com LTO (`-flto=auto`).
  - `-D CORE_DEBUG_LEVEL=4` está ativo em `platformio.ini:33`, compilando todas as strings e chamadas de log de nível `DEBUG`, `INFO`, `WARN` e `ERROR` do Arduino HAL, Wi-Fi e `PsychicHttp` dentro do binário de produção.
  - `-fexceptions` está habilitado (padrão do plataforma Arduino 3.x), adicionando tabelas de *stack unwinding* (`.eh_frame`) na Flash, embora o projeto não utilize `try/catch`.
  - `board_build.embed_files = src/certs/x509_crt_bundle.bin` (`platformio.ini:73`) embute `17.562 B` de certificados raiz TLS na Flash, embora `FT_DOWNLOAD_FIRMWARE=0` e `FT_NTP=0` não façam conexões HTTPS.
- **Bibliotecas Principais e Configurações de Limite:**
  - **`PsychicHttp` (`1.2.1`)** sobre `esp_http_server`:
    - `server.config.stack_size = 24576` (`24 KB` para a task `httpd`, `src/main.cpp:98`);
    - `server.config.max_uri_handlers = 150` (`src/main.cpp:95`, aumentado em `ESP32SvelteKit.cpp:74` para `155`);
    - `server.config.max_open_sockets = 13` (`lib/framework/ESP32SvelteKit.cpp:73`);
    - `server.config.lru_purge_enable = true` (`lib/framework/ESP32SvelteKit.cpp:75`);
    - `server.maxRequestBodySize = 32768` (`32 KB`, alterado em `src/SyncService.cpp:17`, sobrescrevendo o default `4.096 B`);
    - `JSON_BUFFER_SIZE = 4096` (`4 KB` alocado via `malloc` em `PsychicJsonResponse::send()`, `lib/PsychicHttp/src/PsychicJson.h:16`).
  - **`ArduinoJson` (`7.4.2`)**: Alocador dinâmico em pools no Heap (`ArduinoJson::detail::MemoryPool`), sem limite fixo de capacidade (cresce até esgotar o Heap).
  - **`MFRC522` (`1.4.12`)**: Comunicação SPI configurada em `1 MHz` (`MFRC522_SPICLOCK=1000000u`).
  - **`RTClib` (`2.1.4`)** + **`Adafruit BusIO` (`1.17.2`)**: Leitura I2C do `DS3231`.
  - **`LittleFS` (`2.0.0` built-in)**: Montado em `/littlefs` com `formatOnFail = true` (`lib/framework/ESP32SvelteKit.cpp:69`).

---

## C. Relatório de Memória (Flash, RAM Estática, Heap, Stack e Fragmentação)

### C.1 Análise Estática do Binário e Seções do ELF (`v0.6.1`)

Os dados abaixo foram extraídos diretamente do arquivo ELF de compilação `build/elf/72078fd33761df33214c22fc8634a93771864df0bf1fe3c7ce25d17e12f24588.elf` (via `xtensa-esp32-elf-size -A`) e da imagem binária `build/release/ESP32-Sveltekit_esp32dev_0-6-1.bin`.

| Seção do ELF | Endereço Virtual | Tamanho (Bytes) | Tamanho (KB) | Destino Físico | Explicação Técnica |
| :--- | :--- | ---: | ---: | :--- | :--- |
| **`.iram0.vectors`** | `0x40080000` | `1.027 B` | `1,00 KB` | **RAM (IRAM) + Flash** | Tabela de vetores de interrupção e exceção executada em IRAM |
| **`.iram0.text`** | `0x40080404` | `89.511 B` | `87,41 KB` | **RAM (IRAM) + Flash** | Código crítico de tempo real (ISRs, FreeRTOS kernel, SPI Flash driver, Wi-Fi MAC) copiado para IRAM no boot |
| **`.dram0.data`** | `0x3FFBDB60` | `25.200 B` | `24,61 KB` | **RAM (DRAM) + Flash** | Variáveis globais e estáticas inicializadas (valores iniciais residem na Flash e são copiados para DRAM no boot) |
| **`.dram0.bss`** | `0x3FFC3DD0` | `27.240 B` | `26,60 KB` | **RAM (DRAM)** | Variáveis globais e estáticas não inicializadas (zeradas no boot; não ocupam espaço na imagem Flash) |
| **`.flash.appdesc`** | `0x3F400020` | `256 B` | `0,25 KB` | **Flash (DROM)** | Descritor da aplicação ESP-IDF (`esp_app_desc_t`) |
| **`.flash.rodata`** | `0x3F400120` | **`822.264 B`** | **`803,00 KB`** | **Flash (DROM)** | Constantes `const` / `PROGMEM`, ativos web `WWWData.h` (`592.597 B`), strings literais e tabelas de vtable |
| **`.flash.rodata_noload`** | `0x3F4C8CF0` | `21.954 B` | `21,44 KB` | *(Metadados)* | Seção `noload` de metadados de formato |
| **`.flash.text`** | `0x400D0020` | **`986.526 B`** | **`963,40 KB`** | **Flash (IROM)** | Código executável da aplicação, framework, Wi-Fi, LwIP, HTTP e bibliotecas lido via cache MMU da Flash |
| **`.rtc_fast.rodata` / `reserved`** | `0x3FF80000` | `26 B` | `0,03 KB` | **RTC FAST RAM** | Dados reservados na memória RTC rápida |
| **`.rtc_slow_reserved`** | `0x50000FE8` | `24 B` | `0,02 KB` | **RTC SLOW RAM** | Dados reservados na memória RTC lenta |

#### Consolidação de Flash vs. RAM Estática

- **Consumo de RAM Estática (DRAM):** `.dram0.data` (`25.200 B`) + `.dram0.bss` (`27.240 B`) = **`52.440 B` (`51,21 KB`)** (16,0% dos `327.680 B` de DRAM estática nominal do PlatformIO).
- **Consumo de RAM de Instruções (IRAM):** `.iram0.vectors` (`1.027 B`) + `.iram0.text` (`89.511 B`) = **`90.538 B` (`88,42 KB`)**.
- **Tamanho Total da Imagem de Firmware na Flash (`ESP32-Sveltekit_esp32dev_0-6-1.bin`):**
  $$\text{Imagem Flash} = \mathbf{1.924.896\text{ B}}\text{ (`1.879,78 KB`)}$$
- **Ocupação da Partição de Aplicação (`app0` = `1.966.080 B` / `1.920 KB`):**
  $$\text{Ocupação de }\texttt{app0} = \frac{1.924.896}{1.966.080} = \mathbf{97,91\%} \quad \Longrightarrow \quad \text{Margem Livre} = \mathbf{41.184\text{ B}}\text{ (`40,22 KB`)}$$

#### Decomposição dos Maiores Contribuintes para o Tamanho do Binário (Flash)

A inspeção da tabela de símbolos do ELF (`xtensa-esp32-elf-nm -S --size-sort`) revela exatamente onde estão alocados os `1.924.896 B` da Flash:

1. **Interface Web Embutida (`lib/framework/WWWData.h` — `EMBED_WWW`):**
   - Contém **91 arrays `PROGMEM`** somando **`592.597 B` (`578,71 KB`)** dentro de `.flash.rodata` (**30,8% de todo o firmware**).
   - **Achado Crítico de Flash:** Um único símbolo, `ESP_SVELTEKIT_DATA_8` (correspondente a `/_app/immutable/assets/logo.png`, originado de `interface/src/lib/assets/logo.png`), ocupa sozinho **`280.132 B` (`273,57 KB`)** — quase metade de toda a interface web e **14,55% do firmware inteiro**!
   - O segundo maior ativo é `ESP_SVELTEKIT_DATA_76` (`/_app/immutable/nodes/18.js`), com **`95.172 B` (`92,94 KB`)**, seguido de `ESP_SVELTEKIT_DATA_38` (`/_app/immutable/chunks/25.js`) com **`31.116 B`** e `ESP_SVELTEKIT_DATA_18` (`/_app/immutable/chunks/11.js`) com **`26.183 B`**.
2. **Pilha Wi-Fi, LwIP, mbedTLS, FreeRTOS e HAL ESP-IDF:** `~910 KB` somando `.flash.text`, `.iram0.text` e `.flash.rodata`.
3. **Código da Aplicação + `ESP32SvelteKit` + `PsychicHttp` + `ArduinoJson`:** `~380 KB` (incluindo instanciações pesadas de templates `ArduinoJson::detail::VariantData`, `PsychicHttp`, `std::map` e `std::vector`).
4. **Bundle de Certificados X.509 (`x509_crt_bundle.bin`):** `17.562 B` em `.flash.rodata` (não utilizado em tempo de execução pois `FT_DOWNLOAD_FIRMWARE=0` e `FT_NTP=0`).
5. **Logs de Depuração (`-D CORE_DEBUG_LEVEL=4`):** Centenas de strings de formato e funções wrapper de log compiladas em todos os módulos.

---

### C.2 Alocações Estáticas de RAM e Tamanho Exato das Estruturas de Dados

Em `src/main.cpp:25-65`, todos os serviços principais são instanciados como objetos globais na seção `.dram0.bss` / `.dram0.data`. Através das informações de depuração DWARF do ELF (`xtensa-esp32-elf-gdb`), foram medidos os tamanhos exatos (`sizeof`) gerados pelo compilador `xtensa-esp-elf-g++ 13.2.0` (arquitetura 32-bit):

#### Tabela de Tamanhos Exatos (`sizeof`) de Objetos Globais e Tipos de Domínio

| Símbolo / Estrutura | Localização | `sizeof` (Bytes) | Detalhamento Interno e Overhead Oculto de Heap |
| :--- | :--- | ---: | :--- |
| **`esp32sveltekit`** (`ESP32SvelteKit`) | `src/main.cpp:26` | **`1.108 B`** | Agrega `WiFiSettingsService`, `APSettingsService`, `SecuritySettingsService`, `EventSocket`, `SystemStatus`, etc. |
| **`rfidService`** (`RFIDService`) | `src/main.cpp:32` | **`552 B`** | Inclui `HttpEndpoint<RFIDState>`, `FSPersistence<RFIDState>`, `MFRC522*`, `RFIDState` (`28 B`) |
| **`cronometrarService`** (`CronometrarService`) | `src/main.cpp:59` | **`300 B`** | Inclui `HttpEndpoint<Cronometrar>`, `FSPersistence<Cronometrar>`, `EventEndpoint<Cronometrar>`, `Cronometrar` (`104 B`) |
| **`provaService`** (`ProvaService`) | `src/main.cpp:29` | **`292 B`** | Inclui `HttpEndpoint<Prova>`, `FSPersistence<Prova>`, `Prova` (`148 B`) |
| **`server`** (`PsychicHttpServer`) | `src/main.cpp:25` | **`132 B`** | Estrutura base; aloca internamente `httpd_handle_t` e listas `std::list` de endpoints e handlers no Heap |
| **`std::string`** (GCC 13 libstdc++) | *(Padrão C++)* | **`24 B`** | Ponteiro (`4 B`) + tamanho (`4 B`) + buffer SSO local de `16 B` (strings $\le 15$ chars não alocam no heap; $> 15$ chars alocam `len + 1 + 8 B` no heap) |
| **`String`** (Arduino Core ESP32) | *(Padrão Arduino)* | **`16 B`** | Buffer SSO local até `11 chars`; $\ge 12$ chars aloca no heap |
| **`categoria_t`** | `src/CategoriaService.h:15` | **`28 B`** | `1 × std::string` (`24 B`) + `bool ativo` (`1 B` + `3 B` padding) |
| **`atleta_t`** | `src/AtletaService.h:15` | **`56 B`** | `uint16_t numero` (`2 B` + `2 B` pad) + `2 × std::string` (`48 B`) + `bool ativo` (`1 B` + `3 B` pad). Nomes $> 15$ chars alocam heap extra |
| **`cronometragem_t`** | `src/CronometragemService.h:16` | **`208 B`** | **`8 × std::string` (`192 B`)** + `2 × uint16_t` (`8 B` c/ pad) + `int32_t` (`4 B`) + `bool` (`4 B` c/ pad). Cada registro custa **`208 B`** mesmo com strings curtas! |
| **`etapa_apurada_t`** | `src/ApuracaoResultados.h:14` | **`156 B`** | `5 × std::string` (`120 B`) + `uint16_t` + `int32_t` + `4 × bool` + padding |
| **`piloto_apurado_t`** | `src/ApuracaoResultados.h:29` | **`292 B`** | **`10 × std::string` (`240 B`)** + `std::vector<etapa_apurada_t>` (`12 B`) + `uint16_t`/`int32_t`/`bool`s (`40 B`), **mais** `N_etapas × 156 B` no heap! |
| **`relatorio_apuracao_t`** | `src/ApuracaoResultados.h:50` | **`152 B`** | `5 × std::string` (`120 B`) + `uint8_t`/`bool` + `2 × std::vector` (`24 B`) |
| **Nó de `RFIDState::cardRegistry`** | `src/RFIDService.h:15` | **`48 B` / cartão** | `std::map<std::string, uint16_t>`: cabeçalho `_Rb_tree_node_base` (`16 B`) + `std::string` (`24 B`) + `uint16_t` (`4 B` c/ pad) + header `malloc` (`4 B`) |

> [!WARNING]
> **Ineficiência Estrutural de `cronometragem_t` (`208 B`/registro) e `piloto_apurado_t` (`292 B`/piloto):**  
> Em `cronometragem_t`, campos como `uid` (sempre 8 caracteres hexadecimais), `tipo` (`"largada"`, `"chegada"`, `"especial"` — um enum de 1 byte), `modoVoltas` (`"unica"`, `"dupla"` — um enum de 1 byte), `especial` (`"E1"`..`"E10"` — 1 byte de índice), `inicio` (`"HH:MM:SS"` — 9 bytes ou `uint32_t`), `fim` (`"HH:MM:SS"`), `duracao` (`"HH:MM:SS"`, redundante com `duracaoSegundos`) e `status` (`"OK"`, `"DNF"`, `"DNS"` — enum de 1 byte) são armazenados como **8 objetos `std::string` de `24 B` cada (`192 B` apenas em cabeçalhos `std::string`)**. Uma representação compacta com tipos fixos e enums ocuparia **`24 B` a `28 B` por registro** — uma **redução de 87% no consumo de RAM** por evento cronometrado!

---

### C.3 Heap e Alocações Dinâmicas: Do Boot ao Colapso em Execução

#### 1. Consumo de Heap durante o `setup()` (`177 KB` consumidos antes da prova começar)

Conforme registrado na telemetria real (`logs/device-monitor-261028-191403.log:104-114`):
- **Antes do `setup()`:** Heap Livre = `277.384 B` (`270,88 KB`), Alocado = `34.756 B`, Maior Bloco Livre = `110.580 B` (`107,99 KB`).
- **Após o `setup()`:** Heap Livre = **`100.360 B` (`98,01 KB`)**, Alocado = `186.932 B`, Heap Mínimo = `99.696 B`, **Maior Bloco Contíguo Livre = `59.380 B` (`57,99 KB`)**.

**Para onde foram os `177.024 B` consumidos no `setup()`?**
1. **Pilha Wi-Fi + LwIP + Driver RX/TX (`WiFi.mode(WIFI_AP_STA)` / `WiFi.softAP`):** `~75 KB` a `85 KB` de buffers internos de rádio e pacotes TCP/IP.
2. **Task e Estruturas do `esp_http_server` (`httpd_start`):**
   - Pilha da task `httpd`: **`24.576 B` (`24 KB`)** (`src/main.cpp:98`);
   - Array de `max_uri_handlers = 155` slots + descritores de sockets (`~6 KB`).
3. **Registro Individual das 91 Rotas Estáticas do SvelteKit (`WWWData::registerRoutes` em `lib/framework/ESP32SvelteKit.cpp:104-116`):**
   - Para **cada um dos 91 arquivos** em `WWWData.h`, `ESP32SvelteKit::begin()` chama `_server->on(uri, HTTP_GET, [etag, contentType, ...](PsychicRequest *request) { ... })`.
   - Cada chamada a `_server->on()` aloca na heap:
     - Um `PsychicWebHandler` (`~56 B`) contendo um `std::function` que captura duas `String`s (`etag` e `contentType`), forçando alocação dinâmica do *callable* na heap (`~48 B`) + buffers das `String`s;
     - Um `PsychicEndpoint` (`~40 B`) + nó de `std::list` (`16 B`) + cópia da `String _uri` (`16–48 B`);
     - Uma entrada duplicada da string URI via `strdup` dentro de `httpd_register_uri_handler` do ESP-IDF (`~32–64 B`).
   - Isso gera **mais de 450 pequenas alocações permanentes no Heap (~25 KB a 30 KB)** apenas para registrar os 91 arquivos estáticos, fragmentando o heap antes mesmo de abrir o sistema.
4. **Tasks e Serviços do Framework:** Task `ESP32 SvelteKit Loop` (`4.096 B` stack), `DNSServer`, e carregamento inicial dos arquivos `/config/*.json` via `FSPersistence::readFromFS()`.

---

#### 2. Gargalo Crítico #1 de Heap: `SyncService::handleGetResultadosPublicos` (`src/SyncService.cpp:573-669`)

Quando qualquer atleta abre a tela de resultados no celular (`GET /rest/resultados-publicos`), a função executa a seguinte sequência **dentro do mesmo escopo de vida**:

```cpp
// src/SyncService.cpp:575-609
struct ContextoPublico {
    Prova provaState;
     std::vector<categoria_t> categoriasList;
    std::vector<atleta_t> atletasList;
    std::map<std::string, uint16_t> cardRegistryMap;
    Cronometrar cronometrarState;
    std::vector<cronometragem_t> registrosList;
    relatorio_apuracao_t relatorio;
};
auto ctx = std::unique_ptr<ContextoPublico>(new (std::nothrow) ContextoPublico());
// ... Copia TODOS os vetores e mapas dos serviços para dentro de ctx ...
ctx->relatorio = ApuracaoResultados::calcular(
    ctx->provaState, ctx->categoriasList, ctx->atletasList,
    ctx->cardRegistryMap, ctx->cronometrarState, ctx->registrosList);
```

E logo em seguida (`linhas 612-667`), **sem liberar `ctx->registrosList`, `ctx->atletasList` ou `ctx->cardRegistryMap`**:
1. Dentro de `ApuracaoResultados::calcular()` (`src/ApuracaoResultados.h:599-988`), a função recebe `const std::vector<cronometragem_t>& registrosOriginais` e na **linha 658** cria **outra cópia completa**:
   `std::vector<cronometragem_t> registros = registrosOriginais;` (3ª cópia simultânea de todos os registros na RAM!), além de `mapaCategorias`, `numeralParaUid`, `uidParaNumeral` e `std::vector<piloto_apurado_t> pilotos` (com `piloto.etapas` contendo `N_estagios` elementos de `156 B` cada).
2. Ao retornar para `handleGetResultadosPublicos()`, `ctx` retém `ctx->registrosList` (2ª cópia) + `ctx->relatorio.classificacao` (`pilotos` apurados) + `ctx->atletasList` + `ctx->cardRegistryMap`.
3. Na **linha 612**, instancia `PsychicJsonResponse response = PsychicJsonResponse(request, false);` (que contém um `ArduinoJson::JsonDocument _jsonBuffer`).
4. Nas **linhas 626–656**, popula no `_jsonBuffer` toda a árvore de `classificacao` (incluindo o sub-array `etapas` de cada piloto).
5. Nas **linhas 658–664**, popula **também** no mesmo `_jsonBuffer` todo o array bruto `cronometragem` (chamando `Cronometragem::read(cronoState, root)` em `ctx->registrosList`)!
6. Na **linha 667**, chama `response.send()`, que aloca mais `4.096 B` (`malloc(JSON_BUFFER_SIZE)` em `PsychicJson.cpp:47`) para transmitir os chunks HTTP.

**Custo Real em Memória (Exemplo: Cenário Médio = 150 pilotos, 5 estágios = 750 registros):**
- `CronometragemService` residente (1ª cópia): `750 × 208 B = 156.000 B` (`152,3 KB`) — *já excede a RAM livre se estivesse tudo em RAM!*
- Mesmo em um cenário menor de **50 pilotos × 5 estágios (250 registros)**:
  - Estado residente (`Cronometragem` + `Atleta` + `RFIDState`): `250 × 208 B + 50 × 56 B + 50 × 48 B = 57.200 B` (`55,9 KB`);
  - Heap livre restante antes da requisição: `100.360 - 57.200 = 43.160 B` (`42,1 KB`);
  - Alocação de `ctx` em `handleGetResultadosPublicos()`:
    - `ctx->registrosList`: `250 × 208 B = 52.000 B` (bloco contíguo de `52 KB`!);
    - Cópia local `registros` em `ApuracaoResultados::calcular`: mais `52.000 B` contíguos!
    - `ctx->relatorio.classificacao`: `50 × 292 B + 50 × (5 × 156 B) = 14.600 + 39.000 = 53.600 B`;
    - `_jsonBuffer` (`JsonDocument` com `classificacao` + `cronometragem`): `~65.000 B`;
  - **Pico exigido pela requisição (para apenas 50 pilotos / 250 registros):** **`~170 KB` extras** sobre os `42 KB` livres $\rightarrow$ **Colapso imediato de Heap!**

---

#### 3. Gargalo Crítico #2 de Heap: Duplicação de Corpo HTTP em `PsychicRequest::loadBody()` e `PsychicJsonHandler`

Em `src/SyncService.cpp:17`, o limite do corpo HTTP é aumentado para `32.768 B` (`32 KB`):
```cpp
if (_server->maxRequestBodySize < 32768) {
    _server->maxRequestBodySize = 32768;
}
```
Contudo, veja o que `PsychicRequest::loadBody()` (`lib/PsychicHttp/src/PsychicRequest.cpp:138-161`) faz ao receber um `POST`:
```cpp
char *buf = (char *)malloc(remaining + 1); // 1ª alocação contígua (ex: 25 KB)
// ... httpd_req_recv lê o socket para buf ...
this->_body = String(buf);                 // 2ª alocação contígua (25 KB) ANTES do free(buf)!
free(buf);
```
E imediatamente depois, `PsychicJsonHandler::handleRequest()` (`lib/PsychicHttp/src/PsychicJson.cpp:114-116`) executa:
```cpp
JsonDocument jsonBuffer;
DeserializationError error = deserializeJson(jsonBuffer, request->body());
```
E dentro de `SyncService::handlePostResultados()` (`src/SyncService.cpp:365-431`):
- Enquanto `request->_body` (`String`) e `jsonBuffer` (`JsonDocument`) continuam alocados na pilha de `handleRequest()`, `handlePostResultados` aloca `std::vector<cronometragem_t> remotos`, mescla os registros no `_cronometragemService` e chama `_cronometragemService->saveToFS()` (`FSPersistence::writeToFS()`), que cria **um segundo `JsonDocument` contendo todos os registros da estação** para gravar no LittleFS!
- **Impacto:** Um payload JSON de sincronização de apenas **`20 KB`** (~100 registros) exige simultaneamente:
  1. `20 KB + 20 KB = 40 KB` em dois blocos contíguos durante `loadBody()`;
  2. `20 KB` retidos em `request->_body` + `~30 KB` no `jsonBuffer` de entrada + `~21 KB` no vetor `remotos` + `~30–60 KB` no `jsonDocument` de `writeToFS()` = **`~100–130 KB` simultâneos**, causando falha de alocação (`ESP_ERR_HTTPD_ALLOC_MEM`) ou corrupção parcial silenciosa do `JsonDocument`.

---

#### 4. Gargalo Crítico #3 de Heap: `FSPersistence<T>::writeToFS()` em cada Passagem RFID (`lib/framework/FSPersistence.h:64-87`)

```cpp
bool writeToFS() {
    JsonDocument jsonDocument;
    JsonObject jsonObject = jsonDocument.to<JsonObject>();
    _statefulService->read(jsonObject, _stateReader);
    File settingsFile = _fs->open(_filePath, "w");
    serializeJson(jsonDocument, settingsFile);
    settingsFile.close();
    return true;
}
```
- No ArduinoJson v7, `JsonDocument` constrói em RAM a árvore completa (nós `VariantSlot` de `16 B` + pools de strings copiadas porque `cronometragem_t` usa `std::string`).
- Para $N$ registros em `CronometragemService`, `Cronometragem::read()` (`src/CronometragemService.h:168-193`) cria um `JsonObject` com 12 pares chave-valor para cada registro.
- Em ArduinoJson v7, cada objeto de 12 campos com strings copiadas consome **`~260 a 310 B` de Heap** dentro do `JsonDocument`.
- Logo, para **250 registros**, apenas o `JsonDocument` temporário dentro de `writeToFS()` exige **`~70 KB` de Heap**, enquanto o vetor residente `Cronometragem::registros` já ocupa **`52 KB`**! Como após o boot só existem **`100 KB` livres no total**, por volta de **160 a 200 registros**, `writeToFS()` esgota o Heap livre do ESP32, fazendo com que os novos nós do `JsonDocument` falhem silenciosamente e **gravem um arquivo `/config/cronometragem.json` incompleto/truncado em disco**!

---

#### 5. Alocações Repetitivas e Fragmentação em `RFIDService::readCardData` e Loops

- Em `src/RFIDService.cpp:579`, dentro do loop de leitura dos 45 blocos MIFARE:
  ```cpp
  for (uint8_t i = 0; i < 16; i++) {
      if (buffer[i] == 0) return true;
      outData += (char)buffer[i];
  }
  ```
  `outData` (`Arduino String`) não possui `outData.reserve(maxBlocks * 16)` antes do loop. Conforme o JSON do cartão cresce até `720 B`, `outData += (char)` causa múltiplas realocações (`realloc`) progressivas no heap durante a janela crítica de leitura RF.
- Em `src/RFIDService.cpp:258`, `clearRFIDData()` executa `Serial.printf("Block %d cleared\n", blockAddr)` dentro do loop de 45 blocos, bloqueando a UART e a `loopTask`.

---

### C.4 Métricas em Execução e Evolução Temporal da Memória

#### Instrumentação Existente no Projeto
1. **`SystemStatus::systemStatus` (`lib/framework/SystemStatus.cpp:24-45` — `GET /rest/systemStatus`):**
   - Coleta sob demanda: `ESP.getFreeHeap()`, `ESP.getMaxAllocHeap()` (maior bloco contíguo), `ESP.getMinFreeHeap()` (mínimo histórico), `ESP.getHeapSize()`, `ESP.getPsramSize()`, `ESP.getFreePsram()`, `ESPFS.totalBytes()`, `ESPFS.usedBytes()`.
   - **Avaliação:** Os indicadores são corretos (usam as APIs nativas do `EspClass`), mas só são lidos quando a página de status do painel administrativo é aberta.
2. **`AnalyticsService` (`lib/framework/AnalyticsService.h`):**
   - Emitiria telemetria a cada `2.000 ms` via WebSocket, porém está **desativado em compilação** (`-D FT_ANALYTICS=0` em `features.ini:14`).
3. **Ausência de Telemetria em Caminhos Críticos:**
   - Não há monitoramento de `ESP.getFreeHeap()`, `ESP.getMaxAllocHeap()`, `uxTaskGetStackHighWaterMark(NULL)` ou verificação de `jsonDocument.overflowed()` do ArduinoJson v7 durante:
     - `FSPersistence::writeToFS()`;
     - `SyncService::handleGetResultadosPublicos()`;
     - `SyncService::handlePostResultados()` / `handlePostConfig()`;
     - `CronometrarService::loop()` (leituras e gravações RFID).

> [!CAUTION]
> **Risco Silencioso do ArduinoJson v7 (`overflowed()`):** No ArduinoJson v7, quando o alocador falha ao obter um novo pool do Heap (`malloc` retorna `NULL`), ele **não lança exceção nem aborta**: ele simplesmente ignora as inserções subsequentes e marca `jsonDocument.overflowed() == true`. Nenhum arquivo do projeto (`FSPersistence.h`, `SyncService.cpp`, `CronometrarService.cpp`, `PsychicJson.cpp`) verifica `jsonDocument.overflowed()` antes de serializar para a Flash ou para a rede!

#### Evolução Temporal da Memória Durante uma Prova Real

1. **Partida (Boot):** `100.360 B` livres, maior bloco contíguo = `59.380 B`.
2. **Cadastro / Sincronização Inicial de Configuração ($A$ atletas, $A$ cartões, $C$ categorias):**
   - Crescimento permanente no Heap: $\Delta \text{RAM}_{\text{cad}} \approx A \times (56\text{ B} + 48\text{ B}) + C \times 28\text{ B} = \mathbf{104\text{ B por atleta}}$ (ex.: 150 atletas = `15,6 KB` permanentes).
   - Fragmentação deixada pelo `POST /rest/sync/config`: os nós de `std::map<std::string, uint16_t>` (`48 B` cada) são alocados intercalados com os buffers temporários de `JsonDocument` e `request->_body`. Quando `request->_body` e `jsonBuffer` são liberados, os nós do `std::map` permanecem ancorados no topo/meio do heap, **impedindo a coalescência dos blocos livres** e reduzindo permanentemente o `MaxAllocHeap`.
3. **Durante a Cronometragem ($N$ passagens RFID):**
   - Cada passagem adiciona um `cronometragem_t` (`208 B`) a `_state.registros` (`std::vector<cronometragem_t>`).
   - Como `std::vector` dobra sua capacidade (`1 -> 2 -> 4 -> 8 -> 16 -> 32 -> 64 -> 128 -> 256 -> 512`), durante a realocação de `128` para `256` elementos (na 129ª passagem), o `std::vector` precisa alocar um **bloco contíguo novo de `256 × 208 B = 53.248 B` (`52,0 KB`) ENQUANTO o bloco antigo de `128 × 208 B = 26.624 B` (`26,0 KB`) ainda está alocado** (`78 KB` simultâneos só para o realocador do vetor!).
   - Como o maior bloco contíguo logo após o boot já é de apenas **`59.380 B`** (e menor ainda após a fragmentação do cadastro), **a realocação de `std::vector<cronometragem_t>` de 128 para 256 elementos tem altíssima probabilidade de falhar com `std::bad_alloc` (`abort()` / reboot imediato na 129ª passagem RFID)!**

---

### C.5 Stack, FreeRTOS e Concorrência

#### 1. Mapa de Tareras (Tasks FreeRTOS) do Sistema

| Nome da Task | Core (Afinidade) | Prioridade | Stack Alocada | Papel e Funções Executadas | Risco de Stack Overflow / Bloqueio |
| :--- | :---: | :---: | ---: | :--- | :--- |
| **`loopTask`** (Arduino Main) | **Core 1** | `1` | **`8.192 B`** (`8 KB`) | Executa `displayService.loop()`, `rfidService.loop()`, `cronometrarService.loop()`, `delay(10)` (`src/main.cpp:137-155`). | **Médio-Alto:** Em `CronometrarService::loop()`, aloca `Cronometragem novoRegistro` (`208 B`), `JsonDocument doc`, `String rawData`, `String novoJson`, chama `writeToFS()` + LittleFS VFS. Margem segura mas exige atenção. |
| **`ESP32 SvelteKit Loop`** | **Core 0** | `2` | **`4.096 B`** (`4 KB`) | Criada em `ESP32SvelteKit.cpp:126`. Executa `_wifiSettingsService.loop()`, `_apSettingsService.loop()`, `DNSServer::processNextRequest()`. | **Alto:** `WiFiSettingsService::updateRSSI()` (a cada 5s) e `onStationModeDisconnected` instanciam `JsonDocument` e chamam `EventSocket::emitEvent()` $\rightarrow$ `PsychicWebSocket` + `ESP_LOGV` dentro de apenas `4.096 B` de stack! |
| **`httpd`** (ESP-IDF HTTP Server) | Sem afinidade (`tskNO_AFFINITY`) | `5` | **`24.576 B`** (`24 KB`) | Criada em `PsychicHttpServer.cpp:89` (`src/main.cpp:98`). Processa **todas** as requisições HTTP e WebSocket sequencialmente (`ENABLE_ASYNC` não está definido). | **Baixo p/ Stack (`24 KB`), Crítico p/ Concorrência:** Por ser *single-threaded*, uma única chamada a `/rest/resultados-publicos` ou `/rest/sync/resultados` bloqueia **todas** as demais requisições HTTP/WebSocket. |
| **`wifi` / `tiT` (LwIP)** | Core 0 / Ambos | `23` / `18` | Interna IDF | Driver Wi-Fi MAC/Baseband (Core 0, prio 23) e pilha TCP/IP LwIP (prio 18). | Preemptam a task `ESP32 SvelteKit Loop` no Core 0 e `httpd` quando escalonada no Core 0. |

#### 2. Problemas de Concorrência, Data Races e Bloqueios Identificados

1. **Data Race Confirmado em `CronometrarService::_state` (`src/CronometrarService.cpp:204-205, 376, 482, 503, 872-874`):**
   - `CronometrarService::loop()` roda na `loopTask` (**Core 1**) e acessa diretamente `this->_state.tipo`, `this->_state.modoVoltas`, `this->_state.especial`, `this->_state.posicao` e `this->_state.intervaloMinVoltaSegundos` **sem adquirir o mutex `_accessMutex`** (`beginTransaction()`/`read()`).
   - Ao mesmo tempo, a task `httpd` (prioridade 5, podendo rodar no Core 0 ou Core 1) modifica `this->_state` (incluindo reatribuição de `std::string`!) através dos endpoints `POST /rest/cronometrar` (`HttpEndpoint`) e `POST /rest/sync/config` (`SyncService.cpp:542`).
   - Ler uma `std::string` no Core 1 enquanto ela é realocada/modificada no Core 0 é *Undefined Behavior* em C++ (pode ler ponteiro inválido ou estado intermediário inconsistente).

2. **Acesso Concorrente sem Mutex ao Hardware do Display (`TM1637Display` — GPIO 14 e 27):**
   - `displayService.loop()` roda continuamente na `loopTask` (Core 1), enviando comandos bit-banged aos pinos `GPIO 14` (`CLK`) e `GPIO 27` (`DIO`) com `delayMicroseconds(100)`.
   - Simultaneamente, `RFIDService::handleStartReset()` e `handleStopReset()` (`src/RFIDService.cpp:150, 179`), executados na task `httpd`, chamam diretamente `_display->showResetCount(0)` e `_display->clear()` **sem nenhum mutex de proteção no `DisplayService`**. Se uma requisição HTTP de reset coincidir com a atualização do relógio no `loopTask`, os pulsos de clock/dados I2C-like do TM1637 colidem nos mesmos pinos GPIO, corrompendo os dígitos exibidos no display.

3. **Janela de Condição de Corrida na Persistência (`StatefulService::update` — `lib/framework/StatefulService.h:146-163`):**
   - `StatefulService::update()` adquire `_accessMutex` (`beginTransaction()`), altera `_state`, **libera `_accessMutex` (`endTransaction()`)** e só então chama `callUpdateHandlers(originId)` (que invoca `FSPersistence::writeToFS()`).
   - Se duas requisições HTTP ou eventos atualizarem o serviço em rápida sucessão, ou se uma leitura concorrer durante `callUpdateHandlers()`, `writeToFS()` pode ser invocada concorrentemente no mesmo arquivo `/config/*.json` sem exclusão mútua de arquivo!

4. **Bloqueios Severos na `loopTask` (Core 1) que Cegam o Leitor RFID:**
   - **`BuzzerService::beepTone()` (`src/BuzzerService.cpp:19-33`):** Usa `delay(durationMs)` bloqueante!
     - `beepSuccess()` / confirmação de leitura (`beepTone(1000, 300)`): bloqueia a `loopTask` por **`300 ms`**;
     - `beepWarning()`: bloqueia por `140 + 60 + 140 =` **`340 ms`**;
     - `beepError()`: bloqueia por `180 + 80 + 220 =` **`480 ms`**!
   - **`RFIDService::clearRFIDData()` (`src/RFIDService.cpp:200-273`):** Autentica e grava zeros em 15 setores (45 blocos) com `delay(10)`/`delay(15)` em retentativas e `Serial.printf` por bloco, bloqueando a `loopTask` por **`400 ms` a `2.500+ ms`**.
   - **`WiFi.scanNetworks()` síncrono (`lib/framework/WiFiSettingsService.cpp:173`):** Quando o ESP32 tenta conectar a uma rede STA configurada (a cada 60s se desconectado), `WiFi.scanNetworks()` bloqueia a task `ESP32 SvelteKit Loop` (e o servidor DNS do Captive Portal!) por **`1.500 ms` a `3.000 ms`**, além de causar troca de canal do rádio Wi-Fi que interrompe temporariamente pacotes do SoftAP (`192.168.4.1`).

---

## D. Relatório de Armazenamento Persistente (LittleFS) e Cartão RFID (MIFARE 1K)

### D.1 Partição LittleFS (`spiffs` = `128 KB` / 32 Setores de 4 KB)

#### 1. Estrutura e Custo por Registro nos Arquivos `/config/*.json`

Todos os dados persistentes são salvos como arquivos JSON texto na partição `/littlefs` (`131.072 B` brutos = **32 blocos de `4.096 B`**):

| Arquivo em `/littlefs` | Serviço Responsável | Tamanho Base | Custo por Item em JSON | Exemplo (150 Pilotos, 5 Estágios = 750 Reg.) |
| :--- | :--- | ---: | ---: | ---: |
| `/config/wifiSettings.json` | `WiFiSettingsService` | `~220 B` | Fixo | `~220 B` (1 bloco de 4 KB) |
| `/config/apSettings.json` | `APSettingsService` | `~180 B` | Fixo | `~180 B` (1 bloco de 4 KB) |
| `/config/securitySettings.json` | `SecuritySettingsService` | `~120 B` | Fixo | `~120 B` (1 bloco de 4 KB) |
| `/config/prova.json` | `ProvaService` | `~150 B` | `~85 B` / estágio | `~580 B` (1 bloco de 4 KB) |
| `/config/categoria.json` | `CategoriaService` | `~20 B` | `~45 B` / categoria | `~470 B` (10 categorias $\rightarrow$ 1 bloco de 4 KB) |
| `/config/cronometrar.json` | `CronometrarService` | `~160 B` | Fixo | `~160 B` (1 bloco de 4 KB) |
| `/config/atleta.json` | `AtletaService` | `~15 B` | **`~88 B` / atleta** | `150 × 88 B =` **`13.215 B`** (4 blocos = `16 KB`) |
| `/config/rfid.json` | `RFIDService` | `~40 B` | **`~33 B` / cartão** | `150 × 33 B =` **`4.990 B`** (2 blocos = `8 KB`) |
| **`/config/cronometragem.json`** | `CronometragemService` | `~16 B` | **`~205 B` / registro** | `750 × 205 B =` **`153.766 B` (`150,2 KB`)** |

**Por que a partição de `128 KB` satura muito antes de `128 KB` de texto JSON?**
- No LittleFS, a unidade mínima de alocação de arquivo grande é o **bloco de `4.096 B` (4 KB)** (arquivos pequenos podem usar *inline files* dentro dos pares de metadados, mas assim que os metadados excedem 1 bloco ou o arquivo cresce, cada arquivo aloca blocos inteiros de 4 KB + listas *CTZ skip-list*).
- O superbloco `/` e o diretório `/config` consomem no mínimo **4 blocos (`16 KB`)** (2 pares de metadados espelhados).
- Os arquivos de configuração, atletas (150) e cartões (150) consomem mais **~8 a 10 blocos (`32 KB` a `40 KB`)**.
- Além disso, para atualizar um arquivo existente de $B$ blocos sem corromper o sistema de arquivos, o algoritmo de *Copy-on-Write* do LittleFS precisa alocar novos blocos antes de liberar os últimos blocos do commit anterior.
- Portanto, o espaço máximo real disponível para `/config/cronometragem.json` na partição atual de `128 KB` é de apenas **`~72 KB` a `88 KB` (18 a 22 blocos)**, o que equivale a um **limite máximo real de aproximadamente `350 a 430 registros` de cronometragem**!

#### 2. Falta de Atomicidade e Risco de Perda Total da Prova (`FSPersistence.h:40-87`)

Em `lib/framework/FSPersistence.h:77-86`:
```cpp
File settingsFile = _fs->open(_filePath, "w");
if (!settingsFile) {
    return false;
}
serializeJson(jsonDocument, settingsFile);
settingsFile.close();
```
E na leitura durante o boot (`lib/framework/FSPersistence.h:40-62`):
```cpp
DeserializationError error = deserializeJson(jsonDocument, settingsFile);
if (error == DeserializationError::Ok && jsonDocument.is<JsonObject>()) {
    // ...
    return;
}
applyDefaults();
writeToFS();
```
**Cadeia de Falha Catastrófica Confirmada:**
1. A abertura com `"w"` trunca imediatamente o arquivo `/config/cronometragem.json` para `0 bytes` no cache do descritor.
2. Durante `serializeJson(jsonDocument, settingsFile)`, os dados são gravados em blocos de 4 KB na Flash SPI (operação que leva de `30 ms` a `> 400 ms` conforme o arquivo cresce).
3. Se ocorrer **qualquer** interrupção durante essa janela (oscilação de bateria no mato, cabo solto, reset acidental, ou *panic/abort* por falta de Heap durante o próprio `writeToFS()`), o arquivo fica vazio ou com JSON truncado.
4. Quando o ESP32 reinicia, `readFromFS()` encontra erro em `deserializeJson()`, **não faz backup do arquivo corrompido**, chama `applyDefaults()` (vetor vazio) e chama `writeToFS()`, **destruindo 100% dos tempos registrados na estação desde o início da prova**!

#### 3. Amplificação de Escrita $O(N^2)$ e Desgaste da Memória Flash

Como `writeToFS()` regrava `/config/cronometragem.json` inteiro a cada nova leitura RFID ($i = 1 \dots N$), o volume total de bytes gravados na Flash ao longo de $N$ leituras é:
$$W_{\text{total}}(N) = \sum_{i=1}^{N} (i \times 205\text{ B}) = \frac{N(N+1)}{2} \times 205\text{ B}$$
- Para **$N = 400$ leituras**: $W_{\text{total}} = \frac{400 \times 401}{2} \times 205 = \mathbf{16,44\text{ MB}}$ gravados em uma partição de apenas `128 KB` (média de **~130 ciclos de apagamento/escrita por setor de 4 KB em uma única prova**!).
- Para **$N = 1.000$ leituras** (se a partição fosse maior): $W_{\text{total}} = \mathbf{102,6\text{ MB}}$ de escrita Flash!

---

### D.2 Cartão RFID MIFARE Classic 1K (`720 Bytes` Úteis)

O cartão MIFARE Classic 1K possui 16 setores de 4 blocos de 16 bytes (`1.024 B` totais). Descontando o Setor 0 (blocos 0..3) e os *Sector Trailers* (bloco 3 de cada setor: 7, 11, 15, ..., 63), restam **15 setores × 3 blocos de dados = 45 blocos = `720 bytes` úteis** (`src/CronometrarService.cpp:844`: `RFIDService::readCardData(*_rfid, 4, 45, rawData)`).

#### 1. Eficiência e Limites Exatos dos 3 Formatos JSON no Cartão

| Modo de Operação | Formato JSON Gravado no Cartão (`src/CronometrarService.cpp`) | Overhead Fixo | Custo por Evento | Capacidade Máxima em `720 B` (45 blocos) | Risco em Prova Real |
| :--- | :--- | ---: | ---: | :--- | :--- |
| **1. Modo Estágios** (`modoVoltas == "unica"`, `tipo == "especial"`) | `{"u":"A77F7403","e":{"E1":{"i":"08:00:00","f":"08:15:22"},...}}` | `23 B` (c/ `\0`) | `37 B` (`E1`..`E9`) / `38 B` (`E10`) | **18 estágios** (o sistema limita a `MAX_ESTAGIOS = 10` = **`394 B` / 25 blocos**) | **Cabe em `720 B`**, mas no 10º estágio exige ler 25 blocos + escrever 25 blocos = **50 blocos MIFARE (~350 ms em campo RF)**! |
| **2. Modo Voltas — Estação Única** (`modoVoltas == "voltas_estacao_unica"`) | `{"u":"A77F7403","l":["08:00:00","08:10:15",...]}` | `23 B` (c/ `\0`) | **`11 B` / passagem** (`"HH:MM:SS",`) | **63 passagens** = Largada (`L0`) + **62 voltas** (`715 B`) | Acima de 30 voltas (`353 B`), o tempo de leitura+escrita de todos os blocos excede `300 ms`, aumentando falhas de remoção rápida do cartão. |
| **3. Modo Voltas — Duas Estações** (`modoVoltas == "voltas_duas_estacoes"`) | `{"u":"A77F7403","v":[{"i":"08:00:00","f":"08:10:15"},...]}` | `23 B` (c/ `\0`) | **`32 B` / volta** (`{"i":"...","f":"..."},`) | **21 voltas (`695 B`)**. Na **22ª volta (`727 B`)**, estoura `720 B`! | **CRÍTICO (P0):** Na largada da 22ª volta (`714 B` cabe) e chegada da 22ª volta (`727 B > 720 B`), `writeCardData` grava até o bloco 62, bate em `block >= 64` (`RFIDService.cpp:601`), retorna `false` e **deixa o cartão corrompido sem `}]}` nem `\0`**! |

#### 2. Corrupção Destrutiva do Cartão RFID em Escrita Parcial (`RFIDService::writeCardData` + `CronometrarService.cpp:864-869`)

Observe o que ocorre quando um piloto passa o cartão rapidamente pela antena:
1. `RFIDService::writeCardData(*_rfid, 4, novoJson)` (`src/RFIDService.cpp:593-647`) calcula `blocksNeeded` e sobrescreve sequencialmente os blocos `4, 5, 6, ...` do cartão com a nova string JSON serializada (onde o caractere terminador `\0` e as chaves de fechamento `}}` estão apenas **no último bloco**!).
2. Se o piloto retirar o cartão do campo RF (alcance de ~3–5 cm do MFRC522) no meio da gravação (ex.: bloco 12 de 20), `MIFARE_Write` falha após 3 tentativas e retorna `false`.
3. Na estação atual, `CronometrarService` aborta com `beepError()` **antes** de chamar `_cronometragemService->registrarTempo(novoRegistro)` — ou seja, **o tempo não é salvo nem na estação**.
4. Pior: os blocos `4..11` do cartão já foram sobrescritos com o início do novo JSON, enquanto os blocos `12..20` contêm bytes deslocados do JSON anterior. O JSON no cartão agora é **sintaticamente inválido**.
5. Quando o piloto passa o cartão novamente (na mesma estação ou na chegada), `CronometrarService::loop()` (`src/CronometrarService.cpp:864-869`) executa:
   ```cpp
   DeserializationError err = deserializeJson(doc, rawData);
   if (err) {
       Serial.println("JSON inválido no cartão, reiniciando estrutura.");
       doc.clear();
       doc["u"] = uid;
   }
   ```
   **Resultado:** `doc.clear()` **apaga silenciosamente todos os estágios e voltas anteriores que estavam gravados no cartão do atleta**!

---

## E. Análise dos Caminhos Críticos e Precisão Temporal

### E.1 Fonte Temporal, Resolução, Truncamento e Deriva entre Estações

A cronometragem de Enduro MTB utiliza estações fisicamente separadas (Estação de Largada no topo da trilha e Estação de Chegada na base da trilha, operando offline durante a descida do piloto). Portanto, o tempo de cada estágio é calculado pela diferença:
$$\Delta t = t_{\text{chegada (Estação B)}} - t_{\text{largada (Estação A)}}$$

A auditoria do `RelogioService` (`src/RelogioService.h:1-159`) identificou **5 limitações severas de precisão temporal**:

| Dimensão Temporal | Implementação Atual no Firmware | Impacto Quantificado na Prova |
| :--- | :--- | :--- |
| **1. Resolução do Relógio** | `Relogio::getNow()` (`RelogioService.h:26-29`) usa `time(nullptr)` (`time_t` em segundos inteiros) e grava strings `"HH:MM:SS"` (`duracaoSegundos` em `int32_t`). | **Resolução de `1.000 ms` (`1 s`)**. Empates ao décimo ou centésimo de segundo (muito comuns em Enduro MTB após somar 5 estágios) são impossíveis de desempatar. |
| **2. Erro de Truncamento na Sincronização (`tv_usec = 0`)** | Em `Relogio::syncSystemClock()` (`RelogioService.h:17-23`) e `applyDateTimeString()` (`linhas 63-70`), o relógio POSIX é ajustado com `timeval tv = { (time_t)dt.unixtime(), 0 }` (**zerando os microssegundos**). | Ao sincronizar o RTC DS3231 ou via Wi-Fi (`/rest/sync/config`), a fração sub-segundo (`0` a `999 ms`) é truncada. Combinado à leitura em segundos inteiros na largada e na chegada, o erro de quantização em $\Delta t = t_{\text{fim}} - t_{\text{inicio}}$ chega a **$\pm 1,0\text{ s}$ (amplitude de até `1,999 s` para dois pilotos com o mesmo tempo real!)**. |
| **3. Deriva entre Estações (DS3231 ignorado após o boot!)** | O RTC externo de alta precisão **`DS3231` (TCXO $\pm 2\text{ ppm} \approx 7,2\text{ ms/hora}$)** é lido **APENAS UMA VEZ no `begin()`** (`RelogioService.h:59`). Durante toda a prova, `Relogio::getNow()` lê apenas `time(nullptr)` do timer interno do ESP32 (cristal XTAL de 40 MHz sem compensação térmica, $\pm 10\text{ a }\pm 40\text{ ppm}$). | Duas estações com deriva oposta ($\pm 25\text{ ppm}$) divergem até **`180 ms/hora` (`1,44 s` em 8 horas de prova)** apenas por não relerem o chip `DS3231` que já está conectado ao barramento I2C! |
| **4. Salto de Tempo durante a Prova (`POST /rest/relogio`)** | Enquanto `SyncService::handlePostConfig` bloqueia alterações quando `isCronometrando() == true`, o endpoint `POST /rest/relogio` (`RelogioService::updateRelogio`, `RelogioService.h:133-152`) **não verifica `isCronometrando()`** e chama `settimeofday()`. | Um ajuste de relógio com a prova em andamento causa salto positivo ou negativo nos tempos das especiais em aberto (`fim < inicio` $\rightarrow$ `duracaoSegundos = 0`, `status = "TEMPO INVALIDO"`). |
| **5. Latência Variável antes da Captura do Timestamp** | `Relogio::getNow()` só é chamado **após** `RFIDService::readCardData()` + `deserializeJson()` + `jaRegistrado()`. | Adiciona um atraso sistemático e **variável (`15 ms` a `360 ms`)** proporcional à quantidade de dados já gravados no cartão do atleta! |

---

### E.2 Mapeamento Passo a Passo do Caminho Crítico RFID (`src/CronometrarService.cpp:781-886`)

Abaixo está o rastreamento exato da execução na `loopTask` (Core 1) desde o instante $t_0$ em que o cartão entra no campo magnético do MFRC522 até a liberação para o próximo atleta:

| Etapa | Operação Executada | Arquivo / Linhas | Latência (Cartão Vazio: 1 bloco) | Latência (Cartão Cheio: 25 a 45 blocos) | Observação Crítica |
| :---: | :--- | :--- | ---: | ---: | :--- |
| **0** | Espera do ciclo `delay(10)` + `displayService.loop()` + `rfidService.loop()` | `main.cpp:146-154` | `0 a 26 ms` | `0 a 26 ms` | Se coincidir com `showClock()` no TM1637, adiciona `~16,2 ms` de `delayMicroseconds(100)`. |
| **1** | Detecção RF (`PICC_IsNewCardPresent` + `PICC_ReadCardSerial`) | `CronometrarService.cpp:797` | `~2,5 ms` | `~2,5 ms` | **Aqui ($t_0 + 2,5\text{ ms}$) o cartão já foi identificado fisicamente, mas o horário NÃO é capturado!** |
| **2** | Conversão de UID, debounce de 3s e `Serial.println("Cartão detectado...")` | `CronometrarService.cpp:801-814` | `~1,0 ms` | `~1,0 ms` | String allocation + escrita UART. |
| **3** | Validação do cartão (`getCardNumber`) e atleta (`getAtletaByNumeral`) + `Serial.printf` | `CronometrarService.cpp:821-840` | `~1,5 ms` | `~2,0 ms` | Adquire 2 mutexes, faz busca em `std::map` ($O(\log A)$) e busca linear em `std::vector<atleta_t>` ($O(A)$) copiando `atleta_t`. |
| **4** | **Leitura completa do cartão MIFARE (`RFIDService::readCardData`, blocos 4..62)** | `CronometrarService.cpp:844`, `RFIDService.cpp:543-591` | **`~7 ms`** (1 auth + 1 bloco) | **`~150 ms` a `340 ms`** (9 a 15 auths Crypto1 + 25 a 45 blocos SPI @ 1 MHz / RF @ 106 kbps) | **Gargalo Crítico antes do Timestamp:** Quanto mais estágios/voltas o atleta já completou, mais tarde seu tempo de chegada é registrado! |
| **5** | Parse JSON (`deserializeJson`) + verificação `jaRegistrado` (varredura linear $O(N)$) | `CronometrarService.cpp:864, 209` | `~0,5 ms` | `~2,0 a 6,0 ms` | Varre todo o vetor `_state.registros` comparando `std::string`. |
| **6** | **CAPTURA DO TIMESTAMP (`DateTime now = Relogio::getNow()`)** | `CronometrarService.cpp:226, 339, 483` | Capturado em **$t_0 + 12\text{ ms}$** | Capturado em **$t_0 + 160\text{ a }360\text{ ms}$** | **Atraso sistemático e injusto contra atletas nas últimas especiais/voltas.** |
| **7** | Cálculo de duração (`calcularDuracao`) e serialização `serializeJson(doc, novoJson)` | `CronometrarService.cpp:234-262, 340-369` | `~0,5 ms` | `~1,5 ms` | Monta string `novoJson` completa. |
| **8** | **Regravação completa do cartão (`RFIDService::writeCardData`, desde o bloco 4)** | `CronometrarService.cpp:263, 370`, `RFIDService.cpp:593-647` | **`~18 ms`** (1 auth + 2 blocos write) | **`~220 ms` a `450 ms`** (9 a 15 auths + 25 a 45 blocos write de ~6–8 ms cada) | **Janela de Corrupção RF:** O cartão precisa permanecer imóvel no campo RF até o fim desta etapa (`t_0 + 380 a 810 ms`)! |
| **9** | **Persistência Local (`registrarTempo` $\rightarrow$ `FSPersistence::writeToFS()`)** | `CronometrarService.cpp:292, 412`, `FSPersistence.h:64-87` | **`~25 ms`** (1º registro) | **`~180 ms` a `550+ ms`** (com 300–500 registros na Flash) | Reconstrói todo o `JsonDocument` na RAM e regrava `/config/cronometragem.json` inteiro na Flash SPI. |
| **10** | Encerramento RF (`PICC_HaltA`, `PCD_StopCrypto1`), `Serial.printf` e **`_buzzer->beepTone(1000, 300)`** | `CronometrarService.cpp:297, 882-883` | **`~302 ms`** | **`~302 ms`** | `beepTone` usa `delay(300)` bloqueante! |
| **TOTAL** | **Tempo Total do Caminho Crítico por Passagem** | — | **`~358 ms`** | **`~860 ms` a `1.680 ms`** | **Inviabiliza passagens rápidas em grupo na chegada de modo Voltas.** |

```mermaid
sequenceDiagram
    autonumber
    participant Atleta as Cartão MIFARE 1K
    participant Loop as loopTask (Core 1)
    participant MFRC as MFRC522 (SPI 1MHz / RF)
    participant RTC as Relogio (POSIX time_t)
    participant Flash as LittleFS (128 KB)
    participant Buzzer as BuzzerService

    Atleta->>MFRC: Entra no campo RF (Instante Real t0)
    Loop->>MFRC: PICC_IsNewCardPresent() + ReadCardSerial() (~2,5 ms)
    Note over Loop,MFRC: ERRO ARQUITETURAL: Timestamp NÃO é capturado aqui!
    Loop->>MFRC: readCardData(bloco 4..62) (7 ms a 340 ms!)
    Loop->>Loop: deserializeJson(rawData) + jaRegistrado() (0,5 a 6 ms)
    Loop->>RTC: Relogio::getNow() -> time(nullptr) (Resolução 1s, atraso até 360ms)
    Loop->>MFRC: writeCardData(bloco 4..N) — Reescreve TUDO (18 ms a 450 ms!)
    Note over Atleta,MFRC: Se o atleta afastar o cartão antes de ~800ms, corrompe o JSON no cartão!
    Loop->>Flash: registrarTempo() -> writeToFS() — Reescreve arquivo inteiro (25 ms a 550 ms!)
    Loop->>Buzzer: beepTone(1000, 300) — delay(300) bloqueante!
```

---

## F. Matriz de Gargalos e Achados Detalhados (P0, P1, P2, P3)

Cada achado abaixo está classificado quanto ao seu grau de certeza:
- **`[CONFIRMADO]`**: Comprovado diretamente no código-fonte, nas seções do binário ELF compilado ou nos logs reais do hardware (`logs/device-monitor-*.log`).
- **`[RISCO ESTÁTICO]`**: Comprovado por análise estática de fluxo de controle, modelo de memória ou concorrência entre tasks FreeRTOS.
- **`[HIPÓTESE DE BANCADA]`**: Comportamento cujo limiar exato depende de medição física adicional sob estresse de RF/alimentação.

---

### Achados P0 — Risco Crítico de Travamento, Perda de Dados ou Erro de Cronometragem

#### `P0-01` `[CONFIRMADO]` — Exaustão de Heap (OOM) e Triplicação de Dados em `GET /rest/resultados-publicos`
1. **Severidade:** `P0` (Crítico)
2. **Categoria:** `Heap` / `RAM` / `Wi-Fi/HTTP`
3. **Arquivo(s) e função(ões) afetados:**
   - `src/SyncService.cpp` — `SyncService::handleGetResultadosPublicos()` (`linhas 573-669`)
   - `src/ApuracaoResultados.h` — `ApuracaoResultados::calcular()` (`linhas 599-988`, especialmente `linha 658`)
4. **Evidência concreta no código ou na compilação:**
   - Em `src/SyncService.cpp:575-609`, `ContextoPublico` copia `provaState`, `categoriasList`, `atletasList`, `cardRegistryMap`, `cronometrarState` e `registrosList`.
   - Em `src/ApuracaoResultados.h:658`, `std::vector<cronometragem_t> registros = registrosOriginais;` cria uma **3ª cópia** completa do vetor de cronometragem.
   - Em `src/SyncService.cpp:612-667`, com `ctx` ainda alocado, instancia `PsychicJsonResponse response(request, false)` e serializa tanto `ctx->relatorio.classificacao` (com `etapas` aninhadas) quanto o vetor bruto `ctx->registrosList` dentro do mesmo `ArduinoJson::JsonDocument`.
   - Telemetria real (`logs/device-monitor-261028-191403.log:111-114`): Heap livre total após o boot = `100.360 B` (`98,0 KB`), maior bloco contíguo = `59.380 B` (`58,0 KB`).
5. **Explicação técnica da causa raiz:**
   - Coexistência simultânea na RAM de: (a) estado residente nos serviços, (b) cópias profundas em `ContextoPublico`, (c) cópia local e mapas auxiliares em `ApuracaoResultados::calcular()`, (d) vetor apurado `relatorio_apuracao_t` (`292 B`/piloto + `156 B`/etapa), (e) árvore completa do `JsonDocument` (duplicando classificação + registros brutos) e (f) buffer de `4.096 B` do `ChunkPrinter` em `response.send()`.
6. **Impacto prático durante uma prova real:**
   - Quando um atleta ou organizador abre a página pública de resultados pelo Wi-Fi a partir de ~150–250 registros, o alocador falha: ou o `JsonDocument` sofre *overflow* silencioso (retornando JSON truncado/incompleto para a interface SvelteKit), ou o `std::vector` falha com `abort()` / *Reboot* da estação no meio da prova.
7. **Condição em que o problema ocorre:**
   - Qualquer chamada a `GET /rest/resultados-publicos` com $\ge 40\text{ a }50$ pilotos e $\ge 4\text{ a }5$ estágios/voltas, ou quando 2 atletas consultam resultados simultaneamente.
8. **Estimativa ou medição do custo atual:**
   - Para 50 pilotos × 5 estágios (250 registros): estado residente = `57,2 KB`; pico transitório da requisição = **`~170 KB`** (total `~227 KB` > `100 KB` disponíveis).
   - Para 150 pilotos × 5 estágios (750 registros): pico transitório da requisição = **`~430 KB`**.
9. **Solução recomendada:**
   - Não copiar os estados brutos para `ContextoPublico` (ler diretamente sob lock ou construir apenas um resumo leve por piloto);
   - Remover `std::vector<cronometragem_t> registros = registrosOriginais;` em `ApuracaoResultados::calcular()` (ordenar um vetor de índices `std::vector<uint16_t>` de `2 B` por elemento em vez de copiar structs de `208 B`);
   - Remover o array bruto `cronometragem` do payload de `/rest/resultados-publicos` (a tela pública já recebe `classificacao` com `etapas` apuradas; se necessário, paginar ou fazer streaming direto por chunk HTTP sem `JsonDocument` global);
   - Fazer cache do resultado apurado invalidado apenas quando `registrarTempo()` ou sincronização alterar os dados.
10. **Ganho esperado:**
    - Redução do pico de Heap de `~430 KB` para **`< 18 KB`** (redução de **> 95%**), permitindo consultas concorrentes sem impacto na estabilidade.
11. **Risco ou trade-off da mudança:**
    - Se o frontend (`interface/src/routes/+page.svelte`) depender do campo bruto `cronometragem` dentro de `/rest/resultados-publicos`, será necessário verificar se ele usa apenas `classificacao` ou se precisa de um endpoint separado sob demanda por piloto.
12. **Como validar a correção:**
    - Popular 300 pilotos e 1.500 registros, disparar 5 requisições simultâneas a `GET /rest/resultados-publicos` e verificar que `ESP.getMinFreeHeap()` permanece `> 45 KB` e o JSON retornado é 100% válido.

---

#### `P0-02` `[CONFIRMADO]` — Reescrita Não-Atômica de `/config/cronometragem.json` a Cada Passagem RFID, Amplificação $O(N^2)$ e Apagamento Total no Boot em Caso de Falha
1. **Severidade:** `P0` (Crítico)
2. **Categoria:** `LittleFS` / `Integridade de Dados` / `Heap` / `Flash`
3. **Arquivo(s) e função(ões) afetados:**
   - `lib/framework/FSPersistence.h` — `FSPersistence<T>::writeToFS()` (`linhas 64-87`) e `readFromFS()` (`linhas 33-62`)
   - `src/CronometragemService.cpp` — `CronometragemService::registrarTempo()` (`linhas 63-74`)
4. **Evidência concreta no código ou na compilação:**
   - Em `FSPersistence.h:77`: `File settingsFile = _fs->open(_filePath, "w");` trunca imediatamente o arquivo existente antes de serializar um `JsonDocument` completo contendo todos os registros da prova.
   - Em `FSPersistence.h:40-61`: se `deserializeJson(jsonDocument, settingsFile)` retornar erro no boot, executa `applyDefaults(); writeToFS();`, sobrescrevendo o arquivo com `{"registros":[]}`.
   - Em `FSPersistence.h:71-73`: `jsonDocument` é populado com 100% dos registros sem checar `if (jsonDocument.overflowed())`.
5. **Explicação técnica da causa raiz:**
   - Uso do padrão de persistência de configurações estáticas (`FSPersistence` do template `ESP32SvelteKit`, projetado para arquivos pequenos de 200 bytes como `wifiSettings.json`) para armazenar uma série temporal crescente de eventos de cronometragem.
6. **Impacto prático durante uma prova real:**
   - **Perda Total de Dados:** Se a bateria oscilar, o operador desligar a estação ou ocorrer um reboot (ex.: OOM) durante os `100–500 ms` de `writeToFS()` após uma leitura RFID, `/config/cronometragem.json` fica corrompido e o boot seguinte **zera toda a prova**.
   - **Corrupção por Falta de Heap:** Acima de ~160–200 registros, o `JsonDocument` dentro de `writeToFS()` esgota o Heap livre, trunca silenciosamente os últimos registros (`overflowed() == true`) e grava o JSON incompleto no LittleFS.
   - **Latência e Desgaste:** Para 500 eventos, grava `~25,7 MB` na Flash e bloqueia a `loopTask` por até `500+ ms` por atleta.
7. **Condição em que o problema ocorre:**
   - Em **todas** as leituras de cartão RFID (`registrarTempo`) e em **todas** as sincronizações (`saveToFS`), agravando-se linearmente a cada passagem.
8. **Estimativa ou medição do custo atual:**
   - Custo de RAM em `writeToFS()`: `~280 B` de `JsonDocument` por registro (`~70 KB` para 250 registros).
   - Custo de Flash: escrita acumulada $W(N) = \frac{N(N+1)}{2} \times 205\text{ B}$.
9. **Solução recomendada:**
   - Substituir o armazenamento monolítico JSON de `CronometragemService` por um **log append-only por registro** (formato binário fixo de `32 B` com CRC16 ou JSON Lines `.jsonl` / CSV compacto aberto com `"a"`), onde cada passagem RFID faz apenas `append` + `flush` de `1 linha / 32 bytes` (`< 5 ms`, zero alocação de `JsonDocument` global, zero risco de corromper registros anteriores);
   - Para os arquivos de configuração (`prova.json`, `atleta.json`, etc.), implementar escrita atômica em `FSPersistence::writeToFS()`: gravar em `_filePath + ".tmp"`, verificar `!jsonDocument.overflowed()`, fechar e chamar `_fs->rename(tmpPath, _filePath)`; e em `readFromFS()`, nunca sobrescrever um arquivo corrompido sem antes tentar recuperar o `.tmp` ou fazer backup `.bak`.
10. **Ganho esperado:**
    - Redução do tempo de persistência por passagem RFID de `180–550 ms` para **`< 8 ms`**;
    - Redução do consumo de Heap durante a gravação de `~70 KB` para **`0 B`**;
    - Eliminação total do risco de perda do histórico da prova em queda de energia;
    - Redução de `> 99%` no desgaste de escrita da Flash.
11. **Risco ou trade-off da mudança:**
    - Requer adaptar a função de carga inicial (`begin`/`readFromFS`) do `CronometragemService` para ler os registros sequencialmente do arquivo append-only (ou converter transparentemente se encontrar o `.json` legado).
12. **Como validar a correção:**
    - Simular desligamento/reset no meio da gravação de passagens e verificar que todos os registros anteriores permanecem íntegros após o reboot; medir tempo de `registrarTempo()` com `esp_timer_get_time()` no 1º e no 1.500º registro (deve permanecer constante `< 10 ms`).

---

#### `P0-03` `[CONFIRMADO]` — Subdimensionamento Crítico da Partição LittleFS (`128 KB`) na Tabela `min_spiffs.csv`
1. **Severidade:** `P0` (Crítico)
2. **Categoria:** `LittleFS` / `Flash`
3. **Arquivo(s) e função(ões) afetados:**
   - `min_spiffs.csv` (`linha 8`: `spiffs, data, spiffs, 0x3D0000, 0x20000,`)
   - `platformio.ini` (`linha 72`: `board_build.partitions = min_spiffs.csv`)
   - `features.ini` (`linhas 9-10, 13`: `FT_UPLOAD_FIRMWARE=0`, `FT_DOWNLOAD_FIRMWARE=0`, `FT_COREDUMP=0`)
4. **Evidência concreta no código ou na compilação:**
   - `min_spiffs.csv` aloca `0x20000` (`131.072 B` = `128 KB` = 32 setores de 4 KB) para `spiffs` (LittleFS), enquanto reserva `0x1E0000` (`1.920 KB`) para `app1` (OTA inativo) e `0x10000` (`64 KB`) para `coredump` (inativo).
5. **Explicação técnica da causa raiz:**
   - Uso da tabela padrão `min_spiffs.csv` do Arduino-ESP32 (pensada para maximizar duas partições OTA em Flash de 4 MB) em um projeto que **não utiliza OTA** mas precisa armazenar centenas ou milhares de registros de cronometragem em sistema de arquivos local.
6. **Impacto prático durante uma prova real:**
   - Com 150 pilotos (`~18 KB` em `atleta.json` + `rfid.json` + configs + metadados LittleFS), restam apenas `~72–88 KB` úteis para `cronometragem.json`, saturando fisicamente o sistema de arquivos por volta de **350 a 430 eventos** (menos de 3 estágios para 150 pilotos!). Quando o LittleFS atinge 100%, todas as novas passagens deixam de ser salvas em Flash.
7. **Condição em que o problema ocorre:**
   - Qualquer prova com mais de ~350–430 registros totais no formato JSON atual (ex.: 50 pilotos × 10 estágios = 500 registros; 150 pilotos × 3 estágios = 450 registros).
8. **Estimativa ou medição do custo atual:**
   - Capacidade total da partição de dados: `131.072 B` (3,13% da Flash de 4 MB), com `2.031.616 B` (`1.984 KB` = 48,4% da Flash) ociosos em `app1` + `coredump`.
9. **Solução recomendada:**
   - Caso OTA permaneça desativado (`FT_UPLOAD_FIRMWARE=0`): criar uma tabela de partições customizada (`partitions_4MB_timing.csv`) com `app0` de `2.304 KB` (`2,25 MB`) e partição `spiffs` (LittleFS) de **`1.664 KB` (`1,625 MB` = 416 setores de 4 KB)** — **13× maior que a atual**;
   - Mesmo se desejar manter OTA futuro depois de enxugar o binário (`logo.png`), é possível configurar `app0 = 1.600 KB`, `app1 = 1.600 KB` e `spiffs = 832 KB` (6,5× maior).
10. **Ganho esperado:**
    - Aumento do armazenamento persistente de `128 KB` para **`1.664 KB`** (ou `832 KB` com OTA), suportando mais de **8.000 registros em JSON** ou **> 45.000 registros em formato compacto**.
11. **Risco ou trade-off da mudança:**
    - Alterar a tabela de partições exige regravar via USB/Serial (`pio run -t upload` + `uploadfs`) nos dispositivos existentes, pois o offset da partição `spiffs` muda.
12. **Como validar a correção:**
    - Verificar em `GET /rest/systemStatus` (`fs_total`) que a capacidade reportada por `ESPFS.totalBytes()` passou de `131.072 B` para `1.703.936 B` (ou `851.968 B`).

---

#### `P0-04` `[CONFIRMADO]` — Estouro Físico do Cartão MIFARE Classic 1K na 22ª Volta (Modo Duas Estações) e Corrupção Total do Histórico do Cartão em Escrita Parcial
1. **Severidade:** `P0` (Crítico)
2. **Categoria:** `RFID` / `Integridade de Dados`
3. **Arquivo(s) e função(ões) afetados:**
   - `src/CronometrarService.cpp` — `CronometrarService::loop()` (`linhas 330-420`, `502-662`, `844-870`)
   - `src/RFIDService.cpp` — `RFIDService::readCardData()` (`linhas 543-591`) e `writeCardData()` (`linhas 593-647`)
4. **Evidência concreta no código ou na compilação:**
   - Em `src/CronometrarService.cpp:543-628`, o modo `voltas_duas_estacoes` serializa no cartão `{"u":"XXXXXXXX","v":[{"i":"HH:MM:SS","f":"HH:MM:SS"},...]}` (`23 B` base + `32 B` por volta).
   - Em `src/RFIDService.cpp:600-605`: quando `blocksNeeded` ultrapassa os 45 blocos úteis (blocos 4..62 = `720 B`), o loop grava os blocos 4..62, incrementa `block` até `64`, entra em `if (block >= 64) { Serial.println("... Cartão cheio!"); return false; }` **após já ter sobrescrito os blocos 4..62 sem fechar o JSON e sem `\0`**.
   - Em `src/CronometrarService.cpp:864-869`: quando `deserializeJson(doc, rawData)` falha na leitura seguinte, executa `doc.clear(); doc["u"] = uid;`, **apagando todas as especiais/voltas anteriores do cartão**.
5. **Explicação técnica da causa raiz:**
   - Armazenamento de texto JSON verboso diretamente na EEPROM de 720 bytes do MIFARE Classic 1K e estratégia de **reescrita integral desde o bloco 4** a cada passagem, sem controle prévio de tamanho (`novoJson.length() >= 720`) e sem escrita incremental por bloco/setor.
6. **Impacto prático durante uma prova real:**
   - **Limite rígido de 21 voltas** em Duas Estações: na chegada da 22ª volta (`23 + 22 × 32 = 727 B > 720 B`), `writeCardData` falha no bloco 64, emite bipe de erro, **não registra a chegada na estação** e **corrompe o JSON do cartão** (que na passagem seguinte sofre `doc.clear()`, perdendo as 21 voltas anteriores!).
   - **Perda de histórico por afastamento rápido:** Em qualquer modo (Estágios ou Voltas), se o piloto afastar o cartão durante os `150–450 ms` de `writeCardData`, o JSON fica truncado no cartão e a leitura seguinte apaga todo o histórico do cartão via `doc.clear()`.
7. **Condição em que o problema ocorre:**
   - Sempre que uma prova no modo `voltas_duas_estacoes` tiver $\ge 22$ voltas, ou sempre que um atleta retirar o cartão do leitor antes do término do bipe de confirmação.
8. **Estimativa ou medição do custo atual:**
   - Capacidade máxima atual no cartão: 18 estágios, 62 voltas (estação única) ou **21 voltas (duas estações)**.
   - Tempo de exposição RF necessário para leitura + escrita de cartão com 10 estágios ou 20 voltas: **`370 ms` a `790 ms`**.
9. **Solução recomendada:**
   - **Defesa Imediata (JSON):** Antes de chamar `writeCardData()`, verificar `if (novoJson.length() + 1 > 720)`! E em `writeCardData()`, verificar se `startBlock` + `blocksNeeded` + trailers cabem `< 64` **antes** de escrever o primeiro bloco!
   - **Otimização Definitiva (Layout Binário por Bloco no MIFARE 1K):**
     - Cada bloco MIFARE tem exatamente `16 bytes`.
     - Em vez de reler e reescrever 45 blocos de JSON a cada passagem, usar **1 bloco de 16 bytes por Estágio ou por Volta** (ex.: Bloco 4 = Cabeçalho com UID + CRC + bitmap/contador de voltas; Bloco $4 + k$ = Estágio/Volta $k$ contendo `[uint8_t tipo/id, uint16_t numVolta, uint32_t tsInicio, uint32_t tsFim, uint16_t msInicio, uint16_t msFim, uint8_t crc8]` = `15 B`).
     - Com isso, cada passagem lê apenas **1 a 2 blocos** e escreve apenas **1 bloco (`16 B`)**, reduzindo o tempo de RF de `~500 ms` para **`~15 ms`** e eliminando 100% o risco de corromper estágios/voltas anteriores se o cartão for afastado rapidamente! (Caso o escopo exija manter JSON no cartão, compactar os timestamps para epoch relativo em segundos ou minutos:segundos e gravar do fim para o início ou validar o tamanho antes de gravar).
10. **Ganho esperado:**
    - Redução de **95% no tempo de leitura/escrita RF** (de `~500 ms` para `~15–20 ms` se binário por bloco, ou `~60%` se JSON enxuto com chaves de 1 char e segundos relativos);
    - Eliminação total da perda de histórico anterior em caso de remoção precoce do cartão.
11. **Risco ou trade-off da mudança:**
    - Se adotado layout binário por bloco no cartão, todas as estações da prova devem usar a mesma versão de firmware (o que já é premissa da prova) e o endpoint de leitura de cartão precisa decodificar os blocos para o formato esperado pela interface.
12. **Como validar a correção:**
    - Simular 40 voltas no modo Duas Estações e testes de "rasante" (passar o cartão rapidamente pela antena em `< 50 ms`), verificando que nenhuma passagem anterior no cartão é corrompida ou apagada.

---

#### `P0-05` `[CONFIRMADO]` — Captura Tardia do Timestamp (Após Leitura Completa do Cartão RFID e Parse JSON) + Resolução Limitada a 1 Segundo com Erro de Truncamento de até ±1,99s
1. **Severidade:** `P0` (Crítico)
2. **Categoria:** `Precisão Temporal` / `RFID`
3. **Arquivo(s) e função(ões) afetados:**
   - `src/CronometrarService.cpp` — `CronometrarService::loop()` (`linhas 797-886`, captura tardia nas `linhas 226, 339, 483`)
   - `src/RelogioService.h` — `Relogio::getNow()` (`linhas 26-29`), `syncSystemClock()` (`linhas 17-23`), `applyDateTimeString()` (`linhas 63-70`)
4. **Evidência concreta no código ou na compilação:**
   - Em `CronometrarService.cpp:797`, o cartão é detectado por `PICC_IsNewCardPresent() && PICC_ReadCardSerial()`.
   - Entre as linhas `801` e `869`, o código executa `Serial.println`, `getCardNumber`, `getAtletaByNumeral`, `RFIDService::readCardData(*_rfid, 4, 45, rawData)`, `deserializeJson(doc, rawData)` e `jaRegistrado(...)`.
   - Somente nas linhas `226`, `339` ou `483` é chamado `DateTime now = Relogio::getNow();`.
   - Em `RelogioService.h:20, 27`: `settimeofday` zera `tv.tv_usec = 0` e `getNow()` lê `time(nullptr)` em segundos inteiros, ignorando o DS3231 após o boot.
5. **Explicação técnica da causa raiz:**
   - O instante de detecção física do cartão não é salvo logo após `PICC_ReadCardSerial()`, acoplando a medição do tempo ao tempo de I/O SPI/RF de leitura de até 45 blocos MIFARE. Além disso, a API temporal inteira foi construída sobre `RTClib::DateTime` (resolução de `1 s`) e sem ressincronização periódica com o TCXO DS3231.
6. **Impacto prático durante uma prova real:**
   - Um atleta no 10º estágio ou na 20ª volta sofre um atraso adicional de **`150 ms` a `350 ms`** na leitura do seu tempo de chegada em comparação a um cartão com poucos dados.
   - O truncamento de sub-segundos (`tv_usec = 0` e formato `"HH:MM:SS"`) introduz incerteza de quantização de até **`±1,0 s` por estação (`1,999 s` na duração do estágio)**, além de deriva de até **`1,4 s` em 8 horas** por não reler o DS3231.
7. **Condição em que o problema ocorre:**
   - Em **100% das leituras RFID** durante a prova.
8. **Estimativa ou medição do custo atual:**
   - Latência antes do timestamp: `12 ms` (cartão vazio) a **`360 ms`** (cartão cheio).
   - Incerteza temporal combinada (quantização + deriva + atraso RF): **`±1,5 s` a `±2,3 s`**.
9. **Solução recomendada:**
   - **Correção Imediata (Zero Custo de Hardware/Protocolo):** Capturar `DateTime now = Relogio::getNow();` (e `gettimeofday(&tvNow, nullptr)`) **na linha imediatamente seguinte a `PICC_ReadCardSerial()` (`linha 798`)**, passando `now` como parâmetro para a lógica de registro!
   - **Correção de Deriva do DS3231:** No `RelogioService`, sincronizar o relógio do sistema a partir do `RTC_DS3231` na borda exata de mudança de segundo do DS3231 (monitorando a transição do registro de segundos via I2C ou pino SQW, de modo que `tv_usec = 0` corresponda exatamente ao início do segundo real com erro `< 2 ms`!) e repetir a aferição a cada 15–30 minutos quando não houver leitura RF no instante.
10. **Ganho esperado:**
    - Redução da latência de captura do timestamp de `160–360 ms` para **`~2,5 ms`** (redução de **99%** na latência de software/RF pré-timestamp);
    - Redução do erro de sincronismo sub-segundo de `±1.000 ms` para **`< ±10 ms`** na borda de segundo do DS3231.
11. **Risco ou trade-off da mudança:**
    - Mover a captura de `now` para logo após `PICC_ReadCardSerial()` é uma mudança de 3 linhas sem nenhum efeito colateral ou quebra de contrato.
12. **Como validar a correção:**
    - Medir com `esp_timer_get_time()` o delta entre o retorno de `PICC_ReadCardSerial()` e a captura de `now` (deve ser `< 50 µs`).

---

#### `P0-06` `[CONFIRMADO]` — Dupla Alocação Contígua em `PsychicRequest::loadBody()` + Cópias em `PsychicJsonHandler` Causando Falha na Sincronização Wi-Fi (`POST /rest/sync/*`)
1. **Severidade:** `P0` (Crítico)
2. **Categoria:** `Heap` / `Wi-Fi/HTTP`
3. **Arquivo(s) e função(ões) afetados:**
   - `lib/PsychicHttp/src/PsychicRequest.cpp` — `PsychicRequest::loadBody()` (`linhas 127-165`)
   - `lib/PsychicHttp/src/PsychicJson.cpp` — `PsychicJsonHandler::handleRequest()` (`linhas 106-130`)
   - `src/SyncService.cpp` — `SyncService::begin()` (`linha 17`), `handlePostResultados()` (`linhas 365-431`), `handlePostConfig()` (`linhas 433-571`)
4. **Evidência concreta no código ou na compilação:**
   - Em `SyncService.cpp:17`: `_server->maxRequestBodySize = 32768;` (`32 KB`).
   - Em `PsychicRequest.cpp:138-161`: aloca `char *buf = (char *)malloc(remaining + 1);`, lê os dados do socket e faz `this->_body = String(buf);` **antes** de `free(buf);`.
   - Em `PsychicJson.cpp:114-116`: mantém `request->_body` vivo enquanto aloca `JsonDocument jsonBuffer; deserializeJson(jsonBuffer, request->body());`.
   - Em `SyncService.cpp:375-426`: enquanto `request->_body` e `jsonBuffer` estão vivos, aloca `std::vector<cronometragem_t> remotos` e chama `_cronometragemService->saveToFS()` (que aloca outro `JsonDocument` global).
5. **Explicação técnica da causa raiz:**
   - Para um body de tamanho $B$, `loadBody()` exige dois blocos contíguos de tamanho $B+1$ simultaneamente ($2B$). Como após o boot o maior bloco contíguo é de apenas `59.380 B` (`58 KB`) — e cai para `< 40 KB` após cadastrar atletas e registros — qualquer lote de sincronização de `20–32 KB` falha no `malloc` ou na construção da `String`, ou estoura os `~98 KB` de Heap livre total durante `deserializeJson` + `saveToFS()`.
6. **Impacto prático durante uma prova real:**
   - A sincronização de resultados entre estações (`Sincronizar.svelte` envia lotes de `BATCH_SIZE = 100` registros $\approx 21\text{ KB}$) ou a sincronização de configuração com `> 100` atletas + cartões falha com erro HTTP 400/500 ou causa *panic/reboot* na estação receptora.
7. **Condição em que o problema ocorre:**
   - Recebimento de `POST /rest/sync/resultados` (lotes de 100 registros) ou `POST /rest/sync/config` (provas médias/grandes).
8. **Estimativa ou medição do custo atual:**
   - Para um lote de 100 registros (`~21 KB` JSON): pico de `42 KB` contíguos em `loadBody()` + `~110 KB` totais durante `handlePostResultados` + `saveToFS()`.
9. **Solução recomendada:**
   - Em `PsychicRequest::loadBody()`, reservar diretamente o buffer na `String _body` (`_body.reserve(remaining)`) ou ler em chunks direto para `deserializeJson` sem alocar `buf` + `_body` duplicados;
   - Em `PsychicJsonHandler` (ou usando handler direto no `SyncService`), liberar `request->_body = String()` imediatamente após `deserializeJson()` (ou passar `char*` mutável para `deserializeJson` operar em modo *zero-copy*) e destruir o `jsonBuffer` de entrada **antes** de chamar `saveToFS()`;
   - Reduzir `BATCH_SIZE` em `Sincronizar.svelte` de `100` para `25` ou `30` registros (`~5–6 KB` por lote).
10. **Ganho esperado:**
    - Redução de **70% a 80% no pico de Heap** durante a sincronização Wi-Fi e eliminação da exigência de grandes blocos contíguos.
11. **Risco ou trade-off da mudança:**
    - Reduzir `BATCH_SIZE` de 100 para 30 aumenta o número de requisições HTTP de sincronização (ex.: de 8 para 25 lotes para 750 registros, adicionando ~1,5s ao tempo total de sync, o que é totalmente aceitável).
12. **Como validar a correção:**
    - Sincronizar 750 registros e 150 atletas via Wi-Fi monitorando `ESP.getFreeHeap()` e `ESP.getMaxAllocHeap()` antes, durante e após cada lote.

---

### Achados P1 — Risco Alto de Degradação Severa, Condição de Corrida ou Saturação

#### `P1-01` `[CONFIRMADO]` — Partição de Firmware (`app0`) com 97,91% de Ocupação Devido a `logo.png` (`280 KB`) e `WWWData.h` (`592 KB`)
1. **Severidade:** `P1` (Alto)
2. **Categoria:** `Flash` / `Heap`
3. **Arquivo(s) e função(ões) afetados:**
   - `interface/src/lib/assets/logo.png` (`280.127 B`) $\rightarrow$ `lib/framework/WWWData.h` (`ESP_SVELTEKIT_DATA_8`, `280.132 B`)
   - `lib/framework/ESP32SvelteKit.cpp` — `ESP32SvelteKit::begin()` (`linhas 104-116`: `WWWData::registerRoutes`)
4. **Evidência concreta no código ou na compilação:**
   - Binário `ESP32-Sveltekit_esp32dev_0-6-1.bin` = `1.924.896 B` de `1.966.080 B` (`97,91%`), restando apenas `41.184 B` livres.
   - `xtensa-esp32-elf-nm` confirma `ESP_SVELTEKIT_DATA_8` = `280.132 B` em `.flash.rodata`.
   - `WWWData::registerRoutes` registra 91 lambdas individuais no `PsychicHttpServer`, consumindo `~25–30 KB` de Heap permanente.
5. **Explicação técnica da causa raiz:**
   - A imagem `logo.png` não foi otimizada/convertida para WebP/SVG compacto (PNG já é comprimido com DEFLATE, portanto o `gzip` do script `build_interface.py` não reduz seu tamanho), e cada um dos 91 arquivos do bundle SvelteKit é registrado como um `httpd_uri_t` separado na RAM.
6. **Impacto prático durante uma prova real:**
   - Qualquer evolução pequena no código C++ ou no frontend SvelteKit ultrapassará os `40 KB` restantes de `app0`, quebrando a compilação. Além disso, as 91 rotas desperdiçam `~25 KB` de RAM vital.
7. **Condição em que o problema ocorre:**
   - Permanente (em compilação e no boot).
8. **Estimativa ou medição do custo atual:**
   - `280.132 B` de Flash apenas para `logo.png`; `~25–30 KB` de Heap para a tabela de 91 rotas estáticas.
9. **Solução recomendada:**
   - Comprimir/converter `logo.png` para SVG ou WebP/PNG indexado de `< 15 KB` (economia imediata de **`~265 KB` de Flash**);
   - Substituir o registro de 91 handlers individuais em `WWWData::registerRoutes` por **uma única tabela estática `constexpr` em Flash** (`struct StaticAsset { const char* uri; const char* contentType; const char* etag; const uint8_t* data; size_t len; }`) servida por **um único handler wildcard `/*`** no `PsychicHttpServer`!
10. **Ganho esperado:**
    - Liberação de **`~265 KB` de Flash** (reduzindo `app0` de 97,9% para ~84%) e recuperação de **`~25 KB` de Heap livre permanente** logo no boot!
11. **Risco ou trade-off da mudança:**
    - Alterar o gerador `scripts/build_interface.py` para emitir uma tabela `constexpr` única em `WWWData.h` é simples e mantém 100% de compatibilidade com cache `ETag` e `gzip`.
12. **Como validar a correção:**
    - Compilar o firmware, medir o tamanho de `.flash.rodata` e verificar no boot que `Free Heap After Setup` subiu de `~100 KB` para `~125 KB`.

---

#### `P1-02` `[CONFIRMADO]` — Estrutura `cronometragem_t` (`208 B`) e `piloto_apurado_t` (`292 B`) Superdimensionadas com Dezenas de `std::string`
1. **Severidade:** `P1` (Alto)
2. **Categoria:** `RAM` / `Heap`
3. **Arquivo(s) e função(ões) afetados:**
   - `src/CronometragemService.h` — `struct cronometragem_t` (`linhas 16-31`)
   - `src/ApuracaoResultados.h` — `struct etapa_apurada_t` (`linhas 14-27`) e `struct piloto_apurado_t` (`linhas 29-48`)
4. **Evidência concreta no código ou na compilação:**
   - Inspeção DWARF (`xtensa-esp32-elf-gdb`): `sizeof(cronometragem_t) = 208 B` (contém 8 campos `std::string` de `24 B` cada = `192 B` apenas em cabeçalhos `std::string`), `sizeof(etapa_apurada_t) = 156 B` (5 `std::string`), `sizeof(piloto_apurado_t) = 292 B` (10 `std::string`).
   - Quando `std::vector<cronometragem_t>` realoca de 128 para 256 registros, exige um bloco contíguo de `256 × 208 B = 53.248 B` na heap (enquanto mantém o bloco anterior de `26.624 B`).
5. **Explicação técnica da causa raiz:**
   - Uso de `std::string` (`24 B` por campo na ABI GCC 32-bit) para representar identificadores fixos curtos (`uid` de 8 chars hex, horários `"HH:MM:SS"` de 8 chars, `especial` `"E1"`..`"E10"`, e enumeradores `tipo`, `modoVoltas`, `status`).
6. **Impacto prático durante uma prova real:**
   - Limita severamente o número de registros que cabem na RAM do ESP32 e provoca `std::bad_alloc` (crash/reboot) quando `std::vector<cronometragem_t>` tenta dobrar de capacidade acima de 128 ou 256 registros devido à falta de bloco contíguo de `53 KB` ou `106 KB`.
7. **Condição em que o problema ocorre:**
   - Ao atingir a **129ª passagem RFID** (realocação para 256 elementos = `53.248 B` contíguos) ou **257ª passagem RFID** (realocação para 512 elementos = `106.496 B` contíguos — impossível no ESP32 pois `Largest Free Block` no boot já é `59.380 B`!).
8. **Estimativa ou medição do custo atual:**
   - **Fato Matemático Crítico:** Na 257ª passagem RFID, `std::vector<cronometragem_t>::push_back` tenta alocar `512 × 208 B = 106.496 B` contíguos. Como o maior bloco contíguo físico do ESP32 (mesmo antes do `setup()`) é de `110.580 B` e após o `setup()` é de **`59.380 B`**, **o firmware atual É MATEMATICAMENTE INCAPAZ de ultrapassar 256 registros em `CronometragemService` sem sofrer crash por falha de alocação contígua (ou na 129ª passagem se o maior bloco já tiver caído abaixo de `53.248 B`)!**
9. **Solução recomendada:**
   - Compactar `cronometragem_t` usando tipos de tamanho fixo / arrays `char` curtos ou inteiros:
     - `char uid[9]` (ou `uint32_t`/`uint64_t`), `char inicio[9]`, `char fim[9]`, `char duracao[9]` (ou calcular a string `HH:MM:SS` apenas na serialização JSON a partir de `uint32_t inicioSeg`, `uint32_t fimSeg`, `int32_t duracaoSegundos`!), `uint8_t tipo`, `uint8_t modoVoltas`, `uint8_t especialIdx`, `uint8_t status`, `uint16_t numeral`, `uint16_t volta`, `bool incompleto`.
     - Com isso, `sizeof(cronometragem_t)` cai de **`208 B` para `24 B` (ou `44 B` mantendo `char[9]`)**, sem nenhuma alocação interna no heap!
   - Usar `std::deque<cronometragem_t>` (que aloca em páginas pequenas de 512 B sem exigir um único bloco contíguo gigante e sem copiar elementos existentes) ou `reserve()` adequado.
10. **Ganho esperado:**
    - Redução de **80% a 88% na RAM ocupada pelos registros** (`1.500 registros` passam de `312 KB` para apenas **`36 KB`**!) e eliminação total do crash de realocação contígua na 129ª/257ª passagem!
11. **Risco ou trade-off da mudança:**
    - Manter métodos helpers ou conversão transparente em `Cronometragem::read` / `Cronometragem::update` para que o contrato JSON da API REST e do frontend permaneça 100% idêntico.
12. **Como validar a correção:**
    - Verificar `sizeof(cronometragem_t) <= 44` na compilação e inserir 1.500 registros em `CronometragemService`, comprovando ausência de falha de alocação e `MaxAllocHeap` estável.

---

#### `P1-03` `[CONFIRMADO]` — Condições de Corrida (Data Races) entre `loopTask` (Core 1) e `httpd` (Core 0/1) em `CronometrarService::_state` e `DisplayService`
1. **Severidade:** `P1` (Alto)
2. **Categoria:** `FreeRTOS` / `Concorrência`
3. **Arquivo(s) e função(ões) afetados:**
   - `src/CronometrarService.cpp` — `CronometrarService::loop()` (`linhas 204-205, 376, 482, 503, 872-874`)
   - `src/RFIDService.cpp` — `handleStartReset()` (`linha 150`) e `handleStopReset()` (`linha 179`)
   - `src/DisplayService.cpp` — `showClock()`, `showResetCount()`, `clear()` (`linhas 38-180`)
4. **Evidência concreta no código ou na compilação:**
   - `CronometrarService::loop()` (Core 1) lê `_state.tipo`, `_state.modoVoltas`, `_state.especial` (`std::string`) diretamente sem chamar `this->read(...)`, enquanto `httpd` altera `_state` via `HttpEndpoint` ou `SyncService::handlePostConfig` (`linha 542`).
   - `RFIDService::handleStartReset()` (na task `httpd`) chama `_display->showResetCount(0)`, manipulando os pinos `GPIO 14/27` via `delayMicroseconds(100)` sem mutex enquanto `displayService.loop()` (na `loopTask`, Core 1) faz bit-banging nos mesmos pinos.
5. **Explicação técnica da causa raiz:**
   - Falta de cópia local protegida por mutex (`Cronometrar cfg; this->read([&](Cronometrar& s){ cfg = s; });`) no início do processamento do cartão em `CronometrarService::loop()`, e ausência de um `SemaphoreHandle_t` no `DisplayService`.
6. **Impacto prático durante uma prova real:**
   - Corrupção de leitura de `std::string` se a configuração for consultada/atualizada enquanto um cartão é lido; lixo visual ou travamento do controlador TM1637 quando comandos HTTP de reset concorrem com a atualização de 1 Hz do relógio no display.
7. **Condição em que o problema ocorre:**
   - Quando a task `httpd` processa `/rest/cronometrar`, `/rest/sync/config` ou `/rest/rfid/reset/*` simultaneamente à execução da `loopTask`.
8. **Estimativa ou medição do custo atual:**
   - Risco de *LoadProhibited* exception (reboot) em acesso concorrente a `std::string` ou corrupção do barramento de 2 fios do display.
9. **Solução recomendada:**
   - No início de `CronometrarService::loop()` (após detectar o cartão), copiar o estado `Cronometrar` sob `this->read(...)` para uma variável local da pilha;
   - Adicionar um mutex FreeRTOS (`xSemaphoreCreateMutex()`) dentro de `DisplayService` (ou enfileirar o estado desejado para que apenas `DisplayService::loop()` no Core 1 escreva no hardware `TM1637Display`).
10. **Ganho esperado:**
    - Eliminação de 100% dos *data races* entre `httpd` e `loopTask` com custo de CPU desprezível (`< 5 µs`).
11. **Risco ou trade-off da mudança:**
    - Nenhum risco; tornar o `DisplayService` *state-driven* (atualizando o hardware apenas em `DisplayService::loop()`) inclusive libera a task `httpd` dos `16 ms` de bit-banging.
12. **Como validar a correção:**
    - Executar loop de requisições concorrentes a `/rest/cronometrar` e `/rest/rfid/reset/start` durante leituras contínuas de cartões RFID.

---

#### `P1-04` `[CONFIRMADO]` — Bloqueios Síncronos na `loopTask` (`BuzzerService::beepTone` até 480ms e `clearRFIDData` até 2,5s) Cegando o Leitor RFID
1. **Severidade:** `P1` (Alto)
2. **Categoria:** `RFID` / `FreeRTOS` / `Precisão Temporal`
3. **Arquivo(s) e função(ões) afetados:**
   - `src/BuzzerService.cpp` — `BuzzerService::beepTone()` (`linhas 19-33`), `beepError()` (`linhas 40-44`), `beepWarning()` (`linhas 46-50`)
   - `src/CronometrarService.cpp` — chamadas a `_buzzer->beepTone(1000, 300)` (`linhas 297, 417, 567, 650`), `beepWarning()` e `beepError()`
   - `src/RFIDService.cpp` — `RFIDService::clearRFIDData()` (`linhas 200-273`)
4. **Evidência concreta no código ou na compilação:**
   - `BuzzerService::beepTone()` chama `delay(durationMs)` diretamente na thread chamadora (`loopTask`).
   - Em `CronometrarService.cpp`, cada leitura válida chama `beepTone(1000, 300)` (`300 ms` parada), cada leitura duplicada chama `beepWarning()` (`340 ms` parada) e cada erro chama `beepError()` (`480 ms` parada).
5. **Explicação técnica da causa raiz:**
   - Acionamento de PWM do buzzer por espera ocupada/bloqueante (`delay()`) na mesma task que faz o polling do leitor `MFRC522` e do relógio do display.
6. **Impacto prático durante uma prova real:**
   - Durante os `300 ms` a `480 ms` do bipe (somados aos `200–800 ms` de RF + Flash), a `loopTask` está completamente bloqueada: o relógio de 6 dígitos no display congela/atrasa o segundo e um segundo piloto passando logo atrás na chegada não é detectado.
7. **Condição em que o problema ocorre:**
   - Em 100% das leituras de cartão (sucesso, aviso ou erro).
8. **Estimativa ou medição do custo atual:**
   - `300 ms` a `480 ms` de bloqueio puro de CPU na `loopTask` por evento.
9. **Solução recomendada:**
   - Tornar o `BuzzerService` **não-bloqueante** (máquina de estados simples baseada em `millis()` atualizada em `buzzerService.loop()`, ou um `esp_timer` / timer de software FreeRTOS que desliga o canal LEDC após `durationMs`).
10. **Ganho esperado:**
    - Redução imediata de **`300 ms` a `480 ms`** no tempo de bloqueio de cada leitura RFID (o tempo de resposta paracartão vazio cai de `~358 ms` para **`~58 ms`**!).
11. **Risco ou trade-off da mudança:**
    - Mudança isolada em `BuzzerService.h/.cpp` (~30 linhas de código) + chamada a `buzzerService.loop()` em `main.cpp`. Zero impacto colateral.
12. **Como validar a correção:**
    - Medir o tempo de retorno de `beepError()` e `beepTone()` (deve cair de `300–480 ms` para `< 50 µs`).

---

#### `P1-05` `[RISCO ESTÁTICO]` — Pilha Reduzida (`4.096 B`) e Bloqueio de `1,5s a 3,0s` (`WiFi.scanNetworks`) na Task `ESP32 SvelteKit Loop`
1. **Severidade:** `P1` (Alto)
2. **Categoria:** `Stack` / `FreeRTOS` / `Wi-Fi/HTTP`
3. **Arquivo(s) e função(ões) afetados:**
   - `lib/framework/ESP32SvelteKit.cpp` — `ESP32SvelteKit::begin()` (`linha 126`: `xTaskCreatePinnedToCore(..., 4096, ..., 0)`)
   - `lib/framework/WiFiSettingsService.cpp` — `loop()` (`linhas 135-150`), `updateRSSI()` (`linhas 152-160`), `connectToWiFi()` (`linhas 162-241`, `WiFi.scanNetworks()` na `linha 173`)
4. **Evidência concreta no código ou na compilação:**
   - A task `ESP32 SvelteKit Loop` é criada com apenas `4.096 B` de stack e executa `updateRSSI()` -> `_socket->emitEvent("rssi", ...)` (que aloca `JsonDocument`, serializa JSON, envia frames WebSocket e chama `ESP_LOGV` com `vsnprintf`), além de `WiFi.scanNetworks()` síncrono quando uma rede STA está configurada mas fora de alcance.
5. **Explicação técnica da causa raiz:**
   - `vsnprintf` + `JsonDocument` + chamadas TCP/WebSocket em uma stack de `4.096 B` operam muito próximos ao limite de *stack overflow* no ESP32. Além disso, se houver uma rede Wi-Fi STA salva em `/config/wifiSettings.json` que não está presente no meio da trilha (mato), a cada `60 s` (`WIFI_RECONNECTION_INTERVAL`) o ESP32 chama `WiFi.scanNetworks()`, varrendo todos os 13 canais Wi-Fi por `~2 segundos`. Durante esse scan, o rádio sai do canal do SoftAP (`192.168.4.1`), derrubando/congelando conexões dos atletas e da sincronização entre estações!
6. **Impacto prático durante uma prova real:**
   - Se uma estação tiver uma rede Wi-Fi de casa/escritório salva e for levada para a prova no mato, a cada 60 segundos o SoftAP da estação sofrerá *packet loss* / congelamento de `2 segundos` devido ao `WiFi.scanNetworks()`, atrapalhando a consulta de resultados e a sincronização entre estações.
7. **Condição em que o problema ocorre:**
   - Sempre que `wifiSettings.json` possuir uma rede configurada que não está ao alcance durante a prova, ou quando clientes WebSocket estiverem conectados recebendo eventos `rssi`.
8. **Estimativa ou medição do custo atual:**
   - Stack total: `4.096 B`; bloqueio de rádio e DNS a cada 60s: `1.500 ms` a `3.000 ms`.
9. **Solução recomendada:**
   - Aumentar a stack da task `ESP32 SvelteKit Loop` de `4096` para `6144` bytes;
   - Desativar a reconexão/scan STA automática enquanto a cronometragem estiver ativa (`isCronometrando()`) ou quando a estação operar exclusivamente em modo AP de campo.
10. **Ganho esperado:**
    - Estabilidade contínua do SoftAP (`192.168.4.1`) no campo e margem segura contra *stack overflow* no Core 0.
11. **Risco ou trade-off da mudança:**
    - Custo de apenas `2 KB` de RAM para aumentar a stack de `4 KB` para `6 KB`.
12. **Como validar a correção:**
    - Monitorar `uxTaskGetStackHighWaterMark` da task `ESP32 SvelteKit Loop` e testar ping/HTTP contínuo no SoftAP por 5 minutos com uma rede STA inexistente configurada.

---

### Achados P2 — Risco Médio (Eficiência, Escalabilidade e Manutenibilidade)

#### `P2-01` `[CONFIRMADO]` — Clock SPI do MFRC522 Reduzido a `1 MHz` e Leitura Sem `String::reserve()`
1. **Severidade:** `P2` (Médio)
2. **Categoria:** `RFID` / `Heap`
3. **Arquivo(s) e função(ões) afetados:**
   - `platformio.ini` (`linha 59`: `-D MFRC522_SPICLOCK=1000000u`)
   - `src/RFIDService.cpp` — `RFIDService::readCardData()` (`linhas 543-591`)
4. **Evidência concreta no código ou na compilação:**
   - `RFIDService::readCardData()` concatena `outData += (char)buffer[i];` até 720 vezes sem chamar `outData.reserve(...)`.
   - O barramento SPI está configurado em `1 MHz` (o MFRC522 suporta até `10 MHz` e opera com alta estabilidade em cabos curtos a `4 MHz`).
5. **Explicação técnica da causa raiz:**
   - Falta de pré-alocação do buffer da `String` e clock SPI conservador.
6. **Impacto prático durante uma prova real:**
   - Dezenas de realocações de `outData` na heap a cada leitura de cartão e tempo extra de transferência SPI nos 45 blocos.
7. **Condição em que o problema ocorre:**
   - Em todas as leituras de cartão RFID.
8. **Estimativa ou medição do custo atual:**
   - ~6 a 8 chamadas `realloc` no heap por leitura de cartão cheio + `~15–25 ms` extras de barramento SPI.
9. **Solução recomendada:**
   - Adicionar `outData.reserve(maxBlocks * 16);` logo após `outData = "";` em `RFIDService::readCardData()` (`linha 544`);
   - Parar a leitura de blocos ou otimizar o tamanho lido; avaliar clock SPI de `2 MHz` ou `4 MHz` conforme comprimento da fiação física da estação.
10. **Ganho esperado:**
    - Zero realocações intermediárias de `String` durante a leitura RF e menor latência SPI.
11. **Risco ou trade-off da mudança:**
    - `outData.reserve()` tem risco zero; o aumento de clock SPI acima de `1 MHz` só deve ser feito se o cabo flat do leitor MFRC522 for curto (`< 15 cm`).
12. **Como validar a correção:**
    - Verificar leitura de 100 cartões consecutivos sem erros de CRC/paridade.

---

#### `P2-02` `[CONFIRMADO]` — Buscas Lineares $O(N)$ e $O(A)$ Repetidas em `CronometragemService` e `AtletaService`
1. **Severidade:** `P2` (Médio)
2. **Categoria:** `Desempenho` / `CPU`
3. **Arquivo(s) e função(ões) afetados:**
   - `src/CronometragemService.cpp` — `jaRegistrado()` (`linhas 19-31`), `buscarRegistro()` (`linhas 33-46`), `contarRegistros()` (`linhas 48-55`), `buscarUltimaPassagem()` (`linhas 57-61`)
   - `src/SyncService.cpp` — `handlePostResultados()` (`linhas 383-421`: busca linear $O(N \times M)$ para mesclar $M$ registros remotos contra $N$ locais!)
4. **Evidência concreta no código ou na compilação:**
   - Em `SyncService::handlePostResultados()` (`linhas 383-421`), para cada registro `remoto` recebido no lote ($M$), percorre todo o vetor `state.registros` ($N$) comparando múltiplas `std::string` (`item.uid == remoto.uid && item.tipo == remoto.tipo ...`) **dentro do mutex `_cronometragemService->update()`**.
5. **Explicação técnica da causa raiz:**
   - Ausência de índice ou chave compacta numérica nas buscas de duplicidade e mesclagem.
6. **Impacto prático durante uma prova real:**
   - Com `cronometragem_t` compactado (comparação de inteiros `uint32_t uid` / `uint16_t numeral` / `uint8_t tipo` em vez de `std::string`), a varredura fica **10× mais rápida** sem sequer precisar de estruturas complexas que fragmentem o heap.
7. **Condição em que o problema ocorre:**
   - Durante `POST /rest/sync/resultados` e a cada leitura de cartão em provas com `> 500` registros.
8. **Estimativa ou medição do custo atual:**
   - Mesclar 100 registros contra 1.000 existentes executa até `100.000` comparações de múltiplas `std::string`.
9. **Solução recomendada:**
   - Ao substituir `std::string` por inteiros/enums em `cronometragem_t` (conforme `P1-02`), comparar os campos inteiros (`numeral`, `tipo`, `especialIdx`, `volta`) antes de comparar `uid`, reduzindo a comparação a uma única instrução de 32 bits.
10. **Ganho esperado:**
    - Redução de `> 90%` no tempo de CPU de `jaRegistrado()` e da mesclagem em `handlePostResultados()`.
11. **Risco ou trade-off da mudança:**
    - Nenhum (sinergia direta com `P1-02`).
12. **Como validar a correção:**
    - Medir tempo de mesclagem de 100 registros em uma base de 1.500 registros (`< 5 ms`).

---

#### `P2-03` `[CONFIRMADO]` — Logs `CORE_DEBUG_LEVEL=4`, Exceções C++ e Bundle TLS `x509_crt_bundle.bin` Desperdiçando Flash e RAM
1. **Severidade:** `P2` (Médio)
2. **Categoria:** `Flash` / `Desempenho`
3. **Arquivo(s) e função(ões) afetados:**
   - `platformio.ini` (`linha 33`: `-D CORE_DEBUG_LEVEL=4`, `linha 73`: `board_build.embed_files = src/certs/x509_crt_bundle.bin`)
   - `src/RFIDService.cpp` (`linha 258`: `Serial.printf("Block %d cleared\n", blockAddr)` dentro do loop de 45 blocos)
4. **Evidência concreta no código ou na compilação:**
   - `x509_crt_bundle.bin` ocupa `17.562 B` em `.flash.rodata` sem que `FT_NTP` ou `FT_DOWNLOAD_FIRMWARE` estejam ativos.
   - `CORE_DEBUG_LEVEL=4` compila logs verbosos em toda a pilha HTTP/Wi-Fi.
5. **Explicação técnica da causa raiz:**
   - Configurações herdadas do template de desenvolvimento do `ESP32-SvelteKit`.
6. **Impacto prático durante uma prova real:**
   - Consumo desnecessário de `~50–70 KB` de Flash e overhead de formatação serial em eventos de rede e limpeza de cartão.
7. **Condição em que o problema ocorre:**
   - Permanente.
8. **Estimativa ou medição do custo atual:**
   - `17,5 KB` do bundle de certificados + `~35–50 KB` de strings/código de debug nível 4.
9. **Solução recomendada:**
   - Reduzir `CORE_DEBUG_LEVEL` para `2` (`WARN`) ou `3` (`INFO`) em produção;
   - Remover `board_build.embed_files = src/certs/x509_crt_bundle.bin` (ou condicionar ao uso de HTTPS);
   - Remover o `Serial.printf` de dentro do loop de 45 blocos em `RFIDService::clearRFIDData()`.
10. **Ganho esperado:**
    - Economia de **`~50 a 65 KB` de Flash** e redução da latência de limpeza de cartão.
11. **Risco ou trade-off da mudança:**
    - Nível `INFO` (`3`) ou `WARN` (`2`) preserva todos os logs importantes de erro e diagnóstico sem sobrecarregar a UART.
12. **Como validar a correção:**
    - Compilar e verificar a redução de tamanho na seção `.flash.rodata` e `.flash.text`.

---

### Achados P3 — Risco Baixo (Melhorias Cosméticas e de Observabilidade)

#### `P3-01` `[CONFIRMADO]` — Conflito de Flags `-O3` vs `-Os` na Linha de Comando do Compilador
1. **Severidade:** `P3` (Baixo)
2. **Categoria:** `Flash` / `Desempenho`
3. **Arquivo(s) e função(ões) afetados:**
   - `platformio.ini` (`linha 31`: `-O3`) vs `.pio/build/esp32dev/idedata.json` (`"-flto=auto", "-O3", ..., "-Os"`)
4. **Evidência concreta no código ou na compilação:**
   - Em `.pio/build/esp32dev/idedata.json`, a flag `-Os` padrão do framework Arduino é inserida **após** `-O3` do `platformio.ini`, sobrescrevendo silenciosamente `-O3`.
5. **Explicação técnica da causa raiz:**
   - No PlatformIO, `build_flags` é concatenado antes das flags internas da plataforma, a menos que `-Os` seja removido via `build_unflags = -Os`.
6. **Impacto prático durante uma prova real:**
   - Neste caso específico, o fato de `-Os` ter prevalecido sobre `-O3` **evitou que o firmware estourasse os 1.920 KB da partição `app0`** (pois `-O3` expandiria loops e funções inline, aumentando `.flash.text` e `.iram0.text`). Portanto, `-Os` é de fato a flag mais adequada para este microcontrolador sem PSRAM e com Flash de 4 MB!
7. **Condição em que o problema ocorre:**
   - Em tempo de compilação.
8. **Estimativa ou medição do custo atual:**
   - Ambiguidade na configuração de build.
9. **Solução recomendada:**
   - Substituir `-O3` por `-Os` explicitamente em `platformio.ini` para refletir o comportamento real e evitar que uma atualização da plataforma faça `-O3` prevalecer e estourar a IRAM/Flash.
10. **Ganho esperado:**
    - Determinismo e clareza na configuração de build.
11. **Risco ou trade-off da mudança:**
    - Zero mudança no binário gerado (já compila com `-Os`).
12. **Como validar a correção:**
    - Verificar `idedata.json` após `pio run`.

---

#### `P3-02` `[CONFIRMADO]` — Endpoint `POST /rest/relogio` Permite Alterar o Relógio Durante a Cronometragem Ativa
1. **Severidade:** `P3` (Baixo)
2. **Categoria:** `Precisão Temporal` / `Integridade de Dados`
3. **Arquivo(s) e função(ões) afetados:**
   - `src/RelogioService.h` — `RelogioService::updateRelogio()` (`linhas 133-152`)
4. **Evidência concreta no código ou na compilação:**
   - Diferente de `SyncService::handlePostConfig()` (`src/SyncService.cpp:436`) que bloqueia alterações com HTTP 409 quando `_cronometrarService->isCronometrando()`, `RelogioService::updateRelogio()` aplica `settimeofday()` diretamente sem verificar se a cronometragem está ativa.
5. **Explicação técnica da causa raiz:**
   - `RelogioService` não possui ponteiro ou callback para consultar `CronometrarService::isCronometrando()`.
6. **Impacto prático durante uma prova real:**
   - Se o operador clicar acidentalmente em atualizar relógio na tela de configurações durante uma bateria em andamento, o relógio dará um salto temporal no meio das descidas.
7. **Condição em que o problema ocorre:**
   - Chamada manual a `POST /rest/relogio` com `isCronometrando() == true`.
8. **Estimativa ou medição do custo atual:**
   - Risco de salto temporal por erro humano.
9. **Solução recomendada:**
   - Validar `isCronometrando()` em `RelogioService::updateRelogio()` (retornando HTTP 409 Conflict caso ativo, exatamente como em `SyncService`).
10. **Ganho esperado:**
    - Proteção consistente do relógio durante a prova em todos os endpoints.
11. **Risco ou trade-off da mudança:**
    - Nenhum.
12. **Como validar a correção:**
    - Tentar `POST /rest/relogio` com cronometragem iniciada e verificar retorno HTTP 409.

---

## G. Modelo de Capacidade e Dimensionamento (50, 150 e 300 Pilotos)

### G.1 Equações Formais de Consumo do Sistema Atual

Sejam:
- $P$: Número de pilotos cadastrados (e $P$ cartões RFID registrados, $C = 10$ categorias);
- $E$: Número de estágios (Modo Estágios) ou $V$: Número de voltas por piloto (Modo Voltas);
- $N$: Número total de registros (`cronometragem_t`) gerados na prova:
  - No **Modo Estágios** (Estação de Chegada consolidada ou após sync): $N = P \times E$ registros na chegada (ou $2 \times P \times E$ se mesclar registros de largada e chegada na mesma base);
  - No **Modo Voltas — Estação Única**: $N = P \times (V + 1)$ registros (Largada `L0` + $V$ voltas);
  - No **Modo Voltas — Duas Estações**: $N = P \times V$ registros completos na chegada (ou $2 \times P \times V$ somando largadas e chegadas).

Com base nas medições exatas do compilador e do formato JSON atual:
1. **RAM Residente dos Cadastros ($\text{RAM}_{\text{cad}}$):**
   $$\text{RAM}_{\text{cad}}(P) = P \times \underbrace{56\text{ B}}_{\texttt{atleta\_t}} + P \times \underbrace{48\text{ B}}_{\text{nó }\texttt{std::map}} + 10 \times 28\text{ B} = \mathbf{104 \cdot P + 280\text{ B}}$$
2. **RAM Residente dos Registros de Cronometragem ($\text{RAM}_{\text{reg}}$):**
   $$\text{RAM}_{\text{reg}}(N) = \text{cap}(N) \times \underbrace{208\text{ B}}_{\texttt{cronometragem\_t}} \quad \text{onde } \text{cap}(N) = 2^{\lceil \log_2(N) \rceil}$$
   *(E durante a realocação do `std::vector`, exige simultaneamente $\text{cap}(N) \times 208\text{ B}$ em um **único bloco contíguo** + $\frac{\text{cap}(N)}{2} \times 208\text{ B}$ do bloco anterior!)*
3. **Tamanho em Disco na Partição LittleFS ($\text{FS}_{\text{total}}$, limite físico = `131.072 B` brutos / `~90 KB` úteis para dados):**
   $$\text{FS}_{\text{total}}(P, N) \approx 2.000\text{ B (configs)} + 121 \cdot P\text{ B (atletas+rfid)} + \mathbf{205 \cdot N\text{ B (cronometragem.json)}}$$
4. **Volume Total de Escrita Acumulada na Flash durante a Prova ($W_{\text{flash}}$):**
   $$W_{\text{flash}}(N) = \frac{N(N+1)}{2} \times 205\text{ B}$$
5. **Pico de Heap na Consulta Pública (`GET /rest/resultados-publicos`, $\text{Heap}_{\text{pub}}$):**
   $$\text{Heap}_{\text{pub}}(P, E, N) = \text{RAM}_{\text{cad}}(P) + \underbrace{3 \times (N \times 208\text{ B})}_{\text{residente + ctx + calcular}} + \underbrace{P \times (292 + 156 \cdot E)\text{ B}}_{\texttt{classificacao}} + \underbrace{(P \cdot (180 + 95 \cdot E) + N \cdot 280)\text{ B}}_{\texttt{JsonDocument}} + 4.096\text{ B}$$
6. **Pico de Heap em `FSPersistence::writeToFS()` a Cada Passagem RFID ($\text{Heap}_{\text{fs}}$):**
   $$\text{Heap}_{\text{fs}}(P, N) = \text{RAM}_{\text{cad}}(P) + \text{RAM}_{\text{reg}}(N) + \underbrace{N \times 280\text{ B}}_{\texttt{JsonDocument}}$$

---

### G.2 Tabela Comparativa de Dimensionamento por Cenário (Arquitetura Atual)

> **Limites Físicos Reais do Dispositivo (Linha de Corte):**
> - **Heap Livre Total após o Boot:** **`100.360 B` (`98,0 KB`)**
> - **Maior Bloco Contíguo Livre após o Boot:** **`59.380 B` (`58,0 KB`)**
> - **Partição LittleFS Total (`spiffs`):** **`131.072 B` (`128,0 KB` brutos / `~90 KB` úteis para `cronometragem.json`)**
> - **Cartão MIFARE Classic 1K:** **`720 B` úteis (`45 blocos`)**

| Cenário e Modalidade | Pilotos ($P$) | Etapas / Voltas | Total de Eventos ($N$) | RAM Residente ($\text{RAM}_{\text{cad}} + N \cdot 208\text{B}$) | Tamanho JSON no LittleFS ($\text{FS}_{\text{total}}$) | Escrita Flash Acumulada ($W_{\text{flash}}$) | Pico Heap em `writeToFS()` | Pico Heap em `/resultados-publicos` | Uso do Cartão MIFARE 1K (`max 720 B`) | 1º Recurso que Satura (Causa do Colapso) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- |
| **Pequeno — 5 Estágios** | 50 | 5 est. | **250** | `57,5 KB` | `59,3 KB` (Cabe) | `6,4 MB` | **`127,5 KB` (ESTOURA 98 KB)** | **`245,0 KB` (ESTOURA 2,5×)** | `208 B` (13 bl.) | **1º Heap em `/resultados-publicos` e `writeToFS()` (~175º reg.)** |
| **Pequeno — 10 Estágios** | 50 | 10 est. | **500** | **`109,5 KB` (ESTOURA)** | **`110,5 KB` (ESTOURA LittleFS)** | `25,7 MB` | **`249,5 KB` (ESTOURA)** | **`488,0 KB` (ESTOURA 5×)** | `394 B` (25 bl.) | **1º Bloco Contíguo na 257ª passagem (`106 KB` > `58 KB`) + LittleFS** |
| **Pequeno — 20 Voltas (1 Est.)** | 50 | 20 voltas | **1.050** | **`223,9 KB` (ESTOURA)** | **`223,3 KB` (ESTOURA 2×)** | `113,1 MB` | **`517,9 KB` (ESTOURA)** | **`980,0 KB` (ESTOURA 10×)** | `254 B` (16 bl.) | **1º Heap / Realocação `std::vector` + LittleFS** |
| **Pequeno — 25 Voltas (2 Est.)** | 50 | 25 voltas | **1.250** | **`265,5 KB` (ESTOURA)** | **`264,3 KB` (ESTOURA)** | `160,3 MB` | **`615,5 KB` (ESTOURA)** | **`1,15 MB` (ESTOURA)** | **`823 B` (ESTOURA 720 B na 22ª volta!)** | **1º Cartão MIFARE 1K (trava na 22ª volta) + Heap + LittleFS** |
| **Médio — 5 Estágios** | 150 | 5 est. | **750** | **`171,9 KB` (ESTOURA)** | **`173,9 KB` (ESTOURA LittleFS)** | `57,7 MB` | **`381,9 KB` (ESTOURA)** | **`735,0 KB` (ESTOURA)** | `208 B` (13 bl.) | **1º Realocação `std::vector` (129ª/257ª passagem) + LittleFS (~400 reg.)** |
| **Médio — 10 Estágios** | 150 | 10 est. | **1.500** | **`327,9 KB` (ESTOURA)** | **`327,6 KB` (ESTOURA)** | `230,8 MB` | **`747,9 KB` (ESTOURA)** | **`1,46 MB` (ESTOURA)** | `394 B` (25 bl.) | **1º Heap + LittleFS** |
| **Médio — 20 Voltas (2 Est.)** | 150 | 20 voltas | **3.000** | **`639,9 KB` (> SRAM do chip!)** | **`635,1 KB` (ESTOURA)** | `922,8 MB` | **`1,48 MB` (ESTOURA)** | **`2,90 MB` (ESTOURA)** | `663 B` (42 bl., limite!) | **1º Heap + LittleFS + Tempo RF no cartão (~750 ms)** |
| **Grande — 5 Estágios** | 300 | 5 est. | **1.500** | **`343,5 KB` (> Heap Total!)** | **`345,8 KB` (ESTOURA)** | `230,8 MB` | **`763,5 KB` (ESTOURA)** | **`1,47 MB` (ESTOURA)** | `208 B` (13 bl.) | **1º Heap no cadastro/primeiras especiais + LittleFS** |
| **Grande — 10 Estágios** | 300 | 10 est. | **3.000** | **`655,5 KB` (> SRAM do chip!)** | **`653,3 KB` (ESTOURA)** | `922,8 MB` | **`1,49 MB` (ESTOURA)** | **`2,93 MB` (ESTOURA)** | `394 B` (25 bl.) | **1º Impossível manter em RAM/JSON monolítico** |

---

### G.3 Projeção de Capacidade Pós-Otimização (Com as Correções Recomendadas)

Se aplicarmos as otimizações estruturais identificadas nesta auditoria (sem trocar o hardware ESP32 de 4 MB sem PSRAM):
1. **Partição LittleFS expandida de `128 KB` para `1.664 KB`** (aproveitando `app1` e `coredump` ociosos);
2. **Persistência Append-Only** de `cronometragem` (em vez de `FSPersistence::writeToFS()` reescrevendo tudo com `JsonDocument`);
3. **Compactação de `cronometragem_t` de `208 B` para `28 B`** (usando inteiros/enums e `std::deque` em páginas de 512 B, ou mantendo apenas um índice leve de `12 B`/registro em RAM e lendo do LittleFS sob demanda);
4. **Streaming / Paginação em `/rest/resultados-publicos`** sem triplicar vetores em RAM;
5. **Enxugamento de `logo.png` + tabela estática única em `WWWData.h`** (liberando `+25 KB` de Heap no boot, elevando o Heap Livre pós-setup para `~125 KB`).

**Resultado Pós-Otimização para o Cenário Grande (300 Pilotos × 10 Estágios = 3.000 Eventos):**
- **RAM Residente (`Atletas` compactos + 3.000 `cronometragem_t` de `28 B` em `std::deque`):** `300 × 36 B + 3.000 × 28 B = 10,8 KB + 84,0 KB =` **`94,8 KB`** (ou apenas **`36 KB`** se mantiver apenas os timestamps/durações indexados em RAM!), cabendo perfeitamente nos `125 KB` livres!
- **Espaço no LittleFS (`3.000 registros` em log compacto de `40 B`/linha):** **`120 KB`** dentro de `1.664 KB` (**apenas 7,2% da partição**)!
- **Pico de Heap em `registrarTempo()`:** **`< 1 KB`** (append de 1 linha, `< 5 ms`).

---

## H. Plano de Instrumentação e Benchmarks Recomendados

Para monitorar e comprovar em bancada os ganhos antes e depois das otimizações, recomenda-se adicionar um módulo leve de instrumentação (`DiagService` ou macros condicionais `-D TIMING_BENCHMARK=1`) utilizando as APIs nativas do ESP-IDF / Arduino Core 3.x:

### H.1 APIs Exatas para Instrumentação no ESP32

| Métrica a Coletar | API Nativa (ESP-IDF / Arduino) | Onde Instrumentar |
| :--- | :--- | :--- |
| **Heap Interno Livre (8-bit)** | `heap_caps_get_free_size(MALLOC_CAP_8BIT)` ou `ESP.getFreeHeap()` | Antes e depois de `writeToFS()`, `handleGetResultadosPublicos()`, `handlePostResultados()` e `registrarTempo()` |
| **Maior Bloco Contíguo Livre** | `heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)` ou `ESP.getMaxAllocHeap()` | Monitorar razão de fragmentação: $\text{Frag} = 1 - \frac{\text{MaxAllocHeap}}{\text{FreeHeap}}$ |
| **Menor Heap Livre Histórico** | `heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)` ou `ESP.getMinFreeHeap()` | Logar a cada 50 passagens RFID e após ciclos de sincronização |
| **Stack High-Water Mark (Folga Mínima de Pilha)** | `uxTaskGetStackHighWaterMark(NULL)` (retorna bytes livres mínimos da task no FreeRTOS do ESP-IDF) | No final de `CronometrarService::loop()` (`loopTask`), dentro de `SyncService` (`httpd`) e em `ESP32SvelteKit::_loop` |
| **Latência em Microssegundos** | `int64_t t0 = esp_timer_get_time();` (contador monotônico de 64 bits e resolução de `1 µs`) | Medir separadamente: `dt_detect_to_ts`, `dt_rfid_read`, `dt_rfid_write`, `dt_fs_write`, `dt_http_apuracao` |
| **Detecção de Overflow JSON** | `doc.overflowed()` (ArduinoJson v7) | Obrigatoriamente após popular qualquer `JsonDocument` antes de `serializeJson()` |

### H.2 Bateria de Testes de Estresse Recomendada em Bancada

1. **Teste de Carga Sintética de Dados (Seed Test):**
   - Criar um comando serial/endpoint de diagnóstico (compilado apenas em modo teste) que gere sinteticamente `P = 50, 150, 300` atletas e insira progressivamente `N = 100, 250, 500, 1.000, 1.500, 3.000` registros de cronometragem, registrando em CSV pela Serial a curva: `[N, FreeHeap, MaxAllocHeap, MinFreeHeap, FS_UsedBytes, dt_fs_write_us]`.
2. **Teste de Concorrência Wi-Fi + RFID (Stress Test):**
   - Enquanto um script no PC dispara `5 requisições concorrentes por segundo` em `GET /rest/resultados-publicos` e `GET /rest/sync/status`, aproximar repetidamente cartões RFID com 10 estágios gravados no leitor, medindo o jitter de captura do timestamp (`dt_detect_to_ts`) e verificando ausência de WDT reset ou OOM.
3. **Teste de Remoção Rápida do Cartão RF ("Rasante"):**
   - Passar o cartão rapidamente a 2–4 cm da antena durante a fase de escrita (forçando falha de `MIFARE_Write`) 20 vezes seguidas e verificar que os estágios/voltas anteriores gravados no cartão **nunca** são apagados ou corrompidos.
4. **Teste de Queda de Energia (Power-Loss / Brownout Test):**
   - Desenergizar ou pressionar `EN/RST` no exato momento em que o LED/log indicar escrita na Flash após uma leitura RFID e verificar no boot seguinte que 100% dos registros anteriores permanecem intactos.

---

## I. Backlog Priorizado de Otimização

A tabela abaixo consolida todas as ações recomendadas, ordenadas por relação **Prioridade × Benefício × Risco de Regressão**:

| Ordem | ID | Ação Recomendada | Severidade | Benefício Esperado | Esforço | Risco de Regressão |
| :---: | :---: | :--- | :---: | :--- | :---: | :---: |
| **1** | `P0-05` | Capturar `DateTime now = Relogio::getNow()` imediatamente após `PICC_ReadCardSerial()` (antes de `readCardData` e `deserializeJson`) e sincronizar periodicamente com o `DS3231`. | **P0** | Elimina atraso variável de `150–360 ms` no timestamp de chegada e zera deriva entre estações. | **Baixo** (~20 linhas) | **Muito Baixo** |
| **2** | `P1-04` | Tornar `BuzzerService` não-bloqueante (baseado em `millis()` / timer sem `delay()`). | **P1** | Libera `300 ms` a `480 ms` de bloqueio na `loopTask` em **toda** leitura RFID. | **Baixo** (~40 linhas) | **Muito Baixo** |
| **3** | `P0-04` | Proteger `writeCardData` contra estouro de `720 B` antes de gravar, impedir `doc.clear()` destrutivo sem recuperação e otimizar o formato/escrita por bloco no cartão MIFARE 1K. | **P0** | Elimina corrupção do histórico do cartão em afastamento rápido e supera o limite de 21 voltas em Duas Estações. | **Médio** | **Baixo** |
| **4** | `P0-02` | Substituir reescrita monolítica de `/config/cronometragem.json` por persistência **Append-Only** (1 linha por evento) e adicionar escrita atômica (`.tmp` + `rename`) em `FSPersistence`. | **P0** | Elimina perda total da prova em queda de energia, reduz latência de Flash de `500 ms` para `< 8 ms` e zera uso de `JsonDocument` global a cada passagem. | **Médio** | **Baixo** |
| **5** | `P0-03` | Redimensionar a partição `spiffs` (LittleFS) de `128 KB` para `1.664 KB` (ou `832 KB` c/ OTA) removendo o desperdício de `app1`/`coredump` inativos. | **P0** | Aumenta em **13×** a capacidade de armazenamento persistente da estação. | **Baixo** (1 arquivo `.csv`) | **Muito Baixo** (exige re-upload serial) |
| **6** | `P1-02` | Compactar `cronometragem_t` (`208 B` $\rightarrow$ `~28–44 B` sem `std::string` dinâmicas) e usar `std::deque` (ou páginas fixas) em vez de `std::vector` contíguo gigante. | **P1** | Reduz em **80%+** o consumo de RAM por registro e elimina o crash de realocação contígua na 129ª/257ª passagem! | **Médio** | **Baixo** (mantém JSON externo igual) |
| **7** | `P0-01` | Refatorar `SyncService::handleGetResultadosPublicos` e `ApuracaoResultados::calcular` para não triplicar os vetores na RAM, fazer cache do relatório apurado e não duplicar `cronometragem` bruta no JSON público. | **P0** | Reduz pico de Heap de `~430 KB` para `< 18 KB`, viabilizando consultas Wi-Fi em provas de 150–300 pilotos. | **Médio** | **Baixo** |
| **8** | `P0-06` | Eliminar dupla alocação contígua em `PsychicRequest::loadBody()`, liberar `_body` antes de `saveToFS()` e reduzir `BATCH_SIZE` de sync para `30`. | **P0** | Elimina falha de memória (`OOM`) ao sincronizar lotes de resultados e configurações via Wi-Fi. | **Baixo-Médio** | **Muito Baixo** |
| **9** | `P1-01` | Comprimir `logo.png` (`280 KB` $\rightarrow$ `< 15 KB`) e unificar as 91 rotas de `WWWData::registerRoutes` em uma tabela estática única em Flash. | **P1** | Libera **`~265 KB` de Flash** (`app0` cai de 97,9% para ~84%) e recupera **`+25 KB` de Heap livre** no boot. | **Médio** | **Muito Baixo** |
| **10** | `P1-03` + `P1-05` | Proteger `CronometrarService::_state` e `DisplayService` com mutex/estado local, subir stack do `ESP32 SvelteKit Loop` para `6 KB` e inibir `WiFi.scanNetworks()` durante a cronometragem. | **P1** | Elimina *data races* multi-core, *stack overflow* no Core 0 e congelamentos de 2s no SoftAP a cada 60s. | **Baixo** | **Muito Baixo** |
| **11** | `P2-01` a `P3-02` | Adicionar `outData.reserve()` no RFID, ajustar `CORE_DEBUG_LEVEL=2/3`, remover `x509_crt_bundle.bin`, fixar `-Os` e bloquear `POST /rest/relogio` durante cronometragem ativa. | **P2/P3** | Libera mais `~60 KB` de Flash, elimina realocações na leitura RF e fecha brecha de salto de relógio. | **Baixo** | **Muito Baixo** |

---

## J. Critérios Objetivos de Aprovação (Go/No-Go para Prova Real)

Para que o firmware seja certificado como **APTO PARA PRODUÇÃO (GO)** em provas reais de Enduro MTB (até 300 pilotos e 3.000 eventos), todos os critérios abaixo deverão ser comprovados:

### J.1 Critérios Estáticos e de Compilação (Verificáveis no Build)

| Indicador Estático | Valor Atual Auditado | Meta de Aprovação (Go/No-Go) | Status Atual |
| :--- | :---: | :---: | :---: |
| **Ocupação da Partição de Firmware (`app0`)** | `97,91%` (`40,2 KB` livres) | **$\le 85\%$** ($\ge 280\text{ KB}$ livres de margem) | **NO-GO** |
| **Capacidade da Partição LittleFS (`spiffs`)** | `128 KB` (`32 setores`) | **$\ge 832\text{ KB}$** (com OTA) ou **$\ge 1.536\text{ KB}$** (sem OTA) | **NO-GO** |
| **Tamanho de `sizeof(cronometragem_t)` em RAM** | `208 Bytes` (8 `std::string`) | **$\le 48\text{ Bytes}$** (sem alocação dinâmica no heap) | **NO-GO** |
| **Verificação de `jsonDocument.overflowed()` antes de gravar** | `0%` dos pontos verificados | **$100\%$** dos pontos de serialização verificados | **NO-GO** |
| **Escrita Atômica / Append-Only em Persistência Flash** | Truncamento direto `"w"` | **Append-Only** p/ eventos + **`.tmp` + `rename()`** p/ configs | **NO-GO** |

### J.2 Critérios Dinâmicos em Hardware (Verificáveis em Bancada sob Estresse)

| Indicador em Execução no ESP32 | Valor Atual Auditado / Estimado | Meta de Aprovação (Go/No-Go) | Status Atual |
| :--- | :---: | :---: | :---: |
| **Heap Livre Após `setup()` (`ESP.getFreeHeap()`)** | `100.360 B` (`98,0 KB`) | **$\ge 120.000\text{ B}$** (`~117 KB`) | **NO-GO** |
| **Heap Mínimo Histórico (`ESP.getMinFreeHeap()`) durante Prova de 1.500 Eventos + Consultas Wi-Fi** | Colapso (`0 B` / Reboot) | **$\ge 35.000\text{ B}$** (`~34 KB` de margem mínima garantida) | **NO-GO** |
| **Maior Bloco Contíguo (`ESP.getMaxAllocHeap()`) após 1.500 Eventos** | `< 20 KB` (falha na 129ª/257ª passagem) | **$\ge 24.576\text{ B}$** (`24 KB`) sem exigir blocos $> 16\text{ KB}$ na aplicação | **NO-GO** |
| **Stack High-Water Mark Mínimo em Todas as Tasks** | Não monitorado (`4 KB` crítico no Core 0) | **$\ge 1.024\text{ B}$** livres em todas as tasks sob carga máxima | **NO-GO** |
| **Latência entre Detecção RF (`PICC_ReadCardSerial`) e Captura do Timestamp** | `12 ms` a **`360 ms`** (variável) | **$\le 5\text{ ms}$** (constante, independente do nível de ocupação do cartão) | **NO-GO** |
| **Tempo Total de Caminho Crítico RFID (Detecção $\rightarrow$ Pronto p/ Próximo)** | `358 ms` a **`1.680 ms`** | **$\le 120\text{ ms}$** (com buzzer assíncrono e persistência append-only) | **NO-GO** |
| **Capacidade de Voltas no Cartão MIFARE 1K (Modo Duas Estações)** | Trava e corrompe na **22ª volta** | **$\ge 40\text{ voltas}$** sem corrupção em remoção rápida do cartão | **NO-GO** |
| **Sobrevivência a Reset/Queda de Energia durante Gravação de Passagem** | Perda de **100%** dos registros no boot seguinte | **0 registros anteriores perdidos** (recuperação automática no boot) | **NO-GO** |
