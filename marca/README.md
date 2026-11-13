# Marca do Ambitus

Ambitus é o Xournal++ modificado, irmão do Summa. *Ambitus* é latim para contorno, e **∂Ω** é o contorno da região Ω.

- `simbolo.svg`: a ∂ a pincel, em `currentColor`, para seguir o tema e o acento.
- `icone-escuro.svg` e `icone-claro.svg`: o símbolo na placa.
- `traco-original.png`: o traço gerado no ChatGPT, de onde saiu o vetor.

## Como o símbolo foi feito

1. Prompt no mesmo chat da integral do Summa, pedindo imagem nova (não edição) com o mesmo pincel:

   > A single partial derivative sign (∂) painted with one confident stroke of a round brush and black sumi ink, like expert calligraphy: the brush starts at a small round ink drop at the top left, curls up and over to the right, sweeps down the right side and around the bottom, and closes the bowl back up on the left. Thick through the long right-hand sweep, tapering naturally at both ends, never thinner than a third of the thickest part. Organic edges but no splatter. It must look painted by hand, not like a font glyph. Pure black (#000000) on pure white (#FFFFFF). No paper texture, no gray, no shadow, no text, no watermark. Centered, about 65% of a square 1024×1024 canvas.

2. Vetorizado com o port JS do potrace (`potrace@2.1.8`), depois de um desfoque de raio 4 (o "alisado leve"): `threshold 128`, `turdSize 60`, `optTolerance 0.6`, `alphaMax 1.1`.
3. Na placa, a ∂ ocupa um quadro ~6% maior que o da ∫ do Summa, para os dois terem o mesmo peso visual no Dock.

## Família visual

Mesma família do Summa: cinzas neutros, acentos só grafite (#303030; #e6e6e6 no escuro) e violeta (#8044ca; #8957d0 no escuro), IBM Plex Sans e Mono, STIX Two Text no logotipo, cantos de 4 px em controles e 8 px em flutuantes. Sem sobretítulo e sem texto desnecessário.
