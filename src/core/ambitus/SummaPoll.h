/*
 * Ambitus
 *
 * Answers the "Check" of Summa: when the person asks for it there, Summa wants the page on screen. Ambitus has no
 * server, so while the bridge is on (setting ambitusSummaBridge) a worker thread keeps a long-poll on Summa's bridge
 * (library/InkReader.h, GET /pedido-pagina). When Summa asks, the page of the active tab is rendered on the UI
 * thread from the document in memory (so unsaved ink is in it) and sent back (POST /pagina-atual). Nothing shows in
 * the interface, and when Summa is closed the poller retries quietly with a growing wait.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <thread>

class Control;
class Document;

namespace ambitus {

class SummaPoller {
public:
    /// Starts the worker thread. Create and destroy it on the UI thread.
    explicit SummaPoller(Control* ctrl);
    /// Stops the long-poll (cancelling the socket) and joins the thread
    ~SummaPoller();

    SummaPoller(const SummaPoller&) = delete;
    SummaPoller& operator=(const SummaPoller&) = delete;

    struct Shared;  ///< what the worker thread shares with the UI thread (SummaPoll.cpp)

private:
    std::shared_ptr<Shared> shared;
    std::thread worker;
};

/// The PNG of a page (0-based), drawn from the document in memory (so unsaved ink is in it) at the resolution
/// Summa's tutor reads pages at, opaque, with the paper colour and the ruling. UI thread; nothing when it could not
/// be drawn.
std::optional<std::string> renderCurrentPage(Document* doc, size_t page);

}  // namespace ambitus
