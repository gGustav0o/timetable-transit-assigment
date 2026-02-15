#pragma once

#include <filesystem>
#include <memory>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"

namespace timetable::io {

	// Data dir is provided via CLI argument.
	// Required files in data dir: stops.csv, trips.csv, stop_times.csv, walk_links.csv,
	// od.csv, params.json.
	struct DataDirSpec {
		std::filesystem::path root;
	};

	// Pair layout directory:
	// - connection_segments_input.csv
	// - params.txt
	struct PairDataDirSpec {
		std::filesystem::path root;
	};

	// Single input text file.
	struct DataFileSpec {
		std::filesystem::path path;
	};

	enum class DataSourceKind {
		DataDir
		, PairDataDir
		, SingleFile
	};

	struct DataSourceSpec {
		DataSourceKind kind = DataSourceKind::DataDir;
		DataDirSpec    dir;
		PairDataDirSpec pair_dir;
		DataFileSpec   file;
	};

	class DataSource {
	public:
		virtual ~DataSource() = default;
		virtual mathfp::Expected<timetable::domain::AssignmentInput> load() const = 0;
	};

}  // namespace timetable::io
