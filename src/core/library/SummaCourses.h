/*
 * Ambitus
 *
 * Reads the courses (matérias) of the Summa desk, so the library shows the
 * notebooks of each course folder with the course colour.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "filesystem.h"  // for path

namespace ambitus::library {

struct Course {
    std::string id;
    std::string name;
    fs::path path;
    /// "#rrggbb", or empty when Summa has no colour for the course
    std::string color;
};

/**
 * Where Summa keeps its config.json: $LEARNING_DESK_RUNTIME when set (Summa honours it too), otherwise the Electron
 * userData folder "Mesa de Estudos" (Linux: $XDG_CONFIG_HOME, macOS: ~/Library/Application Support,
 * Windows: %APPDATA%).
 */
fs::path summaConfigPath();

/**
 * Parses the "courses" array of a Summa config.json. Entries without id, name or path are skipped; a colour that is
 * not "#rrggbb" is dropped. Invalid JSON gives an empty list.
 */
std::vector<Course> parseSummaCourses(const std::string& json);

/// Reads and parses summaConfigPath(). Missing or unreadable file gives an empty list.
std::vector<Course> loadSummaCourses();

}  // namespace ambitus::library
