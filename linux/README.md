# Referência Linux

Implementação de referência do protocolo X5 em C++ para Linux. Foi usada para
validar a engenharia reversa antes do port para ESP32-C3 e continua útil para
diagnóstico.

## Arquivos

- `session.cpp` — faz discovery, login, imprime o ticket e salva os JPEGs em
  `frame0.jpg`, `frame1.jpg`, ...; roda por 30 s.
- `serve.cpp` — sobe um servidor HTTP com `/video` (MJPEG) e `/snapshot`
  (JPEG) e mantém a sessão com a câmera.
- `Makefile` — compila os dois binários.

## Build

```bash
make -C linux
```

Requisitos: `g++` com C++11/`pthread` e headers POSIX (Linux). Os binários
`linux/serve` e `linux/session` são ignorados pelo git.

## `session`

```bash
./linux/session <camera-ip>
```

Exemplo:

```bash
cd /tmp
/home/keres/Dropbox/WORK/x5-bridge-cpp/linux/session 192.168.0.10
```

Saída típica: logs de LAN search, `PunchPkt`, ticket, `VideoParamSet`,
`StartVideo`, contagem de JPEGs e, ao final, `got_ticket=1 jpegs=N`.

Os arquivos `frame*.jpg` são gravados no diretório atual. Use um diretório
temporário para não sujar o repositório:

```bash
mkdir -p /tmp/x5frames && cd /tmp/x5frames
/home/keres/Dropbox/WORK/x5-bridge-cpp/linux/session 192.168.0.10
```

## `serve`

```bash
./linux/serve <camera-ip> [http-port]
```

Exemplo:

```bash
./linux/serve 192.168.0.10 8080
```

Depois abra:

- `http://localhost:8080/video`
- `http://localhost:8080/snapshot`

O `serve` mantém a sessão aberta e reassembla os JPEGs; o browser recebe MJPEG
em `multipart/x-mixed-replace`.

## Aviso

A câmera atende **uma sessão por vez**. Não rode `serve`/`session` enquanto o
firmware do ESP32-C3 estiver usando a câmera (o bridge mantém a sessão viva).
Se a câmera estiver ocupada, o `LanSearch` pode não responder e o programa
termina com `!! no PunchPkt`.

Para liberar a câmera, pare o processo que estiver usando ou desabilite o slot
no bridge (`/config` → desmarcar `enabled`) e reinicie.

## Protocolo

Os detalhes de discovery, handshake, login, ticket, criptografia e mídia estão
em [`../docs/protocol.md`](../docs/protocol.md).
