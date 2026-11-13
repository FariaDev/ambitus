# Decisões de visual do Ambitus

Fonte única do que vale no visual. Mesmo fluxo do Summa: a mudança nasce em
HTML em `docs/design/rN/`, o Lucas aprova pelo render no T3 e só então vai
para o app. O `build.py` de cada rodada gera as variantes autocontidas
(`python3 docs/design/r1/build.py saida.html tela=editor tema=dark acento=violet capa=linho`).

## Base (2026-11-13)

- **GTK nativo.** Nada de casca Electron: o tema do Summa entra pelo CSS do
  GTK e as telas novas (biblioteca, busca) são widgets nativos.
- **Família visual do Summa**: IBM Plex Sans e Mono, STIX Two Text em
  títulos e matemática, cinzas neutros (`#ffffff`/`#f4f4f4`; escuro
  `#161616`/`#262626`/`#393939`), acentos só grafite e violeta, cantos de
  4 px em controles e 8 px no que flutua, sem texto desnecessário.

## Biblioteca (rodada 1, 2026-11-13)

- A Biblioteca é a primeira aba, fixa; cada caderno aberto vira aba ao lado,
  com a bolinha da cor do caderno.
- Lateral: Recentes, Todos; **Matérias** (lidas do config do Summa: pasta e
  cor) e **Pastas** (adicionadas no Ambitus).
- Capas **personalizáveis**, com estilos prontos: Contorno (curvas de nível
  geradas do nome), Linho (etiqueta e elástico) e Página (miniatura da
  primeira página). A cor é escolha de quem usa; o padrão é a cor da matéria.
- Busca no texto e no LaTeX dos cadernos, com mini-capa, matéria, página e
  trecho; o resultado abre o caderno na página.

## Editor (rodada 1, 2026-11-13)

- Modos **Clássico** (barra completa do Xournal++ com o tema novo),
  **Estúdio** (paleta flutuante no alto da página) e **Foco** (só a página).
- **Menu radial** flutuante, aberto onde se chama.
- **Riscar para apagar**: opção nas Configurações, sem UI própria.
- **Crescer a página**: na borda, faixa listrada com a prévia dos 2 cm e o
  botão "+2 cm".

## Quadro (canvas infinito)

- Tipo próprio de caderno ("Novo quadro"): uma página única que cresce
  sozinha, então o `.xopp` continua abrindo no Xournal++ original.
- A marca é `ambitus=board` na configuração do fundo (pontos de 18 pt); o
  Xournal++ ignora a chave e a mantém ao salvar. É quadro o documento de uma
  página só com essa marca.
- Começa com 89×56 pontos e mantém 28 pontos livres em volta do que está
  escrito, crescendo em pontos inteiros (o padrão não "pula"):
  - para a direita e para baixo, a página só aumenta; isso não entra no
    histórico;
  - para a esquerda e para cima, tudo anda para a direita/baixo e a vista rola
    junto. O deslocamento entra no histórico grudado na ação que o causou
    (um Ctrl+Z desfaz os dois), porque ações antigas guardam coordenadas
    absolutas (traços apagados, centro de escala e rotação). Não acontece ao
    desfazer/refazer nem com seleção ou caixa de texto aberta.
- Página grande é renderizada só em volta da vista (a área visível mais uma
  vista de cada lado), não num buffer inteiro.
- Visual: sem borda nem sombra, a tela inteira é o papel; modo Estúdio fixo;
  zoom vai até 5 %. No canto, minimapa (clicar/arrastar leva até lá) e a
  pílula de zoom com "Ir ao conteúdo".
- Na biblioteca, o quadro é um cartão de duas colunas na cor da matéria, com
  os pontos e o que está escrito em tinta clara. A prévia salva num quadro
  mostra só a parte escrita (até 384 px, sem os pontos).
- **Tutor do Summa.** O `ler_caderno` chama
  `xournalpp <cópia> -n N --export-range=N --create-img <saída> --export-png-dpi 120`,
  uma página por vez, e espera um arquivo só em `<saída>`. Num quadro a
  exportação em imagem (essa e a do diálogo) sai só com a parte escrita e duas
  linhas de pontos de folga, e o DPI baixa se passar de 4096² pixels. Continua
  um arquivo por página, no mesmo nome: nada muda no Summa nem no formato.

## Biblioteca nativa (2026-11-13)

- A biblioteca e o editor dividem a janela num `GtkStack`. O app abre na
  biblioteca quando começa sem arquivo; abrir ou criar um caderno troca para o
  editor; **Arquivo → Biblioteca** (Alt+Home) volta.
- As matérias vêm do `config.json` do Summa (`$LEARNING_DESK_RUNTIME`, senão a
  pasta "Mesa de Estudos" na config do sistema): nome, pasta e cor. As pastas
  próprias e as capas ficam em `library.ini` na config do Ambitus, **nunca no
  `.xopp`**, para os cadernos seguirem arquivos comuns do Xournal++.
- Varredura: até 6 níveis, sem ocultos, `.autosave.xopp` e `~`; link
  simbólico só vale dentro da pasta. Dentro de uma matéria, as subpastas viram
  seções (Aulas, Listas…).
- Capas desenhadas em Cairo, com os mesmos estilos do protótipo: Contorno
  (curvas de nível geradas do nome; "Outro desenho" troca a semente), Linho,
  Página (a miniatura embutida no `.xopp`), Lisa e imagem própria. Capa clara
  ganha tinta escura. Botão direito (ou a tecla Menu) abre o popover da capa.
- Ainda falta: as abas (o Xournal++ tem um documento por janela, então as
  abas pedem refatorar o `Control` para vários documentos).

## Busca nos cadernos (2026-11-13)

- A busca da biblioteca procura no nome, no texto digitado e no código LaTeX
  das fórmulas; a escrita à mão fica de fora. Ignora maiúsculas e acentos.
- O índice fica em `library-index.json` na pasta de cache do Ambitus, **nada
  vai para o `.xopp`**. Ele é montado numa thread ao abrir a biblioteca e só
  relê os cadernos cujo tamanho ou data mudou.
- Cada resultado mostra a capa, a matéria, a página e o trecho com o termo
  marcado; clicar abre o caderno na página. **Ctrl+K** leva à busca de
  qualquer lugar.
- Correção no upstream: `Control::fileLoaded` ignorava a página pedida quando
  o caderno não tinha metadados (nunca aberto); agora rola até ela.

## Crescer a página (2026-11-13)

- **Jornal → Crescer a página para a direita / para baixo** (Alt+Shift+→ e
  Alt+Shift+↓) soma 2 cm à página atual, com desfazer. É só a largura e a
  altura da página no `.xopp`, então o Xournal++ original e o
  `--create-img` do tutor abrem igual.
- Página com fundo de PDF não cresce (o fundo não acompanharia).
- Na borda: com o ponteiro (ou a caneta pairando) perto da borda direita ou
  de baixo, aparece a faixa listrada com a prévia dos 2 cm e a pílula
  "+2 cm"; o clique cresce aquela página. Some ao encostar a caneta, para não
  atrapalhar a escrita. A faixa nunca cobre outra página: embaixo ela fica no
  vão até a próxima. Fica na `GtkOverlay` acima das páginas
  (`gui/PageGrowHandle`), sem mexer no render do Xournal++; o ponteiro é lido
  por um *emission hook* porque o `InputContext` encerra o sinal `event`.

## Riscar para apagar (2026-11-13)

- Um zigue-zague feito com a caneta por cima de traços apaga esses traços e
  some; Ctrl+Z traz de volta. Liga e desliga em **Ferramentas → Riscar para
  apagar** (ligado por padrão, guardado em `settings.xml` como
  `scratchOutEnabled`).
- Conta como rabisco o traço feito em até 3 s que vai e volta pelo menos 5
  vezes na direção principal (cada passada com mais de 40% do comprimento),
  achatado (no máximo 65% de largura) e com caminho de pelo menos 4 vezes o
  comprimento. Escrita à mão anda para a frente e hachura lenta vira tinta.
- Apaga o traço que fica pelo menos 60% dentro da caixa do rabisco e que é
  cruzado por duas passadas não vizinhas; encostar com uma ponta não basta.
  Sem nada embaixo, o zigue-zague fica como tinta.
- O tempo, as passadas não vizinhas e o rabisco achatado vieram do Archey
  Notes como ideia; o código é nosso.
- Só traços por enquanto (texto, imagem e LaTeX ficam). Só a caneta no modo
  normal; marca-texto e reconhecedor de formas não disparam.
- Geometria em `ambitus/ScratchOut` (com testes), ligação em
  `StrokeHandler::scratchOut`.

## Barra de título e modos de tela (2026-11-13)

- A janela usa barra de título própria (`GtkHeaderBar`, `gui/TitleBar`): ∂,
  Biblioteca, nome do caderno, modos, desfazer e **⋯** com o menu inteiro.
  As abas vão entrar nessa barra.
- A biblioteca não mostra a barra de menu; o menu fica no ⋯.
- **Clássico**: menu, barras e lateral como a pessoa deixou nas preferências.
  **Estúdio**: sem menu, barras de cima/lados e lateral; fica a barra de baixo
  (página e zoom) e entra a paleta flutuante no alto (`gui/StudioPalette`):
  caneta, marca-texto, borracha, laço, texto, LaTeX, 5 cores do protótipo e
  3 espessuras, ligadas às ações do Xournal++ (`select-tool`, `tool-color`,
  `tool-size`). **Foco**: só a
  página. O modo é salvo em `settings.xml` (`editorMode`) e as barras são
  escondidas direto nos widgets, sem mexer nas preferências do Clássico.

## Menu radial (2026-11-13)

- Abre no botão direito do mouse ou no segundo botão da caneta, onde está o
  ponteiro, desde que esse botão não tenha ferramenta própria nas
  Configurações (o padrão é não ter).
- Anel de 208 px com o ∂ no buraco: caneta, marca-texto, borracha, laço,
  texto, formas (caneta com reconhecimento de forma), LaTeX e desfazer.
- Dois jeitos: apertar, levar até o item e soltar; ou clicar e depois clicar
  no item. Clique fora ou Esc fecha.
