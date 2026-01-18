#include "timetable/infra/ui_log_sink.hpp"

#include <string>

#include <spdlog/details/log_msg.h>
#include <spdlog/fmt/fmt.h>

namespace timetable::infra {

	UiLogSink::UiLogSink(std::shared_ptr<LogBuffer> buffer)
		: buffer_(std::move(buffer)) {
	}

	void UiLogSink::sink_it_(const spdlog::details::log_msg& msg) {
		if (!buffer_) {
			return;
		}
		spdlog::memory_buf_t formatted;
		base_sink<std::mutex>::formatter_->format(msg, formatted);
		buffer_->push(fmt::to_string(formatted));
	}

}  // namespace timetable::infra
