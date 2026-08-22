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
    enum class Level : std::uint8_t {
        Error = 0,   // always on
        Warning = 1, // always on unless silenced
        Info = 2,    // lifecycle: activation, port state
        Verbose = 3, // unknown messages, ignored messages, coalescing stats
        MidiIn = 4,  // every inbound message (raw bytes)
        MidiOut = 5, // every outbound message (raw bytes)
    };

    using Sink = std::function<void(Level, const std::string&)>;

    Diagnostics() : level_(Level::Info) {}

    void setSink(const Sink& sink) {
        std::lock_guard<std::mutex> lock(mu_);
        sink_ = sink;
    }
    void setLevel(Level level) noexcept { level_.store(level, std::memory_order_relaxed); }
    Level level() const noexcept { return level_.load(std::memory_order_relaxed); }

    static constexpr const char* levelName(Level level) noexcept {
        switch (level) {
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
    void log(Level severity, const std::string& message) {
        if (static_cast<std::uint8_t>(severity) > static_cast<std::uint8_t>(level())) return;
        std::lock_guard<std::mutex> lock(mu_);
        if (sink_) sink_(severity, message);
    }

    void error(const std::string& message) { log(Level::Error, message); }
    void warning(const std::string& message) { log(Level::Warning, message); }
    void info(const std::string& message) { log(Level::Info, message); }
    void verbose(const std::string& message) { log(Level::Verbose, message); }
    void midiIn(const std::string& message) { log(Level::MidiIn, message); }
    void midiOut(const std::string& message) { log(Level::MidiOut, message); }

private:
    std::atomic<Level> level_;
    std::mutex mu_;
    Sink sink_;
};

} // namespace cinemix

#endif // CINEMIX_DIAGNOSTICS_H
