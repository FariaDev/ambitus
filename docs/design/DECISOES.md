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
- Ordem: crescer para direita/baixo → render em ladrilhos (hoje cada página é
  um buffer inteiro, `RenderJob.cpp`) → crescer para esquerda/cima →
  navegação (arrastar, zoom-out, minimapa) → recortes para o tutor do Summa
  em vez do `--create-img` da página inteira (plano de compatibilidade antes).

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
