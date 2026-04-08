from __future__ import annotations

from demand_generator.config import GeneratorConfig
from demand_generator.csv_output import write_pipeline_outputs
from demand_generator.diagnostics import render_diagnostics_summary
from demand_generator.pipeline import run_generation_pipeline
from demand_generator.types import ApplicationResult, OutputPaths, PipelineResult


def run_generation_application(config: GeneratorConfig) -> ApplicationResult:
    pipeline = run_generation_pipeline(config)
    output_paths = write_pipeline_outputs(pipeline, config.output_dir)
    report_lines = build_application_report_lines(config, pipeline, output_paths)
    return ApplicationResult(
        pipeline=pipeline,
        output_paths=output_paths,
        report_lines=report_lines,
    )


def build_application_report_lines(
    config: GeneratorConfig,
    pipeline: PipelineResult,
    output_paths: OutputPaths,
) -> tuple[str, ...]:
    lines = [
        f"input_path={config.input_path}",
        f"output_dir={output_paths.output_dir}",
        f"time_intervals_csv={output_paths.time_intervals_csv}",
        f"od_demand_csv={output_paths.od_demand_csv}",
        *render_diagnostics_summary(pipeline.diagnostics),
        "input_stage=ok",
        "network_features_stage=ok",
        "time_intervals_stage=ok",
        "daily_demand_stage=ok",
        "temporal_profile_stage=ok",
        "split_stage=ok",
        "pipeline_stage=ok",
        "output_stage=ok",
    ]
    return tuple(lines)
