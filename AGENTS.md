# Ambitus — instruções para agentes

Ambitus é o Xournal++ modificado do Lucas, irmão do Summa (`FariaDev/summa`). Fale com o usuário no idioma dele. Licença GPL-2.0+, herdada do Xournal++.

## Origem e upstream

- Fork público de `xournalpp/xournalpp`. O remote `upstream` aponta para lá, só para busca (o push está desligado). Para trazer correções e versões novas: `git fetch upstream && git merge upstream/master` numa branch própria.
- A branch principal é `master`, como no upstream.
- O GitHub Actions está desligado (a cobrança da CI está parada). A prova é local; não religue o Actions nem dispare workflow.

## Regras de produto

- **Manter o `.xopp` compatível com o Xournal++.** O tutor do Summa lê os cadernos com `xournalpp --create-img` e lê o texto e o LaTeX do arquivo; é isso que faz os dois programas conversarem sem mudança no Summa. Mudança de formato (página que cresce, canvas infinito) precisa de plano de compatibilidade antes.
- **Os cadernos vêm do Summa.** A biblioteca de cadernos, a busca, o índice e modelo de tinta (`desk/notebook-ink-*`) e o plugin Lua `MesaPagina` saem do Summa e são reimplementados aqui. O código antigo fica na tag `cadernos-antes-do-xournal` do Summa (commit `5a98aa2c`). O código é do Lucas, então pode entrar aqui sob GPL.
- **O caminho contrário não vale:** código do Xournal++ ou deste repositório não entra no Summa.
- **Archey Notes e Traços servem só de inspiração** de interface e fluxo. Não copie código de lá.

## Git

- Push, PR, merge, release e qualquer publicação só com ok explícito do Lucas. Nunca abra PR no `xournalpp/xournalpp` sem ele pedir.
- Commits feitos por agente terminam com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; corpo de PR termina com `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- O repositório é público: nada de e-mail pessoal nos commits. Worktree sem identidade git: `git -c user.name="Lucas Faria" -c user.email="97044017+FariaDev@users.noreply.github.com" commit ...`, sem mudar a config.
- Nunca `git stash` puro: o stash é compartilhado entre worktrees.
- Antes de apagar código que pode voltar: tag anotada empurrada e um doc curto com o mapa e como recuperar.
- Siga o estilo do Xournal++ nos arquivos dele (`.clang-format` e `.clang-tidy` na raiz). Não reformate arquivos inteiros, para o merge com o upstream continuar limpo.

## Delegados

- Modelo: só DeepSeek V4.1 Flash, target `{"providerInstanceId":"opencode","model":"opencode-go/deepseek-v4.1-flash","options":{"variant":"max","agent":"build"}}`. Nunca Claude, MiMo nem Nemotron. No máximo 5 ao mesmo tempo. Chame `orchestrator_capabilities` antes do primeiro `delegate_task`.
- Divisão: o delegado faz estrutura, código mecânico, testes e capturas; quem orquestra faz o brief, o juízo, o acabamento visual e a conferência final.
- Cada delegado ganha a sua worktree, criada antes por quem orquestra: `git -C <clone> fetch origin && git -C <clone> worktree add -b t3/NOME <pasta> origin/master`. O brief começa com "`cd` para lá; não toque outra pasta ou branch; sem push, sem PR".
- Brief com escopo estrito: arquivos que podem mudar, o que fica de fora, "um commit só, sem refactor nem reformatação; achou outra coisa, anote e não corrija". Ao tirar algo, diga também o que fica (o delegado confunde coisas de nome parecido).
- Antes de remover, faça o inventário de arquivos e linhas e mande no brief, pedindo "confira cada item com `rg`, não presuma".
- Prova de trabalho, não resumo: hash, `git diff --stat`, comandos exatos e as linhas reais de pass/fail, testes alterados com o porquê, e o que anotou sem corrigir.
- Revisão de um passo: leia o diff inteiro (primeiro os arquivos que não são só deleção); confira escopo, recuo, nenhum teste apagado ou afrouxado, listas de exceção só encolhem, linha de coautoria certa; rode de novo o teste principal. Falha "pré-existente" só vale se reproduzida no `master` numa worktree limpa.
- Correção pequena da revisão: faça e diga que fez. Se estiver muito fora, devolva ao delegado ou troque.
- Não cheque delegado em loop; a conclusão acorda a thread.

## Visual

- Todo visual sai primeiro como HTML em `docs/design/`, renderizado para o Lucas aprovar. Mock dele é literal; ressalva sua não vira decisão sem ele aceitar.
- Sem sobretítulo e sem texto desnecessário. Captura nunca com material de curso.
- Debian (`DISPLAY=:0`) primeiro; o Mac só na bateria final, avisando antes de abrir janela lá.
- Marca em [`marca/`](marca/README.md): o símbolo ∂ a pincel, os ícones e a família visual herdada do Summa.

## O que o Lucas quer

Quatro eixos: organização, espaço de trabalho, experiência e matemática. Os que mais incomodam hoje são organização e espaço de trabalho.
