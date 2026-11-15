/*
 * Xournal++
 *
 * Part of the customizable toolbars
 *
 * @author Xournal++ Team
 * https://github.com/xournalpp/xournalpp
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>

#include <gtk/gtk.h>  // for GtkWidget

#include "gui/toolbarMenubar/model/ColorPalette.h"
#include "util/Color.h"       // for Color
#include "util/NamedColor.h"  // for NamedColor
#include "util/Recolor.h"     // for Recolor

#include "AbstractToolItem.h"  // for AbstractToolItem

class ActionDatabase;

class ColorToolItem: public AbstractToolItem {
public:
    ColorToolItem(NamedColor namedColor, const std::optional<Recolor>& recolor, Color outline = Colors::black);
    ColorToolItem(ColorToolItem const&) = delete;
    ColorToolItem(ColorToolItem&&) noexcept = delete;
    auto operator=(ColorToolItem const&) -> ColorToolItem& = delete;
    auto operator=(ColorToolItem&&) noexcept -> ColorToolItem& = delete;
    ~ColorToolItem() override;


public:
    std::string getToolDisplayName() const override;
    GtkWidget* getNewToolIcon() const override;

    Color getColor() const;

    /**
     * @brief Update Color based on (new) palette
     *
     * @param palette
     */
    void updateColor(const Palette& palette);

    /**
     * @brief Update secondary Color based on (new) recoloring settings
     *
     * @param recolor
     */
    void updateSecondaryColor(const std::optional<Recolor>& recolor);

    /// Ambitus: the 1px outline of the swatch follows the theme, so a black swatch shows over the dark toolbar
    void setOutline(Color color);

    xoj::util::WidgetSPtr createItem(bool horizontal) override;

private:
    NamedColor namedColor;
    xoj::util::GVariantSPtr target;       ///< Contains the color in ARGB as a uint32_t
    std::optional<Color> secondaryColor;  //< color for small disk when recoloring is active
    Color outline;                        ///< Ambitus: the border of the theme, not the black one of Xournal++
    GtkWidget* icon = nullptr;            ///< The swatch on the toolbar, while it exists
};
