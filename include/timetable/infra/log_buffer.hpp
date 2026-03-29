#pragma once

#include <cstddef>
#include <mutex>
#include <string>
#include <vector>

#include <boost/circular_buffer.hpp>

#include "timetable/infra/log_entry.hpp"

namespace timetable::infra {

    class LogBuffer {
    public:
        explicit LogBuffer(std::size_t capacity);

        void push(LogEntry entry);
        std::vector<LogEntry> snapshot() const;

    private:
        const std::size_t                capacity_;
        mutable std::mutex               mutex_;
        boost::circular_buffer<LogEntry> buffer_;
    };

}  // namespace timetable::infra
