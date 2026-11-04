# X5 Bridge (YunSync / 2BK-2)

Bridge para câmeras **X5 (YsxLite)** baseado em engenharia reversa do protocolo
P2P proprietário. O repositório reúne a documentação do protocolo, um firmware
funcional para **ESP32-C3** e uma implementação de referência para Linux.

O firmware entrega:

- descoberta de câmeras na LAN;
- login/ticket e sessão P2P contínua;
- vídeo MJPEG em `/video` e JPEG em `/snapshot` (uma porta por câmera);
- página web de status, configuração, descoberta, logs e OTA;
- anúncio MQTT no padrão dos outros devices da casa;
- mDNS (`x5bridge.local`) e portal cativo para configuração de WiFi.

## Status

- Firmware validado em **ESP32-C3** (build v0.1.10) e **ESP32-S3** (build
  v0.1.13) com a câmera `BATE558596HDLZB` em `192.168.0.10`.
- C3: heap ~87 KB livre, ~10 fps sem cliente e ~7,7 fps com 1 cliente.
- S3: 2 MB de PSRAM ativa, heap ~204 KB livre, buffers de frame na PSRAM e
  tasks nos dois cores; ~9 fps sem cliente e ~7,5 fps com 1 cliente.
- No teste, o S3 ficou com RSSI ~-64/-65 dBm contra ~-50 dBm do C3; o FPS ficou
  limitado pelo WiFi, não por CPU/RAM.
- Streaming MJPEG, snapshot, MQTT, OTA, `discover`, `/config` e `/reboot`
  testados nos dois alvos.
- Soak de 10 min no C3 sem reboot e com a câmera online o tempo todo.
- `/reset` não testado para não apagar WiFi/config.
- Segunda câmera simultânea não testada (só havia uma câmera disponível).

## Mapa do repositório

| Caminho | Conteúdo |
|---|---|
| [`firmware/`](firmware/) | firmware PlatformIO do ESP32-C3 |
| [`linux/`](linux/) | referência Linux (`serve.cpp`, `session.cpp`, `Makefile`) |
| [`docs/protocol.md`](docs/protocol.md) | engenharia reversa completa do protocolo |
| [`docs/esp32-bringup.md`](docs/esp32-bringup.md) | medições de hardware e bring-up |
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
```

O `platformio.ini` tem dois envs (`esp32c3` e `esp32s3`) com
`min_spiffs.csv`, `littlefs` e portas `upload_port`/`monitor_port` por
`/dev/serial/by-id`. Ajuste os caminhos se os boards aparecerem em outra porta.
No S3, o env habilita `board_upload.flash_size = 4MB`,
`board_build.arduino.memory_type = qio_qspi` e `-DBOARD_HAS_PSRAM`.
O binário também é copiado para `firmware/firmware.bin`.

### 2. Primeiro boot e WiFi

No primeiro boot o C3 sobe um AP aberto chamado **`x5bridge`**:

1. conecte um celular/notebook nesse AP;
2. abra `http://192.168.4.1`;
3. escolha a rede WiFi, informe a senha e salve.

O device reinicia, entra na rede e responde em `http://x5bridge.local/`.
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
- **Alvos:** o mesmo código roda no C3 e no S3. No S3 os buffers de frame vão
  para PSRAM e as tasks são fixadas no core 0 (câmera) e core 1 (stream); no C3
  tudo fica no core 0 e na RAM interna.

Detalhes de build, endpoints, memória e limitações estão em
[`firmware/README.md`](firmware/README.md).

## Validação

| Item | C3 | S3 |
|---|---|---|
| Build/flash | OK | OK |
| WiFi + portal + mDNS | OK | OK |
| PSRAM + dual-core | — | OK (2 MB, tasks nos cores 0/1) |
| `discover` da câmera | OK | OK |
| `/video` e `/snapshot` | OK | OK |
| MQTT `x5bridge/...` | OK | OK |
| OTA `.bin` via `/update` | OK | OK |
| `/config` + reinício | OK | OK |
| `/reboot` | OK | OK |
| Soak de 10 min | OK, sem reboot | não repetido |
| `/reset` | não testado | não testado |
| 2 câmeras simultâneas | não testado | não testado |

Medições com uma câmera:

- câmera nativa medida pelo Linux: **~11,9 fps**;
- **C3** (RSSI ~-50 dBm): ~10 fps sem cliente; ~7,7 fps com 1 cliente; heap
  ~87 KB livre, mínimo ~50 KB;
- **S3** (RSSI ~-64/-65 dBm): ~9 fps sem cliente; ~7,5 fps com 1 cliente; heap
  ~204 KB livre e PSRAM ~2 MB (buffers de frame na PSRAM);
- JPEG de ~18–22 KB; soak de 10 min no C3 com ~9,6 fps médio.

Os ~16 fps do documento original não se reproduziram nesta câmera; o modo atual
entrega ~12 fps. No S3, o FPS não subiu porque o RSSI estava ~15 dB pior que o
do C3; o limitador foi o WiFi, não CPU/RAM.

## Limitações e pendências

- Admin e OTA **sem autenticação** (decisão de projeto; uso em LAN).
- `/reset` não testado para não derrubar a configuração atual.
- Reconexão após queda de energia da câmera não foi testada explicitamente; o
  firmware tem timeout e relogin.
- Segunda câmera real não testada. O limite é 2 ativas no C3 e 4 no S3
  (PSRAM); cada stream aceita 2 clientes. Com 1 câmera + 1 cliente o C3 fica
  com ~50 KB mínimos; a segunda câmera só inicia se a alocação couber.
- O broker em `192.168.0.250` não preserva `retained`; se isso for necessário,
  trocar/ajustar o broker.
- FPS abaixo do documentado originalmente (~12 fps nativos nesta câmera).
- S3 testado com RSSI ~-64/-65 dBm contra ~-50 dBm do C3. Com antena/posição
  melhor, o S3 deve se aproximar do teto da câmera (~12 fps).

## Documentação

- [`docs/protocol.md`](docs/protocol.md) — protocolo completo (discovery,
  handshake, login, ticket, criptografia, VPS/SV, mídia).
- [`docs/esp32-bringup.md`](docs/esp32-bringup.md) — medições de C3/S3/CAM/DevKit
  e decisões de port.
- [`firmware/README.md`](firmware/README.md) — firmware.
- [`linux/README.md`](linux/README.md) — referência Linux.

## Referências

- [cam-reverse](https://github.com/DavidVentura/cam-reverse) — engenharia reversa
  original do app.
- [YunSync / 2BK-2](https://github.com/DavidVentura/cam-reverse/issues) —
  discussões sobre o protocolo.
