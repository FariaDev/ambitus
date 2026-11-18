/*
 * Ambitus
 *
 * Unit tests for the notebook library model.
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <set>
#include <string>
#include <vector>

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

TEST(LibraryStoreTest, defaultPaperRoundTripsAndFollowsTheCoverOnMove) {
    TempDir d;
    const auto file = d.path / "cfg" / "library.ini";
    const fs::path odd = u8"/cadernos/[rascunho] Cálculo.xopp";
    {
        LibraryStore s(file);
        EXPECT_EQ(s.paper(odd), std::nullopt);
        s.setCover(odd, Cover{CoverStyle::Linen, "#2f5d6b", 7, {}});
        s.setPaper(odd, "dotted");
        s.setPaper("/cadernos/b.xopp", "grid");
        ASSERT_TRUE(s.save());
    }
    {
        LibraryStore s(file);
        EXPECT_EQ(s.paper(odd), std::optional<std::string>("dotted"));
        EXPECT_EQ(s.paper("/cadernos/b.xopp"), std::optional<std::string>("grid"));
        EXPECT_EQ(s.paper("/cadernos/outro.xopp"), std::nullopt);
        s.moveCover(odd, "/cadernos/novo.xopp");  // a rename carries the paper with the cover
        EXPECT_EQ(s.paper(odd), std::nullopt);
        EXPECT_EQ(s.paper("/cadernos/novo.xopp"), std::optional<std::string>("dotted"));
        ASSERT_TRUE(s.save());
    }
    LibraryStore s(file);
    EXPECT_EQ(s.cover("/cadernos/novo.xopp").style, CoverStyle::Linen);
    EXPECT_EQ(s.paper("/cadernos/novo.xopp"), std::optional<std::string>("dotted"));
    s.setPaper("/cadernos/novo.xopp", "");
    EXPECT_EQ(s.paper("/cadernos/novo.xopp"), std::nullopt);
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

auto allStyles() -> std::array<CoverStyle, 22> {
    return {CoverStyle::Contour,    CoverStyle::Linen, CoverStyle::Page,      CoverStyle::Plain,
            CoverStyle::Field,      CoverStyle::Waves, CoverStyle::Sunflower, CoverStyle::Truchet,
            CoverStyle::Lissajous,  CoverStyle::Graph, CoverStyle::Mesh,      CoverStyle::Ford,
            CoverStyle::Pascal,     CoverStyle::Ulam,  CoverStyle::Hilbert,   CoverStyle::Surface,
            CoverStyle::Pythagoras, CoverStyle::Rose,  CoverStyle::Fibonacci, CoverStyle::Blocks,
            CoverStyle::Relief,     CoverStyle::Julia};
}

/// Mean luminance of the first half of the cover (the title sits at the bottom).
auto meanLuminance(cairo_surface_t* s, int y0, int y1) -> double {
    const int w = cairo_image_surface_get_width(s);
    double sum = 0;
    int n = 0;
    for (int y = y0; y < y1; y++) {
        for (int x = 0; x < w; x++) {
            const uint32_t p = pixel(s, x, y);
            sum += ((p >> 16 & 0xff) * .2126 + (p >> 8 & 0xff) * .7152 + (p & 0xff) * .0722) / 255.0;
            n++;
        }
    }
    return sum / n;
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
    for (auto style: allStyles()) {
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

TEST(LibraryCover, everyNewStyleIsStableAndSeeded) {
    const std::array<CoverStyle, 18> fresh = {CoverStyle::Field,      CoverStyle::Waves,     CoverStyle::Sunflower,
                                              CoverStyle::Truchet,    CoverStyle::Lissajous, CoverStyle::Graph,
                                              CoverStyle::Mesh,       CoverStyle::Ford,      CoverStyle::Pascal,
                                              CoverStyle::Ulam,       CoverStyle::Hilbert,   CoverStyle::Surface,
                                              CoverStyle::Pythagoras, CoverStyle::Rose,      CoverStyle::Fibonacci,
                                              CoverStyle::Blocks,     CoverStyle::Relief,    CoverStyle::Julia};
    for (auto style: fresh) {
        CoverInput in{Cover{style, "#5b3f78", 0, {}}, "Cálculo I", true, "", nullptr, nullptr};
        auto* a = renderCover(in);
        auto* b = renderCover(in);
        EXPECT_TRUE(samePixels(a, b)) << coverStyleToString(style) << " changes between renders";
        // pascal, hilbert and fibonacci only draw two variants: some seeds land on the same one
        bool seedChanges = false;
        for (unsigned seed = 1; seed <= 12 && !seedChanges; seed++) {
            CoverInput seeded = in;
            seeded.cover.seed = seed;
            auto* c = renderCover(seeded);
            seedChanges = !samePixels(a, c);
            cairo_surface_destroy(c);
        }
        EXPECT_TRUE(seedChanges) << coverStyleToString(style) << " ignores the seed";
        CoverInput plainIn{Cover{CoverStyle::Plain, "#5b3f78", 0, {}}, "Cálculo I", true, "", nullptr, nullptr};
        auto* p = renderCover(plainIn);
        EXPECT_FALSE(samePixels(a, p)) << coverStyleToString(style) << " draws nothing";
        for (auto* s: {a, b, p}) {
            cairo_surface_destroy(s);
        }
    }
}

TEST(LibraryCover, newStylesDrawDarkInkOnLightCovers) {
    CoverInput plainIn{Cover{CoverStyle::Plain, "#f2efe6", 0, {}},
                       "Equações Diferenciais Ordinárias e Parciais",
                       true,
                       "",
                       nullptr,
                       nullptr};
    auto* plain = renderCover(plainIn);
    const double base = meanLuminance(plain, 0, 100);
    for (auto style: allStyles()) {
        if (style == CoverStyle::Contour || style == CoverStyle::Linen || style == CoverStyle::Page ||
            style == CoverStyle::Plain) {
            continue;
        }
        CoverInput in{Cover{style, "#f2efe6", 0, {}}, plainIn.title, true, "", nullptr, nullptr};
        auto* s = renderCover(in);
        EXPECT_LT(meanLuminance(s, 0, 100), base) << coverStyleToString(style);
        cairo_surface_destroy(s);
    }
    cairo_surface_destroy(plain);
}

TEST(LibraryStoreTest, everyCoverStyleIdRoundTrips) {
    for (auto style: allStyles()) {
        EXPECT_EQ(coverStyleFromString(coverStyleToString(style)), style) << coverStyleToString(style);
    }
    EXPECT_EQ(coverStyleFromString("contorno"), std::nullopt);
}

TEST(LibraryStoreTest, roundTripWithNewStyle) {
    TempDir d;
    const auto file = d.path / "cfg" / "library.ini";
    const fs::path notebook = u8"/cadernos/Análise complexa.xopp";
    {
        LibraryStore s(file);
        s.setCover(notebook, Cover{CoverStyle::Julia, "#8a3b2e", 4, {}});
        ASSERT_TRUE(s.save());
    }
    LibraryStore s(file);
    EXPECT_EQ(s.cover(notebook), (Cover{CoverStyle::Julia, "#8a3b2e", 4, {}}));
}

TEST(LibraryCover, suggestedStyleIsStableAndGenerative) {
    for (const char* name: {"Cálculo I", "Física II", "Química", "Álgebra Linear", "EDOs", "Topologia"}) {
        const auto style = suggestedStyle(name);
        EXPECT_EQ(style, suggestedStyle(name)) << name;
        EXPECT_NE(style, CoverStyle::Linen) << name;
        EXPECT_NE(style, CoverStyle::Page) << name;
        EXPECT_NE(style, CoverStyle::Plain) << name;
    }
    std::set<CoverStyle> seen;
    for (int i = 0; i < 60; i++) {
        seen.insert(suggestedStyle("Caderno " + std::to_string(i)));
    }
    EXPECT_GE(seen.size(), 12u);
}

// Not a check: with AMBITUS_COVER_DUMP set, writes the 22 styles (seed 0) on a dark and a light cover, for a contact
// sheet.
TEST(LibraryCover, contactSheetDump) {
    const char* dir = g_getenv("AMBITUS_COVER_DUMP");
    if (!dir) {
        GTEST_SKIP() << "set AMBITUS_COVER_DUMP to write the contact sheet";
    }
    for (const char* colour: {"#5b3f78", "#f2efe6"}) {
        for (auto style: allStyles()) {
            CoverInput in{Cover{style, colour, 0, {}},
                          "Equações Diferenciais Ordinárias e Parciais",
                          true,
                          "",
                          nullptr,
                          nullptr};
            cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 152, 200);
            cairo_t* cr = cairo_create(s);
            drawCover(cr, 152, 200, in);
            cairo_destroy(cr);
            cairo_surface_flush(s);
            const std::string name =
                    std::string(dir) + "/sheet-" + (colour + 1) + "-" + coverStyleToString(style) + ".png";
            cairo_surface_write_to_png(s, name.c_str());
            cairo_surface_destroy(s);
        }
    }
}
