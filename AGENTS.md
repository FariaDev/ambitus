# Ambitus — instruções para agentes

Ambitus é o Xournal++ modificado do Lucas, irmão do Summa (`FariaDev/summa`). Fale com o usuário no idioma dele. Licença GPL-2.0+, herdada do Xournal++.

## Origem e upstream

- Este repositório tem o histórico inteiro de `xournalpp/xournalpp`. O remote `upstream` aponta para lá; para trazer correções e versões novas: `git fetch upstream && git merge upstream/master` numa branch própria.
- Não é fork do GitHub porque fork de repositório público não pode ser privado. O repositório é privado.
- O GitHub Actions está desligado neste repositório (a cobrança da CI está bloqueada). Não o religue nem dispare workflow sem o ok do Lucas.

## Regras

- **Manter o `.xopp` compatível com o Xournal++.** O tutor do Summa lê os cadernos com `xournalpp --create-img` e lê o texto e o LaTeX do arquivo; é isso que faz os dois programas conversarem sem mudança no Summa. Mudança de formato (página que cresce, canvas infinito) precisa de plano de compatibilidade antes.
- **Os cadernos vêm do Summa.** A biblioteca de cadernos, a busca, o índice e modelo de tinta (`desk/notebook-ink-*`) e o plugin Lua `MesaPagina` saem do Summa e são reimplementados aqui. O código antigo fica na tag `cadernos-antes-do-xournal` do Summa (commit `5a98aa2c`). O código é do Lucas, então pode entrar aqui sob GPL.
- **O caminho contrário não vale:** código do Xournal++ ou deste repositório não entra no Summa.
- **Archey Notes e Traços servem só de inspiração** de interface e fluxo. Não copie código de lá.
- **Nada público** (push para repositório público, release, PR fora deste repo) sem o ok explícito do Lucas.
- Commits terminam com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>` quando feitos por agente. Nunca `git stash` puro.
- Siga o estilo de código do Xournal++ nos arquivos dele (`.clang-format` na raiz); não reformate arquivos inteiros, para o merge com o upstream continuar limpo.

## Marca

Em [`marca/`](marca/README.md): o símbolo ∂ a pincel, os ícones e a família visual herdada do Summa.

## O que o Lucas quer

Quatro eixos: organização, espaço de trabalho, experiência e matemática. Os que mais incomodam hoje são organização e espaço de trabalho.
