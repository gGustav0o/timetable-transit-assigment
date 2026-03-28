#include "timetable/infra/clipboard.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
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
        class ScopedClipboard final {
        public:
            ScopedClipboard() noexcept = default;

            static mathfp::Expected<ScopedClipboard> make() {
                if (!::OpenClipboard(nullptr)) {
                    return mathfp::unexpected(
                        mathfp::internal_error("failed to open clipboard")
                            .ctx("win32_error", static_cast<std::int64_t>(::GetLastError()))
                    );
                }
                auto clipboard = ScopedClipboard{};
                clipboard.active_ = true;
                return clipboard;
            }

            ScopedClipboard(const ScopedClipboard&) = delete;
            ScopedClipboard& operator=(const ScopedClipboard&) = delete;

            ScopedClipboard(ScopedClipboard&& other) noexcept
                : active_(std::exchange(other.active_, false)) {
            }

            ScopedClipboard& operator=(ScopedClipboard&& other) noexcept {
                if (this != &other) {
                    if (active_) {
                        ::CloseClipboard();
                    }
                    active_ = std::exchange(other.active_, false);
                }
                return *this;
            }

            ~ScopedClipboard() {
                if (active_) {
                    ::CloseClipboard();
                }
            }

        private:
            bool active_ = false;
        };

        class ScopedGlobalMemory final {
        public:
            ScopedGlobalMemory() noexcept = default;

            explicit ScopedGlobalMemory(HGLOBAL handle) noexcept
                : handle_(handle) {
            }

            ScopedGlobalMemory(const ScopedGlobalMemory&) = delete;
            ScopedGlobalMemory& operator=(const ScopedGlobalMemory&) = delete;

            ScopedGlobalMemory(ScopedGlobalMemory&& other) noexcept
                : handle_(std::exchange(other.handle_, nullptr)) {
            }

            ScopedGlobalMemory& operator=(ScopedGlobalMemory&& other) noexcept {
                if (this != &other) {
                    if (handle_) {
                        ::GlobalFree(handle_);
                    }
                    handle_ = std::exchange(other.handle_, nullptr);
                }
                return *this;
            }

            ~ScopedGlobalMemory() {
                if (handle_) {
                    ::GlobalFree(handle_);
                }
            }

            HGLOBAL get() const noexcept {
                return handle_;
            }

            HGLOBAL release() noexcept {
                return std::exchange(handle_, nullptr);
            }

        private:
            HGLOBAL handle_ = nullptr;
        };

        mathfp::Expected<std::wstring> utf8_to_utf16(std::string_view text) {
            if (text.empty()) {
                return std::wstring{};
            }

            const auto source_size = static_cast<int>(text.size());
            const auto wide_size = ::MultiByteToWideChar(
                CP_UTF8
                , MB_ERR_INVALID_CHARS
                , text.data()
                , source_size
                , nullptr
                , 0
            );
            if (wide_size <= 0) {
                return mathfp::unexpected(
                    mathfp::invalid_arg("failed to decode clipboard text as UTF-8")
                        .ctx("win32_error", static_cast<std::int64_t>(::GetLastError()))
                        .ctx("source_size", static_cast<std::int64_t>(source_size))
                );
            }

            std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
            const auto converted = ::MultiByteToWideChar(
                CP_UTF8
                , MB_ERR_INVALID_CHARS
                , text.data()
                , source_size
                , wide.data()
                , wide_size
            );
            if (converted != wide_size) {
                return mathfp::unexpected(
                    mathfp::internal_error("clipboard UTF-16 conversion produced inconsistent size")
                        .ctx("expected_size", static_cast<std::int64_t>(wide_size))
                        .ctx("actual_size", static_cast<std::int64_t>(converted))
                );
            }

            return wide;
        }

        mathfp::Expected<ScopedGlobalMemory> make_unicode_clipboard_payload(
            std::wstring_view text
        ) {
            const auto code_units = text.size() + 1;
            const auto bytes = code_units * sizeof(wchar_t);
            auto memory = ScopedGlobalMemory{
                ::GlobalAlloc(GMEM_MOVEABLE, bytes)
            };
            if (!memory.get()) {
                return mathfp::unexpected(
                    mathfp::internal_error("failed to allocate clipboard payload")
                        .ctx("win32_error", static_cast<std::int64_t>(::GetLastError()))
                        .ctx("bytes", static_cast<std::int64_t>(bytes))
                );
            }

            auto* raw = static_cast<wchar_t*>(::GlobalLock(memory.get()));
            if (!raw) {
                return mathfp::unexpected(
                    mathfp::internal_error("failed to lock clipboard payload")
                        .ctx("win32_error", static_cast<std::int64_t>(::GetLastError()))
                );
            }

            if (!text.empty()) {
                std::memcpy(raw, text.data(), text.size() * sizeof(wchar_t));
            }
            raw[text.size()] = L'\0';
            (void)::GlobalUnlock(memory.get());

            return memory;
        }
#endif

    }  // namespace

    mathfp::Expected<mathfp::Unit> copy_text_to_clipboard(std::string_view text) {
#if defined(_WIN32)
        MATHFP_TRY_LET(ScopedClipboard, clipboard, ScopedClipboard::make());
        if (!::EmptyClipboard()) {
            return mathfp::unexpected(
                mathfp::internal_error("failed to clear clipboard")
                    .ctx("win32_error", static_cast<std::int64_t>(::GetLastError()))
            );
        }

        MATHFP_TRY_LET(std::wstring, wide_text, utf8_to_utf16(text));
        MATHFP_TRY_LET(
            ScopedGlobalMemory
            , payload
            , make_unicode_clipboard_payload(wide_text)
        );
        if (!::SetClipboardData(CF_UNICODETEXT, payload.get())) {
            return mathfp::unexpected(
                mathfp::internal_error("failed to publish clipboard payload")
                    .ctx("win32_error", static_cast<std::int64_t>(::GetLastError()))
            );
        }

        (void)payload.release();
        return mathfp::ok();
#else
        (void)text;
        return mathfp::unexpected(
            mathfp::not_implemented("clipboard copy is only implemented on Windows")
        );
#endif
    }

}  // namespace timetable::infra
