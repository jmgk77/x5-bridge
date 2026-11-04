# Protocolo da câmera X5 (YunSync / 2BK-2) — engenharia reversa

Documentação do protocolo P2P usado por câmeras X5 (marca YsxLite), baseada em:

- Engenharia reversa do app Android (`DavidVentura/cam-reverse`)
- Capturas PCAPdroid do app oficial (sessão funcionando)
- Capturas tcpdump da reimplementação em TypeScript
- Reimplementação em C++ (`../linux/serve.cpp`, este repo)

Tudo aqui foi **observado empiricamente**. Campos marcados com `?` são
desconhecidos mas funcionam como constante; campos sem `?` foram decodificados
com certeza.

## Arquitetura

~~~
Câmera X5 (192.168.0.10:32108 UDP) <---> Cliente (Linux, ESP32, ...)
~~~

Todo o tráfego é **UDP**. A câmera escuta na porta 32108. O cliente usa uma
porta efêmera qualquer. Não há TCP, TLS, RTP, RTSP. A "criptografia" é uma
transformação trivial de bytes (ver seção XqBytesEnc).

Um "frame" de vídeo no ar (~16 KB de JPEG) é fragmentado em ~25 pacotes UDP
de 1032 bytes cada. ~16 fps → ~400 pacotes/s de mídia.

## Fluxo completo

~~~
Cliente                                Câmera
  |--- LanSearch (4B) ---------------->|
  |<-- PunchPkt (24B) -----------------|
  |--- P2pRdy (24B, echo) -------------|
  |<-- P2pRdy (24B) -------------------|
  |--- Drw login (180B) ---------------|
  |<-- DrwAck id=0 --------------------|
  |<-- Drw ConnectUserAck (28B) -------|  ticket extraído aqui (offset 24-27 pós-decrypt)
  |--- Drw VideoParamSet (28B) ------->|
  |--- Drw StartVideo (20B) ---------->|  (enviados em sequência, sem esperar ACK)
  |<-- DrwAck id=1 --------------------|  (ack do VPS)
  |<-- DrwAck id=2 --------------------|  (ack do SV)
  |<-- Drw cmd=0x1831 (VPS ack) -------|  (stream=0)
  |--- DrwAck id=1 ------------------->|
  |<-- Drw cmd=0x1031 (SV ack) --------|  (stream=0)
  |--- DrwAck id=2 ------------------->|
  |                                    |
  |<== Drw stream=1 (mídia) ===========|  JPEGs continuamente
  |--- DrwAck ------------------------>|  para cada pacote recebido
  |                                    |
  |--- P2PAlive ---------------------->|  (keepalive ~1 Hz)
  |<-- P2PAlive -----------------------|
  |--- P2PAliveAck ------------------->|
~~~

## 1. Discovery

### LanSearch (cliente → câmera)

4 bytes, sem header:

~~~
f1 30 00 00
~~~

### PunchPkt (câmera → cliente)

24 bytes. Identifica a câmera e seu número de série:

~~~
f1 41 00 14 42 41 54 45 00 00 00 00 00 08 86 04 48 44 4c 5a 42 00 00 00
      ^^^^^ ^^^^^ ^^^^^^^^^^^^^^^^^^^^ ^^^^^^^^^^^^^^^^^^^
      len=20 "BATE" (serial)          "HDLZB" (modelo/sufixo)
~~~

- O serial completo é `BATE558596HDLZB` (16 bytes após `f1 41 00 14`)
- Bytes `00 00 00 08 86 04` no meio são desconhecidos mas constantes por câmera
- O nome do dispositivo é "BATE" + sufixo

## 2. Handshake

### P2pRdy (cliente → câmera)

Echo do PunchPkt com **um byte alterado** (byte[1] de `0x41` → `0x42`):

~~~
f1 42 00 14 42 41 54 45 ... (mesmos 24 bytes, só o 0x41 vira 0x42)
~~~

A câmera responde com um `P2pRdy` próprio (mesmo formato, também `f1 42...`).
Após isso, a câmera está pronta para receber o login.

### Keepalive

- **P2PAlive**: `f1 e0 00 00` — câmera manda a cada ~1s. Cliente responde.
- **P2PAliveAck**: `f1 e1 00 00` — resposta ao P2PAlive.

Se o cliente não responde em ~3s, a câmera encerra a sessão.

## 3. Header Drw (comum a todos os comandos de controle)

Todo Drw tem a mesma estrutura de 20 bytes de header:

~~~
offset  size  nome            notas
------  ----  --------------  -------------------------------------------
0       2     hdr             sempre f1 d0
2       2     length          BE; tamanho total do pacote - 4
4       1     param           sempre d1 (unlocked)
5       1     stream          0 = controle, 1 = mídia
6       2     id              BE; ID do comando (incremental por sessão)
8       2     start_cmd       sempre 11 0a
10      2     command         BE; código do comando (ver tabela)
12      2     payload_len     u16_swap(4 + data_len); ver nota
14      2     cc_dest         BE; destino (ver tabela de comandos)
16      4     ticket          4 bytes; zeros até receber ConnectUserAck
20      ...   data            payload específico do comando
~~~

**Nota sobre payload_len**: o valor é `u16_swap(4 + data_len)` onde `4` são os
bytes do ticket. Ex: para VPS com 8 bytes de data, payload_len = 12, gravado
como `0c 00`.

**Tabela de comandos** (campo `command`):

| Comando          | Valor   | cc_dest |
|------------------|---------|---------|
| ConnectUser      | `20 10` | `ff 00` |
| ConnectUserAck   | `20 11` | `ff 00` |
| VideoParamSet    | `18 30` | `00 00` |
| VideoParamSetAck | `18 31` | `00 00` |
| StartVideo       | `10 30` | `00 00` |
| StartVideoAck    | `10 31` | `00 00` |
| (outros acks)    | `18 32`, `10 32` | `00 00` |

Os comandos `*Ack` (`0x2011`, `0x1831`, `0x1031`) vêm **da câmera para o
cliente**, não o contrário.

## 4. Login (ConnectUser)

### Drw de login (cliente → câmera)

**Pacote de 180 bytes totais**, verificado empiricamente. Estrutura:

- Header Drw (20B): `f1 d0 00 b0 d1 00 <id:2> 11 0a 20 10 a4 00 ff 00`
- Ticket (4B): `00 00 00 00` (zeros — o login não usa ticket)
- Payload (160B): fixo, **copiado verbatim do app YsxLite**

Hex completo (que funciona como login):

~~~
f1d000b0d1000000110a2010a400ff00000000006f0101010101010101010101010101
01010101010101010101010101010160656c686f010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
0101010101010101010101010101010101010101010101010101010101010101010101
01010101010160656c68
~~~

**Importante**: o comando `SendUsrChk` do `cam-reverse` (que produz um Drw de
56 bytes com "admin"+"admin" encriptado) **NÃO funciona** com a X5. A câmera
aceita (manda Ack), mas não entra em modo de streaming contínuo. O app oficial
manda 180 bytes com muito mais informação (device ID, versão, nonce,
capacidades) — o payload acima é o capturado do app e funciona. Se o payload
for idêntico entre sessões, ele pode ser hardcoded (foi o que fizemos).

### DrwAck (câmera → cliente) — ack do login

32 bytes:

~~~
f1 d1 00 06 d2 00 00 01 00 00 00 00 ...
^^^^ ^^^^^ ^^ ^^ ^^^^^ ^^^^^ ^^^^^
DrwAck len stream item=1 pkt_id=0 (id do Drw sendo ackado, BE)
~~~

**Atenção ao offset do pkt_id**: no Drw normal, o id está em [6..7]. No
DrwAck, o id está em **[8..9]** (porque [6..7] é `item_count = 1`). É fácil
inverter.

### ConnectUserAck (câmera → cliente) — 28 bytes, contém o ticket

Header Drw (20B) + 8 bytes de payload **encriptado**. Após decrypt:

~~~
offset  size  conteúdo
------  ----  -------------------------------------
20      4     status / não usado
24      4     TICKET  <-- o que precisamos
~~~

O código de extração (após XqBytesDec no payload de 8 bytes):

~~~c
int payload_len = (buf[12] | (buf[13] << 8));  // LE (!)
int dec_len = payload_len - 4;
if (dec_len > 0) xq_dec(buf + 20, dec_len, 4);
memcpy(ticket, buf + 24, 4);
~~~

**O ticket muda a cada sessão** (é gerado pela câmera), então tem que ser
extraído a cada login. Uma vez extraído, ele é usado em **todos** os Drws
subsequentes (VPS, SV).

## 5. XqBytesEnc / XqBytesDec — a "criptografia"

Não é criptografia de verdade. São duas operações:

1. **Inverter o bit 0** de cada byte: ímpar → subtrai 1, par → soma 1
2. **Rotacionar** o buffer por N bytes (N=4 é o único valor usado)

~~~c
// Encripta in-place (usado em payloads que ENVIAMOS)
void xq_enc(uint8_t* buf, int len, int rot) {
    uint8_t* nb = malloc(len);
    for (int i = 0; i < len; i++) nb[i] = (buf[i] & 1) ? (buf[i]-1) : (buf[i]+1);
    for (int i = 0; i < len-rot; i++) buf[i] = nb[i+rot];      // rotação "para a esquerda"
    for (int i = 0; i < rot; i++) buf[len-rot+i] = nb[i];
    free(nb);
}

// Decripta in-place (usado em payloads que RECEBEMOS)
void xq_dec(uint8_t* buf, int len, int rot) {
    uint8_t* nb = malloc(len);
    for (int i = 0; i < len; i++) nb[i] = (buf[i] & 1) ? (buf[i]-1) : (buf[i]+1);
    for (int i = rot; i < len; i++) buf[i] = nb[i-rot];        // rotação "para a direita"
    for (int i = 0; i < rot; i++) buf[i] = nb[len-rot+i];
    free(nb);
}
~~~

Atenção: a inversão de bit é a **mesma** nas duas operações. A única diferença
é o sentido da rotação. Cuidado ao copiar de outra fonte — algumas
implementações rotacionam ao contrário e viram lixo.

## 6. VideoParamSet + StartVideo

Ambos usam o **ticket** extraído do ConnectUserAck. Devem ser enviados **em
sequência imediata** (sem esperar Ack entre eles) — é o que o app oficial faz.

### VideoParamSet (cliente → câmera) — 28 bytes

~~~
f1 d0 00 18 d1 00 <id:2> 11 0a 18 30 0c 00 00 00 <ticket:4> <payload:8>
^^^^ ^^^^^                            ^^^^^ ^^^^^ ^^^^^^^^^^^ ^^^^^^^^^^
hdr  len=24                           plen  cc=0    ticket      payload
~~~

**id** = 1 (o primeiro id após o ConnectUserAck)
**payload** (8 bytes, antes do encrypt): `01 00 00 00 02 00 00 00`

Significado: `[resolution_id=1 (640x480), ...?, value=2]`. Para 320x240, use
`01 00 00 00 01 00 00 00` (ver tabela em `impl.ts` do cam-reverse).

O payload **deve ser encriptado** com `xq_enc(payload, 8, 4)` antes de ir no
pacote. No ar, o payload (após ticket) é: `03 01 01 01 00 01 01 01`.

### StartVideo (cliente → câmera) — 20 bytes

~~~
f1 d0 00 10 d1 00 <id:2> 11 0a 10 30 04 00 00 00 <ticket:4>
^^^^ ^^^^^                            ^^^^^ ^^^^^ ^^^^^^^^^
hdr  len=16                           plen=4 cc=0    ticket
~~~

**id** = 2. **Sem payload** (payload_len=4 é só o ticket).

### Acks da câmera

Em resposta, a câmera manda dois pacotes stream=0:

~~~
DrwAck id=1          (ack do VideoParamSet)
DrwAck id=2          (ack do StartVideo)
Drw cmd=0x1831       (VideoParamSet ack; stream=0)
Drw cmd=0x1031       (StartVideo ack; stream=0)
~~~

O cliente **deve ACKar todos**, inclusive os dois `cmd=0x18xx/0x10xx` (não são
DrwAcks, são Drws normais). Após esse ciclo, a câmera começa a mandar
`stream=1`.

## 7. Mídia (stream=1)

### Formato dos pacotes

~~~
offset  size  conteúdo
------  ----  --------------------------------------------------
0       2     f1 d0
2       2     length
4       1     d1
5       1     01  (stream=1)
6       2     id (BE, incremental)
8       4     magic: 55 aa 15 a8 (se "framed") OU dado cru
~~~

Existem **dois tipos** de pacote de mídia:

#### Pacote "framed" (início de bloco)

~~~
offset  size  conteúdo
------  ----  --------------------------------------------------
8       4     55 aa 15 a8  (magic)
12      1     tipo: 03 = vídeo, 06 = áudio
13      ...   metadata (não decodificado)
~~~

Aparecem tipicamente com tamanho 36-40 bytes. O tipo=03 precede cada JPEG novo,
o tipo=06 precede cada chunk de áudio.

**Não** contribuem bytes para o JPEG — o JPEG começa no próximo pacote.

#### Pacote "raw" (dados de mídia)

~~~
offset  size  conteúdo
------  ----  --------------------------------------------------
8       ...   dados crus (para vídeo: bytes do JPEG, começando com ff d8 no 1º pacote)
~~~

Tamanho típico: 1000-1032 bytes. Devem ser concatenados para montar o JPEG.

### Montagem de um frame

~~~
para cada pacote stream=1:
    se tem magic e tipo=03:
        # novo frame começou
        se buffer_atual tem ffd8 (SOI) e ff d9 (EOI) no final:
            emite buffer_atual como JPEG
        buffer_atual = vazio
        # não adiciona nada do pacote framed
    senão se tem magic e tipo=06:
        # áudio, ignora
        continue
    senão (raw):
        buffer_atual += bytes[8:]
~~~

O JPEG termina com `ff d9` (marcador EOI). Devido a padding, é comum que o
último pacote tenha bytes extras após o `ff d9` — o código que funciona
**trunca no último `ff d9`** antes de emitir:

~~~c
int end = -1;
for (int i = 0; i < len-1; i++) if (buf[i]==0xff && buf[i+1]==0xd9) end = i;
if (end > 0) emit(buf, end + 2);   // inclusivo
~~~

### Métricas observadas

- **FPS**: ~16 fps (640x480)
- **Tamanho do JPEG**: 14-18 KB por frame (média ~16 KB)
- **Pacotes por frame**: ~25 (24 data + 1 framed)
- **Taxa UDP**: ~400 pacotes/s
- **Throughput**: ~260 KB/s de vídeo

## 8. Notas para port ESP32

- **Sockets**: usar `lwip_*` (API POSIX-like do ESP-IDF). `recvfrom`/`sendto`
  funcionam direto.
- **Keepalive**: `P2PAlive` a cada 1s deve ser respondido em <3s. Usar
  `esp_timer` ou uma task dedicada.
- **HTTP MJPEG**: `esp_http_server` não tem suporte nativo a
  `multipart/x-mixed-replace` streaming. Duas opções: (a) usar
  `httpd_req_send_chunk()` manualmente (funciona, mas com workarounds); (b)
  usar sockets raw TCP + parser HTTP mínimo (é o que fizemos no `../linux/serve.cpp`,
  ~80 linhas).
- **Memória**: cada JPEG tem ~16 KB. Para buffer duplo (enquanto envia o
  anterior, monta o próximo), reserve ~40 KB de heap. Recomendado ESP32 com
  PSRAM para folga.
- **Login hardcoded**: os 180 bytes do login vão em flash (`.rodata`). Não
  tente gerar dinamicamente — o formato real é desconhecido.
- **Não use TLS**: o protocolo é todo plaintext. O WiFi deve ser confiável
  (rede local), o que é aceitável para uso doméstico.
- **Banda de vídeo**: 260 KB/s × 8 = ~2 Mbps. WiFi 802.11n aguenta. Mas se a
  câmera ficar longe do AP, perda de pacotes UDP derruba o frame inteiro —
  implementar retry ou aceitar que alguns frames vão faltar.
- **Descoberta**: `LanSearch` por broadcast UDP (destino
  `255.255.255.255:32108` ou IP direto). No ESP32, `SO_BROADCAST` funciona via
  `lwip`.
- **C++ no ESP-IDF**: `../linux/serve.cpp` usa `std::thread`/`std::mutex`/`std::vector`.
  No ESP-IDF funciona com `CONFIG_COMPILER_CXX_EXCEPTIONS=y` e `pthread`
  habilitado, mas a recomendação é substituir por
  `xTaskCreate`/`xSemaphoreCreateMutex` para reduzir overhead de heap.

## 9. Limitações conhecidas

- **Não decodifica áudio** (o protocolo entrega, mas o formato do payload não
  foi analisado).
- **Não envia áudio** (o app suporta microfone no celular; não implementado).
- **Não muda resolução** (só 640x480; o formato do VPS com outras resoluções
  está em `impl.ts` do cam-reverse mas não testado).
- **Não envia comandos PTZ** (pan/tilt/zoom) — existem comandos no `cam-reverse`
  mas não fazem parte da captura usada.
- **Login fixo**: o login de 180 bytes é do app oficial e foi copiado verbatim.
  Se ele tiver um nonce variável por instalação/sessão, essa implementação pode
  parar de funcionar em alguma versão do firmware da câmera. Testado em X5 com
  firmware `BATE558596HDLZB`.

## 10. Referências

- [cam-reverse](https://github.com/DavidVentura/cam-reverse) — engenharia
  reversa original do app
- [YunSync / 2BK-2](https://github.com/DavidVentura/cam-reverse/issues) —
  discussões sobre o protocolo
- Capturas usadas: `~/Downloads/PCAPdroid_03_nov._17_10_12.pcap` (app Android),
  `~/Downloads/cam-reverse-trace-*.pcap` (implementação Linux)

## 11. Implementação

- Firmware ESP32-C3: [`../firmware/README.md`](../firmware/README.md)
- Bring-up e medições de hardware: [`esp32-bringup.md`](esp32-bringup.md)
- Referência Linux: [`../linux/README.md`](../linux/README.md)
