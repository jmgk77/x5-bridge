# Firmware ESP32-C3 / S3 / ESP32-CAM / ESP32 DevKit — X5 Bridge

Firmware do bridge para câmeras X5, com alvos **ESP32-C3**, **ESP32-S3**,
**ESP32-CAM** e **ESP32 DevKit**. Usa o mesmo stack dos outros devices da casa:
`ESPAsyncWiFiManager`, `ESPAsyncWebServer`, `ESPAsyncHTTPUpdateServer` e
`PicoMQTT`. O vídeo usa TCP raw em portas separadas do servidor de
administração.

## Hardware

- ESP32-C3 (testado: QFN32 rev v0.4, 4 MB flash, sem PSRAM, single-core).
- ESP32-S3 (testado: 4 MB flash, 2 MB PSRAM, dual-core 240 MHz).
- ESP32-CAM (AI-Thinker, 4 MB flash, 2 MB PSRAM, CH340; câmera onboard não é
  usada pelo bridge).
- ESP32 DevKit (D0WD-V3, 4 MB flash, sem PSRAM, CP2102).
- USB nativo/JTAG (C3/S3) ou USB-UART CH340/CP2102 (CAM/DevKit).
- WiFi 2.4 GHz.

## Estrutura do projeto

```
firmware/
  platformio.ini
  buildscript_versioning.py     # gera include/version.h e incrementa versioning
  copy_firmware.py              # copia o .bin para a raiz do projeto
  versioning                    # contador de build
  include/
    main.h  config.h  version.h (gerado)
    eeprom.h  web.h  mqtt.h  camera.h  stream.h  dump.h  logger.h
  src/
    main.cpp  eeprom.cpp  web.cpp  mqtt.cpp
    camera.cpp  stream.cpp  dump.cpp  logger.cpp
```

## Build e gravação

```bash
# ESP32-C3
~/.platformio/penv/bin/pio run -d firmware -e esp32c3
~/.platformio/penv/bin/pio run -d firmware -e esp32c3 -t upload
~/.platformio/penv/bin/pio run -d firmware -e esp32c3 -t monitor

# ESP32-S3 (4 MB flash + 2 MB PSRAM)
~/.platformio/penv/bin/pio run -d firmware -e esp32s3
~/.platformio/penv/bin/pio run -d firmware -e esp32s3 -t upload
~/.platformio/penv/bin/pio run -d firmware -e esp32s3 -t monitor

# ESP32-CAM (CH340 em /dev/ttyUSB0, 4 MB flash + 2 MB PSRAM)
~/.platformio/penv/bin/pio run -d firmware -e esp32cam
~/.platformio/penv/bin/pio run -d firmware -e esp32cam -t upload
~/.platformio/penv/bin/pio run -d firmware -e esp32cam -t monitor

# ESP32 DevKit (CP2102 em /dev/ttyUSB0, sem PSRAM)
~/.platformio/penv/bin/pio run -d firmware -e esp32dev
~/.platformio/penv/bin/pio run -d firmware -e esp32dev -t upload
~/.platformio/penv/bin/pio run -d firmware -e esp32dev -t monitor
```

O `platformio.ini` tem uma seção `[env]` comum e quatro envs:

- comuns: `platform = espressif32@7.1.3`, `min_spiffs.csv` (app0/app1 de
  1,875 MB, LittleFS de 128 KB), `littlefs` e as libs;
- `esp32c3`: `board = esp32-c3-devkitm-1`, USB CDC;
- `esp32s3`: `board = esp32-s3-devkitc-1`,
  `board_upload.flash_size = 4MB`, `board_build.arduino.memory_type = qio_qspi`
  e `-DBOARD_HAS_PSRAM`;
- `esp32cam`: `board = esp32cam`, upload 460800, `-DBOARD_HAS_PSRAM`,
  `MAX_ACTIVE_CAMERAS=2` e default name `x5bridge-cam`;
- `esp32dev`: `board = esp32dev`, upload 115200, `MAX_ACTIVE_CAMERAS=1` e
  default name `x5bridge-dev`.

Os `upload_port`/`monitor_port` usam `/dev/serial/by-id/...` no C3/S3 e
`/dev/ttyUSB0` no CAM/DevKit. CAM e DevKit compartilham `/dev/ttyUSB0`;
conecte um por vez. O firmware ocupa ~0,93–1,00 MB (≈48–51% do slot de app). O
`.bin` final fica em `.pio/build/<env>/firmware.bin` e também é copiado para
`firmware/firmware.bin`.

## Primeiro boot e configuração

1. No primeiro boot, o device sobe um AP aberto: **`x5bridge`** (C3/S3),
   **`x5bridge-cam`** (ESP32-CAM) ou **`x5bridge-dev`** (DevKit).
2. Conecte um celular/notebook nesse AP e abra `http://192.168.4.1`.
3. Escolha a rede WiFi e informe a senha.
4. O device reinicia e passa a responder em `http://x5bridge.local/`,
   `http://x5bridge-cam.local/` ou `http://x5bridge-dev.local/`.

A configuração fica em NVS (`Preferences`, namespace `x5bridge`). O portal do
WiFiManager cuida apenas do WiFi; o resto é configurado na página.

### `/config`

- `device_name` — nome do device e raiz dos tópicos MQTT (default `x5bridge`
  no C3/S3, `x5bridge-cam` no CAM e `x5bridge-dev` no DevKit), sanitizado para
  `[A-Za-z0-9_-]`;
- broker MQTT: IP, porta, usuário e senha;
- 4 slots de câmera (`CAM0`..`CAM3`): nome, IP e habilitado;
- portas fixas por slot: `CAM0`=8080, `CAM1`=8081, `CAM2`=8082, `CAM3`=8083;
- câmeras ativas: C3=**2**, S3=**4**, CAM=**2**, DevKit=**1**.

Ao salvar, o device reinicia para aplicar a configuração. O reinício é agendado
(1,5 s) para o servidor terminar de enviar a resposta HTTP.

### `/discover`

Faz `LanSearch` por broadcast e por broadcast da sub-rede, coleta os `PunchPkt`
por ~2 s e lista as câmeras com nome (extraído do pacote) e IP. A atribuição a
um slot salva o IP, o nome e o `punch` e reinicia o device.

Se a câmera estiver ocupada (por exemplo, com `linux/serve` ou o app oficial),
ela pode não responder ao `LanSearch` e não aparecer na lista.

## Endpoints

| Porta | Caminho | Função |
|---|---|---|
| 80 | `/` | status: versão, IP, MAC, uptime, heap, RSSI, broker, câmeras e links |
| 80 | `/config` | configuração (GET/POST) |
| 80 | `/discover` | descoberta de câmeras |
| 80 | `/info` | dumps de ESP32, FS e configuração |
| 80 | `/logs` | logs (RAM + `/system.log` no LittleFS); `?clear=1` limpa |
| 80 | `/update` | OTA multipart `.bin` |
| 80 | `/reboot` | reinicia |
| 80 | `/reset` | apaga LittleFS, configuração e WiFi (não testado em produção) |
| 8080+ | `/video` | MJPEG contínuo |
| 8080+ | `/snapshot` | JPEG único |

`/video` responde com `multipart/x-mixed-replace; boundary=x5frame`. O primeiro
frame já vem completo. `/snapshot` responde `503` enquanto não houver frame.

## MQTT

O cliente usa `PicoMQTT::Client`. A raiz é `device_name` (default `x5bridge`) e
o anúncio é repetido a cada 5 s (`MQTT_ANNOUNCE_INTERVAL_MS`).

```
<root>/DESCRIPTION   <root>/IP          <root>/STATUS
<root>/ADMIN         <root>/MAC         <root>/UPTIME
<root>/HEAP          <root>/RSSI
<root>/CAM0/DESCRIPTION  <root>/CAM0/IP       <root>/CAM0/STATUS
<root>/CAM0/PORT         <root>/CAM0/URL      <root>/CAM0/SNAPSHOT
<root>/CAM0/CLIENTS      <root>/CAM0/FPS      <root>/CAM0/LAST_FRAME
```

- `DESCRIPTION` = `<device_name> <VERSION>`, com versão, data, hash e branch
  gerados no build.
- `STATUS` = `ONLINE`; o LWT publica `OFFLINE`.
- `CAMn/URL` e `CAMn/SNAPSHOT` já apontam para o IP atual.
- O broker testado (`192.168.0.250`) não preserva `retained`; por isso o
  anúncio periódico curto. Em um broker que preserve, os tópicos funcionam como
  retained.

## Arquitetura

### Tarefas

- **`camera_task`** (prioridade 5, core 0): sockets UDP por câmera, `poll`, ACK
  de cada pacote, keepalive (`P2PAlive`), extração do ticket, envio de VPS/SV e
  montagem dos JPEGs. Timeout de 5 s sem pacotes → relogin.
- **`stream_task`** (prioridade 4, core 1 no S3/CAM/DevKit e core 0 no C3):
  listeners TCP 8080+, parser HTTP mínimo, envio com timeout de socket (1 s) e
  descarte/limite de clientes.
- **Loop principal**: `handle_web()` (mDNS é do stack) e `handle_mqtt()`;
  verifica o reinício agendado.
- **Async web server** e **PicoMQTT** rodam em suas próprias tarefas.

### Memória e buffers

- Cada câmera ativa aloca **2 × 24 KB** (montagem + último JPEG).
- No S3 e no ESP32-CAM esses buffers vão para a **PSRAM** (fallback para RAM
  interna); no C3/DevKit ficam na RAM interna.
- O `stream_task` usa um buffer compartilhado de **24 KB** para enviar sem
  segurar o mutex da câmera.
- Limites: `MAX_ACTIVE_CAMERAS` = C3 **2**, S3 **4**, CAM **2**, DevKit **1**;
  `MAX_CLIENTS_PER_STREAM = 2`.
- C3 com uma câmera + um cliente: heap ~70–77 KB livre (mín. ~50 KB).
- S3 com uma câmera + um cliente: heap ~200 KB livre e PSRAM ~2 MB livre.
- ESP32-CAM com uma câmera + um cliente: heap ~146–158 KB livre e PSRAM
  ~2,05 MB (buffers na PSRAM).
- ESP32 DevKit com uma câmera: heap ~107 KB livre (mín. ~86 KB), buffers na RAM
  interna.
- A segunda câmera só inicia se a alocação couber; caso contrário o slot fica
  offline com log de `BUFFER FAIL`.

### Sessão da câmera

Fluxo implementado em `camera.cpp`:

```
LanSearch -> PunchPkt -> P2pRdy -> login 180B -> DrwAck
          -> ConnectUserAck (ticket) -> VideoParamSet + StartVideo
          -> stream=1 (ACK por pacote) + P2PAlive
```

O login de 180 bytes é hardcoded (copiado do app oficial). A sessão não é
encerrada quando não há espectador; a câmera continua enviando e o firmware
mantém o último frame para `/snapshot` e para novos clientes.

## Versionamento

- `buildscript_versioning.py` roda como `pre:` e:
  - incrementa o arquivo `versioning`;
  - gera `include/version.h` com `VERSION`, `BUILD_NUMBER` e `VERSION_SHORT`
    no formato `v0.1.N - data [hash|branch]`.
- `copy_firmware.py` roda como `post:` e copia o `.bin` para a raiz de
  `firmware/`.
- `include/version.h` e `firmware.bin` são ignorados pelo git.

## Validação

Executada com a câmera `BATE558596HDLZB` em `192.168.0.10`:

| Item | C3 | S3 | CAM | DevKit |
|---|---|---|---|---|
| Build/flash | OK (~0,98 MB) | OK (~0,93 MB) | OK (~1,00 MB) | OK (~0,99 MB) |
| WiFi + portal + mDNS | OK | OK | OK | OK |
| PSRAM + dual-core | — | OK (2 MB) | OK (2 MB) | — |
| `discover` | OK | OK | OK | OK |
| `/video` e `/snapshot` | OK | OK | OK | OK |
| MQTT | OK | OK | OK | OK |
| OTA `.bin` | OK | OK | OK | OK |
| `/config` + reinício | OK | OK | OK | OK |
| `/reboot` | OK | OK | OK | OK |
| Soak 10 min | OK, sem reboot | não repetido | não repetido | não repetido |
| `/reset` | não testado | não testado | não testado | não testado |
| 2 câmeras | não testado | não testado | não testado | não testado |

Medições:

- câmera nativa (Linux `session`): ~11,9 fps (358 frames/30 s);
- C3 (RSSI ~-50 dBm): ~10 fps sem cliente; ~7,7 fps com 1 cliente; heap ~87 KB
  livre (mín. ~50 KB);
- S3 (RSSI ~-64/-65 dBm): ~9 fps sem cliente; ~7,5 fps com 1 cliente; heap
  ~204 KB livre e PSRAM ~2 MB;
- ESP32-CAM (RSSI ~-57/-58 dBm): ~7,7 fps sem cliente; ~6,8 fps com 1 cliente;
  heap ~158 KB livre e PSRAM ~2,05 MB;
- ESP32 DevKit (RSSI ~-48 dBm): ~5,9 fps sem cliente; ~4,3 fps com 1 cliente;
  heap ~161 KB ocioso / ~107 KB com câmera (mín. ~86 KB);
- JPEG ~18–22 KB;
- soak no C3: 601→6103 frames (~9,6 fps médio), heap estável.

Os ~16 fps do doc original não se reproduziram nesta câmera; o modo atual entrega
~12 fps. No S3, o FPS não subiu porque o RSSI estava ~15 dB pior que o do C3; o
limitador foi o WiFi, não CPU/RAM. No CAM/DevKit, o rádio ESP32 clássico limita o
FPS; a PSRAM do CAM ajuda a memória, mas não o rádio.

## Limitações e pendências

- Admin/OTA abertos, sem autenticação (decisão de projeto; LAN).
- `/reset` implementado, mas não testado em produção.
- Reconexão após queda de energia da câmera não foi testada explicitamente.
- Segunda câmera real não testada. Limite: C3=2, S3=4, CAM=2, DevKit=1; cada
  stream aceita 2 clientes.
- Broker sem `retained` no teste.
- FPS abaixo do documentado originalmente (~12 fps nativos nesta câmera).
- S3 testado com RSSI ~-64/-65 dBm contra ~-50 dBm do C3; o FPS ficou limitado
  pelo WiFi. Com antena/posição melhor, o S3 deve se aproximar do teto (~12 fps).
- ESP32-CAM: a câmera onboard da board usada como bridge está quebrada e não é
  usada; o bridge usa a câmera X5 externa. O rádio ESP32 clássico limita o FPS
  (~7,7/6,8). Veja [`../docs/esp32cam-boards.md`](../docs/esp32cam-boards.md).
- ESP32 DevKit: sem PSRAM, 1 câmera ativa e ~5,9/4,3 fps; serve como bancada de
  teste, não como alvo de performance.

## Troubleshooting

- **Portal não aparece**: aguarde o timeout de conexão; o AP é `x5bridge`
  (C3/S3), `x5bridge-cam` (CAM) ou `x5bridge-dev` (DevKit). Se necessário,
  redefina configuração e WiFi via `/reset`.
- **Câmera não aparece no `discover`**: verifique se ela está na mesma sub-rede
  e se não está ocupada por `linux/serve`, `linux/session` ou pelo app oficial.
- **`/video` abre mas não mostra imagem**: confira `/snapshot`; se retornar
  `503`, a câmera ainda não produziu frame (login/ticket).
- **MQTT não conecta**: confirme broker/porta/usuário/senha em `/config` e se o
  broker aceita conexões anônimas.
- **OTA falha**: use `.bin` (não `.gz`); o `Update` do ESP32 não descomprime
  gzip.
- **S3 com FPS baixo/RSSI ruim**: confira a antena (PCB vs externa/jumper) e
  aproxime do AP; o S3 testado ficou ~15 dB pior que o C3.
- **ESP32-CAM/DevKit não aparecem**: os dois usam `/dev/ttyUSB0`; conecte um por
  vez. Upload do CAM em 460800 e do DevKit em 115200.
- **ESP32-CAM reinicia/boot loop**: confira o flat/ribbon da câmera onboard e a
  alimentação 5 V; o bridge não usa a câmera onboard. Veja
  [`../docs/esp32cam-boards.md`](../docs/esp32cam-boards.md).
- **Não rode `linux/serve`/`linux/session` junto com o bridge**: a câmera atende
  uma sessão por vez.

## Documentação relacionada

- [`../docs/protocol.md`](../docs/protocol.md) — protocolo.
- [`../docs/esp32-bringup.md`](../docs/esp32-bringup.md) — bring-up e medições.
- [`../docs/esp32cam-boards.md`](../docs/esp32cam-boards.md) — comparativo das
  três ESP32-CAM.
- [`../linux/README.md`](../linux/README.md) — referência Linux.
