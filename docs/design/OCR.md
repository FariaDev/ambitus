# OCR da escrita à mão: plano

Estado: **direção escolhida (o modelo do Summa), falta o ok para começar.**

## O que se quer

- A busca da biblioteca achar o que foi escrito à mão, como já acha o texto
  digitado e o LaTeX.
- Depois, talvez: a fórmula escrita à mão virar LaTeX para o tutor do Summa.

## Direção: o Summa lê, o Ambitus busca

Em vez de um modelo local, o OCR usa o modelo que a pessoa já usa no Summa
(Claude ou o OpenCode, com a mesma escolha e a mesma configuração travada).
Motivos:

- O tutor do Summa já manda a imagem das páginas para esse modelo
  (`notebook-tool.mjs`, pelo `--create-img`), então ele já lê a letra da
  pessoa. Um modelo local pequeno, só na CPU desta máquina, leria pior e mais
  devagar, e pediria um download de alguns GB.
- A chave, a escolha do modelo, a lista dos grátis com a política de dados
  (`free-models.json`) e a trava do `opencode serve` já existem no Summa.
  Refazer isso no Ambitus seria duplicar e manter duas vezes.
- A regra "código do Xournal++ não entra no Summa" continua de pé: o Summa só
  roda o `xournalpp --create-img`, como já faz, e escreve um JSON.

Quem faz o quê:

1. **Summa:** numa tarefa de fundo, pega as páginas que mudaram, exporta cada
   uma pelo `--create-img` e pede ao modelo a transcrição (texto e LaTeX).
2. **Arquivo ao lado do caderno**, oculto como o autosave:
   `.Nome.ocr.json`, com o texto por página e uma impressão digital do
   conteúdo de cada página. Página que mudou é refeita; as outras não.
3. **Ambitus:** o índice da busca lê esse arquivo e mostra o resultado
   marcado como "escrito à mão". Sem o arquivo, a busca continua como hoje.

Por que não dentro do `.xopp`: o Xournal++ ignora o que não conhece e perde
isso ao salvar, e o Summa escrevendo num `.xopp` aberto no Ambitus brigaria
com o salvamento dele. O arquivo ao lado viaja junto com o caderno numa
sincronização e não muda o formato.

## Cuidados

- **As notas saem da máquina.** É uma opção a ligar no Summa, desligada por
  padrão. Nos modelos grátis, só os que não guardam nada (`policy: zero`).
- **O modelo precisa enxergar imagem.** Nem todo modelo da lista enxerga; o
  Summa avisa e não indexa com um que não enxerga.
- **Custo e limites:** é uma chamada por página. Indexa só o que mudou, em
  segundo plano, com um teto por dia; o limite do grátis (429) só adia.
- Sem rede, nada muda: a busca acha o que já foi indexado.

## Ideia para depois: o chat do Summa junto do caderno

O chat lateral do Summa ao lado da página aberta no Ambitus. Embutir a janela
do Summa no GTK não dá; o caminho leve é um botão "Perguntar ao tutor sobre
esta página" que abre o Summa com o caderno e a página (o tutor já sabe ler
os dois). Fica para depois da rodada de UI.

## Antes de começar

- O ok para mexer no Summa (o trabalho maior é lá, em outro repositório).
- Em quais cadernos medir a taxa de acerto (umas 10 páginas).
