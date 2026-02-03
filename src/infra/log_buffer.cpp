#include "timetable/infra/log_buffer.hpp"

namespace timetable::infra {

	LogBuffer::LogBuffer(std::size_t capacity)
		: capacity_(capacity), buffer_(capacity) {
	}

	void LogBuffer::push(LogEntry entry) {
		std::lock_guard lock(mutex_);
		buffer_.push_back(std::move(entry));
	}

	std::vector<LogEntry> LogBuffer::snapshot() const {
		std::lock_guard lock(mutex_);
		return { buffer_.begin(), buffer_.end() };
	}

}  // namespace timetable::infra
