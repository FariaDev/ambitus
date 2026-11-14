/*
 * Ambitus
 *
 * Reads the handwriting of the notebooks through Summa, for the search. Summa
 * keeps a local bridge (`ambitus-bridge.json` in its runtime folder: port and
 * token) that takes the PNG of a page and answers the text, read by the vision
 * model the person uses there. Ambitus decides what to read: the pages whose
 * strokes changed (SearchIndex::unreadInk), one at a time, in the background.
 * Nothing leaves the machine unless the reading is switched on in Summa.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include <gio/gio.h>  // for GCancellable

#include "NotebookSearch.h"
#include "filesystem.h"  // for path

namespace ambitus::library {

struct SummaBridge {
    int port = 0;
    std::string token;
};

/// Where Summa writes the bridge file: its runtime folder ($LEARNING_DESK_RUNTIME, else "runtimePath" of its
/// config.json, else "runtime" next to it). configJson is the content of the config (may be empty).
fs::path summaBridgePath(const fs::path& configPath, const std::string& configJson, const char* envRuntime);
/// The bridge from the content of the file; empty when it is not a bridge of a version Ambitus knows
std::optional<SummaBridge> parseSummaBridge(const std::string& json);
/// The bridge of the Summa running now, from its runtime folder; empty when Summa is closed
std::optional<SummaBridge> findSummaBridge();

/// Why a page was not read
enum class InkStop {
    None,      ///< read
    NoSumma,   ///< Summa is closed (or too old to have the bridge)
    Disabled,  ///< the reading is off in Summa
    NoModel,   ///< no model chosen in Summa
    NoVision,  ///< the model does not see images
    Free,      ///< the free model is not used for notes
    Claude,    ///< the Claude engine cannot read yet
    Quota,     ///< the quota of the model is over for now
    Busy,      ///< another reading is going on
    Failed,    ///< the reading failed or took too long
};

struct InkReply {
    InkStop stop = InkStop::Failed;
    std::string text;  ///< when stop is None
};
/// The answer of the bridge (HTTP status and JSON body)
InkReply parseInkReply(int status, const std::string& body);

/// Sends the PNG to the bridge and waits for the reading (blocking: call it from a worker thread)
InkReply postInk(const SummaBridge& bridge, const std::string& png, std::chrono::seconds timeout);

/// The PNG of a page as Summa's tutor sees it, without the ruling: this program run with --create-img (on a board,
/// only the written part). Empty when the export failed.
std::optional<std::string> renderPagePng(const fs::path& notebook, size_t page);
/// The same with the resolution and the ruling of the page chosen (the thumbnail of the library keeps the ruling)
std::optional<std::string> renderPagePng(const fs::path& notebook, size_t page, int dpi, bool ruling);

/// The resolution Summa's tutor reads the pages at (notebook-tool.mjs); the page asked by Summa's "Check" uses it too
constexpr int PAGE_DPI = 120;

/// What Summa's "Check" asks Ambitus for: the page on screen. Ambitus has no server, so it keeps a long-poll
/// (GET /pedido-pagina, held by Summa for ~25 s) and Summa answers it when the person asks.
enum class PollKind {
    Request,  ///< Summa wants the page: PollReply::id says which request
    Idle,     ///< nothing asked in the time; ask again at once
    NoSumma,  ///< nobody listens (Summa closed)
    Failed,   ///< anything else (old Summa without the route, token changed, replaced by another poll)
};
struct PollReply {
    PollKind kind = PollKind::Failed;
    std::string id;  ///< when kind is Request
};
/// The answer of the bridge to the long-poll (HTTP status and JSON body)
PollReply parsePollReply(int status, const std::string& body);
/// Holds the long-poll until Summa asks, or answers Idle (blocking: call it from a worker thread; cancel stops it)
PollReply pollPageRequest(const SummaBridge& bridge, GCancellable* cancel);
/// The JSON of POST /pagina-atual with the page: the notebook path is empty when it was never saved, the page is
/// 0-based here and 1-based on the wire, the PNG goes in base64
std::string pageAnswerJson(const std::string& requestId, const std::string& notebook, size_t page,
                           const std::string& name, const std::string& png);
/// The JSON of POST /pagina-atual when there is no document to show
std::string noPageAnswerJson(const std::string& requestId);
/// Sends the answer (blocking: worker thread); true when Summa took it
bool postPageAnswer(const SummaBridge& bridge, const std::string& json, GCancellable* cancel);
/// How long to wait before the long-poll tries again after `failures` in a row (2 s, 5 s, 15 s, then 30 s)
std::chrono::seconds pollBackoff(int failures);

/// What happened to a page sent to Summa's side chat
enum class PageSend {
    Sent,      ///< it is in the draft of the side chat
    NoSumma,   ///< Summa is closed
    OldSumma,  ///< this Summa does not take pages yet
    NotFound,  ///< Summa did not find the notebook on disk
    NoWindow,  ///< Summa has no window to show it in
    Failed,
};
/// The answer of the bridge to /mandar-pagina
PageSend parsePageReply(int status, const std::string& body);
/// Sends a page (0-based) of a saved notebook to the side chat of Summa, where the tutor reads it again by the path
/// (blocking, but Summa answers at once)
PageSend sendPageToSumma(const SummaBridge& bridge, const fs::path& notebook, size_t page, const std::string& name);

/// What a run of readInk did
struct InkRun {
    size_t read = 0;               ///< pages read and kept
    InkStop stop = InkStop::None;  ///< why it stopped before the end (None: nothing left, or the time was up)
};

/// Reads the unread pages of the notebooks, in order, until none is left, the deadline passes, a reason to stop
/// comes (see InkStop) or cancel is set. Keeps every page read in the index and saves it after each one.
InkRun readInk(SearchIndex& index, const std::vector<fs::path>& order, std::chrono::steady_clock::time_point deadline,
               const std::atomic<bool>& cancel);

}  // namespace ambitus::library
