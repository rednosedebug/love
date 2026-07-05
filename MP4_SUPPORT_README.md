# Suporte a MP4 no LÖVE — patch v2 (vídeo + áudio)

## O que mudou nesta versão

**Áudio**: novo `love::sound::lullaby::MP4AudioDecoder`, que extrai e
decodifica a trilha de áudio de um MP4/MOV via FFmpeg (libavcodec +
libswresample), convertendo para PCM 16-bit — o formato que
`love::sound::Decoder`/`love::audio::Source` já esperam. Registrado na
lista de decoders (`Sound::newDecoder`) por último, do mesmo jeito que
o ModPlug já era. Isso significa que **`love.audio.newSource("video.mp4")`
já funciona** sem nenhuma mudança na API pública do LÖVE — o dispatch é
automático, por conteúdo do arquivo, igual ao vídeo.

Uso combinado (áudio+vídeo sincronizados), do jeito que o LÖVE já
suporta hoje para Theora:

```lua
local video  = love.graphics.newVideo("clip.mp4")
local source = love.audio.newSource("clip.mp4", "stream")
video:setSource(source)
source:play()
video:play()
```

**Vídeo — seek mais preciso**: antes, um seek para trás só pulava para
o keyframe anterior e deixava o loop normal de playback "andar" quadro
a quadro até alcançar o alvo (podia levar vários ciclos do worker
thread até estabilizar). Agora `seekDecoder()` decodifica internamente
até alcançar o timestamp pedido antes de devolver o controle,
convergindo no mesmo ciclo em que o seek foi pedido.

**Correção de bug**: identificado e corrigido um bug onde, se
`seekDecoder()` decodificasse frames suficientes para já superar a
posição alvo, o loop externo em `threadedFillBackBuffer()` não entrava
mais nenhuma vez, e a imagem recém-decodificada nunca era copiada pro
back buffer (o vídeo "empacava" visualmente por um frame após seek).
`seekDecoder()` agora retorna se decodificou algo, e esse resultado é
propagado corretamente.

**Robustez**: se o primeiríssimo frame do vídeo falhar ao decodificar
(stream corrompido/vazio), os buffers ficam nos valores neutros
(cinza) já inicializados, em vez de copiar memória não inicializada do
decoder para a textura.

## Arquivos novos/modificados nesta rodada

- `src/modules/sound/lullaby/MP4AudioDecoder.h/.cpp` — novo.
- `src/modules/sound/lullaby/Sound.cpp` — registra o novo decoder.
- `src/modules/video/mp4/MP4VideoStream.h/.cpp` — seek melhorado, bug
  corrigido, robustez no primeiro frame.
- `CMakeLists.txt` — libswresample adicionada à detecção do FFmpeg;
  novo bloco condicional para compilar `MP4AudioDecoder` dentro do
  target `love_sound_lullaby`; macro renomeada de
  `LOVE_VIDEO_MP4_AVAILABLE` para `LOVE_FFMPEG_AVAILABLE` (agora serve
  tanto vídeo quanto áudio).

## Limitações conhecidas (honestas, atualizadas)

1. **Ainda não compilado nem testado end-to-end** — mesma ressalva de
   antes: sem headers `-dev` do FFmpeg neste sandbox sem rede.
2. **Requer FFmpeg >= 5.1** (2022+) por causa da API `AVChannelLayout`
   usada no decoder de áudio (`av_channel_layout_default`,
   `swr_alloc_set_opts2`). FFmpeg mais antigo exigiria trocar essas
   chamadas pela API legada (`swr_alloc_set_opts` +
   `av_get_default_channel_layout`), documentado em comentário no
   próprio arquivo.
3. **Downmix fixo para estéreo.** Trilhas 5.1/7.1 são convertidas para
   2 canais; não há opção de preservar canais surround.
4. **Seek de áudio ainda por keyframe** (AAC costuma ter keyframes bem
   mais frequentes que vídeo, então na prática o impacto é pequeno,
   mas não é sample-exato).
5. **`MP4AudioDecoder::openStream()` não passa hint de formato** de
   propósito, para permitir que `Sound::newDecoder()` caia para o
   próximo decoder da lista se o arquivo não for um container que o
   FFmpeg reconheça — mas isso também significa que arquivos
   corrompidos podem demorar um pouco mais para falhar (o FFmpeg tenta
   várias heurísticas de detecção antes de desistir).
6. **Build MEGA (Windows/Android) e licenciamento**: mesmas ressalvas
   do patch anterior — megasource não vendoriza FFmpeg ainda, e
   distribuir com FFmpeg exige atenção à licença (LGPL vs GPL).

## Como testar de verdade

```bash
sudo apt install libavformat-dev libavcodec-dev libswscale-dev \
                  libavutil-dev libswresample-dev

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

```lua
function love.load()
    video = love.graphics.newVideo("meuvideo.mp4")
    audio = love.audio.newSource("meuvideo.mp4", "stream")
    video:setSource(audio)
    audio:play()
    video:play()
end

function love.draw()
    love.graphics.draw(video, 0, 0)
end
```

