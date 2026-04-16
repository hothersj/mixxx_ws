// Then websocketpp
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "remotewsinterface.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QThread>
#include <atomic>
#include <thread>

// --------------------------------------------------------

namespace mixxx {

class RemoteWebSocketInterface::Impl {
  public:
    Impl(RemoteWebSocketInterface* qptr, quint16 port) : q(qptr), port(port), server(nullptr), running(false) {
        
    }
    ~Impl() {
        stop();
    }

    void setToken(const QString& token) {
        authToken = token;
    }

    bool start() {
        if (running.load()) {
            // already running
            return false;
        }
        try {
            // Create the server instance
            server = std::make_unique<server_t>();

            // Initialize ASIO
            server->init_asio();

            // Allow address reuse
            server->set_reuse_addr(true);

            // Set handlers
            server->set_open_handler([this](websocketpp::connection_hdl hdl) {
                try {
                    auto con = server->get_con_from_hdl(hdl);
                    // Retrieve remote endpoint via Asio:
                    auto& sock = con->get_raw_socket();
                    websocketpp::lib::asio::ip::tcp::endpoint remote_ep = sock.remote_endpoint();
                    std::string remote_addr = remote_ep.address().to_string();
                    unsigned short remote_port = remote_ep.port();
                    QString msg = QStringLiteral("WebSocket client connected: %1:%2")
                                          .arg(QString::fromStdString(remote_addr))
                                          .arg(port);
                    emitInfo(msg);
                } catch (...) {
                    // ignore
                }
            });
            server->set_close_handler([this](websocketpp::connection_hdl hdl) {
                try {
                    auto con = server->get_con_from_hdl(hdl);
                    // Retrieve remote endpoint via Asio:
                    auto& sock = con->get_raw_socket();
                    websocketpp::lib::asio::ip::tcp::endpoint remote_ep = sock.remote_endpoint();
                    std::string remote_addr = remote_ep.address().to_string();
                    unsigned short remote_port = remote_ep.port();
                    QString msg = QStringLiteral("WebSocket client disconnected: %1:%2")
                                          .arg(QString::fromStdString(remote_addr))
                                          .arg(port);
                    emitInfo(msg);
                } catch (...) {
                    // ignore
                }
            });
            server->set_message_handler([this](websocketpp::connection_hdl hdl,
                                                server_t::message_ptr msg) {
                // Only handle text frames
                if (msg->get_opcode() == websocketpp::frame::opcode::text) {
                    onMessage(hdl, msg);
                }
            });
            // Bind to port on all interfaces
            server->listen(websocketpp::lib::asio::ip::tcp::v4(), port);
            server->start_accept();

            running.store(true);
            // Launch the ASIO run loop in a background thread
            thread = std::thread([this] {
                try {
                    server->run();
                } catch (const std::exception& e) {
                    emitError(QStringLiteral("WebSocket server exception: %1").arg(e.what()));
                }
                running.store(false);
            });
            return true;
        } catch (const std::exception& e) {
            emitError(QStringLiteral("Failed to start WebSocket server on port %1: %2")
                            .arg(port)
                            .arg(e.what()));
            running.store(false);
            return false;
        }
    }

    void stop() {
        if (!running.load()) {
            return;
        }
        running.store(false);
        try {
            if (server) {
                // Stop accepting new connections
                server->stop_listening();
                // Close all existing connections
                // Note: websocketpp does not provide a direct "close all" in server API,
                // but stop() will terminate the run loop; existing connections will be aborted.
                server->stop();
            }
        } catch (...) {
            // ignore
        }
        // Join thread
        if (thread.joinable()) {
            thread.join();
        }
        server.reset();
    }

    // Helper to send a JSON response back to the same connection
    void sendJsonResponse(websocketpp::connection_hdl hdl, const QJsonObject& response) {
        if (!server)
            return;
        QJsonDocument doc(response);
        std::string s = doc.toJson(QJsonDocument::Compact).toStdString();
        try {
            server->send(hdl, s, websocketpp::frame::opcode::text);
        } catch (const std::exception& e) {
            // Could not send; log
            emitError(QStringLiteral("Failed to send response: %1").arg(e.what()));
        }
    }

    // Build a simple {"status":"ok","command":...} response
    QJsonObject makeOkResponse(const QString& command) {
        QJsonObject obj;
        obj["status"] = "ok";
        obj["command"] = command;
        return obj;
    }
    // Build {"status":"error","message":...}
    QJsonObject makeErrorResponse(const QString& message) {
        QJsonObject obj;
        obj["status"] = "error";
        obj["message"] = message;
        return obj;
    }

    // Build found phrases of track response
    QJsonObject makePhraseResponse(const QString& command, std::map<uint16_t, const char*> phrases) {
        QJsonObject obj = makeOkResponse(command);
        
        // Now place phrase stuff in this object
        QJsonObject jsonPhrases;
        for (const auto& kv : phrases) {
            uint16_t key = kv.first;
            const char* val = kv.second;
            QString strKey = QString::number(key);
            QString strVal = QString::fromUtf8(val);
            jsonPhrases.insert(strKey, QJsonValue(strVal));
        }
        
        obj["phrases"] = jsonPhrases;

        return obj;
    }

    //Build response for get loaded track
    QJsonObject makeGetLoadedTrackResponse(const QString& command, const QString& path, const QString& title) {
        QJsonObject obj = makeOkResponse(command);

        obj["path"] = path;
        obj["title"] = title;

        return obj;
    }

    //Build response for get deck play position
    QJsonObject makeGetPlayPosition(const QString& command, float playPosition) {
        QJsonObject obj = makeOkResponse(command);

        obj["play_position"] = playPosition;

        return obj;
    }

  private:
    // The websocketpp server type
    using server_t = websocketpp::server<websocketpp::config::asio>;

    RemoteWebSocketInterface* q; // pointer back to the QObject for emitting signals
    quint16 port;
    QString authToken;
    std::unique_ptr<server_t> server;
    std::thread thread;
    std::atomic<bool> running;
    websocketpp::connection_hdl lastConHdl;

    // Called when a text message arrives
    void onMessage(websocketpp::connection_hdl hdl, server_t::message_ptr msg) {
        lastConHdl = hdl;

        std::string payload = msg->get_payload();
        QString text = QString::fromUtf8(payload.data(), int(payload.size())).trimmed();
        emitInfo(QStringLiteral("Received message: %1").arg(text));

        // Parse JSON
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8(), &parseError);
        if (doc.isNull() || !doc.isObject()) {
            emitError(QStringLiteral("Invalid JSON: %1").arg(parseError.errorString()));
            // Optionally, send an error response back to client:
            sendJsonResponse(hdl, makeErrorResponse(QStringLiteral("Invalid JSON: %1").arg(parseError.errorString())));
            return;
        }
        QJsonObject obj = doc.object();

        // Optional auth token check
        if (!authToken.isEmpty()) {
            if (!obj.contains("token") || obj.value("token").toString() != authToken) {
                emitError(QStringLiteral("Authentication failed for message: %1").arg(text));
                sendJsonResponse(hdl, makeErrorResponse(QStringLiteral("Authentication failed")));
                return;
            }
        }

        // Dispatch commands
        if (!obj.contains("command") || !obj.value("command").isString()) {
            emitError(QStringLiteral("Missing or invalid 'command' field"));
            sendJsonResponse(hdl, makeErrorResponse(QStringLiteral("Missing or invalid 'command' field")));
            return;
        }
        QString cmd = obj.value("command").toString();
        if (cmd == QLatin1String("loadPath")) {
            if (!obj.contains("path") || !obj.contains("deck") || !obj.contains("play")) {
                emitError(QStringLiteral("loadPath: missing fields"));
                sendJsonResponse(hdl, makeErrorResponse(QStringLiteral("loadPath: missing 'path'/'deck'/'play'")));
                return;
            }
            QString path = obj.value("path").toString();
            int deck = obj.value("deck").toInt();
            bool play = obj.value("play").toBool();
            emitInfo(QStringLiteral("Command loadPath: %1, deck=%2, play=%3")
                            .arg(path)
                            .arg(deck)
                            .arg(play));
            // Emit Qt signal. Emitting from this background thread: Qt will queue to receiver thread.
            QMetaObject::invokeMethod(q, [this, path, deck, hdl, play]() {
                emit q->loadFileRequested(path, deck, hdl, play);
            });
            //sendJsonResponse(hdl, makeOkResponse("loadPath"));
        } else if (cmd == QLatin1String("setDeckCue")) {
            if (!obj.contains("deck") || !obj.contains("beatNum")) {
                emitError(QStringLiteral("setDeckCue: missing fields"));
                sendJsonResponse(hdl, makeErrorResponse(QStringLiteral("setDeckCue: missing 'deck'/'beatNum'")));
                return;
            }
            int deck = obj.value("deck").toInt();
            int beatNum = obj.value("beatNum").toInt();
            emitInfo(QStringLiteral("Command beatNum: %1, deck=%2")
                            .arg(beatNum)
                            .arg(deck));
            QMetaObject::invokeMethod(q, [this, deck, beatNum]() {
                emit q->setDeckCue(deck, beatNum);
            });
            sendJsonResponse(hdl, makeOkResponse("setDeckCue"));
        } else if (cmd == QLatin1String("getLoadedTrack")) {
            if (!obj.contains("deck")) {
                emitError(QStringLiteral("getLoadedTrack: missing fields"));
                sendJsonResponse(hdl, makeErrorResponse(QStringLiteral("getLoadedTrack: missing 'deck'")));
                return;
            }

            int deck = obj.value("deck").toInt();
            emitInfo(QStringLiteral("Command deck: %1").arg(deck));
            QMetaObject::invokeMethod(q, [this, deck, hdl]() {
                emit q->getLoadedTrack(deck, hdl);
            });
        }
        else {
            emitError(QStringLiteral("Unknown command: %1").arg(cmd));
            sendJsonResponse(hdl, makeErrorResponse(QStringLiteral("Unknown command: %1").arg(cmd)));
        }
    }

    // Emit Qt infoMessage signal from this thread:
    void emitInfo(const QString& msg) {
        if (!q)
            return;
        QMetaObject::invokeMethod(q, [this, msg]() {
            emit q->infoMessage(msg);
        });
    }
    // Emit Qt errorMessage signal from this thread:
    void emitError(const QString& msg) {
        if (!q)
            return;
        QMetaObject::invokeMethod(q, [this, msg]() {
            emit q->errorMessage(msg);
        });
    }
};

////////////////////////////////////////////////////////////////////////////////
// RemoteWebSocketInterface public methods

RemoteWebSocketInterface::RemoteWebSocketInterface(quint16 port, QObject* parent)
        : QObject(parent),
          m_impl(std::make_unique<Impl>(this, port)) {
}

RemoteWebSocketInterface::~RemoteWebSocketInterface() {
    // Impl::~Impl calls stop(), joining thread
    // unique_ptr will delete Impl here, so ensure Impl is complete type
}

bool RemoteWebSocketInterface::start() {
    return m_impl->start();
}

void RemoteWebSocketInterface::stop() {
    m_impl->stop();
}

void RemoteWebSocketInterface::setAuthToken(const QString& token) {
    m_impl->setToken(token);
}

void RemoteWebSocketInterface::returnLoadedFileSegments(std::map<uint16_t, const char*> phrases, websocketpp::connection_hdl hdl) {
    m_impl->sendJsonResponse(hdl, m_impl->makePhraseResponse("loadTrack", phrases));
}

void RemoteWebSocketInterface::returnLoadedTrack(const QString& location, const QString& title, const QString& artist, int trackId, websocketpp::connection_hdl hdl) {
    m_impl->sendJsonResponse(hdl, m_impl->makeGetLoadedTrackResponse("getLoadedTrack", location, title));
}

} // namespace mixxx

#include "moc_remotewsinterface.cpp"
