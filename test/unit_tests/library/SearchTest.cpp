/*
 * Ambitus
 *
 * Unit tests for the search inside notebooks.
 *
 * @license GNU GPLv2 or later
 */

#include <string>

#include <glib.h>
#include <gtest/gtest.h>
#include <zip.h>
#include <zlib.h>

#include "library/NotebookSearch.h"

#include "filesystem.h"

using namespace ambitus::library;

namespace {
struct TempDir {
    fs::path path;
    TempDir() {
        gchar* p = g_dir_make_tmp("ambitus-search-XXXXXX", nullptr);
        path = fs::canonical(p);
        g_free(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

const std::string XML = R"(<?xml version="1.0" standalone="no"?>
<xournal creator="Xournal++ 1.3.0" fileversion="4">
<title>Xournal++ document</title>
<page width="595" height="842"><background type="solid" color="#ffffffff" style="lined"/>
<layer>
<stroke tool="pen" color="#000000ff" width="1.41">10 10 20 20</stroke>
<text font="Sans" size="12" x="10" y="10" color="#000000ff">Limite lateral à direita
de 1/x em 0 é +∞</text>
</layer>
</page>
<page width="595" height="842"><background type="solid" color="#ffffffff" style="plain"/>
<layer>
<teximage text="\lim_{x \to 0^+} \frac{1}{x} = +\infty" left="1" top="1" right="2" bottom="2">AAAA</teximage>
<text font="Sans" size="12" x="1" y="1" color="#000000ff">Exercício: cada LIMITE LATERAL existe?</text>
</layer>
</page>
</xournal>
)";

void writeGz(const fs::path& p, const std::string& content) {
    gzFile f = gzopen(p.string().c_str(), "wb");
    gzwrite(f, content.data(), static_cast<unsigned>(content.size()));
    gzclose(f);
}

void writeZip(const fs::path& p, const std::string& content) {
    int err = 0;
    zip_t* z = zip_open(p.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    ASSERT_NE(z, nullptr);
    static const std::string mime = "application/xournal++";
    static const std::string version = "current=1\nmin=1\n";
    zip_file_add(z, "mimetype", zip_source_buffer(z, mime.data(), mime.size(), 0), 0);
    zip_file_add(z, "META-INF/version", zip_source_buffer(z, version.data(), version.size(), 0), 0);
    zip_file_add(z, "content.xml", zip_source_buffer(z, content.data(), content.size(), 0), 0);
    ASSERT_EQ(zip_close(z), 0);
}
}  // namespace

TEST(LibrarySearch, extractsTextAndLatexFromGzip) {
    TempDir d;
    writeGz(d.path / "a.xopp", XML);
    auto pages = extractNotebookText(d.path / "a.xopp");
    ASSERT_TRUE(pages);
    ASSERT_EQ(pages->size(), 2u);
    EXPECT_EQ((*pages)[0].texts, (std::vector<std::string>{"Limite lateral à direita\nde 1/x em 0 é +∞"}));
    EXPECT_TRUE((*pages)[0].latex.empty());
    EXPECT_EQ((*pages)[1].latex, (std::vector<std::string>{"\\lim_{x \\to 0^+} \\frac{1}{x} = +\\infty"}));
    EXPECT_EQ((*pages)[1].texts.size(), 1u);
}

TEST(LibrarySearch, extractsFromZip) {
    TempDir d;
    writeZip(d.path / "z.xopp", XML);
    auto pages = extractNotebookText(d.path / "z.xopp");
    ASSERT_TRUE(pages);
    EXPECT_EQ(pages->size(), 2u);
}

TEST(LibrarySearch, badFilesGiveNothing) {
    TempDir d;
    writeGz(d.path / "bad.xopp", "<xournal><page>");
    EXPECT_FALSE(extractNotebookText(d.path / "bad.xopp"));
    EXPECT_FALSE(extractNotebookText(d.path / "missing.xopp"));
}

TEST(LibrarySearch, foldIgnoresCaseAndAccents) {
    auto f = foldForSearch("Exercício À");
    EXPECT_EQ(f.text, "exercicio a");
    // the folded "i" of "í" points back to the two bytes of "í"
    EXPECT_EQ(f.start[5], 5u);
    EXPECT_EQ(f.end[5], 7u);
    EXPECT_EQ(f.start[6], 7u);  // and the next character follows
}

TEST(LibrarySearch, findsMatchesWithExcerpt) {
    TempDir d;
    const auto nb = d.path / "Limites.xopp";
    writeGz(nb, XML);
    SearchIndex index(d.path / "index.json");
    EXPECT_EQ(index.update({nb}), 1u);

    auto hits = index.search("limite lateral", {nb});
    ASSERT_EQ(hits.size(), 2u);
    EXPECT_EQ(hits[0].page, 0u);
    EXPECT_EQ(hits[0].before, "");
    EXPECT_EQ(hits[0].match, "Limite lateral");
    EXPECT_EQ(hits[0].after, " à direita de 1/x em 0 é +∞");
    EXPECT_EQ(hits[1].page, 1u);
    EXPECT_EQ(hits[1].before, "Exercício: cada ");
    EXPECT_EQ(hits[1].match, "LIMITE LATERAL");

    auto tex = index.search("\\frac", {nb});
    ASSERT_EQ(tex.size(), 1u);
    EXPECT_TRUE(tex[0].latex);

    EXPECT_EQ(index.search("exercicio", {nb}).size(), 1u);  // accent-insensitive
    EXPECT_TRUE(index.search("   ", {nb}).empty());
    EXPECT_TRUE(index.search("limite", {}).empty());  // only the notebooks asked for
}

TEST(LibrarySearch, longTextIsCut) {
    TempDir d;
    const auto nb = d.path / "long.xopp";
    std::string filler(200, 'a');
    writeGz(nb, "<xournal><page><layer><text>" + filler + " alvo " + filler + "</text></layer></page></xournal>");
    SearchIndex index(d.path / "index.json");
    index.update({nb});
    auto hits = index.search("alvo", {nb});
    ASSERT_EQ(hits.size(), 1u);
    EXPECT_EQ(hits[0].before.rfind("…", 0), 0u);
    EXPECT_LT(hits[0].before.size(), 50u);
    EXPECT_EQ(hits[0].after.substr(hits[0].after.size() - 3), "…");
}

TEST(LibrarySearch, indexIsSavedAndOnlyChangedFilesAreRead) {
    TempDir d;
    const auto a = d.path / "a.xopp";
    const auto b = d.path / "b.xopp";
    writeGz(a, XML);
    writeGz(b, "<xournal><page><layer><text>outra coisa</text></layer></page></xournal>");
    {
        SearchIndex index(d.path / "cache" / "index.json");
        EXPECT_EQ(index.update({a, b}), 2u);
        ASSERT_TRUE(index.save());
    }
    SearchIndex index(d.path / "cache" / "index.json");
    EXPECT_EQ(index.size(), 2u);
    EXPECT_EQ(index.update({a, b}), 0u);
    EXPECT_EQ(index.search("limite", {a, b}).size(), 2u);

    // b changes size: only b is read again; a is dropped when no longer listed
    writeGz(b, "<xournal><page><layer><text>agora fala de limite</text></layer></page></xournal>");
    EXPECT_EQ(index.update({b}), 1u);
    EXPECT_EQ(index.size(), 1u);
    EXPECT_EQ(index.search("limite", {a, b}).size(), 1u);
}

TEST(LibrarySearch, corruptCacheStartsEmpty) {
    TempDir d;
    g_file_set_contents((d.path / "index.json").string().c_str(), "{\"version\": 1, \"notebooks\": [{\"path\": 3}", -1,
                        nullptr);
    SearchIndex index(d.path / "index.json");
    EXPECT_EQ(index.size(), 0u);
}

TEST(LibrarySearch, theIndexKnowsTheBoardsAndKeepsThem) {
    TempDir d;
    const auto board = d.path / "quadro.xopp";
    const auto notebook = d.path / "caderno.xopp";
    writeGz(board, R"(<xournal><page width="1602" height="1008">)"
                   R"(<background type="solid" color="#ffffffff" style="dotted" config="r1=18,ambitus=board"/>)"
                   R"(<layer><text>mapa</text></layer></page></xournal>)");
    writeGz(notebook, XML);
    {
        SearchIndex index(d.path / "index.json");
        index.update({board, notebook});
        EXPECT_TRUE(index.isBoard(board));
        EXPECT_FALSE(index.isBoard(notebook));
        EXPECT_FALSE(index.isBoard(d.path / "other.xopp"));
        ASSERT_TRUE(index.save());
    }
    SearchIndex index(d.path / "index.json");
    EXPECT_EQ(index.update({board, notebook}), 0u);  // from the cache
    EXPECT_TRUE(index.isBoard(board));
}
