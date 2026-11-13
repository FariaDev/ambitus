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
