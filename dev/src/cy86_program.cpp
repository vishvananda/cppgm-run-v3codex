#include "cy86_program.h"

#include <chrono>
#include <stdexcept>

#include "cy86_internal.h"

namespace cppgm
{

Cy86Stats::Cy86Stats()
	: source_bytes(0), tokens(0), peak_statement_tokens(0), identifiers(0),
	  identifier_bytes(0), statements(0), labels(0), fixups(0),
	  instruction_bytes(0), image_bytes(0), elapsed_nanoseconds(0) {}

struct Cy86Program::Impl
{
	explicit Impl(Cy86Stats* output_stats)
		: stats(output_stats), parser(CreateCy86Parser(model, output_stats)),
		  finished(false), started(std::chrono::steady_clock::now()) {}

	~Impl()
	{
		DestroyCy86Parser(parser);
	}

	void Finish()
	{
		if (finished) return;
		FinishCy86Program(*parser);
		finished = true;
		if (stats)
		{
			stats->identifiers = model.identifiers.Size();
			stats->identifier_bytes = model.identifiers.Bytes();
		}
	}

	Cy86ProgramModel model;
	Cy86Stats* stats;
	Cy86ParserState* parser;
	bool finished;
	std::chrono::steady_clock::time_point started;
};

Cy86Program::Cy86Program(Cy86Stats* stats) : impl_(new Impl(stats)) {}

Cy86Program::~Cy86Program()
{
	delete impl_;
}

void Cy86Program::AddTranslationUnit(const std::string& path,
	const std::string& source, const PreprocessingOptions& options)
{
	if (impl_->finished)
		throw std::logic_error("cannot add a translation unit after CY86 lowering");
	if (impl_->stats) impl_->stats->source_bytes += source.size();
	ParseCy86TranslationUnit(*impl_->parser, path, source, options);
}

void Cy86Program::WriteExecutable(const std::string& path)
{
	impl_->Finish();
	WriteCy86Executable(impl_->model, path, impl_->stats);
	if (impl_->stats)
	{
		impl_->stats->elapsed_nanoseconds =
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - impl_->started).count();
	}
}

}
