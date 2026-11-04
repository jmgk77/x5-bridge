# ESP32-CAM — comparativo das três boards

Registro do exame de três módulos **AI-Thinker ESP32-CAM** para escolher qual
usar como bridge X5. O bridge não usa a câmera onboard; o objetivo era separar
a board com a câmera defeituosa e reaproveitá-la.

## Método

Foi gravado o firmware de diagnóstico
[`tools/esp-probe/`](../tools/esp-probe/) no env `esp32cam` (`-DCAMERA_PROBE`).
O probe testa `camera_init`, lê o `sensor_pid` e tenta **3 frames** em cada
resolução: QVGA 320x240, VGA 640x480, SVGA 800x600 e UXGA 1600x1200.

## Resultado

| Board | MAC | Chip | Flash | PSRAM | Câmera | QVGA | VGA | SVGA | UXGA |
|---|---|---|---|---|---|---|---|---|---|
| 1 | `3C:E9:0E:86:3F:84` | ESP32-D0WD-V3 rev v3.0 | 4 MB | 2 MB | OV2640, PID `0x26`, init OK | OK 3/3 | OK 3/3 | 1 frame, depois FAIL | FAIL |
| 2 | `A0:A3:B3:29:F9:2C` | ESP32-D0WD-V3 rev v3.1 | 4 MB | 2 MB | OV2640, PID `0x26`, init OK | OK 3/3 | OK 3/3 | 1 frame, depois FAIL | FAIL |
| 3 | `A0:A3:B3:2C:6F:B4` | ESP32-D0WD-V3 rev v3.1 | 4 MB | 2 MB | OV2640, PID `0x26`, init OK | **FAIL** | **FAIL** | **FAIL** | **FAIL** |

## Conclusão

- **Board 3** é a com a câmera defeituosa: o sensor é detectado, mas **nenhum
  frame** é entregue em nenhuma resolução.
- Boards 1 e 2 têm o mesmo comportamento: funcionam bem em VGA e falham em
  SVGA/UXGA. Isso parece ser uma limitação comum do conjunto OV2640/ESP32-CAM
  usado no bring-up, não um defeito exclusivo de uma board.
- A **board 3** foi escolhida como bridge, já que a câmera onboard não é usada.

## Observação sobre o bridge na board 3

Na primeira gravação do firmware do bridge, a board 3 entrou em boot loop e o
AP/`x5bridge-cam.local` não estabilizava. Após **reconectar o flat ribbon** da
câmera e regravar, ela passou a rodar estável:

- `CAM0` com a câmera X5 externa em `192.168.0.10`;
- buffers de frame na PSRAM (2 MB);
- heap interno ~158 KB (mín. ~141 KB);
- `/video`, `/snapshot`, MQTT, OTA e `/reboot` validados;
- ~7,7 fps sem cliente e ~6,8 fps com 1 cliente (limitado pelo WiFi/UDP do
  ESP32 clássico).

Como a câmera onboard não é usada, o defeito dela não afeta o bridge. Se a
board voltar a resetar, o primeiro suspeito é o módulo/flat da câmera ou a
alimentação 5 V.

## Links

- [`../firmware/README.md`](../firmware/README.md) — firmware e alvos.
- [`esp32-bringup.md`](esp32-bringup.md) — bring-up e medições de hardware.
- [`../tools/esp-probe/`](../tools/esp-probe/) — firmware de diagnóstico.
