#pragma once

#include <mutex>
#include <string>
#include <vector>

namespace timetable::ui {

	struct UiSnapshot {
		std::vector<std::string> status_lines;
		std::vector<std::string> log_lines;
	};

	class UiModel {
	public:
		UiSnapshot snapshot() const;
		void set_status_lines(std::vector<std::string> lines);
		void set_log_lines(std::vector<std::string> lines);

	private:
		mutable std::mutex mutex_;
		std::vector<std::string> status_lines_;
		std::vector<std::string> log_lines_;
	};

}  // namespace timetable::ui
