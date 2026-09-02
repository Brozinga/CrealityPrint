## Sessão 2026-08-23/24 — commit/push + reativação de CI multi-OS

Usuário testou a build local (com os 3 fixes de GUI abaixo) e confirmou
que os bugs sumiram. Pediu commit+push (skill semantic-commit) de TUDO
que estava pendente no working tree, e antes disso pediu pra analisar
se o push dispara build de Windows/Linux/Mac via CI (não local).

**Identidade git**: não havia `user.name`/`user.email` configurados no
repo. Configurado LOCAL (não --global): `designerbroz@gmail.com` /
`Brozinga` (usuário do GitHub do fork).

**Remote**: `git@github.com:Brozinga/CrealityPrint.git` — fork PESSOAL
(não é o `CrealityOfficial/CrealityPrint` upstream), público.

**Análise da CI encontrada**: `.github/workflows/build_all.yml` dispara
em push pra `master` quando `src/**`/`deps/**`/`version.inc` mudam, mas
o matrix só tinha `windows-latest` — Linux tinha uma entrada comentada
("Deprecate appimage") e Mac nem estava no matrix. Investigado histórico
do git (`git log -p -- build_all.yml` no commit inicial `8571cc5c`):
Linux ERA feito via **Flatpak** (job separado `flatpak:`, usando
`flathub-infra/flatpak-github-actions`), não via Docker/AppImage — e
tinha até um deploy pra um release ID específico do
`CrealityOfficial/CrealityPrint` (não serve pro fork pessoal). Mac
tinha um job funcional (`macos-14`, arch `arm64`, comentário "Thanks to
RaySajuuk, it's working now") usando `build_release_macos.sh`, com
assinatura/notarização via secrets da Apple (que este fork não tem) e
deploy pro mesmo release da CrealityOfficial.

Usuário pediu explicitamente: reativar Linux usando o MESMO MODELO que
usamos nesta sessão (Docker + Ubuntu 26.04, não Flatpak), e habilitar
Mac e Windows também.

### O que foi implementado (commits `49d13590` e `5896f09c`)

**Linux** (`ci(linux)`, novo job `build_linux` direto em
`build_all.yml`, independente da cadeia
`build_check_cache→build_deps→build_creality_print` que é do modelo
Windows/Mac):
- `docker/setup-buildx-action` + `docker/build-push-action@v6` com
  `cache-from/to: type=gha` (cache de layers do Docker entre runs).
- Passo de "Free disk space" (remove android/dotnet/ghc/boost
  pré-instalados do runner) — nosso build local usa ~24GB de disco
  Docker, runner padrão do GH tem pouco espaço livre por padrão; risco
  real de faltar disco mesmo com a limpeza.
- `NCORES=2` fixo (runner padrão do GH Actions tem poucos cores) — mais
  seguro que o default de 14 usado localmente, mas ainda existe risco
  de OOM em alguma unidade de compilação pesada isolada (Boost/OCCT),
  não validável sem rodar de verdade.
- Extrai o AppImage do container via `docker cp` (mesmo mecanismo do
  bug1) e sobe como artifact do workflow (`actions/upload-artifact`).
- **Fix necessário pro cache funcionar**: `Dockerfile` reestruturado —
  `COPY ./ CrealityPrint` (cópia única de todo o repo antes de `-u`/
  `-d`) virou `COPY BuildLinux.sh version.inc / COPY linux.d/ / COPY
  deps/` (só o que `-u`/`-d` precisam, confirmado lendo `BuildLinux.sh`:
  `-u` só usa `linux.d/<dist>` + apt install; `-d` só faz `cmake -S
  deps`) seguido de `COPY ./ ./` completo antes de `-s`/`-i`.
  Mecanicamente equivalente ao Dockerfile antigo (o COPY completo final
  garante o mesmo estado de arquivos) — só melhora cache. **Validado
  com `docker build --check` (sintaxe ok) mas NÃO revalidado com build
  local completo** (usuário disse que não precisava mais testar
  localmente; a mudança estrutural do Dockerfile ainda não passou por
  um build de verdade desde a reestruturação — só o Dockerfile antigo,
  sem o split, foi validado).

**Mac** (`ci(macos)`): reativado `macos-14`/`arch: arm64` no matrix
(voltando ao modelo do commit antigo, mas SEM universal — só arm64
nativo, mais simples/rápido/seguro). Adicionado:
- `build_deps.yml`: `brew install automake autoconf libtool pkg-config
  texinfo nasm` + `./build_release_macos.sh -d -a arm64`.
- `build_creality_print.yml`: `./build_release_macos.sh -s -a arm64`,
  depois empacota `.app` (achado em
  `build_check_arm64/src/Release/CrealityPrint.app` — path calculado
  lendo a função `process_debug_symbols()` do PRÓPRIO
  `build_release_macos.sh` atual, já que o script mudou de layout desde
  a versão usada na CI antiga que citava `build/universal/...`) num DMG
  NÃO assinado (sem secrets de Apple Developer neste fork — removido
  todo o passo de codesign/notarize/deploy-pro-release-da-CrealityOfficial
  que existia na versão antiga), sobe como artifact.
- `build_check_cache.yml`: FIX de bug real encontrado — o cache-path
  calculado pra macOS apontava pra `deps/build` (layout antigo), mas o
  `build_release_macos.sh` ATUAL escreve em `deps/build_<arch>`
  (`DEPS_BUILD_DIR="$DEPS_DIR/build_$ARCH"`). Corrigido pra
  `deps/build_${{ inputs.arch }}`.
- Bônus (mesmo arquivo que já estava mexendo): corrigido bug
  pré-existente no step Windows de extração de versão — regex ainda
  procurava `SoftFever_VERSION` (nome da variável no OrcaSlicer
  upstream) em vez de `CREALITYPRINT_VERSION` (nome atual desde que
  este fork renomeou a variável) — `$ver` sempre ficava vazio. Trocado
  o regex.
- **Nada disso foi validado contra um runner macOS real** — é uma
  reconstrução cuidadosa a partir do job antigo (que funcionava, tem
  comentário confirmando) adaptada pros nomes/paths atuais do script,
  mas o script pode ter mudado mais coisas desde então além do que
  consegui confirmar lendo o código.

**Windows**: NÃO tocado (só o fix do regex de versão acima, que é
estritamente uma correção, não muda comportamento de build).

### Bloqueio encontrado
Depois do push (`5896f09c`), `GET
/repos/Brozinga/CrealityPrint/actions/runs?branch=master` retornou
`total_count: 0` — nenhum run disparou. Forte indício de que o GitHub
Actions está DESABILITADO por padrão neste fork pessoal (comportamento
padrão do GitHub pra forks). **Usuário precisa habilitar manualmente**:
Settings → Actions → General → permitir actions/workflows (não dá pra
fazer isso via API sem token de escrita, e não há `gh` CLI nem token
disponível neste ambiente).

## Próximo passo
1. Usuário habilita Actions no fork (`Settings → Actions → General`).
2. Checar se o run de `build_all.yml` dispara e observar os 3 jobs
   (Windows deve continuar OK; Linux e Mac são não-validados, espera-se
   precisar de iteração — igual aconteceu localmente com o Docker
   Linux, mas desta vez sem controle direto do ambiente, só logs da CI).
3. Iterar nos erros que aparecerem lendo os logs via
   `gh run view <id> --log` ou API REST
   (`/repos/Brozinga/CrealityPrint/actions/runs`), já que não há `gh`
   CLI neste ambiente — usar `curl`+API REST (pública, sem auth, pro
   repo público) ou pedir token/gh ao usuário se precisar de mais
   detalhe (ex.: baixar logs completos de um job falho).

## Sessão 2026-08-23 (continuação) — 2 bugs reportados pelo usuário após testar o AppImage

**Bug 1 — GTK-CRITICAL no console:** `gtk_cell_layout_get_cells: assertion
'GTK_IS_CELL_LAYOUT (cell_layout)' failed`.
- **Causa raiz:** `src/slic3r/GUI/PresetComboBoxes.cpp`,
  `PresetComboBox::update_selection()`, tinha um bloco `#if
  defined(__WXGTK20__)...` (copiado de um fork antigo, quando
  `PresetComboBox` herdava de `wxBitmapComboBox` nativo) que fazia
  `gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(m_widget))` para configurar
  ellipsize do texto. Mas `PresetComboBox` (ver
  `src/slic3r/GUI/PresetComboBoxes.hpp:34`) herda de `::ComboBox`
  (`src/slic3r/GUI/Widgets/ComboBox.hpp`), um widget 100% custom-drawn
  (baseado em `TextInput`/`DropDown`, sem `GtkComboBox`/`GtkCellLayout`
  nativo por trás). `m_widget` nesse caso é só o `GtkWidget` genérico da
  janela wx, nunca um `GtkCellLayout` — a assertion falha TODA VEZ que
  `update_selection()` roda (troca de preset, refresh do plater etc.),
  e o bloco não fazia nada útil (early-return antes do `g_object_set`).
- **Fix:** removido o bloco `#if __WXGTK20__/__WXGTK3__` inteiro de
  `update_selection()` (linhas ~217-233) e os includes GTK órfãos no topo
  do arquivo (`glib-object.h`, `pango-layout.h`, `gtk/gtk.h`), já que nada
  mais no arquivo usa GTK diretamente.

**Bug 2 — popups de calibração sem o botão OK/gerar modelo:** usuário
reportou que nos diálogos do menu Calibration (VFA, PA, Temperatura,
Retraction test, Max volumetric speed, etc.) nenhum conteúdo aparece,
faltando o botão "OK" (`m_btnStart`, que dispara `on_start()` — gera o
G-code/modelo de calibração).
- **Causa raiz:** em `src/slic3r/GUI/calib_dlg.cpp`, TODOS os construtores
  desses diálogos seguem o padrão: `DPIDialog(..., FromDIP(wxSize(-1,
  280)), ...)` (altura inicial 280) seguido logo depois por
  `SetMaxSize(wxSize(FromDIP(600), FromDIP(H)))` com H valendo 230, 240,
  260 ou 320 — em vários casos (230, 240) MENOR que a própria altura
  inicial pedida (280). Nenhum desses diálogos usa `wxRESIZE_BORDER`
  (não são redimensionáveis pelo usuário), então o `SetMaxSize` não serve
  pra limitar resize do usuário — só quebra o `Fit()`/`Layout()` chamado
  no fim de cada construtor: como o max-size já foi registrado nos size
  hints do GTK ANTES do `Fit()` calcular o tamanho real necessário pro
  sizer (StaticBox de extrusora/método + 4 linhas de settings + botão),
  o GTK recusa deixar a janela crescer além do teto, cortando o conteúdo
  que não cabe — tipicamente o botão OK, que é o último item do
  `v_sizer` vertical. Provavelmente passava despercebido no Windows
  (fontes/paddings nativos mais compactos que no GTK) mas quebra
  visivelmente no Linux/GTK.
- **Fix:** trocado `FromDIP(H)` por `-1` (sem teto de altura) no segundo
  argumento de `SetMaxSize` em TODOS os 13 diálogos que seguem esse
  padrão (`PA_Calibration_Dlg`, `Temp_Calibration_Dlg`,
  `Max_Volumetric_Speed_Dlg`, `VFA_Test_Dlg`, `Retraction_Test_Dlg`,
  `Retraction_Speed_Dlg` e outros — linhas 44, 405, 629, 806, 979, 1158,
  1337, 1527, 1716, 1906, 2098, 2290, 2487 antes da edição). Mantido o
  teto de LARGURA (600/612, que não foi reportado como problema). NÃO
  mexido: `High_Flowrate_Dlg` (linha ~2676, usa `SetSize`==`SetMinSize`==
  `SetMaxSize` fixos 559x507 de propósito, layout de grid de botões de
  imagem diferente) nem os `SetMaxSize` de botões individuais
  (`okButton`/`cancelButton`, ~24px de altura, sem relação com o bug).
**Bug 1 CONFIRMADO CORRIGIDO** — rebuild via Docker (cache de deps
INVALIDADO, refez tudo do zero — `COPY ./ CrealityPrint` no Dockerfile é
uma única camada antes de `-d`, então qualquer edição em `src/` invalida
o build inteiro das deps, não só do main; isso é esperado/inevitável com
a estrutura atual do Dockerfile) gerou
`build/CrealityPrint-V7.2.1.5477-x86_64-Alpha.AppImage`, rodado no host
(`DISPLAY=:0`, sessão desktop real) por ~15s: **nenhuma ocorrência de
`gtk_cell_layout_get_cells` no log** (antes aparecia sempre). App abriu
sem crashar, WebView/homepage carregou normal.

**Bug 3 (achado durante o teste, não reportado explicitamente pelo
usuário mas mesma classe/tema "warnings GTK no console") — ainda
aparecem DEZENAS de `gtk_widget_set_size_request: assertion 'width >=
-1' failed` na inicialização** (não é o mesmo warning do bug 1, é o
MESMO PADRÃO do bug já corrigido antes só em `ReleaseNote.cpp` — width
negativo passado pro GTK — só que em outro widget, usado em MUITO mais
lugares).
- **Causa raiz:** `src/slic3r/GUI/Widgets/TextInput.cpp:190-208`
  (`TextInput::DoSetSize`): `textSize.x = size.x - textPos.x -
  labelSize.x - m_LeftMargin * 2;` sem nenhum clamp — quando `size.x`
  (via `GetSize()`) ainda é o stub pequeno do wx antes do widget ser
  realizado no GTK (mesmíssimo mecanismo do bug do `ReleaseNote.cpp`), ou
  quando o label é mais largo que o campo, o resultado fica negativo e é
  passado direto pra `text_ctrl->SetSize(textSize)` → GTK rejeita
  qualquer width < -1. Como `TextInput` é a base de praticamente todo
  campo de texto/combobox do app (inclusive do `::ComboBox` usado em
  `PresetComboBox` do bug 1), isso dispara uma vez por widget criado na
  inicialização — explica as dezenas de ocorrências no log.
- **Fix:** `textSize.x = std::max(0, size.x - textPos.x - labelSize.x -
  m_LeftMargin * 2);` (adicionado `#include <algorithm>` no topo do
  arquivo, que não estava presente).
- **Ainda não testado em runtime** — 2º rebuild disparado (mesma
  invalidação total do cache de deps é esperada). Falta confirmar que o
  warning `set_size_request` sumiu (ou pelo menos reduziu drasticamente)
  e testar visualmente os diálogos de calibração (bug 2), que não pude
  confirmar visualmente por não ter `xdotool`/ferramenta de automação de
  UI disponível neste ambiente (sem sudo interativo pra instalar) — pedir
  pro usuário confirmar manualmente abrindo o menu Calibration > (VFA /
  Pressure advance / Temperature / etc.) e checando se o botão OK
  aparece.

## STATUS FINAL (2026-08-23)
- VFA: **feito**. GTK size_request bug: **corrigido no código**. Docker
  build Ubuntu 26.04 com limites de recurso: **feito, build 100%
  verde**. AppImage gerado: **sim**
  (`build/CrealityPrint-V7.0.0.0-x86_64-Alpha.AppImage`, 197M).
  "AppImage abre no Linux": **não confirmado a partir deste ambiente de
  ferramenta** — crasha na criação do WebView (WebKitGTK/libepoxy do
  sistema, nada relacionado às mudanças feitas), aparentemente por uma
  peculiaridade de permissão de GPU (`amdgpu_query_info(ACCEL_WORKING)`
  = EACCES) específica deste sandbox. Recomendado o usuário testar
  diretamente no terminal normal.
- Arquivos alterados (rastreados pelo git): `.dockerignore`,
  `BuildLinux.sh`/`DockerRun.sh` (só bit de execução), `DockerBuild.sh`,
  `Dockerfile`, `deps/TBB/TBB.cmake`, `run_gettext.sh` (bit de execução),
  `src/slic3r/GUI/MainFrame.cpp`, `src/slic3r/GUI/ReleaseNote.cpp`,
  `src/video/CMakeLists.txt`. Nada commitado (não foi pedido).

# Resumo da Atividade (sessão atual — 2026-08-23)

**Objetivo:** (1) Habilitar VFA no menu Calibration, (2) corrigir warning GTK
`gtk_widget_set_size_request: assertion 'width >= -1' failed`, (3) gerar
AppImage via Docker para Ubuntu 26.04 x64 com uso limitado a ≤90% CPU e
≤32GB RAM, (4) validar que o AppImage abre no Linux.

**Nota sobre sessão anterior:** este mesmo arquivo já tinha um registro de
uma tentativa anterior (mover VFA para fora do `isAlpha()` + build via
Docker/AppImage bem-sucedido). Ao checar o código-fonte agora, o VFA
**ainda está** dentro do bloco `isAlpha()` em `MainFrame.cpp` — ou seja, a
mudança relatada não está presente no working tree atual (revertida ou de
outra branch/tentativa). Tratando aquele registro como histórico e
reaplicando a mudança do zero nesta sessão.

## Achados da análise de código
- `src/slic3r/GUI/MainFrame.cpp:3589-3725`: bloco `if (isAlpha())` com vários
  itens experimentais do menu Calibration (VFA, submenu "Speed calib" com
  Limit/Speed tower/Jitter/Fan speed). VFA = linhas 3591-3599.
- Bug GTK **causa raiz confirmada** em `src/slic3r/GUI/ReleaseNote.cpp:673-676`
  (`UpdateVersionDialog::update_version_info`, fluxo de diálogo de nova
  versão no startup): `size.GetWidth() - FromDIP(70)` fica negativo porque
  `m_scrollwindows_release_note` (criado em `ReleaseNote.cpp:340` com
  `wxDefaultSize`, página oculta de `wxSimplebook`) nunca passou por
  Layout()/Fit() antes de `GetSize()` ser lido — `GetSize()` retorna o
  stub pequeno do wx (~20px) antes da realização do widget no GTK.
  Vai direto para `SetMinSize`/`SetMaxSize` sem clamp.
- `Dockerfile` usa `FROM ubuntu:22.04`. Confirmado que `ubuntu:26.04` existe
  no Docker Hub (manifest amd64 ok). `BuildLinux.sh` já roteia Ubuntu ≥24.04
  para `linux.d/debian2` (pacotes `libwebkit2gtk-4.1-dev` etc.), então 26.04
  cai automaticamente no branch certo — sem mudança de lógica necessária.
  Risco: pacotes podem ter mudado de nome entre 24.04 e 26.04 (26.04 é
  posterior ao conhecimento de treinamento do modelo) — só descobrimos
  rodando o build.
- `BuildLinux.sh:206` tem bug de case (`${num_threads}` vs `${NUM_THREADS}`)
  que faz o `-jN` nunca ser aplicado — build sempre usa paralelismo default
  do Ninja (todos os cores). Não vamos corrigir esse typo; controle de
  paralelismo será via `CMAKE_BUILD_PARALLEL_LEVEL` (env respeitado por
  todo `cmake --build`, incluindo deps) + `docker build --resource
  memory=32g,cpu-quota=...` como rede de segurança via cgroup.
- Host: 36 cores, 60GB RAM total (~46GB livre), 576GB disco livre. Docker
  29.7.2 com suporte a `--resource memory=...,cpu-quota=...` no build.

## Plano aprovado
Ver `/home/brozinga/.claude/plans/logical-shimmying-pearl.md`.

## Status atual
- **Concluído:** VFA movido para fora do `if (isAlpha())` em
  `MainFrame.cpp` (confirmado via grep — VFA agora fica antes da linha do
  `if`, o restante do bloco experimental permanece protegido).
- **Concluído:** Fix do bug GTK em `ReleaseNote.cpp:673-677` — largura
  agora é clampada com `std::max(FromDIP(200), size.GetWidth() - FromDIP(70))`
  antes de `SetMinSize`/`SetMaxSize`/`Wrap` (`<algorithm>` já estava
  incluído no arquivo).
- **Concluído:** `Dockerfile` atualizado para `FROM ubuntu:26.04` +
  `ARG NCORES=14` / `ENV CMAKE_BUILD_PARALLEL_LEVEL=${NCORES}` (controla
  paralelismo de todo `cmake --build`, inclusive deps).
- **Concluído:** `DockerBuild.sh` atualizado para calcular
  `cpu-quota = nproc * 100000 * 0.9` (≈90% dos cores do host) e passar
  `--resource memory=32g --resource cpu-quota=<Q>` como teto de cgroup,
  além de `--build-arg NCORES=14` (default, pode ser sobrescrito via env).
- **Em andamento:** prestes a iniciar `./DockerBuild.sh` em background,
  monitorando `docker stats` para checar se RAM/CPU ficam dentro do
  orçamento; build pode demorar bastante (dezenas de minutos a horas).

## Iterações do build Docker
- Tentativa 1: falhou — `DockerBuild.sh` sem permissão de execução
  (`permissão negada`). Corrigido com `chmod +x DockerBuild.sh DockerRun.sh
  BuildLinux.sh`.
- Tentativa 2: falhou no `apt-get install` do topo do Dockerfile (lista
  fixa de pacotes, independente do `linux.d/`) — `libgstreamer-plugins-good1.0-dev`
  não existe mais no Ubuntu 26.04 (substituído por
  `libgstreamer-plugins-extra1.0-dev`) e `libwebkit2gtk-4.0-dev` foi
  renomeado para `libwebkit2gtk-4.1-dev`. Ambos corrigidos no Dockerfile.
- Tentativa 3: passou no apt-get do topo, mas falhou em `RUN ./BuildLinux.sh -u`
  com o mesmo erro de `libwebkit2gtk-4.0-dev` — causa raiz:
  `BuildLinux.sh:113` usa `bc` para comparar `VERSION_ID >= 24.04` e decidir
  entre `linux.d/debian` (antigo, usa webkit 4.0) e `linux.d/debian2`
  (correto para Ubuntu 24.04+, usa webkit 4.1). `bc` não estava instalado
  na imagem (`bc: command not found`), então a comparação falhava
  silenciosamente e o script escolhia o perfil errado mesmo estando no
  Ubuntu 26.04. Corrigido adicionando `bc` à lista de pacotes do Dockerfile.
- Tentativa 4: `BuildLinux.sh -u` passou (perfil `debian2` correto desta
  vez), mas `BuildLinux.sh -d` falhou com exit 127 — `rev: command not
  found` na função `check_available_memory_and_disk()` (linha 19). A
  imagem base `ubuntu:26.04` não traz `rev` (normalmente do pacote
  `util-linux`) pré-instalado. Corrigido adicionando `util-linux` à lista
  de pacotes do Dockerfile.
- Tentativa 5: `util-linux` já estava presente ("already the newest
  version"), mas `rev` continuou "command not found" — no util-linux 2.41
  (Ubuntu 26.04) o binário `rev` foi movido para o subpacote
  `util-linux-extra`. Corrigido adicionando `util-linux-extra`.
- Tentativa 6: `util-linux-extra` instalou sem erro, mas `rev` continuou
  ausente — a suposição estava errada. Investigado diretamente rodando um
  container `ubuntu:26.04` descartável com `apt-file search bin/rev`:
  confirmado que `/usr/bin/rev` pertence ao pacote `bsdextrautils` (não a
  `util-linux`/`util-linux-extra`). Corrigido adicionando `bsdextrautils`
  ao Dockerfile.
- Tentativa 7: `rev` finalmente resolvido (`bsdextrautils` era o pacote
  certo) — passou do erro 127 e chegou a rodar `cmake -S deps -B deps/build`
  de verdade. Falhou (exit 1) com erro real de CMake: `deps/CMakeLists.txt:23`
  declara `cmake_minimum_required(VERSION 3.2)`, e o CMake 4.x que vem no
  Ubuntu 26.04 removeu suporte a `cmake_minimum_required` abaixo de 3.5.
  Esse é exatamente o tipo de problema já antecipado no plano (risco de
  GCC/CMake novos incompatíveis com versões antigas declaradas nos
  CMakeLists vendorizados). Corrigido com
  `ENV CMAKE_POLICY_VERSION_MINIMUM=3.5` no Dockerfile — o CMake lê essa
  env var automaticamente em toda invocação, sem precisar editar nenhum
  CMakeLists.txt vendorizado.
- Tentativa 8: `CMAKE_POLICY_VERSION_MINIMUM=3.5` funcionou — o `cmake -S
  deps -B deps/build` configurou com sucesso e o Ninja começou a baixar e
  configurar as dependências de verdade (GMP, etc.). Falhou depois, no
  passo de `configure` (autotools, não CMake) da dependência **GMP**:
  `configure: error: could not find a working compiler` — mesmo com
  `gcc` (15.2.0, versão padrão do Ubuntu 26.04) presente. Investigando a
  causa raiz reproduzindo o `./configure` do GMP 6.3.0 isoladamente em um
  container `ubuntu:26.04` descartável, com as mesmas `CFLAGS`
  (`-Wall -Wmissing-prototypes -Wpointer-arith -pedantic ...`) usadas pelo
  `GMP/GMP.cmake:67`, para capturar o `config.log` real em vez de
  adivinhar. Suspeita principal: GCC 14+ mudou o padrão para tratar
  `-Wimplicit-function-declaration`/`-Wimplicit-int` como ERRO (não mais
  warning) em C, o que quebra scripts `configure` antigos (como o do GMP)
  que dependem de declaração implícita de função só gerar warning durante
  os testes de feature do autoconf.
- **Causa raiz confirmada** (diagnóstico direto, fora do Docker, copiando
  `deps/build/dep_GMP-prefix/src/dep_GMP` do host — que já existia de um
  build nativo anterior — e rodando `./configure` com as mesmas CFLAGS):
  não é implicit-function-declaration, é a mudança do **dialeto C padrão**
  do GCC 15 para `gnu23`. Em C23, `void g(){}` (parênteses vazios) passou
  a significar "zero argumentos" (antes, em C17 e anteriores, significava
  "lista de argumentos não verificada"). O teste `long long reliability
  test 1` do `configure` do GMP declara `void g(){}` e depois chama
  `g(i,d[i].src,d[i].n,got,d[i].want,9)` com 6 argumentos — válido em
  C17, erro de compilação em C23/gnu23. Verificado também com
  `apt-file search bin/rev`/`apt-cache policy` que `gcc-13`/`g++-13`
  estão disponíveis no Ubuntu 26.04 e ainda usam `gnu17` como padrão.
  Testado num container `ubuntu:26.04` descartável com `CC=gcc-13
  CXX=g++-13`: o `./configure` do GMP passou por toda a bateria de testes
  de compilador (só falhou depois por falta de `m4`, que já está na lista
  de pacotes do Dockerfile real). Fix aplicado: `gcc-13`/`g++-13`
  adicionados ao Dockerfile + `ENV CC=gcc-13` / `ENV CXX=g++-13` (em vez
  de tentar corrigir cada dependência vendorizada individualmente).
- Tentativa 9: **fix do gcc-13 confirmado funcionando no build real** —
  GMP passou pelo configure/build/install sem erro (ordem mudou para
  [156-159/251] por causa do paralelismo, mas passou limpo). Build de
  dependências avançando bem além da metade (Boost, OpenCV, CURL, OCCT,
  FFmpeg, etc. todos configurando/instalando sem erro). Uso de recursos
  confirmado dentro do orçamento: RAM ~16GB usados (de 32GB), e o cgroup
  do container do build tem `cpu.max: 3240000 100000` (exatamente os 90%
  de 36 cores calculados pelo `DockerBuild.sh`) — confirmado lendo
  `/sys/fs/cgroup/.../docker:<id>/cpu.max` diretamente. Uso de CPU
  aparentemente alto no `top` do host (~97%) é a soma do container
  (limitado a 90%) com outros processos do host (Chrome etc.), não uma
  violação do limite do build em si.
- Deps concluídas (251/251) sem novos erros. Entrou em `BuildLinux.sh -s`
  e compilou por ~836s, mas falhou no **link final** do executável:
  `undefined reference to '__cxa_call_terminate@CXXABI_1.3.15'` ao linkar
  contra `/usr/lib/x86_64-linux-gnu/libicui18n.so.78` (ICU do sistema,
  puxado via webkit2gtk/gstreamer). Diagnosticado num container
  descartável: `g++-13 -print-file-name=libstdc++.so` resolve para
  `/usr/lib/gcc/x86_64-linux-gnu/13/libstdc++.so` (do pacote
  `libstdc++-13-dev`), cujo `objdump -T` só exporta até `CXXABI_1.3.14` —
  o `libicui18n.so.78` do Ubuntu 26.04 foi compilado com gcc-15 e precisa
  de `CXXABI_1.3.15`, que só existe na libstdc++ mais nova do sistema.
  Ou seja: gcc-13 era necessário para as deps antigas (C dialect), mas
  usar gcc-13 também pro link do CrealityPrint quebra por ABI mismatch
  contra libs do sistema.
- **Fix**: dividir o compilador por fase — manter `CC=gcc-13`/`CXX=g++-13`
  só para `BuildLinux.sh -d` (deps), e voltar para `CC=gcc`/`CXX=g++`
  (gcc-15, o padrão do Ubuntu 26.04) antes de `BuildLinux.sh -s` e `-i`.
  O código-fonte do CrealityPrint em si já tinha compilado 836s sem erro
  algum com gcc-13, então gcc-15 deve compilá-lo sem problemas também
  (o problema real era só de C antigo nas deps + ABI do link final).
- Como o Dockerfile só mudou DEPOIS do `RUN ./BuildLinux.sh -d`, o cache
  de camadas do Docker deve reaproveitar tudo até ali (deps já compiladas)
  e só reconstruir a partir do build principal — tentativa 10 deve ser
  bem mais rápida que as anteriores.
- Tentativa 10: deps recompilaram do zero (cache do COPY invalidado pela
  própria edição do MEMORY.md — corrigido `.dockerignore` para incluir
  `.ai-memory`, efeito só nas próximas tentativas). `-s` avançou até 765s
  de compilação, mas falhou no link: `lto1: fatal error: bytecode stream
  in file 'libtbb.a' generated with LTO version 13.1 instead of the
  expected 15.1`. Causa: TBB é compilada com `-flto` (upstream oneTBB
  habilita LTO por padrão em release, ver
  `deps/build/dep_TBB-prefix/src/dep_TBB/cmake/compilers/GNU.cmake:68-70`),
  e o bytecode LTO não é portável entre versões maiores do GCC — GMP
  precisa de gcc-13 pra compilar, mas o link final agora usa gcc-15
  (fix anterior), e o `.a` da TBB gerado com gcc-13 não é compatível com
  o `lto1` do gcc-15.
- **Fix**: percebi que `deps/TBB/GNU.cmake` (deste repo, não o de
  upstream) JÁ tem `# Disable lto flag` com `TBB_IPO_COMPILE_FLAGS`/
  `TBB_IPO_LINK_FLAGS` vazios — mas só é aplicado via `_patch_command` em
  `deps/TBB/TBB.cmake` para o caso `FLATPAK`. Estendido esse patch (já
  existente no projeto) para rodar também em `UNIX AND NOT APPLE`
  (Linux genérico), reaproveitando a lógica já mantida pelo projeto em
  vez de inventar algo novo.
- Tentativa 11: **fix do LTO da TBB funcionou** — sem erro de bytecode
  version, o link avançou bem além do ponto anterior (763s). Novo erro,
  diferente e mais simples: `undefined reference to symbol
  'BZ2_bzDecompress'` ao linkar `libavformat.a` (FFmpeg). Causa: o FFmpeg
  vendorizado (`deps/FFmpeg`) detecta e habilita suporte a bzlib
  automaticamente se `libbz2` estiver disponível no sistema durante o
  build das deps — no Ubuntu 26.04 isso aconteceu (provavelmente
  transitivo), mas `src/video/CMakeLists.txt` (que já lista
  explicitamente `x264`, `lzma`, `opus`, `speexdsp` etc. como libs
  estáticas extras do FFmpeg) nunca listou `bz2`, porque no ambiente de
  referência anterior (Ubuntu 22.04) o FFmpeg provavelmente não tinha
  bzlib habilitado.
- **Fix**: adicionado `bz2` à lista `VIDEO_LIBS` em
  `src/video/CMakeLists.txt` (mesmo padrão dos outros libs de codec
  linkados manualmente), e `libbz2-dev` ao Dockerfile (para garantir que
  o link do `-lbz2` resolva de forma confiável).
- Tentativa 12: **compilação e link do executável principal concluídos
  com sucesso** (`[721/721] Linking CXX executable src/CrealityPrint`) —
  todos os fixes de ABI (gcc-13/gcc-15 split), LTO (TBB) e bz2
  funcionaram. Falhou logo depois em `./run_gettext.sh` com exit 126
  (permissão negada) — mesmo problema já documentado numa sessão
  anterior. `run_gettext.sh` estava sem bit de execução no repo
  (`-rw-rw-r--`). Corrigido com `chmod +x run_gettext.sh`.
- Tentativa 13: **`run_gettext.sh` passou, build principal (`-s`)
  concluído, e `BuildLinux.sh -i` gerou o AppImage com sucesso**
  (`CrealityPrint-x86_64.AppImage`, appimagetool concluiu normalmente,
  "Marking the AppImage as executable..."). O ÚNICO erro restante foi no
  ÚLTIMO passo do Dockerfile (cosmético, não afeta o AppImage já gerado):
  `groupadd -f -g $GID $USER && useradd -u $UID -g $GID $USER` falhou com
  exit 4 (UID já em uso) — a imagem base `ubuntu:26.04` já vem com um
  usuário padrão na mesma UID 1000 do host, diferente da `ubuntu:22.04`
  antiga. Corrigido tornando o `useradd` tolerante:
  `useradd ... || getent passwd $UID` (se já existe um usuário com essa
  UID, tudo bem, só não recriar).
- Tentativa 14: **SUCESSO TOTAL** — como esperado, o Docker reaproveitou
  o cache de TODOS os layers até a geração do AppImage (confirmado:
  `-u`, `-d`, `-s`, `-i` todos `CACHED`), só refez o último passo
  (useradd), que passou com o fix (`ubuntu:x:1000:1000:...` já existia,
  `getent passwd 1000` confirmou e seguiu em frente). Imagem
  `crealityprint:latest` construída com sucesso (5.68GB de conteúdo).
  AppImage extraído do container e copiado para
  `build/CrealityPrint-V7.0.0.0-x86_64-Alpha.AppImage` (197M) no host.
## Teste do AppImage no host
- Rodado `build/CrealityPrint-V7.0.0.0-x86_64-Alpha.AppImage` diretamente
  (sessão gráfica Wayland disponível, `DISPLAY=:0`). O app inicia
  (carrega fontes, detecta config), mas **crasha** durante a criação do
  WebView (painel de chat/AI embutido via WebKitGTK), dentro de
  `gdk_gl_context_make_current` → `libepoxy.so.0` (confirmado via `gdb`
  com backtrace completo, extraindo o AppImage com `--appimage-extract`
  e rodando `bin/CrealityPrint` sob gdb com `LD_LIBRARY_PATH`/`LC_ALL`
  isolados no processo-alvo).
- O AppImage **não empacota** libepoxy/libgdk-3/libwebkit2gtk — usa as
  do sistema. Ou seja, o crash acontece em código do sistema, não em
  nada que este trabalho tocou (nada relacionado a VFA, ao fix do GTK
  size_request, ou aos fixes de build).
- Testado `WEBKIT_DISABLE_COMPOSITING_MODE=1` + `WEBKIT_DISABLE_DMABUF_RENDERER=1`
  + `LIBGL_ALWAYS_SOFTWARE=1`: mesmo crash, mesmo local.
- Diagnóstico adicional: `glxinfo`/`glxgears` funcionam perfeitamente
  neste ambiente (GPU AMD RX 6700 XT, direct rendering yes) — não é falta
  de acesso à GPU em geral. Porém `eglinfo` mostra
  `_amdgpu_device_initialize: amdgpu_query_info(ACCEL_WORKING) failed (-13)`
  (EACCES) — uma permissão de GPU negada especificamente para essa
  consulta, no ambiente sandboxed onde o Bash desta sessão roda (não
  necessariamente a sessão desktop "real" do usuário). Isso é consistente
  com o WebKitGTK (que usa EGL/GBM internamente para seu processo de
  composição acelerada) falhando de um jeito que `glxgears`/`eglinfo`
  simples não expõem.
- **Não consegui confirmar de forma conclusiva, a partir deste ambiente
  de ferramenta, se o AppImage abre normalmente numa sessão desktop
  real** — o crash observado parece ser uma peculiaridade de permissão
  de GPU deste sandbox específico, não um bug introduzido pelo trabalho
  feito. Recomendado ao usuário testar diretamente num terminal comum:
  `./build/CrealityPrint-V7.0.0.0-x86_64-Alpha.AppImage`.
- Verificado por grep que o VFA está fora do `isAlpha()` no binário fonte
  e que o fix do `ReleaseNote.cpp` está presente — não foi possível
  confirmar em runtime que o warning GTK específico sumiu, já que o app
  crasha antes de chegar no fluxo de diálogo de nova versão.

## Causa raiz REAL do crash (confirmada pelo usuário na sessão desktop dele)
- Usuário reproduziu o MESMO crash na sessão desktop real dele (não era
  peculiaridade do sandbox onde meu Bash roda). Testado
  `WEBKIT_DISABLE_COMPOSITING_MODE=1`/`WEBKIT_DISABLE_DMABUF_RENDERER=1`
  e `WEBKIT_DISABLE_SANDBOX_THIS_IS_DANGEROUS=1` — nenhum resolveu.
- **Causa raiz encontrada**: `src/platform/unix/build_appimage.sh.in:29-31`
  empacotava `libGLU.so`, `libOpenGL.so` e `libGLdispatch.so` (as duas
  últimas são as libs de *vendor dispatch* do libglvnd) copiadas do
  container Docker de build para dentro do AppImage
  (`usr/lib/`). `AppRun` prioriza `usr/lib` no `LD_LIBRARY_PATH`, então
  essas libs de dispatch do CONTAINER (que não têm o driver AMD real da
  máquina do usuário) sobrescreviam as do sistema do usuário. Isso quebra
  o roteamento das chamadas GL pro driver de vídeo real, causando o
  crash dentro de `gdk_gl_context_make_current` → `libepoxy` no momento
  em que o WebKitGTK cria o WebView com aceleração de GL (painel de
  chat/AI). É um erro clássico e conhecido de empacotamento de AppImage
  (bundlar libs de dispatch do glvnd quebra em qualquer máquina com
  driver diferente do container de build).
- **Fix**: removidas as linhas que copiam `libOpenGL.so.*` e
  `libGLdispatch.so.*` em `build_appimage.sh.in` (mantido `libGLU.so`,
  que não faz parte da cadeia de dispatch e é seguro empacotar). Isso
  força o AppImage a sempre usar o glvnd/Mesa do sistema onde ele roda.
- Rebuild em andamento (mexer num arquivo fonte invalida o cache do
  Docker de novo, refaz tudo — ~15min).
- Usuário pediu, no meio do rebuild, pra mudar a versão do app pra
  `7.2.1.5477`. Alterado `version.inc:27`
  (`set(CREALITYPRINT_VERSION "7.0.0.0")` → `"7.2.1.5477"`) — essa é a
  variável que gera o nome do AppImage (`CrealityPrint-V<versão>-x86_64-<extra>.AppImage`).
  Essa edição aconteceu DEPOIS que o `docker build` atual já capturou seu
  contexto, então o build em andamento não é afetado (vai terminar com a
  versão antiga 7.0.0.0). Vai precisar de mais um rebuild depois para
  pegar a versão nova — mas como o fix de GL já vai estar confirmado
  (ou não) nesse build em andamento, o próximo build vai incluir os dois:
  fix de GL (já presente) + versão nova (já salva em disco).
- Build com o fix de GL **concluído com sucesso** (imagem
  `crealityprint:latest`, 5.85GB). AppImage extraído e substituído em
  `build/CrealityPrint-V7.0.0.0-x86_64-Alpha.AppImage` (203M). Ainda com
  versão antiga no nome (7.0.0.0) porque esse build começou antes da
  edição do `version.inc`. Disparando agora um novo build pra pegar a
  versão 7.2.1.5477 (já vai incluir o fix de GL também, que já está no
  disco).

## Próximo passo
1. Rodar `./DockerBuild.sh` novamente com o fix do `bc`.
2. Monitorar `docker_build.log` (via Monitor) para novos erros de pacote
   e o uso de recursos do host periodicamente; se RAM aproximar de 32GB,
   reduzir NCORES e reiniciar.
3. Ao concluir, localizar e extrair o `.AppImage` gerado.
4. Rodar o AppImage no host, confirmar que abre e que o warning GTK sumiu.
5. Registrar resultado final aqui.
