/*
 * Ambitus
 *
 * @license GNU GPLv2 or later
 */

#include <gio/gio.h>
#include <gtest/gtest.h>

#include "ambitus/Commands.h"

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
