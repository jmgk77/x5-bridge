# Firmware ESP32-C3 — X5 Bridge

Firmware do bridge para câmeras X5, alvo **ESP32-C3** (4 MB flash, sem PSRAM).
Usa o mesmo stack dos outros devices da casa: `ESPAsyncWiFiManager`,
`ESPAsyncWebServer`, `ESPAsyncHTTPUpdateServer` e `PicoMQTT`. O vídeo usa TCP
raw em portas separadas do servidor de administração.

## Hardware

- ESP32-C3 (testado: QFN32 rev v0.4, 4 MB flash, sem PSRAM).
- USB nativo/JTAG; a gravação usa `/dev/ttyACM0` por padrão.
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
# build
~/.platformio/penv/bin/pio run -d firmware

# gravar no C3
~/.platformio/penv/bin/pio run -d firmware -t upload

# monitor serial
~/.platformio/penv/bin/pio run -d firmware -t monitor
```

O `platformio.ini` usa:

- `platform = espressif32@7.1.3`, `board = esp32-c3-devkitm-1`;
- `board_build.partitions = min_spiffs.csv` (app0/app1 de 1,875 MB, LittleFS de
  128 KB);
- `board_build.filesystem = littlefs`;
- `-DARDUINO_USB_MODE=1` e `-DARDUINO_USB_CDC_ON_BOOT=1`;
- `upload_port = /dev/ttyACM0` (ajuste se necessário).

O firmware ocupa ~0,98 MB (≈50% do slot de app). O `.bin` final fica em
`.pio/build/esp32c3/firmware.bin` e também é copiado para
`firmware/firmware.bin`.

## Primeiro boot e configuração

1. No primeiro boot, o C3 sobe o AP aberto **`x5bridge`**.
2. Conecte um celular/notebook nesse AP e abra `http://192.168.4.1`.
3. Escolha a rede WiFi e informe a senha.
4. O device reinicia e passa a responder em `http://x5bridge.local/`.

A configuração fica em NVS (`Preferences`, namespace `x5bridge`). O portal do
WiFiManager cuida apenas do WiFi; o resto é configurado na página.

### `/config`

- `device_name` — nome do device e raiz dos tópicos MQTT (default `x5bridge`,
  sanitizado para `[A-Za-z0-9_-]`);
- broker MQTT: IP, porta, usuário e senha;
- 4 slots de câmera (`CAM0`..`CAM3`): nome, IP e habilitado;
- portas fixas por slot: `CAM0`=8080, `CAM1`=8081, `CAM2`=8082, `CAM3`=8083.

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

- **`camera_task`** (prioridade 5): sockets UDP por câmera, `poll`, ACK de
  cada pacote, keepalive (`P2PAlive`), extração do ticket, envio de VPS/SV e
  montagem dos JPEGs. Timeout de 5 s sem pacotes → relogin.
- **`stream_task`** (prioridade 4): listeners TCP 8080+, parser HTTP mínimo,
  envio com timeout de socket (1 s) e descarte/limite de clientes.
- **Loop principal**: `handle_web()` (mDNS é do stack) e `handle_mqtt()`;
  verifica o reinício agendado.
- **Async web server** e **PicoMQTT** rodam em suas próprias tarefas.

### Memória e buffers

- Cada câmera ativa aloca **2 × 24 KB** (montagem + último JPEG).
- O `stream_task` usa um buffer compartilhado de **24 KB** para enviar sem
  segurar o mutex da câmera.
- Limites: `MAX_ACTIVE_CAMERAS = 2`, `MAX_CLIENTS_PER_STREAM = 2`.
- Com uma câmera + um cliente, o heap livre fica em ~70–77 KB (mín. ~50 KB).
  A segunda câmera só inicia se a alocação couber; caso contrário o slot fica
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

| Item | Resultado |
|---|---|
| Build/flash | OK (~0,98 MB, 50% do slot) |
| WiFi + portal + mDNS | OK |
| `discover` | OK |
| `/video` e `/snapshot` | OK |
| MQTT | OK (`x5bridge/...` e `x5bridge/CAM0/...`) |
| OTA `.bin` | OK |
| `/config` + reinício | OK |
| `/reboot` | OK |
| Soak 10 min | OK, sem reboot |
| `/reset` | não testado (apaga WiFi/config) |
| 2 câmeras | não testado (sem hardware) |

Medições:

- câmera nativa (Linux `session`): ~11,9 fps (358 frames/30 s);
- C3 sem cliente: ~10 fps, heap ~87 KB livre;
- C3 com 1 cliente: ~7,7 fps, heap ~70–77 KB livre (mín. ~50 KB);
- JPEG ~18–22 KB;
- soak: 601→6103 frames (~9,6 fps médio), heap estável.

Os ~16 fps do doc original não se reproduziram nesta câmera; o modo atual entrega
~12 fps.

## Limitações e pendências

- Admin/OTA abertos, sem autenticação (decisão de projeto; LAN).
- `/reset` implementado, mas não testado em produção.
- Reconexão após queda de energia da câmera não foi testada explicitamente.
- Segunda câmera real não testada; memória é o limitador.
- Broker sem `retained` no teste.
- FPS abaixo do documentado originalmente.

## Troubleshooting

- **Portal não aparece**: aguarde o timeout de conexão; o AP `x5bridge` leva
  alguns segundos. Se necessário, redefina configuração e WiFi via `/reset`.
- **Câmera não aparece no `discover`**: verifique se ela está na mesma sub-rede
  e se não está ocupada por `linux/serve`, `linux/session` ou pelo app oficial.
- **`/video` abre mas não mostra imagem**: confira `/snapshot`; se retornar
  `503`, a câmera ainda não produziu frame (login/ticket).
- **MQTT não conecta**: confirme broker/porta/usuário/senha em `/config` e se o
  broker aceita conexões anônimas.
- **OTA falha**: use `.bin` (não `.gz`); o `Update` do ESP32 não descomprime
  gzip.
- **Não rode `linux/serve`/`linux/session` junto com o bridge**: a câmera atende
  uma sessão por vez.

## Documentação relacionada

- [`../docs/protocol.md`](../docs/protocol.md) — protocolo.
- [`../docs/esp32-bringup.md`](../docs/esp32-bringup.md) — bring-up e medições.
- [`../linux/README.md`](../linux/README.md) — referência Linux.
