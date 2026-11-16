/*
 * Ambitus
 *
 * The subject field of the cards: the square of the colour, the name and a chevron; the list of the library opens in a
 * popover, the chosen one with a soft background and a check. "New notebook" and "Keep draft" share it.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

#include "library/LibraryView.h"

namespace ambitus {

class SubjectPicker {
public:
    SubjectPicker();
    ~SubjectPicker() = default;
    SubjectPicker(const SubjectPicker&) = delete;
    SubjectPicker& operator=(const SubjectPicker&) = delete;

    /// The field to put in the card; it opens the list when clicked
    GtkWidget* field() const { return fieldWidget; }

    /// Fills the list and starts at the first subject; the field shows it, but nothing is told to the card yet
    void setSubjects(std::vector<library::Subject> subjects);
    /// Chooses `index` (clamped to the list) and tells the card
    void choose(size_t index);

    bool empty() const { return subjects.empty(); }
    size_t selected() const { return subject; }
    const std::vector<library::Subject>& list() const { return subjects; }

    /// The subject changed (by the list, or by choose())
    void setOnChanged(std::function<void()> callback) { onChanged = std::move(callback); }
    /// An item of the list was picked: the card may put the focus back where it was
    void setOnPicked(std::function<void()> callback) { onPicked = std::move(callback); }
    void popdown();

private:
    void buildPopover();
    void showSubject(size_t index);

    GtkWidget* fieldWidget = nullptr;
    GtkWidget* dot = nullptr;
    GtkWidget* name = nullptr;
    GtkWidget* popover = nullptr;
    std::vector<GtkWidget*> itemButtons;
    std::vector<GtkWidget*> itemChecks;

    std::vector<library::Subject> subjects;
    size_t subject = 0;
    std::function<void()> onChanged;
    std::function<void()> onPicked;
};

}  // namespace ambitus
