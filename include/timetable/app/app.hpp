#pragma once

#include <cstddef>
#include <chrono>
#include <filesystem>

#include <mathfp/core/expected.hpp>

#include "timetable/io/io.hpp"

namespace timetable::app {

	struct AppConfig {
		std::size_t               worker_count         = 0;
		std::size_t               log_capacity         = 5'000;
		std::chrono::milliseconds log_refresh_interval { 150 };
		std::filesystem::path     log_dir              = "logs";
	};

	mathfp::Expected<mathfp::Unit> run(
		const AppConfig& config
		, const io::DataSource& data_source
	);

}  // namespace timetable::app
