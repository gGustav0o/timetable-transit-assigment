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
    // If params.txt is present, the active runtime path uses modeled runtime
    // parameters represented in the domain model.
    // Pair layout directory:
    // - 7064/connection_segments_7064.csv preferred by default, or
    //   connection_segments_input.csv as a legacy fallback
    // - params.txt
    // - dod_7064_full.xlsx as the default daily OD matrix, or legacy
    //   time_intervals.csv + od_demand.csv / generated_demand/od_demand.csv
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
