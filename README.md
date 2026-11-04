# X5 Bridge (YunSync / 2BK-2)

Bridge para câmeras **X5 (YsxLite)** baseado em engenharia reversa do protocolo
P2P proprietário. O repositório reúne a documentação do protocolo, um firmware
funcional para **ESP32-C3, ESP32-S3, ESP32-CAM e ESP32 DevKit** e uma
implementação de referência para Linux.

O firmware entrega:

- descoberta de câmeras na LAN;
- login/ticket e sessão P2P contínua;
- vídeo MJPEG em `/video` e JPEG em `/snapshot` (uma porta por câmera);
- página web de status, configuração, descoberta, logs e OTA;
- anúncio MQTT no padrão dos outros devices da casa;
- mDNS (`x5bridge.local`) e portal cativo para configuração de WiFi.

## Status

- Firmware validado em **ESP32-C3** (v0.1.10), **ESP32-S3** (v0.1.27),
  **ESP32-CAM** (v0.1.26) e **ESP32 DevKit** (v0.1.23) com a câmera
  `BATE558596HDLZB` em `192.168.0.10`.
- C3: heap ~87 KB livre, ~10 fps sem cliente e ~7,7 fps com 1 cliente.
- S3: 2 MB de PSRAM ativa, heap ~204 KB livre, buffers de frame na PSRAM e
  tasks nos dois cores; ~9 fps sem cliente e ~7,5 fps com 1 cliente.
- ESP32-CAM (board 3, câmera onboard quebrada e não usada): 2 MB PSRAM, heap
  ~158 KB, buffers na PSRAM; ~7,7 fps sem cliente e ~6,8 fps com 1 cliente.
- ESP32 DevKit (sem PSRAM): heap ~161 KB ocioso / ~107 KB com câmera; ~5,9 fps
  sem cliente e ~4,3 fps com 1 cliente.
- Teste com **2 câmeras**: no ESP32-CAM as duas funcionam, mas o rádio clássico
  satura (CAM0 4,07/2,03 fps; CAM1 6,73/6,13 fps ocioso/cliente). No S3, a
  CAM1 (`.71`) funcionou bem; a CAM0 (`.10`) ficou ONLINE com ticket mas 0
  frames, exigindo power-cycle/sequência limpa.
- A antena externa do ESP32-CAM não melhorou o RSSI no teste (-58/-61 dBm);
  provavelmente a seleção PCB/externa não está ativa.
- O S3 ficou com RSSI ~-64/-65 dBm contra ~-50 dBm do C3; o FPS ficou limitado
  pelo WiFi, não por CPU/RAM. O ESP32-CAM/DevKit usam o rádio ESP32 clássico e
  ficam naturalmente abaixo de C3/S3.
- Streaming MJPEG, snapshot, MQTT, OTA, `discover`, `/config` e `/reboot`
  testados nos quatro alvos.
- Soak de 10 min no C3 sem reboot e com a câmera online o tempo todo; não
  repetido nos demais.
- `/reset` não testado para não apagar WiFi/config.
- Teste de 2 câmeras feito no ESP32-CAM e parcialmente no S3; ver detalhes
  abaixo. A câmera `.10` pode ficar ONLINE com ticket e 0 frames após trocas de
  bridge; power-cycle e re-login limpo resolvem.

## Mapa do repositório

| Caminho | Conteúdo |
|---|---|
| [`firmware/`](firmware/) | firmware PlatformIO (C3, S3, ESP32-CAM, ESP32 DevKit) |
| [`linux/`](linux/) | referência Linux (`serve.cpp`, `session.cpp`, `Makefile`) |
| [`docs/protocol.md`](docs/protocol.md) | engenharia reversa completa do protocolo |
| [`docs/esp32-bringup.md`](docs/esp32-bringup.md) | medições de hardware e bring-up |
| [`docs/esp32cam-boards.md`](docs/esp32cam-boards.md) | comparativo das três ESP32-CAM |
| [`tools/esp-probe/`](tools/esp-probe/) | firmware de diagnóstico reutilizável |
| [`firmware/README.md`](firmware/README.md) | build, configuração, endpoints e arquitetura |
| [`linux/README.md`](linux/README.md) | como compilar e usar a referência Linux |

## Quick start

### 1. Build e gravação

```bash
# ESP32-C3
~/.platformio/penv/bin/pio run -d firmware -e esp32c3
~/.platformio/penv/bin/pio run -d firmware -e esp32c3 -t upload

# ESP32-S3 (4 MB flash + 2 MB PSRAM)
~/.platformio/penv/bin/pio run -d firmware -e esp32s3
~/.platformio/penv/bin/pio run -d firmware -e esp32s3 -t upload

# ESP32-CAM (4 MB flash + 2 MB PSRAM, CH340 em /dev/ttyUSB0)
~/.platformio/penv/bin/pio run -d firmware -e esp32cam
~/.platformio/penv/bin/pio run -d firmware -e esp32cam -t upload

# ESP32 DevKit (D0WD-V3, sem PSRAM, CP2102 em /dev/ttyUSB0)
~/.platformio/penv/bin/pio run -d firmware -e esp32dev
~/.platformio/penv/bin/pio run -d firmware -e esp32dev -t upload
```

O `platformio.ini` tem quatro envs (`esp32c3`, `esp32s3`, `esp32cam` e
`esp32dev`) com `min_spiffs.csv`, `littlefs` e portas
`upload_port`/`monitor_port`. Ajuste os caminhos se os boards aparecerem em
outra porta. No S3, o env habilita `board_upload.flash_size = 4MB`,
`board_build.arduino.memory_type = qio_qspi` e `-DBOARD_HAS_PSRAM`. No CAM,
`board = esp32cam`, upload 460800, PSRAM e `MAX_ACTIVE_CAMERAS=2`. No DevKit,
`board = esp32dev`, upload 115200, sem PSRAM e `MAX_ACTIVE_CAMERAS=1`. CAM e
DevKit compartilham `/dev/ttyUSB0`; conecte um por vez. O binário também é
copiado para `firmware/firmware.bin`.

### 2. Primeiro boot e WiFi

No primeiro boot o device sobe um AP aberto: **`x5bridge`** (C3/S3),
**`x5bridge-cam`** (ESP32-CAM) ou **`x5bridge-dev`** (DevKit):

1. conecte um celular/notebook nesse AP;
2. abra `http://192.168.4.1`;
3. escolha a rede WiFi, informe a senha e salve.

O device reinicia, entra na rede e responde em `http://x5bridge.local/`,
`http://x5bridge-cam.local/` ou `http://x5bridge-dev.local/`.
O IP também aparece na página inicial e no serial (115200).

### 3. Câmera

1. abra `http://<ip-do-bridge>/discover`;
2. o bridge faz `LanSearch` por broadcast e lista as câmeras encontradas;
3. atribua a câmera a um slot (`CAM0` → porta 8080, `CAM1` → 8081, ...);
4. o device reinicia e começa a sessão P2P.

Também é possível informar o IP manualmente em `/config`.

### 4. Vídeo

| URL | Formato |
|---|---|
| `http://<ip>:8080/video` | MJPEG contínuo |
| `http://<ip>:8080/snapshot` | JPEG único |
| `http://<ip>:8081/video` | segunda câmera (se configurada) |

A resolução é fixa em **640x480**.

### 5. MQTT

O broker padrão é `192.168.0.250:1883` (configurável em `/config`). A raiz dos
tópicos é o `device_name` (default `x5bridge`):

```text
x5bridge/DESCRIPTION   x5bridge/IP          x5bridge/STATUS
x5bridge/ADMIN         x5bridge/MAC         x5bridge/UPTIME
x5bridge/HEAP          x5bridge/RSSI
x5bridge/CAM0/DESCRIPTION  x5bridge/CAM0/IP       x5bridge/CAM0/STATUS
x5bridge/CAM0/PORT         x5bridge/CAM0/URL      x5bridge/CAM0/SNAPSHOT
x5bridge/CAM0/CLIENTS      x5bridge/CAM0/FPS      x5bridge/CAM0/LAST_FRAME
```

O anúncio é repetido a cada 5 s; `STATUS` tem LWT `OFFLINE`. O broker usado no
teste não preserva mensagens `retained`, por isso o período curto.

## Arquitetura resumida

- **Câmera:** um socket UDP por sessão; `LanSearch` → `PunchPkt` → `P2pRdy` →
  login → ticket → `VideoParamSet` + `StartVideo` → mídia. A sessão fica viva
  (não há comando de stop); se ficar 5 s sem pacotes, o firmware refaz o login.
- **Vídeo:** servidor TCP raw em 8080+ com parser HTTP mínimo. O frame JPEG é
  montado em buffers de 24 KB e enviado com descarte/limite para cliente lento.
- **Admin:** `ESPAsyncWebServer` na porta 80, com `ESPAsyncWiFiManager` para o
  portal cativo, mDNS e `ESPAsyncHTTPUpdateServer` para OTA `.bin`.
- **MQTT:** `PicoMQTT` com anúncio periódico e LWT.
- **Persistência:** `Preferences` (NVS) para device name, broker e slots de
  câmera; LittleFS para logs.
- **Alvos:** o mesmo código roda em C3, S3, ESP32-CAM e ESP32 DevKit. No S3 e
  no CAM os buffers de frame vão para PSRAM; no C3/DevKit ficam na RAM interna.
  Em dual-core, câmera no core 0 e stream no core 1; no C3 tudo no core 0.
  Limite de câmeras ativas: C3=2, S3=4, CAM=2, DevKit=1.

Detalhes de build, endpoints, memória e limitações estão em
[`firmware/README.md`](firmware/README.md).

## Validação

| Item | C3 | S3 | CAM | DevKit |
|---|---|---|---|---|
| Build/flash | OK | OK | OK | OK |
| WiFi + portal + mDNS | OK | OK | OK | OK |
| PSRAM + dual-core | — | OK (2 MB) | OK (2 MB) | — |
| `discover` da câmera | OK | OK | OK | OK |
| `/video` e `/snapshot` | OK | OK | OK | OK |
| MQTT | OK | OK | OK | OK |
| OTA `.bin` via `/update` | OK | OK | OK | OK |
| `/config` + reinício | OK | OK | OK | OK |
| `/reboot` | OK | OK | OK | OK |
| Soak de 10 min | OK | não repetido | não repetido | não repetido |
| `/reset` | não testado | não testado | não testado | não testado |
| 2 câmeras simultâneas | não testado | parcial (CAM1 OK; CAM0 travou) | OK (baixo FPS) | não testado |

Medições com uma câmera:

- câmera nativa medida pelo Linux: **~11,9 fps**;
- **C3** (RSSI ~-50 dBm): ~10 fps sem cliente; ~7,7 fps com 1 cliente; heap
  ~87 KB livre, mínimo ~50 KB;
- **S3** (RSSI ~-64/-65 dBm): ~9 fps sem cliente; ~7,5 fps com 1 cliente; heap
  ~204 KB livre e PSRAM ~2 MB (buffers de frame na PSRAM);
- **ESP32-CAM** (RSSI ~-57/-58 dBm): ~7,7 fps sem cliente; ~6,8 fps com 1
  cliente; heap ~158 KB livre e PSRAM ~2,05 MB (buffers na PSRAM);
- **ESP32 DevKit** (RSSI ~-48 dBm): ~5,9 fps sem cliente; ~4,3 fps com 1
  cliente; heap ~161 KB ocioso / ~107 KB com câmera (mín. ~86 KB), buffers na
  RAM interna;
- **2 câmeras** no ESP32-CAM (RSSI ~-57/-58): CAM0 4,07 fps ocioso / 2,03 com
  cliente; CAM1 6,73 / 6,13; heap ~148 KB (mín. ~115 KB), PSRAM ~2,0 MB;
- **2 câmeras** no S3: CAM1 `.71` ~9–15 fps inicial; CAM0 `.10` ONLINE com
  0 frames (sessão travada);
- antena externa do ESP32-CAM: RSSI não melhorou (-58/-61 dBm);
- JPEG de ~18–22 KB; soak de 10 min no C3 com ~9,6 fps médio.

Os ~16 fps do documento original não se reproduziram nesta câmera; o modo atual
entrega ~12 fps. No S3, o FPS não subiu porque o RSSI estava ~15 dB pior que o
do C3; o limitador foi o WiFi, não CPU/RAM. No CAM/DevKit, o rádio ESP32
clássico limita o FPS; a PSRAM do CAM ajuda a memória, mas não o rádio.

## Limitações e pendências

- Admin e OTA **sem autenticação** (decisão de projeto; uso em LAN).
- `/reset` não testado para não derrubar a configuração atual.
- Reconexão após queda de energia da câmera não foi testada explicitamente; o
  firmware tem timeout e relogin.
- Teste com 2 câmeras: ESP32-CAM funcional mas com FPS baixo (rádio clássico);
  S3 parcial (CAM1 `.71` OK, CAM0 `.10` travou). Limite: C3=2, S3=4, CAM=2,
  DevKit=1; cada stream aceita 2 clientes. Com 1 câmera + 1 cliente o C3 fica
  com ~50 KB mínimos.
- Câmera `.10` pode ficar ONLINE (com ticket) e **0 frames** após trocas de
  bridge; power-cycle e re-login limpo resolvem. A `.71` também precisou de
  power-cycle uma vez.
- Antena externa do ESP32-CAM: RSSI não melhorou (-58/-61); verificar seleção
  PCB/externa.
- O broker em `192.168.0.250` não preserva `retained`; se isso for necessário,
  trocar/ajustar o broker.
- FPS abaixo do documentado originalmente (~12 fps nativos nesta câmera).
- S3 testado com RSSI ~-64/-65 dBm contra ~-50 dBm do C3. Com antena/posição
  melhor, o S3 deve se aproximar do teto da câmera (~12 fps).
- ESP32-CAM: a board 3 tem a câmera onboard quebrada (não usada); o bridge usa
  a câmera X5 externa. O rádio ESP32 clássico limita o FPS (~7,7/6,8).
- ESP32 DevKit: sem PSRAM, 1 câmera ativa e ~5,9/4,3 fps; serve como bancada de
  teste, não como alvo de performance.
- Não rode dois bridges na mesma câmera; ela atende uma sessão por vez.

## Documentação

- [`docs/protocol.md`](docs/protocol.md) — protocolo completo (discovery,
  handshake, login, ticket, criptografia, VPS/SV, mídia).
- [`docs/esp32-bringup.md`](docs/esp32-bringup.md) — medições de C3/S3/CAM/DevKit
  e decisões de port.
- [`docs/esp32cam-boards.md`](docs/esp32cam-boards.md) — comparativo das três
  ESP32-CAM e escolha da board do bridge.
- [`firmware/README.md`](firmware/README.md) — firmware.
- [`linux/README.md`](linux/README.md) — referência Linux.

## Referências

- [cam-reverse](https://github.com/DavidVentura/cam-reverse) — engenharia reversa
  original do app.
- [YunSync / 2BK-2](https://github.com/DavidVentura/cam-reverse/issues) —
  discussões sobre o protocolo.
