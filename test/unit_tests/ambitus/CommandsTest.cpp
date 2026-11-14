/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <gio/gio.h>
#include <gtest/gtest.h>

#include "ambitus/Commands.h"
#include "gui/CommandPalette.h"

using namespace ambitus;

namespace {
GMenuModel* sampleMenu() {
    GMenu* menu = g_menu_new();
    GMenu* edit = g_menu_new();
    GMenu* section = g_menu_new();
    GMenuItem* undo = g_menu_item_new("_Desfazer", "win.undo");
    g_menu_item_set_attribute(undo, "accel", "s", "<Ctrl>z");
    g_menu_append_item(section, undo);
    g_object_unref(undo);
    g_menu_append(section, "Copiar página como imagem", "win.copy-page-as-image");
    g_menu_append_section(edit, nullptr, G_MENU_MODEL(section));
    GMenu* arrange = g_menu_new();
    g_menu_append(arrange, "Trazer para a frente", "win.arrange-selection-order(1)");
    g_menu_append_submenu(edit, "Organizar Seleção", G_MENU_MODEL(arrange));
    g_menu_append_submenu(menu, "_Editar", G_MENU_MODEL(edit));
    GMenu* tools = g_menu_new();
    GMenuItem* pen = g_menu_item_new("_Caneta", nullptr);
    g_menu_item_set_action_and_target_value(pen, "win.select-tool", g_variant_new_uint64(1));
    g_menu_append_item(tools, pen);
    g_object_unref(pen);
    g_menu_append(tools, "Sem ação", nullptr);
    g_menu_append_submenu(menu, "Ferramentas", G_MENU_MODEL(tools));
    for (GMenu* m: {edit, section, arrange, tools}) {
        g_object_unref(m);
    }
    return G_MENU_MODEL(menu);
}
}  // namespace

TEST(Commands, everyItemWithAnActionWithItsWayAndShortcut) {
    GMenuModel* menu = sampleMenu();
    auto cmds = collectCommands(menu);
    g_object_unref(menu);
    ASSERT_EQ(cmds.size(), 4u);
    EXPECT_EQ(cmds[0].label, "Desfazer");
    EXPECT_EQ(cmds[0].path, "Editar");
    EXPECT_EQ(cmds[0].action, "win.undo");
    EXPECT_EQ(cmds[0].accel, "<Ctrl>z");
    EXPECT_EQ(cmds[2].label, "Trazer para a frente");
    EXPECT_EQ(cmds[2].path, "Editar › Organizar Seleção");
    EXPECT_EQ(cmds[3].label, "Caneta");
    EXPECT_EQ(cmds[3].path, "Ferramentas");
    ASSERT_TRUE(cmds[3].target);
    EXPECT_EQ(g_variant_get_uint64(cmds[3].target.get()), 1u);
    EXPECT_FALSE(cmds[1].target);
}

TEST(Commands, theSearchIgnoresAccentsAndCapitals) {
    EXPECT_EQ(foldForSearch("Página Ç"), "pagina c");
    GMenuModel* menu = sampleMenu();
    auto cmds = collectCommands(menu);
    g_object_unref(menu);
    auto hit = matchCommands(cmds, "PAGINA", 10);
    ASSERT_EQ(hit.size(), 1u);
    EXPECT_EQ(cmds[hit[0]].label, "Copiar página como imagem");
}

TEST(Commands, theNameStartingWithTheWordComesFirst) {
    std::vector<Command> cmds(3);
    cmds[0].label = "Exportar como imagem";
    cmds[1].label = "Imagem";
    cmds[2].label = "Copiar página como imagem";
    cmds[2].path = "Editar";
    auto hit = matchCommands(cmds, "imag", 10);
    ASSERT_EQ(hit.size(), 3u);
    EXPECT_EQ(hit[0], 1u);
    // every word must be somewhere, the way to it counts too
    EXPECT_EQ(matchCommands(cmds, "editar imagem", 10), std::vector<size_t>{2});
    EXPECT_TRUE(matchCommands(cmds, "imagem zebra", 10).empty());
    EXPECT_EQ(matchCommands(cmds, "", 2).size(), 2u);
}

namespace {
std::vector<Candidate> notebooks() {
    return {{"Resumo para a P1", "Cálculo I"},
            {"Rascunho à mão", "Cálculo I"},
            {"Espaços vetoriais", "Álgebra Linear"},
            {"Lista 2", "Física II"},
            {"Reta tangente", "Cálculo I"}};
}
}  // namespace

TEST(Commands, theThreeSearchesOfThePrototype) {
    const auto books = notebooks();
    // "cálc" finds what is in the course Cálculo I, with or without the accent, in any case
    EXPECT_EQ(rankCandidates(books, "cálc", 10), (std::vector<size_t>{0, 1, 4}));
    EXPECT_EQ(rankCandidates(books, "CALC", 10), (std::vector<size_t>{0, 1, 4}));
    // "reta": the name starting with it first, then the others in the order given
    EXPECT_EQ(rankCandidates(books, "reta", 10), (std::vector<size_t>{4}));
    // "summa": the command that sends the page, no notebook
    std::vector<Candidate> commands = {{"Enviar a página ao Summa", ""}, {"Grade de alinhamento", "Exibir"}};
    EXPECT_EQ(rankCandidates(commands, "summa", 10), std::vector<size_t>{0});
    EXPECT_TRUE(rankCandidates(books, "summa", 10).empty());
}

TEST(Commands, theLimitCutsAfterTheOrdering) {
    std::vector<Candidate> c = {{"Cópia", ""}, {"Copiar", ""}, {"Copiar página", ""}};
    EXPECT_EQ(rankCandidates(c, "cop", 2), (std::vector<size_t>{0, 1}));
    EXPECT_TRUE(rankCandidates(c, "cop", 0).empty());
}

TEST(Commands, theMarkFollowsTheTextWithoutMindingAccents) {
    using R = std::vector<std::pair<size_t, size_t>>;
    EXPECT_EQ(highlightRanges("Cálculo I", "calc"), (R{{0, 5}}));  // "Cálc" has the á in two bytes
    EXPECT_EQ(highlightRanges("Resumo para a P1", "PARA resumo"), (R{{0, 6}, {7, 11}}));
    EXPECT_EQ(highlightRanges("Lista 2", "zebra"), R{});
    EXPECT_EQ(highlightRanges("Lista 2", ""), R{});
    // words that overlap are one mark
    EXPECT_EQ(highlightRanges("Reta tangente", "ret eta"), (R{{0, 4}}));
}

TEST(Commands, withNothingTypedTheRecentNotebooksShow) {
    const auto books = notebooks();
    std::vector<Candidate> commands = {{"Caneta", ""}, {"Marca-texto", ""}};
    using S = PaletteSection;
    auto rows = arrangePalette(commands, books, {4, 0, 3}, "  ");
    EXPECT_EQ(rows, (std::vector<PaletteRow>{{S::Recent, 4}, {S::Recent, 0}, {S::Recent, 3}}));
    PaletteLimits two;
    two.recent = 2;
    EXPECT_EQ(arrangePalette(commands, books, {4, 0, 3}, "", two).size(), 2u);
    // no recent notebook: the first commands, not an empty box
    EXPECT_EQ(arrangePalette(commands, books, {}, ""), (std::vector<PaletteRow>{{S::Commands, 0}, {S::Commands, 1}}));
}

TEST(Commands, withATextTheCommandsComeBeforeTheNotebooks) {
    const auto books = notebooks();
    std::vector<Candidate> commands = {{"Caneta", ""}, {"Calcular área", "Ferramentas"}, {"Zoom", ""}};
    using S = PaletteSection;
    auto rows = arrangePalette(commands, books, {0}, "calc");
    // "calc": one command, and the notebooks of Cálculo I in the order of the list
    EXPECT_EQ(rows,
              (std::vector<PaletteRow>{{S::Commands, 1}, {S::Notebooks, 0}, {S::Notebooks, 1}, {S::Notebooks, 4}}));
    PaletteLimits one;
    one.notebooks = 1;
    EXPECT_EQ(arrangePalette(commands, books, {}, "calc", one).size(), 2u);
    EXPECT_TRUE(arrangePalette(commands, books, {}, "zebra").empty());
}

TEST(Commands, theBoxFadesAtTheTimesOfTheSumma) {
    EXPECT_EQ(palette::FADE_IN_MS, 180);
    EXPECT_EQ(palette::FADE_OUT_MS, 120);
}
