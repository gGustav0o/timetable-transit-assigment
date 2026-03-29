#include "timetable/ui/result_bus.hpp"

#include <mutex>
#include <utility>

namespace timetable::ui::result {
    namespace {

        Sink make_noop_sink() {
            return [](UiResultSnapshot) {};
        }

        Sink normalize_sink(
            Sink sink
        ) {
            return sink ? std::move(sink) : make_noop_sink();
        }

        class ResultBusState final {
        public:
            mathfp::Expected<mathfp::Unit> set_sink(
                Sink sink
            ) {
                std::lock_guard lock(mutex_);
                sink_ = normalize_sink(std::move(sink));
                return mathfp::kUnit;
            }

            void publish(
                UiResultSnapshot snapshot
            ) {
                copy_sink()(std::move(snapshot));
            }

        private:
            Sink copy_sink() {
                std::lock_guard lock(mutex_);
                return sink_;
            }

            std::mutex mutex_{};
            Sink sink_ = make_noop_sink();
        };

        ResultBusState& bus() {
            static ResultBusState state;
            return state;
        }

    }  // namespace

    mathfp::Expected<mathfp::Unit> set_sink(
        Sink sink
    ) {
        return bus().set_sink(std::move(sink));
    }

    mathfp::Expected<mathfp::Unit> clear_sink() {
        return set_sink(Sink{});
    }

    void publish(
        UiResultSnapshot snapshot
    ) {
        bus().publish(std::move(snapshot));
    }

}  // namespace timetable::ui::result
