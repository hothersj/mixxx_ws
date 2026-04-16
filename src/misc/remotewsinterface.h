#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <websocketpp/common/connection_hdl.hpp>

namespace mixxx {

/**
 * RemoteWebSocketInterface:
 *  - Listens on a WebSocket port (using websocketpp + standalone Asio in a background thread).
 *  - Accepts JSON text messages with commands like:
 *      {"command":"loadPath", "path":"/full/path/to/song.mp3", "deck":1, "play":false}
 *  - Emits Qt signals when commands arrive, e.g. loadFileRequested or loadByNameRequested.
 *  - Optionally checks an auth token field in the JSON if set via setAuthToken().
 *
 * Usage:
 *   auto ws = std::make_unique<RemoteWebSocketInterface>(port, parentQObject);
 *   connect(ws.get(), &RemoteWebSocketInterface::loadFileRequested, libraryPtr, &Library::slotLoadLocationToPlayer);
 *   ...
 *   ws->setAuthToken("mysecret"); // optional
 *   if (!ws->start()) { ... }
 *
 * On destruction, stop() is called and the background thread is joined.
 */
class RemoteWebSocketInterface : public QObject {
    Q_OBJECT
  public:
    /// ctor: port to listen on, parent QObject for Qt ownership
    explicit RemoteWebSocketInterface(quint16 port, QObject* parent = nullptr);

    ~RemoteWebSocketInterface() override;

    /// Start listening; returns true on success (i.e., bound to port).
    bool start();

    /// Stop listening and join the background thread. Safe to call multiple times.
    void stop();

    /// Optional: require incoming JSON to include {"token": "<authToken>"} that matches.
    void setAuthToken(const QString& token);

  signals:
    /// Emitted when a load-by-path command arrives.
    /// location: full file path; deck: deck identifier integer; play: whether to start playback.
    void loadFileRequested(const QString& location, int deck, websocketpp::connection_hdl hdl, bool play);

    // Emitted when a request for a deck's track arrives.
    void getLoadedTrack(int deck, websocketpp::connection_hdl hdl);

    /// General info messages (e.g. client connected, JSON parsed, etc.).
    void infoMessage(const QString& message);

    /// Error messages (e.g. JSON parse error, auth failure, etc.).
    void errorMessage(const QString& message);

    //Set main cue point (in beats)
    void setDeckCue(int deck, int beatNum);

  public slots:
    void returnLoadedFileSegments(std::map<uint16_t, const char*> phrases, websocketpp::connection_hdl hdl); // called when loadFileRequested() is finished, will send back a response over websocket with segments
    void returnLoadedTrack(const QString& location, const QString& title, const QString& artist, int trackId, websocketpp::connection_hdl hdl);

  private:
    // PIMPL to hide websocketpp/Asio details from this header:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mixxx