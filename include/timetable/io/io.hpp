#pragma once

#include <filesystem>
#include <memory>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"

namespace timetable::io {

	// Deprecated compatibility input.
	// This path is not maintained against the actively evolving assignment logic.
	// Required files in data dir: stops.csv, trips.csv, stop_times.csv, walk_links.csv,
	// od.csv, params.json.
	struct DataDirSpec {
		std::filesystem::path root;
	};

	// Primary maintained input path.
	// Pair layout directory:
	// - connection_segments_input.csv
	// - params.txt
	struct PairDataDirSpec {
		std::filesystem::path root;
	};

	// Deprecated compatibility input.
	// This path is not maintained against the actively evolving assignment logic.
	struct DataFileSpec {
		std::filesystem::path path;
	};

	enum class DataSourceKind {
		DataDir
		, PairDataDir
		, SingleFile
	};

	struct DataSourceSpec {
		DataSourceKind kind = DataSourceKind::PairDataDir;
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
