# OCR da escrita à mão: plano do protótipo

Estado: **plano, falta decidir o motor.** Nada instalado ainda.

## O que se quer

- A busca da biblioteca achar o que foi escrito à mão, como já acha o texto
  digitado e o LaTeX.
- Depois, talvez: a fórmula escrita à mão virar LaTeX para o tutor do Summa.

## Regras que valem para qualquer motor

- **Local primeiro.** Nada de nota sai da máquina sem o Lucas pedir.
- **Nada vai para o `.xopp`.** O texto reconhecido fica no índice da busca
  (`library-index.json` no cache), por página, refeito só quando o arquivo
  muda (mesma regra de hoje: tamanho e data). O formato continua o do
  Xournal++.
- Roda numa thread de fundo, uma página por vez, sem travar a biblioteca. Um
  resultado da busca vindo do OCR aparece marcado como "escrito à mão".
- A imagem da página sai do próprio renderizador (como o `--create-img`); num
  quadro, só a parte escrita (já é assim).

## Opções de motor

O que há nesta máquina hoje: Ryzen com Vega integrada, 14 GB de RAM, sem
torch, tesseract, ollama ou llama.cpp. Qualquer opção abaixo pede instalar
coisas e baixar o modelo.

| Motor | Português à mão | Matemática | Tamanho | Observação |
|---|---|---|---|---|
| Modelo de visão pequeno pelo llama.cpp (família Qwen-VL, por exemplo) | o melhor candidato | pode devolver LaTeX | alguns GB | só CPU aqui: lento por página, tem de medir; serve para indexar em segundo plano |
| TrOCR (ONNX) | fraco: treinado em inglês, erra acento | não | centenas de MB | precisa cortar a página em linhas antes |
| Tesseract | fraco para escrita à mão | não | pequeno | bom só para texto impresso |
| Reconhecimento "online" pelos traços (usa a ordem dos pontos, que o `.xopp` guarda) | bom nos motores comerciais | alguns fazem | — | não conheço motor aberto e local bom para alfabeto latino; os bons (MyScript, por exemplo) são pagos e/ou na nuvem |

## Protótipo proposto (depois da escolha)

1. Script fora do app que pega um caderno real, exporta as páginas e roda o
   motor; mede o tempo por página e a taxa de acerto em umas 10 páginas do
   Lucas.
2. Se passar: o índice ganha o texto de OCR por página (versão nova do
   índice) e a busca mostra esses resultados.
3. Só depois: LaTeX das fórmulas para o tutor.

## O que o Lucas precisa decidir

- Qual motor testar primeiro (a sugestão é o modelo de visão local) e se
  topa o download de alguns GB.
- Em quais cadernos medir.
