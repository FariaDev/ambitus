/*
 * Ambitus
 *
 * Unit tests for the notebook library model.
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <string>

#include <glib.h>
#include <gtest/gtest.h>

#include "library/CoverRenderer.h"
#include "library/LibraryStore.h"
#include "library/NotebookScanner.h"
#include "library/SummaCourses.h"

#include "filesystem.h"

using namespace ambitus::library;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        gchar* p = g_dir_make_tmp("ambitus-library-XXXXXX", nullptr);
        path = fs::canonical(p);
        g_free(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

auto names(const std::vector<Notebook>& v) -> std::vector<std::string> {
    std::vector<std::string> out;
    for (auto& n: v) {
        out.push_back(n.folder.empty() ? n.name : n.folder + "/" + n.name);
    }
    std::sort(out.begin(), out.end());
    return out;
}
}  // namespace

TEST(LibrarySumma, parsesCourses) {
    auto courses = parseSummaCourses(R"({
        "vaultPath": "",
        "courses": [
            {"id": "calculo-1", "name": "Cálculo I", "path": "/vault/Cálculo I", "color": "#5b4a86"},
            {"id": "fisica", "name": "Física II", "path": "/vault/Física", "color": "azul"},
            {"id": "", "name": "Sem id", "path": "/x"},
            {"id": "sem-caminho", "name": "Sem caminho"},
            "lixo",
            {"id": "quimica", "name": "Química", "path": "/vault/Química", "color": 12}
        ]})");
    ASSERT_EQ(courses.size(), 3u);
    EXPECT_EQ(courses[0].id, "calculo-1");
    EXPECT_EQ(courses[0].name, "Cálculo I");
    EXPECT_EQ(courses[0].path, fs::path(u8"/vault/Cálculo I"));
    EXPECT_EQ(courses[0].color, "#5b4a86");
    EXPECT_EQ(courses[1].color, "");  // not #rrggbb
    EXPECT_EQ(courses[2].color, "");  // not a string
}

TEST(LibrarySumma, invalidJsonGivesNothing) {
    EXPECT_TRUE(parseSummaCourses("").empty());
    EXPECT_TRUE(parseSummaCourses("{").empty());
    EXPECT_TRUE(parseSummaCourses("[]").empty());
    EXPECT_TRUE(parseSummaCourses(R"({"courses": {}})").empty());
}

TEST(LibrarySumma, configPathHonoursRuntimeEnv) {
    g_setenv("LEARNING_DESK_RUNTIME", "/tmp/summa-runtime", true);
    EXPECT_EQ(summaConfigPath(), fs::path("/tmp/summa-runtime/config.json"));
    g_unsetenv("LEARNING_DESK_RUNTIME");
    EXPECT_EQ(summaConfigPath().filename(), fs::path("config.json"));
    EXPECT_EQ(summaConfigPath().parent_path().filename(), fs::path("Mesa de Estudos"));
}

TEST(LibraryScanner, findsNotebooksAndSkipsTheRest) {
    TempDir d;
    touch(d.path / "aula 1.xopp");
    touch(d.path / "Listas" / "lista 2.xopp");
    touch(d.path / "Listas" / "fundo.pdf");
    touch(d.path / ".oculta" / "escondido.xopp");
    touch(d.path / ".oculto.xopp");
    touch(d.path / "aula 1.autosave.xopp");
    touch(d.path / "aula 1.xopp~");
    EXPECT_EQ(names(scanNotebooks(d.path)), (std::vector<std::string>{"Listas/lista 2", "aula 1"}));
}

TEST(LibraryScanner, respectsDepth) {
    TempDir d;
    touch(d.path / "a" / "b" / "c" / "fundo.xopp");
    EXPECT_EQ(scanNotebooks(d.path, 3).size(), 1u);
    EXPECT_EQ(scanNotebooks(d.path, 2).size(), 0u);
}

TEST(LibraryScanner, symlinksStayInsideAndCyclesEnd) {
    TempDir d;
    TempDir outside;
    touch(outside.path / "alheio.xopp");
    touch(d.path / "Aulas" / "aula.xopp");
    fs::create_directory_symlink(outside.path, d.path / "fora");
    fs::create_directory_symlink(d.path, d.path / "Aulas" / "volta");            // cycle
    fs::create_symlink(d.path / "Aulas" / "aula.xopp", d.path / "atalho.xopp");  // same file twice
    auto found = scanNotebooks(d.path);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].path, d.path / "Aulas" / "aula.xopp");
}

TEST(LibraryScanner, missingRootIsEmpty) { EXPECT_TRUE(scanNotebooks("/nao/existe/mesmo").empty()); }

TEST(LibraryStoreTest, roundTrip) {
    TempDir d;
    const auto file = d.path / "cfg" / "library.ini";
    const fs::path odd = u8"/cadernos/[rascunho] Cálculo.xopp";
    {
        LibraryStore s(file);
        EXPECT_TRUE(s.addFolder("/home/eu/Leituras"));
        EXPECT_FALSE(s.addFolder("/home/eu/Leituras"));
        EXPECT_TRUE(s.addFolder("/home/eu/Rascunhos/"));
        s.setCover(odd, Cover{CoverStyle::Linen, "#2f5d6b", 7, {}});
        s.setCover("/cadernos/b.xopp", Cover{CoverStyle::Page, "", 0, "/img/capa.png"});
        s.setCover("/cadernos/padrao.xopp", Cover{});  // default is not stored
        ASSERT_TRUE(s.save());
    }
    LibraryStore s(file);
    ASSERT_EQ(s.folders().size(), 2u);
    EXPECT_EQ(s.folders()[0].name, "Leituras");
    EXPECT_EQ(s.folders()[1].name, "Rascunhos");
    EXPECT_EQ(s.cover(odd), (Cover{CoverStyle::Linen, "#2f5d6b", 7, {}}));
    EXPECT_EQ(s.cover("/cadernos/b.xopp").image, fs::path("/img/capa.png"));
    EXPECT_EQ(s.cover("/cadernos/nenhum.xopp"), Cover{});
}

TEST(LibraryStoreTest, moveAndRemove) {
    TempDir d;
    LibraryStore s(d.path / "library.ini");
    s.setCover("/a.xopp", Cover{CoverStyle::Plain, "#123456", 0, {}});
    s.moveCover("/a.xopp", "/b.xopp");
    EXPECT_EQ(s.cover("/a.xopp"), Cover{});
    EXPECT_EQ(s.cover("/b.xopp").color, "#123456");
    s.addFolder("/x");
    EXPECT_TRUE(s.removeFolder("/x"));
    EXPECT_FALSE(s.removeFolder("/x"));
}

TEST(LibraryStoreTest, unreadableFileStartsEmpty) {
    TempDir d;
    std::ofstream(d.path / "library.ini") << "[sem fim\nlixo";
    LibraryStore s(d.path / "library.ini");
    EXPECT_TRUE(s.folders().empty());
}

namespace {
auto renderCover(const CoverInput& in, int w = 152, int h = 200) -> cairo_surface_t* {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* cr = cairo_create(s);
    drawCover(cr, w, h, in);
    cairo_destroy(cr);
    cairo_surface_flush(s);
    if (const char* dir = g_getenv("AMBITUS_COVER_DUMP")) {
        auto name = std::string(dir) + "/" + coverStyleToString(in.cover.style) + "-" + std::to_string(in.cover.seed) +
                    "-" + std::to_string(w) + ".png";
        cairo_surface_write_to_png(s, name.c_str());
    }
    return s;
}

auto samePixels(cairo_surface_t* a, cairo_surface_t* b) -> bool {
    const int n = cairo_image_surface_get_stride(a) * cairo_image_surface_get_height(a);
    return std::equal(cairo_image_surface_get_data(a), cairo_image_surface_get_data(a) + n,
                      cairo_image_surface_get_data(b));
}

auto pixel(cairo_surface_t* s, int x, int y) -> uint32_t {
    auto* row = cairo_image_surface_get_data(s) + y * cairo_image_surface_get_stride(s);
    return reinterpret_cast<uint32_t*>(row)[x];
}
}  // namespace

TEST(LibraryCover, colourFallsBackToCourseThenDefault) {
    CoverInput in;
    EXPECT_EQ(effectiveColor(in), DEFAULT_COVER_COLOR);
    in.courseColor = "#5b4a86";
    EXPECT_EQ(effectiveColor(in), "#5b4a86");
    in.cover.color = "#2f5d6b";
    EXPECT_EQ(effectiveColor(in), "#2f5d6b");
}

TEST(LibraryCover, contourIsStableAndSeedChangesIt) {
    CoverInput in{Cover{CoverStyle::Contour, "#5b4a86", 0, {}}, "Cálculo I", true, "", nullptr, nullptr};
    auto* a = renderCover(in);
    auto* b = renderCover(in);
    EXPECT_TRUE(samePixels(a, b));
    in.cover.seed = 3;
    auto* c = renderCover(in);
    EXPECT_FALSE(samePixels(a, c));
    // the corners are cut round: the very corner pixel stays transparent
    EXPECT_EQ(pixel(a, 151, 0) >> 24, 0u);
    // the middle of the fore edge is painted
    EXPECT_EQ(pixel(a, 140, 100) >> 24, 255u);
    for (auto* s: {a, b, c}) {
        cairo_surface_destroy(s);
    }
}

TEST(LibraryCover, everyStyleDraws) {
    for (auto style: {CoverStyle::Contour, CoverStyle::Linen, CoverStyle::Page, CoverStyle::Plain}) {
        CoverInput in{Cover{style, "#8a3b2e", 0, {}},
                      "Equações Diferenciais Ordinárias e Parciais",
                      true,
                      "",
                      nullptr,
                      nullptr};
        for (int w: {152, 76}) {
            auto* s = renderCover(in, w, w * 200 / 152);
            EXPECT_EQ(pixel(s, w / 2, w * 100 / 152) >> 24, 255u) << coverStyleToString(style) << " " << w;
            cairo_surface_destroy(s);
        }
    }
}
