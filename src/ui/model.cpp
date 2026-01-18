#include "timetable/ui/model.hpp"

namespace timetable::ui {

	UiSnapshot UiModel::snapshot() const {
		std::lock_guard lock(mutex_);
		return UiSnapshot{ status_lines_, log_lines_ };
	}

	void UiModel::set_status_lines(std::vector<std::string> lines) {
		std::lock_guard lock(mutex_);
		status_lines_ = std::move(lines);
	}

	void UiModel::set_log_lines(std::vector<std::string> lines) {
		std::lock_guard lock(mutex_);
		log_lines_ = std::move(lines);
	}

}  // namespace timetable::ui
