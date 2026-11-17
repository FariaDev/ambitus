/*
 * Xournal++
 *
 * Previews of the pages in the document
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>  // for size_t
#include <memory>   // for unique_ptr
#include <string>   // for string
#include <tuple>    // for tuple
#include <vector>   // for vector

#include <glib.h>     // for gulong
#include <gtk/gtk.h>  // for GtkWidget

#include "gui/IconNameHelper.h"                            // for IconNameHe...
#include "gui/sidebar/previews/base/SidebarPreviewBase.h"  // for SidebarPre...
#include "util/raii/GObjectSPtr.h"

class Control;
class GladeGui;


class SidebarPreviewPages: public SidebarPreviewBase {
public:
    SidebarPreviewPages(Control* control);
    ~SidebarPreviewPages() override;

public:
    void enableSidebar() override;

    /**
     * Ambitus: the Studio column. The miniatures get their numbers below them, the right click menu is the Studio's,
     * the toolbar goes and the "Page" tile comes after the last miniature. The miniatures are rebuilt (their size
     * changes).
     */
    void setStudioColumn(bool studio);
    bool isStudioColumn() const;

    /// Ambitus: the numbers go below the miniatures: in the Studio column, or with the setting of the numbers
    bool numbersBelow();

    /**
     * @overwrite
     */
    std::string getName() override;

    /**
     * @overwrite
     */
    std::string getIconName() override;

    /**
     * Update the preview images
     * @overwrite
     */
    void updatePreviews() override;

public:
    // DocumentListener interface (only the part which is not handled by SidebarPreviewBase)
    void pageSizeChanged(size_t page) override;
    void pageChanged(size_t page) override;
    void pageSelected(size_t page) override;
    void pageInserted(size_t page) override;
    void pageDeleted(size_t page) override;

private:
    /**
     * Unselect the last selected page, if any
     */
    void unselectPage();

    /**
     * Updates the indices of the pages
     */
    void updateIndices();

private:
    IconNameHelper iconNameHelper;

    /// Ambitus: the Studio column is shown (see setStudioColumn())
    bool studioColumn = false;
};
