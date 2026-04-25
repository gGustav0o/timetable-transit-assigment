#pragma once

#include <filesystem>
#include <memory>

#include <mathfp/core/expected.hpp>

#include "timetable/domain/assignment.hpp"

namespace timetable::io {

    // Deprecated compatibility input.
    // Explicitly outside the current maintained minimal-working-project scope.
    struct DataDirSpec {
        std::filesystem::path root;
    };

    // Primary maintained input path.
    // This is the only supported runtime contract in the current project stage.
    // params.txt may be present in the directory, but is intentionally ignored
    // by the active runtime path for now; built-in defaults are used instead.
    // Pair layout directory:
    // - connection_segments_input.csv
    // - params.txt
    // - time_intervals.csv      or generated_demand/time_intervals.csv
    // - od_demand.csv           or generated_demand/od_demand.csv
    struct PairDataDirSpec {
        std::filesystem::path root;
    };

    // Deprecated compatibility input.
    // Explicitly outside the current maintained minimal-working-project scope.
    struct DataFileSpec {
        std::filesystem::path path;
    };

    enum class DataSourceKind {
          DataDir
        , PairDataDir
        , SingleFile
    };

    struct DataSourceSpec {
        DataSourceKind  kind = DataSourceKind::PairDataDir;
        DataDirSpec     dir;
        PairDataDirSpec pair_dir;
        DataFileSpec    file;
    };

    class DataSource {
    public:
        virtual ~DataSource()                                                     = default;
        virtual mathfp::Expected<timetable::domain::AssignmentInput> load() const = 0;
    };

}  // namespace timetable::io
