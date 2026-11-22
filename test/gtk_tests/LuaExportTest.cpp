#include "config-features.h"

#ifdef ENABLE_PLUGINS

#include <fstream>
#include <memory>

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <gtest/gtest.h>

#include "control/Control.h"
#include "dialog/GtkTest.h"
#include "gui/GladeSearchpath.h"
#include "model/Document.h"
#include "model/XojPage.h"
#include "plugin/Plugin.h"
#include "util/PathUtil.h"

#include "config-test.h"

class LuaExportTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GladeSearchpath paths;
        paths.addSearchDirectory(GET_UI_FOLDER);
        paths.addSearchDirectory(XOJ_TEST_RESOURCE_DIR);
        Control control(G_APPLICATION(app), &paths);
        control.getDocument()->addPage(std::make_shared<XojPage>(72, 144));

        const auto directory = Util::getCacheSubfolder("lua-export-test");
        ASSERT_TRUE(std::ofstream(directory / "plugin.ini") << R"ini([default]
enabled=true
[plugin]
mainfile=main.lua
)ini");
        ASSERT_TRUE(std::ofstream(directory / "main.lua") << R"lua(
function exportPngs(directory)
  local cases = {
    height = {pngHeight = 128},
    legacy = {dpiHeight = 64},
    both = {pngHeight = 128, dpiHeight = 64},
    width = {pngHeight = 128, pngWidth = 40},
    dpi = {pngHeight = 128, pngWidth = 40, pngDpi = 72},
  }
  for name, options in pairs(cases) do
    options.outputFile = directory .. "/" .. name .. ".png"
    options.range = "1"
    app.export(options)
  end
  for _, ext in ipairs({"png", "PNG", "pNg", "pdf", "PDF", "svg", "SVG"}) do
    local path = directory .. "/format." .. ext
    os.remove(path)
    app.export({outputFile = path, range = "1", pngWidth = 40})
    local file = assert(io.open(path, "rb"), ext .. " export missing")
    local header = file:read(256)
    file:close()
    if ext:lower() == "png" then
      assert(header:sub(1, 8) == "\137PNG\r\n\26\n", ext)
    elseif ext:lower() == "pdf" then
      assert(header:sub(1, 5) == "%PDF-", ext)
    else
      assert(header:find("<svg", 1, true), ext)
    end
  end
  for _, ext in ipairs({".jpg", ".bogus", ""}) do
    local path = directory .. "/unsupported" .. ext
    os.remove(path)
    local ok, err = pcall(app.export, {outputFile = path, range = "1"})
    assert(not ok, "unsupported extension accepted: " .. ext)
    assert(err == "Unsupported export extension '" .. ext .. "'. Supported extensions: .pdf, .svg, .png", err)
    assert(err:find("Unsupported export extension", 1, true), err)
    for _, expected in ipairs({ext, ".pdf", ".svg", ".png"}) do
      assert(err:find(expected, 1, true), err)
    end
    assert(io.open(path, "rb") == nil, "unsupported export created a file")
  end
end
)lua");

        Plugin plugin(&control, "LuaExportTest", directory);
        ASSERT_TRUE(plugin.isValid());
        plugin.loadScript();
        ASSERT_TRUE(plugin.isValid());
        ASSERT_TRUE(plugin.callFunction("exportPngs", directory.string().c_str()));

        constexpr struct {
            const char* filename;
            int width;
            int height;
        } cases[] = {{"height.png", 64, 128},
                     {"legacy.png", 32, 64},
                     {"both.png", 64, 128},
                     {"width.png", 40, 80},
                     {"dpi.png", 72, 144}};
        for (const auto& [filename, expectedWidth, expectedHeight]: cases) {
            SCOPED_TRACE(filename);
            int width = 0;
            int height = 0;
            ASSERT_NE(gdk_pixbuf_get_file_info(Util::toGFilename(directory / filename).c_str(), &width, &height),
                      nullptr);
            EXPECT_EQ(width, expectedWidth);
            EXPECT_EQ(height, expectedHeight);
        }
    }
};

TEST_F(LuaExportTest, ResolvesPngSizeOptionsAndExportExtensions) {}

#endif
