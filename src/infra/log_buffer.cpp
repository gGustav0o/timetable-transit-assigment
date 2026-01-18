#include "timetable/infra/log_buffer.hpp"

namespace timetable::infra {

	LogBuffer::LogBuffer(std::size_t capacity)
		: capacity_(capacity), buffer_(capacity) {
	}

	void LogBuffer::push(std::string line) {
		std::lock_guard lock(mutex_);
		buffer_.push_back(std::move(line));
	}

	std::vector<std::string> LogBuffer::snapshot() const {
		std::lock_guard lock(mutex_);
		return { buffer_.begin(), buffer_.end() };
	}

}  // namespace timetable::infra
