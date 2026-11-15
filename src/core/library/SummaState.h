/*
 * Ambitus
 *
 * Reads the interface state of the Summa desk (desk.json): the language the person chose for the interface and the
 * accent color, so the first-time setup starts from them. Only the choices Summa saves are read; missing or unknown
 * values stay empty, and each caller keeps its own default.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <string>

#include "filesystem.h"  // for path

namespace ambitus::library {

struct SummaState {
    /// "pt-BR" or "en"; empty when Summa did not choose an interface language (or chose one Ambitus does not know)
    std::string language;
    /// "graphite" or "violet"; empty when Summa did not choose an accent
    std::string accent;
};

/**
 * Parses the content of Summa's desk.json. Invalid JSON, a root that is not an object or values of another type give
 * empty fields; an unknown language or accent is left empty too.
 */
SummaState parseSummaState(const std::string& json);

/**
 * Where Summa keeps desk.json: its runtime folder ($LEARNING_DESK_RUNTIME, else "runtimePath" of its config.json, else
 * "runtime" next to it), the same folder of the bridge. `configJson` is the content of the config (may be empty).
 */
fs::path summaStatePath(const fs::path& configPath, const std::string& configJson, const char* envRuntime);

/// Reads desk.json of the Summa set up on this computer. A missing or unreadable file gives empty fields.
SummaState loadSummaState();

}  // namespace ambitus::library
