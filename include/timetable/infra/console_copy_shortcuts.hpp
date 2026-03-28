#pragma once

#include <mathfp/core/expected.hpp>

namespace timetable::infra {

    /**
     * @brief Keep terminal-managed copy shortcuts from aborting the process.
     *
     * On Windows console hosts, Ctrl+C may be delivered as a console control
     * event even when the user intends to copy text. This guard suppresses that
     * process-level interruption while the TUI is active, so terminal copy
     * shortcuts such as Ctrl+C and Ctrl+Shift+C remain usable.
     */
    class ScopedConsoleCopyShortcuts final {
    public:
        static mathfp::Expected<ScopedConsoleCopyShortcuts> make();

        ScopedConsoleCopyShortcuts(const ScopedConsoleCopyShortcuts&) = delete;
        ScopedConsoleCopyShortcuts& operator=(const ScopedConsoleCopyShortcuts&) = delete;

        ScopedConsoleCopyShortcuts(ScopedConsoleCopyShortcuts&& other) noexcept;
        ScopedConsoleCopyShortcuts& operator=(ScopedConsoleCopyShortcuts&& other) noexcept;

        ~ScopedConsoleCopyShortcuts();

    private:
        explicit ScopedConsoleCopyShortcuts(bool active) noexcept;

        bool active_ = false;
    };

}  // namespace timetable::infra
