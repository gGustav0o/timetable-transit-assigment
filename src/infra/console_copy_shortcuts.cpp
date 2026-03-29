#include "timetable/infra/console_copy_shortcuts.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>

#include <mathfp/core/error.hpp>
#include <mathfp/core/try.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace timetable::infra {
    namespace {

#if defined(_WIN32)
        BOOL WINAPI suppress_copy_shortcut_interrupt(DWORD signal) noexcept {
            return signal == CTRL_C_EVENT ? TRUE : FALSE;
        }

        std::mutex g_console_copy_shortcuts_mutex;
        std::size_t g_console_copy_shortcuts_refcount = 0;
        HANDLE g_console_input                        = INVALID_HANDLE_VALUE;
        DWORD g_original_console_mode                 = 0;

        mathfp::Expected<mathfp::Unit> acquire_console_copy_shortcuts() {
            std::lock_guard lock(g_console_copy_shortcuts_mutex);
            if (g_console_copy_shortcuts_refcount == 0) {
                const auto input = ::GetStdHandle(STD_INPUT_HANDLE);
                if (input == INVALID_HANDLE_VALUE || input == nullptr) {
                    return mathfp::unexpected(
                        mathfp::internal_error("failed to access console input handle")
                            .ctx("win32_error", static_cast<std::int64_t>(::GetLastError()))
                    );
                }

                DWORD mode = 0;
                if (!::GetConsoleMode(input, &mode)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("failed to read console input mode")
                            .ctx("win32_error", static_cast<std::int64_t>(::GetLastError()))
                    );
                }

                g_console_input         = input;
                g_original_console_mode = mode;

                const auto copy_friendly_mode = mode & ~ENABLE_PROCESSED_INPUT;
                if (!::SetConsoleMode(input, copy_friendly_mode)) {
                    g_console_input         = INVALID_HANDLE_VALUE;
                    g_original_console_mode = 0;
                    return mathfp::unexpected(
                        mathfp::internal_error("failed to install copy-friendly console input mode")
                            .ctx("win32_error"   , static_cast<std::int64_t>(::GetLastError()))
                            .ctx("original_mode" , static_cast<std::int64_t>(mode))
                            .ctx("requested_mode", static_cast<std::int64_t>(copy_friendly_mode))
                    );
                }

                if (!::SetConsoleCtrlHandler(suppress_copy_shortcut_interrupt, TRUE)) {
                    (void)::SetConsoleMode(input, mode);
                    g_console_input         = INVALID_HANDLE_VALUE;
                    g_original_console_mode = 0;
                    return mathfp::unexpected(
                        mathfp::internal_error("failed to install console copy shortcut guard")
                            .ctx("win32_error", static_cast<std::int64_t>(::GetLastError()))
                    );
                }
            }

            ++g_console_copy_shortcuts_refcount;
            return mathfp::ok();
        }

        void release_console_copy_shortcuts() noexcept {
            std::lock_guard lock(g_console_copy_shortcuts_mutex);
            if (g_console_copy_shortcuts_refcount == 0) {
                return;
            }

            --g_console_copy_shortcuts_refcount;
            if (g_console_copy_shortcuts_refcount == 0) {
                (void)::SetConsoleCtrlHandler(suppress_copy_shortcut_interrupt, FALSE);
                if (g_console_input != INVALID_HANDLE_VALUE) {
                    (void)::SetConsoleMode(g_console_input, g_original_console_mode);
                }
                g_console_input         = INVALID_HANDLE_VALUE;
                g_original_console_mode = 0;
            }
        }
#else
        mathfp::Expected<mathfp::Unit> acquire_console_copy_shortcuts() {
            return mathfp::ok();
        }

        void release_console_copy_shortcuts() noexcept {
        }
#endif

    }  // namespace

    mathfp::Expected<ScopedConsoleCopyShortcuts> ScopedConsoleCopyShortcuts::make() {
        MATHFP_TRY(acquire_console_copy_shortcuts());
        return ScopedConsoleCopyShortcuts{ true };
    }

    ScopedConsoleCopyShortcuts::ScopedConsoleCopyShortcuts(bool active) noexcept
        : active_(active) {
    }

    ScopedConsoleCopyShortcuts::ScopedConsoleCopyShortcuts(
        ScopedConsoleCopyShortcuts&& other
    ) noexcept
        : active_(std::exchange(other.active_, false)) {
    }

    ScopedConsoleCopyShortcuts& ScopedConsoleCopyShortcuts::operator=(
        ScopedConsoleCopyShortcuts&& other
    ) noexcept {
        if (this != &other) {
            if (active_) {
                release_console_copy_shortcuts();
            }
            active_ = std::exchange(other.active_, false);
        }
        return *this;
    }

    ScopedConsoleCopyShortcuts::~ScopedConsoleCopyShortcuts() {
        if (active_) {
            release_console_copy_shortcuts();
        }
    }

}  // namespace timetable::infra
