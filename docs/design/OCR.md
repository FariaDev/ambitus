# OCR da escrita à mão: plano

Estado: **direção escolhida (a ponte do Summa), falta o ok para começar.**

## O que se quer

- A busca da biblioteca achar o que foi escrito à mão, como já acha o texto
  digitado e o LaTeX.
- Depois, talvez: a fórmula escrita à mão virar LaTeX para o tutor do Summa.

## Direção: o Summa lê a tinta, o Ambitus decide o que ler

Em vez de um modelo local, a leitura usa o modelo de visão que a pessoa já usa
no Summa. Isso já existiu e funcionou no Summa antes de os cadernos virem para
cá (tag `cadernos-antes-do-xournal`, código do Lucas, pode voltar):

- `src/agents/notebook-ink-bridge.cjs` (#158): lê o texto manuscrito de uma
  imagem com o modelo DA PESSOA, numa sessão descartável, sem ferramentas,
  sem log. A instrução já pede LaTeX entre `$`.
- `notebook-ink-model.cjs`: só usa um modelo que recebe imagem e que não é
  o grátis do Zen; senão diz o motivo e a busca fica com o texto digitado.
- `ink-convert-host.cjs` (#159): a ponte local. HTTP em 127.0.0.1, porta
  efêmera, token de 32 bytes em `runtime/ink-convert-bridge.json` (0600),
  desligada por padrão. O plugin Lua `MesaPagina` mandava a seleção por ela
  e recebia o texto ("Converter a seleção em texto").
- `notebook-ink-index.cjs`: o índice por página com hash, para reler só o
  que mudou.

Proposta: **trazer a ponte de volta ao Summa, e o Ambitus fala com ela.**

1. **Summa:** a ponte, com a leitura da tinta e o modelo de sempre. Nada de
   Xournal++ entra lá: ela recebe uma imagem e devolve texto.
2. **Ambitus:** decide o que ler. O índice da busca (`library-index.json`)
   já sabe quais cadernos mudaram; ganha, por página, o hash da tinta e o
   texto lido. Em segundo plano, renderiza a página que mudou (num quadro, só
   a parte escrita) e manda à ponte. Sem o Summa aberto ou com a opção
   desligada, a busca continua como hoje.
3. A mesma ponte serve para mais duas coisas:
   - **Converter a seleção em texto**, como fazia o plugin, agora no app.
   - **Mandar a página ao tutor:** o Summa já tem o chat solto (#157), uma
     janela à parte, encostada à direita e "sempre por cima", feita para
     ficar ao lado do Xournal++. O que falta é a página chegar nele. O
     Conferir de hoje fotografa a janela e só existe no macOS e no Windows;
     o Ambitus pode mandar a própria página renderizada, inteira e nítida,
     com o caminho e o número, para o tutor reler pelo `ler_caderno`.

Nada vai para o `.xopp`: o texto lido fica no índice do cache.

## Cuidados

- **As notas saem da máquina.** É uma opção a ligar no Summa, desligada por
  padrão, como era a `inkReading`.
- **Custo e limites:** é uma chamada por página. Só o que mudou, em segundo
  plano, com um teto; cota esgotada só adia.
- Sem rede ou sem o Summa, nada muda: a busca acha o que já foi lido.

## Antes de começar

- O ok para mexer no Summa: trazer a ponte de volta da tag, mais o pedido
  "mandar a página ao chat solto".
- Em quais cadernos medir a taxa de acerto (umas 10 páginas).
