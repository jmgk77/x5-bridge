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

- Firmware **v0.1.10** validado em ESP32-C3 (4 MB flash, sem PSRAM) com a câmera
  `BATE558596HDLZB` em `192.168.0.10`.
- Streaming MJPEG, snapshot, MQTT, OTA, `discover`, `/config` e `/reboot`
  testados.
- Soak de 10 min sem reboot e com a câmera online o tempo todo.
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
~/.platformio/penv/bin/pio run -d firmware
~/.platformio/penv/bin/pio run -d firmware -t upload
```

O `platformio.ini` usa `esp32-c3-devkitm-1`, `min_spiffs.csv` e
`/dev/ttyACM0`. Ajuste `upload_port` se o seu C3 aparecer em outro caminho.
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

Detalhes de build, endpoints, memória e limitações estão em
[`firmware/README.md`](firmware/README.md).

## Validação

| Item | Resultado |
|---|---|
| Build/flash no C3 | OK |
| WiFi + portal + mDNS | OK |
| `discover` da câmera | OK |
| `/video` e `/snapshot` | OK (primeiro frame completo) |
| MQTT `x5bridge/...` | OK |
| OTA `.bin` via `/update` | OK |
| `/config` + reinício | OK |
| `/reboot` | OK |
| Soak de 10 min | OK, sem reboot |
| `/reset` | não testado (apaga WiFi/config) |
| 2 câmeras simultâneas | não testado (sem hardware) |

Medições com uma câmera:

- câmera nativa medida pelo Linux: **~11,9 fps**;
- C3 sem cliente: **~10 fps**, heap ~87 KB livre;
- C3 com 1 cliente MJPEG: **~7,7 fps**, heap ~70–77 KB livre (mín. ~50 KB);
- JPEG de ~18–22 KB; soak de 10 min com ~9,6 fps médio.

Os ~16 fps do documento original não se reproduziram nesta câmera; o modo atual
entrega ~12 fps.

## Limitações e pendências

- Admin e OTA **sem autenticação** (decisão de projeto; uso em LAN).
- `/reset` não testado para não derrubar a configuração atual.
- Reconexão após queda de energia da câmera não foi testada explicitamente; o
  firmware tem timeout e relogin.
- Segunda câmera real não testada. O limite do código é 2 ativas e 2 clientes
  por stream, mas com 1 câmera + 1 cliente o heap já fica apertado (~50 KB
  mínimos); a segunda câmera só inicia se a alocação couber.
- O broker em `192.168.0.250` não preserva `retained`; se isso for necessário,
  trocar/ajustar o broker.
- FPS abaixo do documentado originalmente (~12 fps nativos nesta câmera).

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
