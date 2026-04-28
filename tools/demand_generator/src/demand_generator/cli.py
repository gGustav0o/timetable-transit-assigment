from __future__ import annotations

import argparse
from pathlib import Path

from demand_generator.application import run_generation_application
from demand_generator.config import (
    DEFAULT_AUTO_TIME_RANGE,
    DEFAULT_DEMAND_SCALE,
    DEFAULT_END_SEC,
    DEFAULT_INTERVAL_SEC,
    DEFAULT_MAX_WRITTEN_DEMAND_ROWS,
    DEFAULT_MAX_WRITTEN_OD_PAIRS,
    DEFAULT_SEED,
    DEFAULT_SPARSE_OD_THRESHOLD,
    DEFAULT_START_SEC,
    GeneratorConfig,
    make_generator_config,
)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="timetable-demand-generator",
        description="Generate synthetic time intervals and OD demand CSV files.",
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--start-sec", type=int, default=DEFAULT_START_SEC)
    parser.add_argument("--end-sec", type=int, default=DEFAULT_END_SEC)
    parser.add_argument("--interval-sec", type=int, default=DEFAULT_INTERVAL_SEC)
    parser.add_argument("--seed", type=int, default=DEFAULT_SEED)
    parser.add_argument("--demand-scale", type=float, default=DEFAULT_DEMAND_SCALE)
    parser.add_argument("--include-intrazonal", action="store_true")
    parser.add_argument("--sparse-od-threshold", type=float, default=DEFAULT_SPARSE_OD_THRESHOLD)
    parser.add_argument("--max-written-od-pairs", type=int, default=DEFAULT_MAX_WRITTEN_OD_PAIRS)
    parser.add_argument(
        "--max-written-demand-rows",
        type=int,
        default=DEFAULT_MAX_WRITTEN_DEMAND_ROWS,
    )
    parser.add_argument("--auto-time-range", action="store_true", default=DEFAULT_AUTO_TIME_RANGE)
    parser.add_argument("--verbose", action="store_true")
    return parser


def config_from_args(args: argparse.Namespace) -> GeneratorConfig:
    return make_generator_config(
        input_path=args.input,
        output_dir=args.output_dir,
        start_sec=args.start_sec,
        end_sec=args.end_sec,
        interval_sec=args.interval_sec,
        seed=args.seed,
        demand_scale=args.demand_scale,
        include_intrazonal=args.include_intrazonal,
        sparse_od_threshold=args.sparse_od_threshold,
        max_written_od_pairs=args.max_written_od_pairs,
        max_written_demand_rows=args.max_written_demand_rows,
        auto_time_range=args.auto_time_range,
        verbose=args.verbose,
    )


def main() -> int:
    config = config_from_args(build_parser().parse_args())
    result = run_generation_application(config)

    if config.verbose:
        for line in result.report_lines:
            print(line)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
