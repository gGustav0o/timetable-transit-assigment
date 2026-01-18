#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include <boost/circular_buffer.hpp>

namespace timetable::infra {

	class LogBuffer {
	public:
		explicit LogBuffer(std::size_t capacity);

		void push(std::string line);
		std::vector<std::string> snapshot() const;

	private:
		const std::size_t                   capacity_;
		mutable std::mutex                  mutex_;
		boost::circular_buffer<std::string> buffer_;
	};

}  // namespace timetable::infra
