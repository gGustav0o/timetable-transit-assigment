#pragma once

#include <memory>

#include <spdlog/sinks/base_sink.h>

#include "timetable/infra/log_buffer.hpp"

namespace timetable::infra {

	class UiLogSink final : public spdlog::sinks::base_sink<std::mutex> {
	public:
		explicit UiLogSink(std::shared_ptr<LogBuffer> buffer);

	protected:
		void sink_it_(const spdlog::details::log_msg& msg) override;
		void flush_() override {}

	private:
		std::shared_ptr<LogBuffer> buffer_;
	};

}  // namespace timetable::infra
