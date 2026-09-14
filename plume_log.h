// Lightweight backend error reporting; independent of the host application's logger.
#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>

namespace plume {
    enum class LogSeverity { Error, Warning };
#ifdef None
#pragma push_macro("None")
#undef None
#define PLUME_RESTORE_NONE
#endif
    enum class LogErrorDomain { None, HResult, VkResult, Win32, SDL };
#ifdef PLUME_RESTORE_NONE
#pragma pop_macro("None")
#undef PLUME_RESTORE_NONE
#endif

    struct LogRecord {
        LogSeverity severity;
        const char *backend;
        const char *api;
        LogErrorDomain domain;
        uint32_t rawCode; // Original 32 bits, including negative HRESULT/VkResult values.
        const char *detail;
        uint32_t occurrence; // Per failure call site; gaps indicate suppressed repeats.
    };

    // Delivery is synchronous. Record strings are borrowed for this call only.
    // Register before backend creation; callback code must outlive backend threads.
    // A callback owns delivery completely, including its own last-resort sink.
    using LogCallback = void (*)(const LogRecord &);

    namespace log_detail {
        inline std::atomic<LogCallback> callback{nullptr};
        inline thread_local bool delivering = false;
    }

    inline void SetLogCallback(LogCallback callback) noexcept {
        log_detail::callback.store(callback, std::memory_order_release);
    }

    inline const char *LogErrorDomainName(LogErrorDomain domain) noexcept {
        switch (domain) {
        case LogErrorDomain::HResult: return "HRESULT";
        case LogErrorDomain::VkResult: return "VkResult";
        case LogErrorDomain::Win32: return "Win32";
        case LogErrorDomain::SDL: return "SDL";
        default: return "none";
        }
    }

    // The first eight failures and subsequent powers of two retain evidence while
    // bounding repeated per-frame failures to logarithmic output. Only failures
    // touch this counter; successful graphics operations do no logging work.
    struct LogSite {
        std::atomic<uint32_t> count{0};

        uint32_t Next() noexcept {
            uint32_t current = count.load(std::memory_order_relaxed);
            while (current != (std::numeric_limits<uint32_t>::max)()) {
                if (count.compare_exchange_weak(current, current + 1, std::memory_order_relaxed)) {
                    const uint32_t next = current + 1;
                    return (next <= 8 || (next & (next - 1)) == 0) ? next : 0;
                }
            }
            return 0;
        }
    };

    inline void LogMessage(LogSite &site, LogSeverity severity, const char *backend,
        const char *api, LogErrorDomain domain, uint32_t rawCode, const char *format, ...) noexcept {
        // Do not recursively invoke either the host logger or stderr, including
        // when a callback changes the registration before reporting another error.
        if (log_detail::delivering) return;
        const uint32_t occurrence = site.Next();
        if (occurrence == 0) return;

        char detail[1024] = {};
        va_list args;
        va_start(args, format);
        const int length = std::vsnprintf(detail, sizeof(detail), format ? format : "", args);
        va_end(args);
        if (length < 0) {
            std::snprintf(detail, sizeof(detail), "%s", "diagnostic formatting failed");
        }
        else if (static_cast<size_t>(length) >= sizeof(detail)) {
            constexpr char suffix[] = " [truncated]";
            std::memcpy(detail + sizeof(detail) - sizeof(suffix), suffix, sizeof(suffix));
        }
        // Keep each event a single record even when a driver provides newlines.
        for (char *c = detail; *c; ++c) {
            if (*c == '\r' || *c == '\n') *c = ' ';
        }

        const LogRecord record{severity, backend ? backend : "unknown", api ? api : "unknown",
            domain, rawCode, detail, occurrence};
        const LogCallback callback = log_detail::callback.load(std::memory_order_acquire);
        log_detail::delivering = true;
        if (callback) {
            try { callback(record); }
            catch (...) { /* A diagnostic callback must not change API failure behavior. */ }
        }
        else {
            std::fprintf(stderr, "plume: severity=%s backend=%s api=%s domain=%s code=0x%08X occurrence=%u %s\n",
                severity == LogSeverity::Error ? "error" : "warning", record.backend, record.api,
                LogErrorDomainName(domain), static_cast<unsigned>(rawCode), static_cast<unsigned>(occurrence), detail);
        }
        log_detail::delivering = false;
    }
}

#define PLUME_LOG_ERROR(backend, api, domain, code, ...) do { \
    static ::plume::LogSite plumeErrorSite; \
    ::plume::LogMessage(plumeErrorSite, ::plume::LogSeverity::Error, backend, api, domain, \
        static_cast<uint32_t>(code), __VA_ARGS__); \
} while (0)

#define PLUME_LOG_WARNING(backend, api, domain, code, ...) do { \
    static ::plume::LogSite plumeWarningSite; \
    ::plume::LogMessage(plumeWarningSite, ::plume::LogSeverity::Warning, backend, api, domain, \
        static_cast<uint32_t>(code), __VA_ARGS__); \
} while (0)
