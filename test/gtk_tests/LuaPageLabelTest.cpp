#include "config-features.h"

#ifdef ENABLE_PLUGINS

#include <fstream>

#include <cairo-pdf.h>
#include <gtest/gtest.h>

#include "control/Control.h"
#include "dialog/GtkTest.h"
#include "gui/GladeSearchpath.h"
#include "model/Document.h"
#include "plugin/Plugin.h"
#include "util/PathUtil.h"
#include "util/StringUtils.h"
#include "util/raii/CairoWrappers.h"

#include "config-test.h"

class LuaPageLabelTest: public GtkTest {
    void runTest(GtkApplication* app) override {
        GladeSearchpath paths;
        paths.addSearchDirectory(GET_UI_FOLDER);
        paths.addSearchDirectory(XOJ_TEST_RESOURCE_DIR);
        Control control(G_APPLICATION(app), &paths);
        const auto directory = Util::getCacheSubfolder("lua-page-label-test");
        const auto background = directory / "background.pdf";
        {
            xoj::util::CairoSurfaceSPtr surface(
                    cairo_pdf_surface_create(char_cast(background.u8string().c_str()), 100, 100), xoj::util::adopt);
            xoj::util::CairoSPtr cr(cairo_create(surface.get()), xoj::util::adopt);
            cairo_show_page(cr.get());
            cairo_surface_finish(surface.get());
            ASSERT_EQ(cairo_surface_status(surface.get()), CAIRO_STATUS_SUCCESS);
        }
        ASSERT_TRUE(std::ofstream(directory / "plugin.ini") << R"ini([default]
enabled=true
[plugin]
mainfile=main.lua
)ini");
        ASSERT_TRUE(std::ofstream(directory / "main.lua") << R"lua(
function checkEmpty()
  local value, message = app.getPageLabel(1)
  assert(value == nil and message == "page nr 1 is out of range", tostring(message))
end
function checkLabels()
  assert(app.getPageLabel(1) == "1")
  local failures = {}
  for _, page in ipairs({0, 2, -1, math.mininteger}) do
    local ok, value, message = pcall(app.getPageLabel, page)
    if not ok or value ~= nil or message ~= "page nr " .. page .. " is out of range" then
      failures[#failures + 1] = "page=" .. page .. " ok=" .. tostring(ok) ..
        " value=" .. tostring(value) .. " message=" .. tostring(message)
    end
  end
  assert(#failures == 0, table.concat(failures, "\n"))
end
)lua");
        Plugin plugin(&control, "LuaPageLabelTest", directory);
        ASSERT_TRUE(plugin.isValid());
        plugin.loadScript();
        ASSERT_TRUE(plugin.isValid());
        EXPECT_TRUE(plugin.callFunction("checkEmpty"));
        ASSERT_TRUE(control.getDocument()->readPdf(background, true, false));
        EXPECT_TRUE(plugin.callFunction("checkLabels"));
    }
};

TEST_F(LuaPageLabelTest, ReturnsLabelsAndRangeErrors) {}

#endif
