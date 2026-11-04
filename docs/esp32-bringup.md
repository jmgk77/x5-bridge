# Bring-up ESP32 (C3 e S3) — medições e decisões

Registro do que foi executado para validar a viabilidade do port do bridge X5.
Serve para não refazer a investigação nem perder as configurações que funcionaram.

## Ferramentas usadas

| Ferramenta | Versão | Uso |
|---|---|---|
| PlatformIO Core | 6.2.0 (`~/.platformio/penv/bin/pio`) | build, upload, monitor |
| platform espressif32 | 7.1.3 (6.12.0 também instalada) | toolchain/framework |
| framework-arduinoespressif32 | 4.20017.260907 (reporta IDF 4.4.7) | firmware do probe |
| toolchain-riscv32-esp | 8.4.0+2021r2-patch5 | C3 |
| toolchain-xtensa-esp32s3 | 8.4.0+2021r2-patch5 | S3 |
| esptool (sistema) | 4.7.0 (`/usr/bin/esptool`) | `chip_id` |
| esptool (PlatformIO) | 4.11.0 (`~/.platformio/packages/tool-esptoolpy/esptool.py`) | fallback p/ S3 |
| pyserial (penv) | — | leitura da serial USB-CDC |

> O esptool 4.7.0 do sistema **não consegue gravar/ler S3** (falta
> `stub_flasher_32s3.json`). Use o esptool do PlatformIO ou o `pio run`.
> Para C3 o esptool do sistema funciona.

## Firmware de probe

Fonte: [`tools/esp-probe/`](../tools/esp-probe/) (`platformio.ini` + `src/main.cpp`).
Imprime chip, revisão, cores, frequência, flash, PSRAM, heap, scan WiFi e testa
alocações de 16/20/24/32 KB (3 de cada).

### Comandos

```bash
# C3
~/.platformio/penv/bin/pio run -d tools/esp-probe -e esp32-c3 -t upload

# S3 (módulo 4MB flash + 2MB PSRAM)
~/.platformio/penv/bin/pio run -d tools/esp-probe -e esp32-s3 -t upload

# ESP32-CAM (AI-Thinker, CH340 em /dev/ttyUSB0; inclui teste da OV2640)
~/.platformio/penv/bin/pio run -d tools/esp-probe -e esp32cam -t upload

# leitura da serial (USB-CDC nativo)
~/.platformio/penv/bin/python - <<'PY'
import serial, glob, time
p = serial.Serial(sorted(glob.glob('/dev/ttyACM*'))[0], 115200, timeout=1)
t = time.time()
while time.time() - t < 20:
    c = p.read(4096)
    if c: print(c.decode(errors='replace'), end='')
PY
```

## Resultado — ESP32-C3

| Item | Valor |
|---|---|
| Chip | ESP32-C3 (QFN32), **rev v0.4** |
| Cores / clock | 1 / 160 MHz |
| Flash | 4 MB embutida (XMC), 80 MHz |
| PSRAM | **nenhuma** |
| Rádio | WiFi 4 + BLE |
| USB | nativo Espressif (VID:PID `303a:1001`), JTAG/serial |
| MAC | `1C:DB:D4:E3:8A:D4` |
| SDK (Arduino) | `v4.4.7-dirty` |
| Heap total / livre | 297.988 B / **234.052 B** |
| Heap mínimo / maior bloco | 135.700 B / 212.980 B |
| Alocações | 3×16 KB, 3×20 KB, 3×24 KB, 3×32 KB — **todas OK** |

Scan WiFi: 8–9 redes; canal 1 mais forte/ocupado (`ELEUSIS` −51, `#CLARO-WIFI` −50).

**Conclusão:** 2×24 KB de frame pool + lwIP + HTTP cabem com folga.
O C3 é **suficiente** para 1 câmera e 1–2 clientes MJPEG.

## Resultado — ESP32-S3

| Item | Valor |
|---|---|
| Chip | ESP32-S3 (QFN56), rev v0.2 (esptool) / v0.0 no Arduino |
| Cores / clock | 2 / 240 MHz |
| Flash | 4 MB embutida (XMC), 80 MHz |
| PSRAM | **2 MB embutida (AP_3v3)**, detectada |
| Rádio | WiFi 4 + BLE |
| USB | nativo Espressif (`303a:1001`) |
| MAC | `E4:B3:23:F8:FB:2C` |
| SDK (Arduino) | `v4.4.7-dirty` |
| Heap interno total / livre | 360.932 B / **300.828 B** |
| Heap mínimo / maior bloco interno | 202.476 B / 286.708 B |
| PSRAM livre / maior bloco | 2.080.767 B / 2.064.372 B |
| Alocações (RAM interna) | 3×16 KB, 3×20 KB, 3×24 KB, 3×32 KB — **todas OK** |

Scan WiFi: 3 redes (`ELEUSIS` −62, `#CLARO-WIFI` −62, `DTU:Inverter` −88).
A diferença para o C3 provavelmente é antena/posição, não uma medição de throughput.

**Conclusão:** S3 dá margem real: pool de 3×24 KB em PSRAM, double buffering e
2–4 clientes, além de separar Task CAM (core 0) e HTTP (core 1).

### Configuração obrigatória do S3 no PlatformIO

O board `esp32-s3-devkitc-1` assume 8 MB de flash. Para o módulo de 4 MB:

```ini
board_upload.flash_size = 4MB
board_build.partitions = default.csv   ; tabela de 4MB (app 1.2MB)
board_build.arduino.memory_type = qio_qspi  ; PSRAM quad de 2MB
build_flags = -DBOARD_HAS_PSRAM
```

Sem esses ajustes a PSRAM não inicializa (`psram_found = 0`) ou a partição
default_8MB fica inválida em flash de 4MB.

## Resultado — ESP32-CAM (AI-Thinker)

Board com CH340 em `/dev/ttyUSB0` (sem USB nativo). Firmware de probe com
`lib_deps = espressif/esp32-camera` (2.0.4) e pinagem AI-Thinker.

| Item | Valor |
|---|---|
| Chip | ESP32-D0WD-V3, **rev v0.3** |
| Cores / clock | 2 / 240 MHz |
| Flash | 4 MB (XMC), **40 MHz, modo DIO** |
| PSRAM | **2 MB** (não 4 MB), detectada |
| Rádio | WiFi 4 + BT/BLE |
| USB | **CH340 externo** (sem USB nativo/JTAG) |
| MAC | `3C:E9:0E:86:3F:84` |
| SDK (Arduino) | `v4.4.7-dirty` |
| Heap total / livre (após WiFi) | 319.036 B / **246.384 B** |
| Maior bloco interno | **110.580 B** (menor que C3/S3) |
| PSRAM livre / maior bloco | 2.095.103 B / 2.064.372 B |
| Alocações | 3×16/20/24/32 KB na RAM interna — **OK** |

### Câmera OV2640

`camera_init = ESP_OK`, sensor **PID 0x26 (OV2640)**, JPEG. Resultado por resolução:

| Resolução | Resultado |
|---|---|
| QVGA 320x240 | **OK** (3/3 frames, ~4,5 KB) |
| VGA 640x480 | **OK** (2/2 frames válidos após o 1º, ~11 KB) |
| SVGA 800x600 | 1º frame OK, **2º FAIL** |
| UXGA 1600x1200 | **FAIL** |

Interpretação: a câmera **não está totalmente quebrada** — funciona em QVGA/VGA.
A falha em SVGA/UXGA indica limite/dano no sensor, no flat ou na banda
PSRAM/DMA. Para o bridge X5 a câmera onboard **não é usada**, então o board é
aproveitável: o ESP32, 4 MB de flash, 2 MB de PSRAM e o WiFi funcionam.

### Observações específicas do ESP32-CAM

- Após o upload, a serial pode sair como lixo (`xxxx...`) porque o board fica
  preso no bootloader/estado de reset. Para ler corretamente, abra a porta e
  pulse o reset:
  ```python
  p.setDTR(False); p.setRTS(True); time.sleep(0.15)
  p.setRTS(False); time.sleep(0.2)
  ```
- O upload em `/dev/ttyUSB0` funcionou direto (a placa/adaptador tem auto-reset).
- Sem câmera em uso, a PSRAM de 2 MB fica livre para frame pool/buffers.
- O maior bloco **interno** é só ~110 KB; mantenha o frame pool na PSRAM.

## Resultado — ESP32 DevKit antigo (D0WD-V3)

Placa genérica com CP2102 em `/dev/ttyUSB0` (sem USB nativo), upload em 115200.

| Item | Valor |
|---|---|
| Chip | ESP32-D0WD-V3, **rev v3.1** (esptool) / v0.3 no Arduino |
| Cores / clock | 2 / 240 MHz |
| Flash | 4 MB (XMC), 40 MHz, modo DIO |
| PSRAM | **nenhuma** |
| Rádio | WiFi 4 + BT/BLE |
| USB | CP2102 externo (sem JTAG nativo) |
| MAC | `20:50:0D:E7:16:68` |
| SDK (Arduino) | `v4.4.7-dirty` |
| Heap total / livre (após WiFi) | 333.496 B / **261.280 B** |
| Maior bloco interno | **110.580 B** |
| Alocações | 3×16/20/24/32 KB na RAM interna — **OK** |

Scan WiFi: 8 redes, sinal bom (`ELEUSIS`/`#CLARO-WIFI` −51 dBm).

Observações:
- `upload_speed` precisou ser **115200**; em 460800 o esptool não conectou.
- Ao abrir a serial com pulso DTR/RTS podem aparecer vários boots seguidos;
  depois estabiliza e imprime os relatórios normalmente.
- Sem PSRAM: o frame pool (2×24 KB) cabe na RAM interna, mas divide espaço com
  lwIP/TCP. Serve para o caso básico (1 câmera, 1–2 clientes).

## Comparativo dos módulos testados

| Módulo | CPU | RAM livre pós-WiFi | Maior bloco interno | PSRAM | USB |
|---|---|---|---|---|---|
| ESP32-C3 | 1×160 MHz | 234 KB | 213 KB | — | nativo/JTAG |
| **ESP32-S3** | **2×240 MHz** | **301 KB** | **287 KB** | **2 MB** | **nativo/JTAG** |
| ESP32-CAM | 2×240 MHz | 246 KB | 110 KB | 2 MB | CH340 |
| ESP32 DevKit | 2×240 MHz | 261 KB | 110 KB | — | CH340 |

## Recomendação

**Mais apropriado: ESP32-S3.** É o único com folga simultânea de CPU (dual-core),
RAM interna (301 KB livres / 287 KB de bloco) e PSRAM (2 MB), além de USB
nativo/JTAG. Permite o desenho ideal do plano: Task CAM no core 0, HTTP no
core 1, frame pool de 3×24 KB em PSRAM e 2–4 clientes sem risco de o envio HTTP
travar a recepção UDP.

Alternativas, em ordem:

1. **ESP32-CAM (reaproveitado, sem a câmera)** — melhor custo/benefício se a
   ideia é usar o que já existe: dual-core + 2 MB de PSRAM + 246 KB de heap.
   Perde para o S3 em RAM interna (110 KB de bloco) e não tem USB nativo.
2. **ESP32-C3** — suficiente para o caso básico (1 câmera, 1–2 clientes), menor
   consumo e mais simples, mas single-core e sem PSRAM; menos margem para
   multicliente/áudio/PTZ.
3. **ESP32 DevKit antigo** — funciona, mas sem PSRAM e com bloco interno de
   110 KB; fica como bancada de teste, não como alvo final.
4. **ESP32-C6** — não testado; exigiria ESP-IDF e, por ser single-core sem
   PSRAM, não supera o S3 para este workload.

## Toolchain: o que funciona para cada chip

| Alvo | Arduino (PlatformIO 7.1.3) | ESP-IDF (PlatformIO) |
|---|---|---|
| ESP32-C3 | **sim** (testado) | sim |
| ESP32-S3 | **sim** (testado) | sim |
| ESP32 (D0WD-V3) | **sim** (testado no ESP32-CAM) | sim |
| ESP32-C6 | **não** (board só declara `espidf`) | sim |

Consequência para o plano: se o alvo for **C6**, o projeto tem que nascer em
**ESP-IDF** (não Arduino com esta instalação do PlatformIO). Para **C3/S3**,
Arduino funciona, mas o core instalado usa **IDF 4.4.7**.

## Atualizações no plano de port

1. **C3 vira alvo principal de desenvolvimento** (hardware já disponível e medido).
2. C3: frame pool de **2 slots × 24 KB**, `max_open_sockets = 2`, envio HTTP
   não-bloqueante, Task CAM prioritária (single-core).
3. S3: frame pool de **3 slots × 24 KB em PSRAM**, Task CAM no core 0 e HTTP no
   core 1, até 4 clientes. Hot buffers (RX UDP, lwIP) continuam em RAM interna.
4. Não portar `std::thread`/`std::vector`/`send()` bloqueante do `serve.cpp`.
5. `WiFi.setSleep(false)` (`WIFI_PS_NONE`) em ambos.
6. USB-CDC nativo dispensa UART externa; C3/S3 têm JTAG embutido.
   ESP32-CAM usa CH340 e exige reset DTR/RTS correto para console.
7. Se for C6: prever instalação do ESP-IDF e reescrever o plano de componentes
   para IDF (sem depender do core Arduino).
8. **ESP32-CAM é reutilizável como bridge** (câmera onboard fora de escopo):
   dual-core, 4 MB flash, 2 MB PSRAM. Frame pool deve ir para a PSRAM, pois o
   maior bloco interno é só ~110 KB. Não tem USB nativo/JTAG.
9. A câmera OV2640 do ESP32-CAM funciona em QVGA/VGA e falha em SVGA/UXGA;
   se um dia for usada, limitar a VGA e investigar flat/alimentação.

## Observações

- O flash do C3 estava essencialmente **vazio** antes do probe (100% `0xFF`,
  exceto 24 bytes em `0x50000`), então não havia firmware a preservar.
- O probe fica gravado no board; para regravar outro firmware, basta `pio run -t upload`.
- O relatório do Arduino mostra `chip_revision v0.0` no S3, divergindo do
  `esptool` (`v0.2`): usar o valor do esptool como referência.
- Medições de heap foram feitas **sem associar ao AP**; após conexão WiFi real,
  esperar uma queda adicional (fila/associação) e revalidar com a câmera.
