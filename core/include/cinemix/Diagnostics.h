// Diagnostics — leveled, optional logging. Never used on real-time paths;
// sinks must be fast and must not block the bridge worker for long.
#ifndef CINEMIX_DIAGNOSTICS_H
#define CINEMIX_DIAGNOSTICS_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

namespace cinemix {

class Diagnostics {
public:
    enum class Level : uint8_t {
        Error = 0,   // always on
        Warning = 1, // always on unless silenced
        Info = 2,    // lifecycle: activation, port state
        Verbose = 3, // unknown messages, ignored messages, coalescing stats
        MidiIn = 4,  // every inbound message (raw bytes)
        MidiOut = 5, // every outbound message (raw bytes)
    };

    typedef std::function<void(Level, const std::string&)> Sink;

    Diagnostics() : level_(Level::Info) {}

    void setSink(const Sink& sink) {
        std::lock_guard<std::mutex> lock(mu_);
        sink_ = sink;
    }
    void setLevel(Level l) { level_.store(l, std::memory_order_relaxed); }
    Level level() const { return level_.load(std::memory_order_relaxed); }

    static const char* levelName(Level l) {
        switch (l) {
        case Level::Error: return "error";
        case Level::Warning: return "warning";
        case Level::Info: return "info";
        case Level::Verbose: return "verbose";
        case Level::MidiIn: return "midi-in";
        case Level::MidiOut: return "midi-out";
        }
        return "?";
    }

    // The real-time safety rule: log() may lock briefly and may call into the
    // sink; only call from non-audio threads.
    void log(Level l, const std::string& message) {
        if (static_cast<uint8_t>(l) > static_cast<uint8_t>(level())) return;
        std::lock_guard<std::mutex> lock(mu_);
        if (sink_) sink_(l, message);
    }

    void error(const std::string& m) { log(Level::Error, m); }
    void warning(const std::string& m) { log(Level::Warning, m); }
    void info(const std::string& m) { log(Level::Info, m); }
    void verbose(const std::string& m) { log(Level::Verbose, m); }
    void midiIn(const std::string& m) { log(Level::MidiIn, m); }
    void midiOut(const std::string& m) { log(Level::MidiOut, m); }

private:
    std::atomic<Level> level_;
    std::mutex mu_;
    Sink sink_;
};

} // namespace cinemix

#endif // CINEMIX_DIAGNOSTICS_H
