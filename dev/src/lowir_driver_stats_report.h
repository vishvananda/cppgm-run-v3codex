#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace cppgm { struct LowIRLoweringStats; }
namespace lowir_model {
struct FunctionPruningSummary;
struct LowirPreparationStats;
struct Program;
}
namespace lowir_opt { struct Stats; }

namespace lowir_driver_stats_report
{

void ReportOptimizer(std::ostream& output, const std::string& input,
	const lowir_opt::Stats& stats);

void FinalizeOptimizer(const lowir_model::Program& program,
	const lowir_model::FunctionPruningSummary& pruning,
	lowir_opt::Stats* stats, std::uint64_t elapsed_nanoseconds);

void ReportPreparation(std::ostream& output, const std::string& path,
	const cppgm::LowIRLoweringStats& stats,
	const lowir_model::LowirPreparationStats& preparation_stats);

void ReportCompilePhases(std::ostream& output,
	const cppgm::LowIRLoweringStats& stats,
	const lowir_model::LowirPreparationStats& preparation_stats,
	std::uint64_t typed_pipeline_nanoseconds,
	std::uint64_t adapter_nanoseconds,
	std::uint64_t text_parse_nanoseconds,
	std::uint64_t prune_nanoseconds,
	std::uint64_t debug_nanoseconds,
	std::uint64_t lowir_opt_nanoseconds);

}
