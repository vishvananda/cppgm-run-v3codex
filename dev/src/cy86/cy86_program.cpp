#include "cy86/cy86_program.h"

#include "cy86/errors.h"

#include <chrono>
#include "cy86/cy86_internal.h"

namespace cppgm
{

Cy86Stats::Cy86Stats()
	: source_bytes(0), tokens(0), peak_statement_tokens(0), identifiers(0),
	  identifier_bytes(0), literal_bytes(0), opcode_identities(0),
	  statements(0), operands(0), labels(0), fixups(0),
	  instruction_bytes(0), image_bytes(0), peak_live_source_bytes(0),
	  frontend_nanoseconds(0), lowering_nanoseconds(0), writing_nanoseconds(0),
	  elapsed_nanoseconds(0) {}

struct Cy86Program::Impl
{
	explicit Impl(Cy86Stats* output_stats)
		: stats(output_stats), parser(CreateCy86Parser(model, output_stats)),
		  finished(false), written(false),
		  started(std::chrono::steady_clock::now()) {}

	~Impl()
	{
		DestroyCy86Parser(parser);
	}

	void Finish()
	{
		if (finished) return;
		FinishCy86Program(*parser);
		DestroyCy86Parser(parser);
		parser = 0;
		finished = true;
		if (stats)
		{
			stats->identifiers = model.identifiers.Size();
			stats->identifier_bytes = model.identifiers.Bytes();
			stats->literal_bytes = model.literal_bytes.size();
			stats->opcode_identities = model.opcodes.size() - 1;
		}
	}

	Cy86ProgramModel model;
	Cy86Stats* stats;
	Cy86ParserState* parser;
	bool finished;
	bool written;
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
		cy86_errors::ThrowInternal("cannot add a translation unit after CY86 lowering");
	if (impl_->stats) impl_->stats->source_bytes += source.size();
	const std::chrono::steady_clock::time_point start = impl_->stats ?
		std::chrono::steady_clock::now() :
		std::chrono::steady_clock::time_point();
	ParseCy86TranslationUnit(*impl_->parser, path, source, options);
	if (impl_->stats)
		impl_->stats->frontend_nanoseconds += static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - start).count());
}

void Cy86Program::WriteExecutable(const std::string& path)
{
	if (impl_->written)
		cy86_errors::ThrowInternal("CY86 executable has already been written");
	impl_->Finish();
	impl_->written = true;
	WriteCy86Executable(impl_->model, path, impl_->stats);
	if (impl_->stats)
	{
		impl_->stats->elapsed_nanoseconds =
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - impl_->started).count();
	}
}

}
