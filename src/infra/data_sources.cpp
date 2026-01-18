#include "timetable/infra/data_sources.hpp"

#include "timetable/infra/file_data_source.hpp"

#include <mathfp/core/error.hpp>

namespace timetable::infra {

	mathfp::Expected<std::unique_ptr<io::DataSource>> make_data_source(
		const io::DataSourceSpec& spec
	) {
		switch (spec.kind) {
		case io::DataSourceKind::File:
			return make_file_data_source(spec.file);
		}
		return mathfp::unexpected(
			mathfp::invalid_arg("unknown data source kind"));
	}

}  // namespace timetable::infra
