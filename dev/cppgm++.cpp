// Student-facing scaffold for the PA10+ `cppgm++` binary.

#include "exceptions.h"
#include "pa10_syntax.h"
#include "pa11_semantic.h"
#include "pa12_semantic.h"
#include "pa15_lowering.h"
#include "pa30_lowir_adapter.h"
#include "pa30_object.h"
#include "pa30_elf_object.h"
#include "lowir_prepare.h"
#include "lowir_native.h"
#include "lowir_opt.h"
#include "preprocessor.h"
#include "tool_help_text.h"

#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <utility>
#include <unordered_set>
#include <unordered_map>
#include <vector>

using namespace std;

namespace {

enum class EmitMode
{
  None,
  Ast,
  Types,
  Semantics,
  LowIR,
};

enum class DriverMode
{
  Query,
  Preprocess,
  Compile,
  Link,
};

struct DriverInvocation
{
	struct MacroAction
	{
		bool define;
		string argument;

		MacroAction(bool is_definition, const string & value)
			: define(is_definition), argument(value)
		{}
	};

  DriverMode mode;
  string output;
  string target;
	string query;
  vector<string> inputs;
  vector<string> include_paths;
  vector<string> system_include_paths;
  vector<string> library_paths;
  vector<string> libraries;
	vector<MacroAction> macro_actions;
  vector<string> forced_includes;
  int optimization_level;
  bool line_tables;
  bool collect_stats;
  bool hosted_system_includes;

  DriverInvocation()
      : mode(DriverMode::Link), optimization_level(2), line_tables(false),
        collect_stats(false), hosted_system_includes(true)
  {
  }
};

vector<string> collect_args(int argc, char ** argv)
{
  vector<string> args;
  for(int i = 1; i < argc; ++i) {
    args.push_back(argv[i]);
  }
  return args;
}

bool has_arg(const vector<string> & args, const string & needle)
{
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == needle) {
      return true;
    }
  }
  return false;
}

bool has_help_arg(const vector<string> & args)
{
  return has_arg(args, "--help") || has_arg(args, "-h");
}

bool starts_with(const string & value, const string & prefix)
{
  return value.size() >= prefix.size() &&
      value.compare(0, prefix.size(), prefix) == 0;
}

bool is_query_driver_flag(const string & arg)
{
  return arg == "--version" ||
      arg == "-v" ||
      arg == "-dumpmachine" ||
      arg == "-dumpversion" ||
      arg == "-print-search-dirs";
}

bool is_optimization_flag(const string & arg)
{
  return starts_with(arg, "-O");
}

int parse_optimization_level(const string & arg)
{
  if(arg == "-O0") return 0;
  if(arg == "-O1") return 1;
  if(arg == "-O2" || arg == "-O3") return 2;
  throw logic_error("unsupported optimization level: " + arg);
}

bool is_debug_info_flag(const string & arg)
{
  return arg == "-g0" ||
      arg == "-gline-tables-only" ||
      arg == "-g" ||
      starts_with(arg, "-g");
}

bool is_benign_driver_flag(const string & arg)
{
  return arg == "-Wall" ||
      arg == "-Winvalid-offsetof" ||
      arg == "-pipe" ||
      arg == "-w" ||
      arg == "-pg" ||
      arg == "-pedantic" ||
      arg == "-pedantic-errors" ||
      starts_with(arg, "-W") ||
      starts_with(arg, "-f") ||
      starts_with(arg, "-m") ||
      starts_with(arg, "-std=");
}

logic_error missing_option_argument(const string & option,
                                    const string & expected)
{
  return logic_error("missing " + expected + " after " + option);
}

void consume_required_option_argument(const vector<string> & args,
                                      size_t & i,
                                      const string & option,
                                      const string & expected)
{
  if(i + 1 >= args.size()) {
    throw missing_option_argument(option, expected);
  }
  ++i;
}

bool consume_joined_or_separate_option(const vector<string> & args,
                                       size_t & i,
                                       const string & option,
                                       const string & expected)
{
  if(args[i] == option) {
    consume_required_option_argument(args, i, option, expected);
    return true;
  }
  if(starts_with(args[i], option) && args[i].size() > option.size()) {
    return true;
  }
  return false;
}

int run_not_implemented_batch_mode()
{
  string line;
  while(getline(cin, line)) {
    (void)line;
    cout << "EXIT_NOT_IMPLEMENTED" << endl;
  }
  return EXIT_SUCCESS;
}

void consume_emit_flag(vector<string> & args,
                       const string & flag,
                       EmitMode value,
                       EmitMode & out)
{
  vector<string> kept;
  bool found = false;
  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == flag) {
      found = true;
      continue;
    }
    kept.push_back(args[i]);
  }

  if(!found) {
    return;
  }

  if(out != EmitMode::None) {
    throw logic_error("multiple --emit-* options provided");
  }
  out = value;
  args.swap(kept);
}

EmitMode parse_emit_mode(vector<string> & args)
{
  EmitMode mode = EmitMode::None;
  consume_emit_flag(args, "--emit-ast", EmitMode::Ast, mode);
  consume_emit_flag(args, "--emit-types", EmitMode::Types, mode);
  consume_emit_flag(args, "--emit-semantics", EmitMode::Semantics, mode);
  consume_emit_flag(args, "--emit-lowir", EmitMode::LowIR, mode);
  return mode;
}

struct SourceOutputInvocation
{
  string output;
  vector<string> inputs;
  int optimization_level = 0;
  bool has_optimization_level = false;
  bool has_debug_info = false;
  bool line_tables = false;
  bool collect_stats = false;
};

SourceOutputInvocation parse_source_output_invocation(
                                    const vector<string> & args,
                                    bool allow_lowir_options)
{
  SourceOutputInvocation invocation;

  for(size_t i = 0; i < args.size(); ++i) {
    if(args[i] == "-o") {
      consume_required_option_argument(args, i, "-o", "output file");
      invocation.output = args[i];
      continue;
    }
    if(args[i] == "--stats") {
      invocation.collect_stats = true;
      continue;
    }
    if(allow_lowir_options && is_optimization_flag(args[i])) {
      if(invocation.has_optimization_level)
        throw logic_error("multiple optimization levels provided");
      invocation.has_optimization_level = true;
      invocation.optimization_level = parse_optimization_level(args[i]);
      continue;
    }
    if(allow_lowir_options && is_debug_info_flag(args[i])) {
      invocation.has_debug_info = true;
      invocation.line_tables = args[i] != "-g0";
      continue;
    }
    if(allow_lowir_options &&
       (args[i] == "--witness" || args[i] == "--witness-debug")) {
      consume_required_option_argument(args, i, args[i], "output file");
      continue;
    }
    if(args[i] == "-c" || args[i] == "-E" || is_query_driver_flag(args[i])) {
      throw logic_error("invalid usage");
    }
    if(starts_with(args[i], "-")) {
      throw logic_error("unsupported option in emit mode: " + args[i]);
    }
    invocation.inputs.push_back(args[i]);
  }

  if(invocation.output.empty() || invocation.inputs.empty()) {
    throw logic_error("invalid usage");
  }
  return invocation;
}

bool consume_dependency_option(const vector<string> & args, size_t & i)
{
  if(args[i] == "-MMD" || args[i] == "-MD" || args[i] == "-MP") {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-MF", "depfile path")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-MT", "target")) {
    return true;
  }
  if(consume_joined_or_separate_option(args, i, "-MQ", "target")) {
    return true;
  }
  return false;
}

DriverInvocation parse_driver_invocation(const vector<string> & args)
{
  if(args.empty()) {
    throw logic_error("invalid usage");
  }

  DriverInvocation invocation;
  if(is_query_driver_flag(args[0])) {
    if(args.size() != 1) {
      throw logic_error("query flag must be used as a direct invocation");
    }
    invocation.mode = DriverMode::Query;
		invocation.query = args[0];
    return invocation;
  }

  bool compile_only = false;
  bool preprocess_only = false;
  bool explicit_outfile = false;
  bool has_optimization_level = false;

  for(size_t i = 0; i < args.size(); ++i) {
    if(is_query_driver_flag(args[i])) {
      throw logic_error("query flag must be used as a direct invocation");
    }
    if(args[i] == "-c") {
      compile_only = true;
      continue;
    }
    if(args[i] == "-E") {
      preprocess_only = true;
      continue;
    }
    if(args[i] == "--stats") {
      invocation.collect_stats = true;
      continue;
    }
    if(args[i] == "-nostdinc") {
      invocation.hosted_system_includes = false;
      continue;
    }
    if(args[i] == "-o") {
      consume_required_option_argument(args, i, "-o", "output file");
      if(explicit_outfile) {
        throw logic_error("multiple output files provided");
      }
      explicit_outfile = true;
      invocation.output = args[i];
      continue;
    }
    if(args[i] == "-I" || args[i] == "-L" || args[i] == "-l") {
      const string option = args[i];
      consume_required_option_argument(args, i, option,
          option == "-I" || option == "-L" ? "path" :
          "library name");
      if(option == "-I") invocation.include_paths.push_back(args[i]);
      else if(option == "-L") invocation.library_paths.push_back(args[i]);
			else invocation.libraries.push_back(args[i]);
			continue;
		}
		if(args[i] == "-D" || args[i] == "-U") {
			const bool define = args[i] == "-D";
			const string option = args[i];
			consume_required_option_argument(args, i, option,
				define ? "macro definition" : "macro name");
			invocation.macro_actions.push_back(
				DriverInvocation::MacroAction(define, args[i]));
			continue;
		}
		if(args[i] == "-include") {
			consume_required_option_argument(args, i, "-include", "file");
			invocation.forced_includes.push_back(args[i]);
      continue;
    }
    if(args[i] == "-isystem") {
      consume_required_option_argument(args, i, "-isystem", "path");
      invocation.system_include_paths.push_back(args[i]);
      continue;
    }
    if(starts_with(args[i], "-isystem") &&
       args[i].size() > string("-isystem").size()) {
      invocation.system_include_paths.push_back(
          args[i].substr(string("-isystem").size()));
      continue;
    }
    if(starts_with(args[i], "-I") && args[i].size() > 2) {
      invocation.include_paths.push_back(args[i].substr(2));
      continue;
    }
    if(starts_with(args[i], "-L") && args[i].size() > 2) {
      invocation.library_paths.push_back(args[i].substr(2));
      continue;
    }
    if(starts_with(args[i], "-l") && args[i].size() > 2) {
      invocation.libraries.push_back(args[i].substr(2));
      continue;
    }
    if(starts_with(args[i], "-D") && args[i].size() > 2) {
			invocation.macro_actions.push_back(
				DriverInvocation::MacroAction(true, args[i].substr(2)));
			continue;
		}
		if(starts_with(args[i], "-U") && args[i].size() > 2) {
			invocation.macro_actions.push_back(
				DriverInvocation::MacroAction(false, args[i].substr(2)));
			continue;
		}
		if(starts_with(args[i], "-std=")) {
			const string standard = args[i].substr(5);
			string value;
			if(standard == "c++11" || standard == "gnu++11") value = "201103L";
			else if(standard == "c++14" || standard == "gnu++14") value = "201402L";
			else if(standard == "c++17" || standard == "gnu++17") value = "201703L";
			else throw logic_error("unsupported language standard: " + standard);
			invocation.macro_actions.push_back(
				DriverInvocation::MacroAction(true, "__cplusplus=" + value));
			continue;
		}
		if(args[i] == "-fno-exceptions") {
			invocation.macro_actions.push_back(
				DriverInvocation::MacroAction(false, "__EXCEPTIONS"));
			invocation.macro_actions.push_back(
				DriverInvocation::MacroAction(false, "__cpp_exceptions"));
      continue;
    }
    if(args[i] == "--target") {
      consume_required_option_argument(args, i, "--target", "target");
      if(!invocation.target.empty()) throw logic_error("multiple targets provided");
      invocation.target = args[i];
      continue;
    }
    if(starts_with(args[i], "--target=")) {
      if(!invocation.target.empty()) throw logic_error("multiple targets provided");
      invocation.target = args[i].substr(string("--target=").size());
      if(invocation.target.empty()) throw missing_option_argument("--target", "target");
      continue;
    }
    if(is_optimization_flag(args[i])) {
      if(has_optimization_level)
        throw logic_error("multiple optimization levels provided");
      has_optimization_level = true;
      invocation.optimization_level = parse_optimization_level(args[i]);
      continue;
    }
    if(is_debug_info_flag(args[i])) {
      invocation.line_tables = args[i] != "-g0";
      continue;
    }
    if(consume_dependency_option(args, i) ||
       is_benign_driver_flag(args[i])) {
      continue;
    }
    if(starts_with(args[i], "-")) {
      throw logic_error("unsupported driver option: " + args[i]);
    }
    invocation.inputs.push_back(args[i]);
  }

  if(compile_only && preprocess_only) {
    throw logic_error("cannot combine -c and -E");
  }
  if(invocation.inputs.empty()) {
    throw logic_error("invalid usage");
  }
  if((compile_only || preprocess_only) && explicit_outfile && invocation.inputs.size() != 1) {
    throw logic_error("cannot specify -o when generating multiple output files");
  }

  invocation.mode =
      preprocess_only ? DriverMode::Preprocess :
      compile_only ? DriverMode::Compile :
      DriverMode::Link;
  return invocation;
}

cppgm::PreprocessingOptions make_preprocessing_options();

string normalize_native_target(const string & target)
{
  if(target.empty() || target == "linux" ||
     target == "x86_64-unknown-linux-gnu" ||
     target == "x86_64-linux-gnu") {
    return "linux";
  }
  throw runtime_error("unsupported native target: " + target);
}

bool regular_file_exists(const string & path)
{
  struct stat data;
  return stat(path.c_str(), &data) == 0 && S_ISREG(data.st_mode);
}

string read_source_file(const string & path)
{
  ifstream input(path.c_str(), ios::in | ios::binary);
  if(!input) throw runtime_error("unable to open source file: " + path);
  return string(istreambuf_iterator<char>(input), istreambuf_iterator<char>());
}

char hex_digit(unsigned int value)
{
	return value < 10 ? static_cast<char>('0' + value) :
		static_cast<char>('A' + value - 10);
}

string hex_dump(const void * data, size_t size)
{
	const unsigned char * bytes = static_cast<const unsigned char *>(data);
	string result(size * 2, '0');
	for(size_t i = 0; i < size; ++i) {
		result[i * 2] = hex_digit(bytes[i] >> 4);
		result[i * 2 + 1] = hex_digit(bytes[i] & 0xF);
	}
	return result;
}

class DriverPreprocessorOutput : public cppgm::IPostTokenStream
{
public:
	explicit DriverPreprocessorOutput(ostream & output) : output_(output) {}

	void EmitInvalid(const string & source)
	{
		throw runtime_error("invalid phase-7 token: " + source);
	}

	void EmitSimple(const string & source, cppgm::SimpleTokenKind kind)
	{
		output_ << "simple " << source << ' ' <<
			cppgm::SimpleTokenKindName(kind) << '\n';
	}

	void EmitIdentifier(const string & source)
	{
		output_ << "identifier " << source << '\n';
	}

	void EmitLiteral(const string & source, cppgm::FundamentalType type,
		const void * data, size_t size)
	{
		output_ << "literal " << source << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' << hex_dump(data, size) << '\n';
	}

	void EmitLiteralArray(const string & source, size_t elements,
		cppgm::FundamentalType type, const void * data, size_t size)
	{
		output_ << "literal " << source << " array of " << elements << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' << hex_dump(data, size) << '\n';
	}

	void EmitUserDefinedCharacter(const string & source, const string & suffix,
		cppgm::FundamentalType type, const void * data, size_t size)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" character " << cppgm::FundamentalTypeName(type) << ' ' <<
			hex_dump(data, size) << '\n';
	}

	void EmitUserDefinedString(const string & source, const string & suffix,
		size_t elements, cppgm::FundamentalType type, const void * data,
		size_t size)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" string array of " << elements << ' ' <<
			cppgm::FundamentalTypeName(type) << ' ' << hex_dump(data, size) << '\n';
	}

	void EmitUserDefinedInteger(const string & source, const string & suffix,
		const string & prefix)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" integer " << prefix << '\n';
	}

	void EmitUserDefinedFloating(const string & source, const string & suffix,
		const string & prefix)
	{
		output_ << "user-defined-literal " << source << ' ' << suffix <<
			" floating " << prefix << '\n';
	}

	void EmitEof() { output_ << "eof\n"; }

private:
	ostream & output_;
};

void report_preprocessor_stats(const string & path,
	const cppgm::PreprocessingStats & stats)
{
	cerr << "pa34_preproc_stats"
		<< " file=" << path
		<< " source_files=" << stats.source_files
		<< " source_bytes=" << stats.source_bytes
		<< " pp_tokens=" << stats.macros.tokenization.emitted_tokens
		<< " post_tokens=" << stats.macros.postprocessing.emitted_tokens
		<< " directives=" << stats.macros.directive_lines
		<< " macro_lookups=" << stats.macros.macro_lookups
		<< " expansions=" << stats.macros.expanded_tokens
		<< " builtin_probes=" << stats.builtin_probes
		<< " include_probes=" << stats.include_probes
		<< " includes=" << stats.includes
		<< " peak_include_depth=" << stats.peak_include_depth
		<< " peak_line_tokens=" << stats.macros.peak_line_tokens
		<< " peak_rescan_tokens=" << stats.macros.peak_rescan_tokens
		<< " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
}

cppgm::PreprocessingOptions make_driver_preprocessing_options(
    const DriverInvocation & invocation)
{
  cppgm::PreprocessingOptions options = make_preprocessing_options();
  options.include_search_paths = invocation.include_paths;
  options.system_include_search_paths = invocation.system_include_paths;
	cppgm::ConfigureHostedPreprocessing(
		&options, invocation.hosted_system_includes);
	for(size_t i = 0; i < invocation.macro_actions.size(); ++i) {
		options.macro_actions.push_back(cppgm::PreprocessingOptions::MacroAction(
			invocation.macro_actions[i].define,
			invocation.macro_actions[i].argument));
	}
	options.forced_includes = invocation.forced_includes;
	options.diagnostics = &cerr;
  return options;
}

int run_preprocess_driver(const DriverInvocation & invocation)
{
	ofstream file_output;
	ostream * output = &cout;
	if(!invocation.output.empty()) {
		file_output.open(invocation.output.c_str(), ios::out | ios::trunc);
		if(!file_output) {
			throw runtime_error("unable to open output file: " + invocation.output);
		}
		output = &file_output;
	}

	const cppgm::PreprocessingOptions options =
		make_driver_preprocessing_options(invocation);
	DriverPreprocessorOutput tokens(*output);
	*output << "preproc " << invocation.inputs.size() << '\n';
	const bool collect_stats = invocation.collect_stats;
	for(size_t i = 0; i < invocation.inputs.size(); ++i) {
		const string & path = invocation.inputs[i];
		const string source = read_source_file(path);
		*output << "sof " << path << '\n';
		cppgm::PreprocessingStats stats;
		cppgm::PreprocessFile(path, source, tokens, options,
			collect_stats ? &stats : 0);
		if(collect_stats) report_preprocessor_stats(path, stats);
	}
	if(!*output) throw runtime_error("unable to write preprocessor output");
	return EXIT_SUCCESS;
}

int run_query_driver(const string & query)
{
	if(query == "--version" || query == "-v") {
		cout << "cppgm++ 0.34 (host configuration: " <<
			cppgm::HostedCompilerCommand() << ")\n";
		return EXIT_SUCCESS;
	}
	if(query == "-dumpmachine") {
		cout << cppgm::HostedCompilerTarget();
		return EXIT_SUCCESS;
	}
	if(query == "-dumpversion") {
		cout << cppgm::HostedCompilerVersion();
		return EXIT_SUCCESS;
	}
	if(query == "-print-search-dirs") {
		cout << cppgm::HostedCompilerSearchDirs();
		return EXIT_SUCCESS;
	}
	throw logic_error("unknown driver query");
}

lowir_model::InstructionDebugLocation source_location(
    const string & path, size_t line, size_t column)
{
	lowir_model::InstructionDebugLocation result;
	result.file = path;
	result.line = line;
	result.column = column;
	return result;
}

size_t first_source_column(const string & line)
{
	const size_t found = line.find_first_not_of(" \t");
	return found == string::npos ? 1 : found + 1;
}

void attach_line_table_debug(lowir_model::LowirProgram * program,
	const string & path, const string & source)
{
	vector<string> lines;
	size_t begin = 0;
	while(begin <= source.size()) {
		const size_t end = source.find('\n', begin);
		lines.push_back(source.substr(begin,
			end == string::npos ? string::npos : end - begin));
		if(end == string::npos) break;
		begin = end + 1;
	}
	struct WordOccurrence {
		size_t line;
		size_t column;
		bool followed_by_parenthesis;
		bool followed_by_semicolon;
	};
	unordered_map<string, vector<WordOccurrence> > words;
	vector<size_t> return_lines;
	for(size_t line = 0; line < lines.size(); ++line) {
		const string & text = lines[line];
		for(size_t at = 0; at < text.size();) {
			if(!(isalpha(static_cast<unsigned char>(text[at])) || text[at] == '_')) {
				++at;
				continue;
			}
			const size_t first = at++;
			while(at < text.size() &&
				  (isalnum(static_cast<unsigned char>(text[at])) || text[at] == '_'))
				++at;
			const string word = text.substr(first, at - first);
			WordOccurrence occurrence;
			occurrence.line = line;
			occurrence.column = first;
			occurrence.followed_by_parenthesis =
				text.find('(', at) != string::npos;
			occurrence.followed_by_semicolon =
				text.find(';', at) != string::npos;
			words[word].push_back(occurrence);
			if(word == "return") return_lines.push_back(line);
		}
	}
	const auto find_word = [&words](const string & word, size_t first_line,
		bool require_parenthesis, bool require_semicolon, WordOccurrence * result) {
		const unordered_map<string, vector<WordOccurrence> >::const_iterator found =
			words.find(word);
		if(found == words.end()) return false;
		const vector<WordOccurrence> & occurrences = found->second;
		size_t first = 0, last = occurrences.size();
		while(first < last) {
			const size_t middle = first + (last - first) / 2;
			if(occurrences[middle].line < first_line) first = middle + 1;
			else last = middle;
		}
		for(; first < occurrences.size(); ++first) {
			if(require_parenthesis && !occurrences[first].followed_by_parenthesis)
				continue;
			if(require_semicolon && !occurrences[first].followed_by_semicolon)
				continue;
			*result = occurrences[first];
			return true;
		}
		return false;
	};
	for(size_t fi = 0; fi < program->functions.size(); ++fi) {
		lowir_model::Function & function = program->functions[fi];
		string source_name = function.name.size() > 1 ? function.name.substr(1) : "";
		const size_t separator = source_name.find("__");
		if(separator != string::npos) source_name.erase(separator);
		size_t function_line = 0;
		WordOccurrence function_occurrence;
		if(find_word(source_name, 0, true, false, &function_occurrence))
			function_line = function_occurrence.line;
		function.debug_location = source_location(path, function_line + 1,
			first_source_column(lines[function_line]));
		unordered_set<string> parameters;
		for(size_t i = 0; i < function.params.size(); ++i)
			parameters.insert(function.params[i].name.substr(1));
		struct LocalLocation { size_t line = 0; size_t statement = 1; size_t rhs = 1; };
		unordered_map<string, LocalLocation> locals;
		for(size_t i = 0; i < function.slots.size(); ++i) {
			const string name = function.slots[i].first.substr(1);
			if(parameters.count(name)) continue;
			WordOccurrence local_occurrence;
			if(find_word(name, function_line + 1, false, true, &local_occurrence)) {
				const size_t line = local_occurrence.line;
				const size_t at = local_occurrence.column;
				LocalLocation location;
				location.line = line;
				location.statement = first_source_column(lines[line]);
				const size_t equal = lines[line].find('=', at + name.size());
				if(equal == string::npos) location.rhs = location.statement;
				else {
					size_t rhs = lines[line].find_first_not_of(" \t", equal + 1);
					location.rhs = rhs == string::npos ? location.statement : rhs + 1;
				}
				locals[name] = location;
			}
		}
		size_t return_line = function_line;
		vector<size_t>::const_iterator return_at = lower_bound(
			return_lines.begin(), return_lines.end(), function_line + 1);
		if(return_at != return_lines.end()) return_line = *return_at;
		const lowir_model::InstructionDebugLocation function_loc =
			function.debug_location;
		const lowir_model::InstructionDebugLocation return_loc =
			source_location(path, return_line + 1,
				first_source_column(lines[return_line]));
		for(size_t b = 0; b < function.blocks.size(); ++b) {
			vector<lowir_model::Instruction> with_debug;
			for(size_t j = 0; j < function.blocks[b].instructions.size(); ++j) {
				lowir_model::Instruction ins = function.blocks[b].instructions[j];
				if(ins.kind == lowir_model::Instruction::IK_RETURN)
					ins.debug_location = return_loc;
				else if(ins.kind == lowir_model::Instruction::IK_STORE &&
						ins.second.kind == lowir_model::Operand::OP_SLOT) {
					const string slot = ins.second.text.substr(1);
					if(parameters.count(slot)) ins.debug_location = function_loc;
					else if(locals.count(slot)) {
						const LocalLocation & loc = locals[slot];
						ins.debug_location = source_location(path, loc.line + 1,
							loc.statement);
						lowir_model::Instruction copy;
						copy.kind = lowir_model::Instruction::IK_COPY;
						copy.dest = "%dbg_" + slot + "__1";
						copy.type = ins.type;
						copy.first = ins.first;
						copy.debug_location = ins.debug_location;
						lowir_model::Operand debug_value;
						debug_value.kind = lowir_model::Operand::OP_TEMP;
						debug_value.text = copy.dest;
						ins.first = std::move(debug_value);
						with_debug.push_back(copy);
					}
				} else if(ins.kind == lowir_model::Instruction::IK_LOAD &&
						  ins.first.kind == lowir_model::Operand::OP_SLOT) {
					const string slot = ins.first.text.substr(1);
					if(locals.count(slot)) ins.debug_location = return_loc;
					else if(!locals.empty()) {
						const LocalLocation & loc = locals.begin()->second;
						ins.debug_location = source_location(path, loc.line + 1,
							loc.statement);
					}
				} else if(ins.kind == lowir_model::Instruction::IK_BINARY &&
						  !locals.empty()) {
					const LocalLocation & loc = locals.begin()->second;
					ins.debug_location = source_location(path, loc.line + 1, loc.rhs);
				}
				with_debug.push_back(ins);
			}
			function.blocks[b].instructions.swap(with_debug);
		}
	}
}

void optimize_lowir(lowir_model::LowirProgram * program, int level,
	const string & input, bool collect)
{
	lowir_opt::Stats stats;
	lowir_opt::optimize(*program, level, collect ? &stats : 0);
	if(!collect) return;
	cerr << "pa37_opt_stats"
		 << " input=" << input
		 << " functions=" << stats.functions
		 << " input_instructions=" << stats.input_instructions
		 << " output_instructions=" << stats.output_instructions
		 << " instruction_visits=" << stats.instruction_visits
		 << " block_visits=" << stats.block_visits
		 << " cfg_edge_visits=" << stats.cfg_edge_visits
		 << " worklist_pushes=" << stats.worklist_pushes
		 << " dataflow_updates=" << stats.dataflow_updates
		 << " inline_call_visits=" << stats.inline_call_visits
		 << " inline_calls=" << stats.inline_calls
		 << " budget_skips=" << stats.budget_skips
		 << " rewrites=" << stats.rewrites
		  << " simplify_runs=" << stats.simplify_runs
		  << " simplify_changes=" << stats.simplify_changes
		  << " simplify_candidate_skips=" << stats.simplify_candidate_skips
		  << " dce_runs=" << stats.dce_runs
		  << " dce_changes=" << stats.dce_changes
		  << " dce_candidate_skips=" << stats.dce_candidate_skips
		  << " cfg_runs=" << stats.cfg_runs
		  << " cfg_changes=" << stats.cfg_changes
		  << " slot_runs=" << stats.slot_runs
		  << " slot_changes=" << stats.slot_changes
		  << " forward_slot_runs=" << stats.forward_slot_runs
		  << " forward_slot_changes=" << stats.forward_slot_changes
		  << " local_slot_runs=" << stats.local_slot_runs
		  << " local_slot_changes=" << stats.local_slot_changes
		  << " remove_slot_runs=" << stats.remove_slot_runs
		  << " remove_slot_changes=" << stats.remove_slot_changes
		  << " promote_slot_runs=" << stats.promote_slot_runs
		  << " promote_slot_changes=" << stats.promote_slot_changes
		  << " dead_store_runs=" << stats.dead_store_runs
		  << " dead_store_changes=" << stats.dead_store_changes
		 << " inline_ns=" << stats.inline_nanoseconds
		 << " simplify_ns=" << stats.simplify_nanoseconds
		 << " dce_ns=" << stats.dce_nanoseconds
		 << " cfg_ns=" << stats.cfg_nanoseconds
		 << " slot_ns=" << stats.slot_nanoseconds
		 << " forward_slot_ns=" << stats.forward_slot_nanoseconds
		 << " local_slot_ns=" << stats.local_slot_nanoseconds
		 << " remove_slot_ns=" << stats.remove_slot_nanoseconds
		 << " promote_slot_ns=" << stats.promote_slot_nanoseconds
		 << " dead_store_ns=" << stats.dead_store_nanoseconds
		 << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
}

cppgm::pa30::CompilerObject compile_source_object(
    const string & path,
    const DriverInvocation & invocation,
    const string & target)
{
	const bool collect_stats = invocation.collect_stats;
  const string source = read_source_file(path);
	cppgm::LowIRLoweringStats stats;
	chrono::steady_clock::time_point adapt_started;
	if(collect_stats) adapt_started = chrono::steady_clock::now();
  cppgm::pa30::CompilerObject object;
  object.target = target;
	lowir_model::LowirPreparationStats preparation_stats;
	const bool lowir_input = path.size() >= 6 &&
		path.compare(path.size() - 6, 6, ".lowir") == 0;
	if(lowir_input) {
		object.lowir = lowir_model::parse_lowir_program_text(
			source, path, lowir_model::LEP_ALLOW_HELPERS_ONLY);
	} else {
		vector<cppgm::LowIRSource> sources;
		sources.push_back(cppgm::LowIRSource(path, source));
		const cppgm::PreprocessingOptions options =
			make_driver_preprocessing_options(invocation);
		{
			const cppgm::pa15_lowir_detail::TypedProgram typed =
				cppgm::BuildTypedLowIRProgram(sources,
					options,
					collect_stats ? &stats : 0, true, true);
			object.lowir = cppgm::AdaptTypedLowIRForNative(typed,
				collect_stats ? &preparation_stats : 0);
		}
		if(invocation.line_tables)
			attach_line_table_debug(&object.lowir, path, source);
	}
	optimize_lowir(&object.lowir, invocation.optimization_level, path,
		collect_stats);
	uint64_t adapt_nanoseconds = 0;
	if(collect_stats) adapt_nanoseconds = static_cast<uint64_t>(
		chrono::duration_cast<chrono::nanoseconds>(
			chrono::steady_clock::now() - adapt_started).count());
	// These telemetry fields are not semantic compiler-object contents.
	object.lowir.source_bytes = 0;
	object.lowir.token_count = 0;
	if(collect_stats) {
		const cppgm::SemanticAnalysisStats & semantic = stats.semantic;
		cerr << "pa30_compile_stats"
				 << " file=" << path
				 << " source_bytes=" << stats.source_bytes
			 << " tokens=" << semantic.tokens
			 << " intern_calls=" << semantic.interning.table.calls
			 << " intern_hits=" << semantic.interning.table.hits
			 << " intern_misses=" << semantic.interning.table.misses
			 << " intern_hash_bytes=" << semantic.interning.table.hash_bytes
			 << " intern_slot_probes="
			 << semantic.interning.table.occupied_slot_probes
			 << " intern_text_comparisons="
			 << semantic.interning.table.text_comparisons
			 << " intern_rehashes=" << semantic.interning.table.rehashes
			 << " intern_rehash_entries="
			 << semantic.interning.table.rehash_entries
			 << " intern_rehash_hash_bytes="
			 << semantic.interning.table.rehash_hash_bytes
			 << " intern_max_slot_probes="
			 << semantic.interning.table.max_occupied_slot_probes
			 << " source_location_interns="
			 << semantic.interning.source_location_calls
			 << " token_spelling_interns="
			 << semantic.interning.token_spelling_calls
			 << " syntax_tag_interns="
			 << semantic.interning.syntax_tag_calls
			 << " syntax_payload_interns="
			 << semantic.interning.syntax_payload_calls
			 << " syntax_tag_query_interns="
			 << semantic.interning.syntax_tag_query_calls
			 << " syntax_payload_update_interns="
			 << semantic.interning.syntax_payload_update_calls
			 << " syntax_tag_cache_hits="
			 << semantic.interning.syntax_tag_cache_hits
			 << " syntax_tag_cache_misses="
			 << semantic.interning.syntax_tag_cache_misses
			 << " source_file_cache_hits="
			 << semantic.interning.source_file_cache_hits
			 << " source_file_cache_misses="
			 << semantic.interning.source_file_cache_misses
			 << " syntax_nodes=" << semantic.syntax_nodes
			 << " semantic_nodes=" << semantic.semantic_nodes
			 << " semantic_edges=" << semantic.semantic_edges
			 << " declarations=" << semantic.declarations
			 << " canonical_types=" << semantic.canonical_types
			 << " scopes=" << semantic.scopes
			 << " lookup_queries=" << semantic.lookup_queries
			 << " lookup_scope_visits=" << semantic.lookup_scope_visits
			 << " lookup_edge_visits=" << semantic.lookup_edge_visits
			 << " lookup_cache_hits=" << semantic.lookup_cache_hits
			 << " lookup_cache_misses=" << semantic.lookup_cache_misses
			 << " lookup_cache_invalidations="
			 << semantic.lookup_cache_invalidations
			 << " lookup_cache_dependency_edges="
			 << semantic.lookup_cache_dependency_edges
			 << " lookup_cache_invalidation_pushes="
			 << semantic.lookup_cache_invalidation_pushes
			 << " overload_candidates=" << semantic.overload_candidates
			 << " overload_order_comparisons="
			 << semantic.overload_order_comparisons
			 << " function_candidate_index_visits="
			 << semantic.function_candidate_index_visits
			 << " conversion_checks=" << semantic.conversion_checks
			 << " call_conversion_cache_hits="
			 << semantic.call_conversion_cache_hits
			 << " call_conversion_cache_misses="
			 << semantic.call_conversion_cache_misses
			 << " braced_fact_cache_hits="
			 << semantic.braced_fact_cache_hits
			 << " braced_fact_cache_misses="
			 << semantic.braced_fact_cache_misses
			 << " template_requests=" << semantic.template_specialization_requests
			 << " template_cache_hits=" << semantic.template_specialization_cache_hits
			 << " constexpr_call_requests="
			 << semantic.constexpr_call_requests
			 << " constexpr_call_cache_hits="
			 << semantic.constexpr_call_cache_hits
			 << " constexpr_step_visits="
			 << semantic.constexpr_step_visits
			 << " demand_pushes=" << semantic.demand_worklist_pushes
			 << " demanded_functions=" << semantic.demanded_function_emissions
			 << " validation_only_completions="
			 << semantic.definition_validation_only_completions
			 << " emission_required_completions="
			 << semantic.definition_emission_required_completions
			 << " demand_requests=" << semantic.demand_requests
			 << " demand_unique_edges=" << semantic.demand_unique_edges
			 << " demand_root_edges=" << semantic.demand_root_edges
			 << " demand_dependency_edges="
			 << semantic.demand_dependency_edges
			 << " demand_replayed_functions="
			 << semantic.demand_replayed_functions
			 << " demand_replayed_edges="
			 << semantic.demand_replayed_edges
			 << " demand_evaluated="
			 << semantic.demand_evaluated_use_requests
			 << " demand_retained_calls="
			 << semantic.demand_retained_call_requests
			 << " demand_addresses=" << semantic.demand_address_requests
			 << " demand_lifecycle=" << semantic.demand_lifecycle_requests
			 << " demand_vtable=" << semantic.demand_vtable_requests
			 << " demand_static_lifecycle="
			 << semantic.demand_static_lifecycle_requests
			 << " demand_exception_cleanup="
			 << semantic.demand_exception_cleanup_requests
			 << " demand_explicit_instantiation="
			 << semantic.demand_explicit_instantiation_requests
			 << " demand_abi_support="
			 << semantic.demand_abi_support_requests
			 << " functions=" << stats.functions
			 << " globals=" << stats.globals
			 << " instructions=" << stats.instructions
			 << " force_inline_candidates=" << stats.force_inline_candidates
			 << " force_inline_recursive_candidates="
			 << stats.force_inline_recursive_candidates
			 << " force_inline_call_probes=" << stats.force_inline_call_probes
			 << " force_inline_calls=" << stats.force_inline_calls
			 << " force_inline_blocks=" << stats.force_inline_blocks
			 << " force_inline_cloned_instructions="
			 << stats.force_inline_cloned_instructions
			 << " post_inline_reachable_functions="
			 << stats.post_inline_reachable_functions
			 << " post_inline_unreachable_weak_functions="
			 << stats.post_inline_unreachable_weak_functions
			 << " semantic_program_bytes="
			 << semantic.semantic_program_storage_bytes
			 << " binding_layout_facts="
			 << semantic.binding_layout_fact_records
			 << " binding_template_facts="
			 << semantic.binding_template_fact_records
			 << " binding_output_facts="
			 << semantic.binding_output_fact_records
			 << " binding_operator_facts="
			 << semantic.binding_operator_fact_records
			 << " binding_value_records="
			 << semantic.binding_value_records
			 << " semantic_dump_bytes="
			 << semantic.semantic_dump_storage_bytes
			 << " semantic_side_bytes="
			 << semantic.semantic_side_storage_bytes
			 << " semantic_shared_string_bytes="
			 << semantic.semantic_shared_string_bytes
			 << " semantic_peak_bytes=" << semantic.peak_stage_storage_bytes
			 << " typed_bytes=" << stats.typed_storage_bytes
			 << " preprocess_ns="
			 << semantic.preprocessing.elapsed_nanoseconds
			 << " parse_ns=" << semantic.parse_nanoseconds
			 << " semantic_ns=" << semantic.analysis_nanoseconds
			 << " frontend_ns=" << semantic.elapsed_nanoseconds
			 << " lowering_ns=" << stats.lowering_nanoseconds
			 << " adapt_ns=" << adapt_nanoseconds << '\n';
		cerr << "pa37_prepare_stats"
			 << " file=" << path
			 << " reference_operand_visits="
			 << preparation_stats.reference_operand_visits
			 << " referenced_symbols=" << preparation_stats.referenced_symbols
			 << " declaration_visits=" << preparation_stats.declaration_visits
			 << " retained_declarations="
			 << preparation_stats.retained_declarations
			 << " function_order_visits="
			 << preparation_stats.function_order_visits
			 << " function_moves=" << preparation_stats.function_moves
			 << " function_copies=" << preparation_stats.function_copies
			 << " alias_order_visits="
			 << preparation_stats.alias_order_visits
			 << " alias_moves=" << preparation_stats.alias_moves
			 << " serialized_operand_visits="
			 << preparation_stats.serialized_operand_visits
			 << " derived_operand_visits="
			 << preparation_stats.derived_operand_visits
			 << " boundary_call_visits="
			 << preparation_stats.boundary_call_visits
			 << " exports=" << preparation_stats.exports
			 << " frontend_canonical_ns="
			 << preparation_stats.frontend_canonical_nanoseconds
			 << " serialized_canonical_ns="
			 << preparation_stats.serialized_canonical_nanoseconds
			 << " derived_facts_ns="
			 << preparation_stats.derived_facts_nanoseconds << '\n';
	}
  return object;
}

string find_library_object(const DriverInvocation & invocation,
                           const string & library)
{
  for(size_t i = 0; i < invocation.library_paths.size(); ++i) {
    string path = invocation.library_paths[i];
    if(!path.empty() && path[path.size() - 1] != '/') path.push_back('/');
    path += "lib" + library + ".o";
    if(regular_file_exists(path)) return path;
  }
  throw runtime_error("library not found: " + library);
}

int run_compile_driver(const DriverInvocation & invocation,
                       const string & target)
{
  if(invocation.inputs.size() != 1 || invocation.output.empty())
    throw logic_error("compile mode requires one input and -o");
  const cppgm::pa30::CompilerObject object =
      compile_source_object(invocation.inputs[0], invocation, target);
	cppgm::pa30::ObjectSerializationStats serialization_stats;
  lowir_native::Stats native_stats;
  const bool private_object =
      cppgm::pa30::UsesPrivateCompilerObjectFormat(invocation.output);
  if(private_object) {
    cppgm::pa30::WriteCompilerObject(
      invocation.output, object,
      invocation.collect_stats ? &serialization_stats : 0);
  } else {
    lowir_native::write_linux_relocatable(
      invocation.output, object.lowir, target,
      invocation.optimization_level,
      invocation.collect_stats ? &native_stats : 0);
  }
  if(invocation.collect_stats) {
    cerr << "pa31_object_stats"
         << " private_object=" << (private_object ? 1 : 0)
         << " functions=" << native_stats.functions
         << " lowir_instructions=" << native_stats.lowir_instructions
         << " mir_instructions=" << native_stats.mir_instructions
         << " machine_opt_input="
         << native_stats.machine_opt_input_instructions
         << " machine_opt_output="
         << native_stats.machine_opt_output_instructions
         << " machine_opt_visits="
         << native_stats.machine_opt_instruction_visits
         << " machine_opt_cfg_edges="
         << native_stats.machine_opt_cfg_edge_visits
         << " machine_opt_pushes="
         << native_stats.machine_opt_worklist_pushes
         << " machine_opt_rewrites=" << native_stats.machine_opt_rewrites
         << " machine_opt_peak_bytes="
         << native_stats.machine_opt_peak_analysis_bytes
         << " live_location_scans=" << native_stats.live_location_scans
         << " live_location_value_visits="
         << native_stats.live_location_value_visits
         << " live_location_alias_queries="
         << native_stats.live_location_alias_queries
         << " live_location_updates=" << native_stats.live_location_updates
         << " spill_attempts=" << native_stats.spill_attempts
         << " spill_value_visits=" << native_stats.spill_value_visits
         << " spill_candidates=" << native_stats.spill_candidates
         << " spill_full_scan_fallbacks="
         << native_stats.spill_full_scan_fallbacks
         << " spills=" << native_stats.spills
         << " reclaim_attempts=" << native_stats.reclaim_attempts
         << " reclaim_parameter_visits="
         << native_stats.reclaim_parameter_visits
         << " reclaims=" << native_stats.reclaims
         << " eh_region_states=" << native_stats.eh_region_states
         << " eh_region_edges=" << native_stats.eh_region_edges
         << " eh_call_sites=" << native_stats.eh_call_sites
         << " fixups=" << native_stats.fixups
         << " output_bytes=" << native_stats.output_bytes
         << " lower_ns=" << native_stats.lower_nanoseconds
         << " machine_opt_ns=" << native_stats.machine_opt_nanoseconds
         << " encode_ns=" << native_stats.encode_nanoseconds
         << " write_ns=" << native_stats.write_nanoseconds
		 << " payload_reserved_bytes=" << serialization_stats.reserved_bytes
		 << " payload_bytes=" << serialization_stats.output_bytes
		 << " payload_buffer_growths=" << serialization_stats.buffer_growths
		 << " payload_full_buffer_copies="
		 << serialization_stats.full_buffer_copies
		 << " payload_serialize_ns="
		 << serialization_stats.elapsed_nanoseconds << '\n';
  }
  return EXIT_SUCCESS;
}

int run_link_driver(const DriverInvocation & invocation,
                    const string & target)
{
  if(invocation.output.empty()) throw logic_error("link mode requires -o");
  const bool collect_stats = invocation.collect_stats;
  vector<cppgm::pa30::CompilerObject> objects;
  vector<lowir_native::RelocatableObject> foreign_objects;
	chrono::steady_clock::time_point input_started;
	if(collect_stats) input_started = chrono::steady_clock::now();
  for(size_t i = 0; i < invocation.inputs.size(); ++i) {
    if(cppgm::pa30::IsCompilerObject(invocation.inputs[i]))
      objects.push_back(cppgm::pa30::ReadCompilerObject(invocation.inputs[i]));
    else if(cppgm::pa30::UsesPrivateCompilerObjectFormat(
              invocation.inputs[i]) ||
            (invocation.inputs[i].size() >= 2 &&
             invocation.inputs[i].compare(
               invocation.inputs[i].size() - 2, 2, ".o") == 0))
      throw runtime_error(
        "native or invalid object cannot be linked by cppgm++: " +
        invocation.inputs[i]);
    else
      objects.push_back(compile_source_object(invocation.inputs[i], invocation,
                                              target));
  }
  for(size_t i = 0; i < invocation.libraries.size(); ++i) {
    const string path = find_library_object(invocation, invocation.libraries[i]);
    if(cppgm::pa30::IsCompilerObject(path))
      objects.push_back(cppgm::pa30::ReadCompilerObject(path));
    else
      foreign_objects.push_back(cppgm::pa30::ReadElfRelocatableObject(
          path, foreign_objects.size()));
  }
	uint64_t input_nanoseconds = 0;
	if(collect_stats) input_nanoseconds = static_cast<uint64_t>(
		chrono::duration_cast<chrono::nanoseconds>(
			chrono::steady_clock::now() - input_started).count());
  cppgm::pa30::LinkStats link_stats;
  const lowir_model::LowirProgram lowir = cppgm::pa30::LinkCompilerObjects(
      std::move(objects), target,
      collect_stats ? &link_stats : 0);
  lowir_native::Stats native_stats;
  lowir_native::write_linux_executable(invocation.output, lowir, target,
      foreign_objects, invocation.optimization_level,
      collect_stats ? &native_stats : 0);
  if(collect_stats) {
    cerr << "pa30_driver_stats"
         << " objects=" << link_stats.objects
         << " symbols=" << link_stats.symbols
         << " symbol_probes=" << link_stats.symbol_probes
		 << " rename_probes=" << link_stats.rename_probes
         << " definitions=" << link_stats.definitions
         << " weak_coalesces=" << link_stats.coalesced_weak_definitions
         << " functions=" << native_stats.functions
         << " lowir_instructions=" << native_stats.lowir_instructions
         << " mir_instructions=" << native_stats.mir_instructions
		 << " machine_opt_input="
		 << native_stats.machine_opt_input_instructions
		 << " machine_opt_output="
		 << native_stats.machine_opt_output_instructions
		 << " machine_opt_visits="
		 << native_stats.machine_opt_instruction_visits
		 << " machine_opt_cfg_edges="
		 << native_stats.machine_opt_cfg_edge_visits
		 << " machine_opt_pushes="
		 << native_stats.machine_opt_worklist_pushes
		 << " machine_opt_rewrites=" << native_stats.machine_opt_rewrites
			 << " machine_opt_peak_bytes="
			 << native_stats.machine_opt_peak_analysis_bytes
			 << " live_location_scans=" << native_stats.live_location_scans
			 << " live_location_value_visits="
			 << native_stats.live_location_value_visits
			 << " live_location_alias_queries="
			 << native_stats.live_location_alias_queries
			 << " live_location_updates=" << native_stats.live_location_updates
			 << " spill_attempts=" << native_stats.spill_attempts
			 << " spill_value_visits=" << native_stats.spill_value_visits
			 << " spill_candidates=" << native_stats.spill_candidates
			 << " spill_full_scan_fallbacks="
			 << native_stats.spill_full_scan_fallbacks
			 << " spills=" << native_stats.spills
			 << " reclaim_attempts=" << native_stats.reclaim_attempts
			 << " reclaim_parameter_visits="
			 << native_stats.reclaim_parameter_visits
			 << " reclaims=" << native_stats.reclaims
			 << " output_bytes=" << native_stats.output_bytes
		 << " input_ns=" << input_nanoseconds
		 << " link_ns=" << link_stats.link_nanoseconds
		 << " lower_ns=" << native_stats.lower_nanoseconds
		 << " machine_opt_ns=" << native_stats.machine_opt_nanoseconds
		 << " encode_ns=" << native_stats.encode_nanoseconds
		 << " write_ns=" << native_stats.write_nanoseconds << '\n';
  }
  return EXIT_SUCCESS;
}

cppgm::PreprocessingOptions make_preprocessing_options()
{
  const time_t now = time(0);
  const tm * local = localtime(&now);
  if(!local) {
    throw runtime_error("unable to determine build time");
  }
  const char * text = asctime(local);
  if(!text) {
    throw runtime_error("unable to format build time");
  }
  const string formatted(text);
  if(formatted.size() < 24) {
    throw runtime_error("invalid asctime result");
  }
  cppgm::PreprocessingOptions options;
  options.build_date = formatted.substr(4, 7) + formatted.substr(20, 4);
  options.build_time = formatted.substr(11, 8);
  options.author = "Vishvananda Abrams";
  return options;
}

int run_emit_ast_mode(const vector<string> & args)
{
  const SourceOutputInvocation invocation =
      parse_source_output_invocation(args, false);
  ofstream output(invocation.output.c_str(), ios::out | ios::trunc);
  if(!output) {
    throw runtime_error("unable to open output file: " + invocation.output);
  }

  const cppgm::PreprocessingOptions options = make_preprocessing_options();

  output << invocation.inputs.size() << " translation units\n";
  for(size_t i = 0; i < invocation.inputs.size(); ++i) {
    const string & path = invocation.inputs[i];
    ifstream input(path.c_str(), ios::in | ios::binary);
    if(!input) {
      throw runtime_error("unable to open source file: " + path);
    }
    const string source((istreambuf_iterator<char>(input)),
                        istreambuf_iterator<char>());
    output << "start translation unit " << i + 1 << '\n';
    cppgm::SyntaxStats stats;
    cppgm::WriteSyntaxTranslationUnit(path, source, options, output,
        invocation.collect_stats ? &stats : 0);
    if(invocation.collect_stats) {
      cerr << "pa10_stats file=" << path
           << " tokens=" << stats.tokens
           << " syntax_nodes=" << stats.syntax_nodes
           << " syntax_edges=" << stats.syntax_edges
           << " syntax_output_bytes=" << stats.syntax_output_bytes
           << " max_syntax_depth=" << stats.max_syntax_depth
           << " checkpoints=" << stats.parser_checkpoints
           << " rollbacks=" << stats.parser_rollbacks
           << " template_probes=" << stats.template_argument_probes
           << " template_scans=" << stats.template_argument_scans
           << " template_cache_hits="
           << stats.template_argument_cache_hits
           << " template_scan_tokens=" << stats.template_argument_scan_tokens
           << " max_template_scan_tokens="
           << stats.max_template_argument_scan_tokens
           << " failed_template_scans="
           << stats.failed_template_argument_scans
           << " parser_fact_changes=" << stats.parser_fact_changes
           << " parser_storage_bytes=" << stats.parser_storage_bytes
           << " render_stack_storage_bytes="
           << stats.render_stack_storage_bytes
           << " peak_stage_storage_bytes=" << stats.peak_stage_storage_bytes
           << " parse_ns=" << stats.parse_nanoseconds
           << " render_ns=" << stats.render_nanoseconds
           << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
    }
    output << "end translation unit\n";
  }
  if(!output) {
    throw runtime_error("unable to write output file: " + invocation.output);
  }
  return EXIT_SUCCESS;
}

int run_emit_types_mode(const vector<string> & args)
{
  const SourceOutputInvocation invocation =
      parse_source_output_invocation(args, false);
  ofstream output(invocation.output.c_str(), ios::out | ios::trunc);
  if(!output) {
    throw runtime_error("unable to open output file: " + invocation.output);
  }
  const cppgm::PreprocessingOptions options = make_preprocessing_options();
  output << invocation.inputs.size() << " translation units\n";
  for(size_t i = 0; i < invocation.inputs.size(); ++i) {
    const string & path = invocation.inputs[i];
    ifstream input(path.c_str(), ios::in | ios::binary);
    if(!input) {
      throw runtime_error("unable to open source file: " + path);
    }
    const string source((istreambuf_iterator<char>(input)),
                        istreambuf_iterator<char>());
    output << "start translation unit " << i + 1 << '\n';
    cppgm::TypeAnalysisStats stats;
    cppgm::WriteTypeTranslationUnit(path, source, options, output,
        invocation.collect_stats ? &stats : 0);
    if(invocation.collect_stats) {
      cerr << "pa11_stats file=" << path
           << " tokens=" << stats.tokens
           << " syntax_nodes=" << stats.syntax_nodes
           << " names=" << stats.interned_names
           << " canonical_types=" << stats.canonical_types
           << " scopes=" << stats.scopes
           << " declarations=" << stats.declarations
           << " lookup_queries=" << stats.lookup_queries
           << " lookup_scope_visits=" << stats.lookup_scope_visits
           << " lookup_edge_visits=" << stats.lookup_edge_visits
           << " name_index_probes=" << stats.name_index_probes
           << " type_index_probes=" << stats.type_index_probes
           << " using_index_probes=" << stats.using_index_probes
           << " rendered_type_nodes=" << stats.rendered_type_nodes
           << " max_scope_depth=" << stats.max_scope_depth
           << " render_stack_storage_bytes="
           << stats.render_stack_storage_bytes
           << " semantic_storage_bytes=" << stats.semantic_storage_bytes
           << " peak_stage_storage_bytes=" << stats.peak_stage_storage_bytes
           << " analysis_ns=" << stats.analysis_nanoseconds
           << " render_ns=" << stats.render_nanoseconds
           << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
    }
    output << "end translation unit\n";
  }
  if(!output) {
    throw runtime_error("unable to write output file: " + invocation.output);
  }
  return EXIT_SUCCESS;
}

int run_emit_semantics_mode(const vector<string> & args)
{
  const SourceOutputInvocation invocation =
      parse_source_output_invocation(args, false);
  ofstream output(invocation.output.c_str(), ios::out | ios::trunc);
  if(!output) {
    throw runtime_error("unable to open output file: " + invocation.output);
  }
  const cppgm::PreprocessingOptions options = make_preprocessing_options();
  output << invocation.inputs.size() << " translation units\n";
  for(size_t i = 0; i < invocation.inputs.size(); ++i) {
    const string & path = invocation.inputs[i];
    ifstream input(path.c_str(), ios::in | ios::binary);
    if(!input) {
      throw runtime_error("unable to open source file: " + path);
    }
    const string source((istreambuf_iterator<char>(input)),
                        istreambuf_iterator<char>());
    output << "start translation unit " << i + 1 << '\n';
    cppgm::SemanticAnalysisStats stats;
    cppgm::WriteSemanticTranslationUnit(path, source, options, output,
        invocation.collect_stats ? &stats : 0);
    if(invocation.collect_stats) {
      cerr << "pa12_stats file=" << path
           << " tokens=" << stats.tokens
           << " syntax_nodes=" << stats.syntax_nodes
           << " semantic_nodes=" << stats.semantic_nodes
           << " semantic_edges=" << stats.semantic_edges
           << " names=" << stats.interned_names
           << " canonical_types=" << stats.canonical_types
           << " scopes=" << stats.scopes
           << " declarations=" << stats.declarations
           << " expressions=" << stats.expressions
           << " class_layouts=" << stats.class_layouts
		   << " class_layout_member_visits="
		   << stats.class_layout_member_visits
		   << " virtual_base_layout_edge_visits="
		   << stats.virtual_base_layout_edge_visits
		   << " virtual_base_layout_facts="
		   << stats.virtual_base_layout_facts
		   << " virtual_base_layout_lookups="
		   << stats.virtual_base_layout_lookups
		   << " virtual_base_layout_probes="
		   << stats.virtual_base_layout_probes
		   << " direct_base_validation_visits="
		   << stats.direct_base_validation_visits
		   << " class_zero_offset_subobject_visits="
           << stats.class_zero_offset_subobject_visits
           << " special_member_fact_lookups="
           << stats.special_member_fact_lookups
           << " special_member_subobject_visits="
           << stats.special_member_subobject_visits
           << " constructor_member_action_visits="
           << stats.constructor_member_action_visits
		   << " constructor_base_action_visits="
		   << stats.constructor_base_action_visits
		   << " constructor_delegation_action_visits="
		   << stats.constructor_delegation_action_visits
		   << " destructor_subobject_action_visits="
		   << stats.destructor_subobject_action_visits
		   << " lexical_cleanup_action_visits="
		   << stats.lexical_cleanup_action_visits
		   << " unwind_cleanup_scope_visits="
		   << stats.unwind_cleanup_scope_visits
		   << " unwind_cleanup_action_visits="
		   << stats.unwind_cleanup_action_visits
		   << " initializer_list_lifetime_queries="
		   << stats.initializer_list_lifetime_queries
		   << " empty_constructor_chain_requests="
		   << stats.empty_constructor_chain_requests
		   << " empty_constructor_chain_cache_hits="
		   << stats.empty_constructor_chain_cache_hits
		   << " empty_constructor_chain_entity_visits="
		   << stats.empty_constructor_chain_entity_visits
		   << " empty_constructor_chain_dependency_edges="
		   << stats.empty_constructor_chain_dependency_edges
		   << " empty_destructor_chain_visits="
		   << stats.empty_destructor_chain_visits
		   << " empty_destructor_chain_cache_hits="
		   << stats.empty_destructor_chain_cache_hits
		   << " namespace_object_actions="
		   << stats.namespace_object_actions
           << " lookup_queries=" << stats.lookup_queries
           << " lookup_scope_visits=" << stats.lookup_scope_visits
           << " lookup_edge_visits=" << stats.lookup_edge_visits
           << " lookup_cache_hits=" << stats.lookup_cache_hits
           << " lookup_cache_misses=" << stats.lookup_cache_misses
           << " lookup_cache_invalidations="
           << stats.lookup_cache_invalidations
           << " lookup_cache_dependency_edges="
           << stats.lookup_cache_dependency_edges
           << " lookup_cache_invalidation_pushes="
           << stats.lookup_cache_invalidation_pushes
           << " base_path_queries=" << stats.base_path_queries
           << " base_path_cache_hits=" << stats.base_path_cache_hits
           << " base_path_cache_misses=" << stats.base_path_cache_misses
           << " base_path_edge_visits=" << stats.base_path_edge_visits
           << " virtual_base_path_visits="
           << stats.virtual_base_path_visits
           << " associated_scope_visits="
           << stats.associated_scope_visits
           << " associated_declaration_visits="
           << stats.associated_declaration_visits
           << " function_candidate_index_visits="
           << stats.function_candidate_index_visits
           << " overload_candidates=" << stats.overload_candidates
           << " overload_order_comparisons="
           << stats.overload_order_comparisons
           << " conversion_checks=" << stats.conversion_checks
           << " call_conversion_cache_hits="
           << stats.call_conversion_cache_hits
           << " call_conversion_cache_misses="
           << stats.call_conversion_cache_misses
           << " braced_fact_cache_hits=" << stats.braced_fact_cache_hits
           << " braced_fact_cache_misses=" << stats.braced_fact_cache_misses
           << " function_signature_lookups="
           << stats.function_signature_lookups
           << " polymorphic_classes=" << stats.polymorphic_classes
           << " virtual_slots=" << stats.virtual_slots
           << " virtual_signature_lookups="
           << stats.virtual_signature_lookups
		   << " virtual_overrides=" << stats.virtual_overrides
		   << " polymorphic_virtual_view_lookups="
		   << stats.polymorphic_virtual_view_lookups
		   << " polymorphic_virtual_view_merges="
		   << stats.polymorphic_virtual_view_merges
           << " virtual_slot_lookups=" << stats.virtual_slot_lookups
           << " vtable_demands=" << stats.vtable_demands
           << " access_checks=" << stats.access_checks
           << " access_path_visits=" << stats.access_path_visits
           << " access_grant_probes=" << stats.access_grant_probes
           << " template_specialization_requests="
           << stats.template_specialization_requests
           << " template_specialization_cache_hits="
           << stats.template_specialization_cache_hits
		   << " function_template_default_materializations="
		   << stats.function_template_default_materializations
		   << " function_template_default_request_cache_hits="
		   << stats.function_template_default_request_cache_hits
		   << " function_template_default_failure_cache_hits="
		   << stats.function_template_default_failure_cache_hits
		   << " function_template_exception_specification_requests="
		   << stats.function_template_exception_specification_requests
		   << " function_template_exception_specification_cache_hits="
		   << stats.function_template_exception_specification_cache_hits
		   << " function_template_exception_specification_evaluations="
		   << stats.function_template_exception_specification_evaluations
		   << " template_argument_list_requests="
		   << stats.template_argument_list_requests
		   << " template_argument_list_cache_hits="
		   << stats.template_argument_list_cache_hits
		   << " template_argument_list_index_probes="
		   << stats.template_argument_list_index_probes
		   << " template_partition_requests="
		   << stats.template_partition_requests
		   << " template_partition_cache_hits="
		   << stats.template_partition_cache_hits
		   << " template_partition_index_probes="
		   << stats.template_partition_index_probes
		   << " function_template_result_identity_requests="
		   << stats.function_template_result_identity_requests
		   << " function_template_result_identity_cache_hits="
		   << stats.function_template_result_identity_cache_hits
		   << " function_template_result_identity_index_probes="
		   << stats.function_template_result_identity_index_probes
		   << " function_template_result_identity_atom_visits="
		   << stats.function_template_result_identity_atom_visits
		   << " function_template_result_identity_syntax_visits="
		   << stats.function_template_result_identity_syntax_visits
		   << " function_template_result_identity_environment_probes="
		   << stats.function_template_result_identity_environment_probes
		   << " function_template_result_identity_alias_expansions="
		   << stats.function_template_result_identity_alias_expansions
		   << " template_partial_candidates="
		   << stats.template_partial_candidates
		   << " template_partial_order_comparisons="
		   << stats.template_partial_order_comparisons
		   << " template_partial_shape_materializations="
		   << stats.template_partial_shape_materializations
		   << " template_partial_shape_cache_hits="
		   << stats.template_partial_shape_cache_hits
			   << " template_partial_deduction_visits="
			   << stats.template_partial_deduction_visits
			   << " function_template_deduction_visits="
			   << stats.function_template_deduction_visits
			   << " lambda_closure_requests="
			   << stats.lambda_closure_requests
			   << " lambda_closure_cache_hits="
			   << stats.lambda_closure_cache_hits
			   << " lambda_capture_summary_requests="
			   << stats.lambda_capture_summary_requests
			   << " lambda_capture_summary_cache_hits="
			   << stats.lambda_capture_summary_cache_hits
			   << " lambda_capture_syntax_visits="
			   << stats.lambda_capture_syntax_visits
			   << " lambda_capture_name_uses="
			   << stats.lambda_capture_name_uses
	           << " constexpr_call_requests=" << stats.constexpr_call_requests
	           << " constexpr_call_cache_hits=" << stats.constexpr_call_cache_hits
	           << " constant_conversion_fact_requests="
	           << stats.constant_conversion_fact_requests
	           << " constant_conversion_fact_cache_hits="
	           << stats.constant_conversion_fact_cache_hits
	           << " constexpr_local_index_probes="
           << stats.constexpr_local_index_probes
           << " constexpr_scope_index_probes="
           << stats.constexpr_scope_index_probes
           << " constexpr_object_projection_visits="
           << stats.constexpr_object_projection_visits
           << " constexpr_step_visits=" << stats.constexpr_step_visits
           << " constexpr_max_depth=" << stats.constexpr_max_depth
           << " constexpr_peak_locals=" << stats.constexpr_peak_locals
           << " constexpr_scratch_peak_nodes="
           << stats.constexpr_scratch_peak_nodes
           << " demand_worklist_pushes=" << stats.demand_worklist_pushes
           << " demanded_function_emissions="
           << stats.demanded_function_emissions
           << " default_constructor_emissions="
           << stats.default_constructor_emissions
           << " demand_requests=" << stats.demand_requests
           << " demand_unique_edges=" << stats.demand_unique_edges
           << " demand_root_edges=" << stats.demand_root_edges
           << " demand_dependency_edges=" << stats.demand_dependency_edges
           << " demand_evaluated=" << stats.demand_evaluated_use_requests
           << " demand_retained_calls="
           << stats.demand_retained_call_requests
           << " demand_addresses=" << stats.demand_address_requests
           << " demand_lifecycle=" << stats.demand_lifecycle_requests
           << " demand_vtable=" << stats.demand_vtable_requests
           << " demand_static_lifecycle="
           << stats.demand_static_lifecycle_requests
           << " demand_exception_cleanup="
           << stats.demand_exception_cleanup_requests
           << " demand_explicit_instantiation="
           << stats.demand_explicit_instantiation_requests
           << " demand_abi_support=" << stats.demand_abi_support_requests
           << " semantic_storage_bytes=" << stats.semantic_storage_bytes
           << " peak_stage_storage_bytes=" << stats.peak_stage_storage_bytes
		   << " preprocess_ns=" << stats.preprocessing.elapsed_nanoseconds
		   << " parse_ns=" << stats.parse_nanoseconds
           << " analysis_ns=" << stats.analysis_nanoseconds
           << " render_ns=" << stats.render_nanoseconds
           << " elapsed_ns=" << stats.elapsed_nanoseconds << '\n';
    }
    output << "end translation unit\n";
  }
  if(!output) {
    throw runtime_error("unable to write output file: " + invocation.output);
  }
  return EXIT_SUCCESS;
}

void report_lowir_semantic_stats(const cppgm::LowIRLoweringStats & stats);
void report_lowir_lowering_stats(const cppgm::LowIRLoweringStats & stats);

int run_emit_lowir_mode(const vector<string> & args)
{
	const SourceOutputInvocation invocation =
		parse_source_output_invocation(args, true);
	ofstream output(invocation.output.c_str(), ios::out | ios::trunc);
	if(!output) {
		throw runtime_error("unable to open output file: " + invocation.output);
	}
	cppgm::PreprocessingOptions options = make_preprocessing_options();
	vector<cppgm::LowIRSource> sources;
	for(size_t i = 0; i < invocation.inputs.size(); ++i) {
		const string & path = invocation.inputs[i];
		ifstream input(path.c_str(), ios::in | ios::binary);
		if(!input) throw runtime_error("unable to open source file: " + path);
		sources.push_back(cppgm::LowIRSource(path,
			string((istreambuf_iterator<char>(input)), istreambuf_iterator<char>())));
	}
	cppgm::LowIRLoweringStats stats;
	const bool object_capable_output = invocation.has_debug_info ||
		invocation.optimization_level != 0;
	if(!object_capable_output) {
		cppgm::WriteLowIRProgram(sources, options, output,
			invocation.collect_stats ? &stats : 0);
	} else {
		cppgm::ConfigureHostedPreprocessing(&options, true);
		lowir_model::LowirProgram program;
		{
			const cppgm::pa15_lowir_detail::TypedProgram typed =
				cppgm::BuildTypedLowIRProgram(sources, options,
					invocation.collect_stats ? &stats : 0, true, true);
			program = cppgm::AdaptTypedLowIRForNative(typed);
		}
		if(invocation.line_tables && sources.size() == 1)
			attach_line_table_debug(&program, sources[0].path, sources[0].source);
		optimize_lowir(&program, invocation.optimization_level,
			sources.size() == 1 ? sources[0].path : "<translation-unit>",
			invocation.collect_stats);
		output << lowir_model::serialize_lowir_program(program);
	}
	if(invocation.collect_stats) {
		report_lowir_semantic_stats(stats);
		report_lowir_lowering_stats(stats);
	}
	return EXIT_SUCCESS;
}

void report_lowir_semantic_stats(const cppgm::LowIRLoweringStats & stats)
{
	const cppgm::SemanticAnalysisStats & semantic = stats.semantic;
	cerr << "pa15_stats"
			 << " source_bytes=" << stats.source_bytes
			 << " tokens=" << semantic.tokens
			 << " scopes=" << semantic.scopes
			 << " declarations=" << semantic.declarations
			 << " semantic_nodes=" << semantic.semantic_nodes
			 << " semantic_edges=" << semantic.semantic_edges
			 << " lowered_nodes=" << stats.lowered_nodes
			 << " class_layouts=" << semantic.class_layouts
			 << " class_layout_member_visits="
			 << semantic.class_layout_member_visits
			 << " virtual_base_layout_edge_visits="
			 << semantic.virtual_base_layout_edge_visits
			 << " virtual_base_layout_facts="
			 << semantic.virtual_base_layout_facts
			 << " virtual_base_layout_lookups="
			 << semantic.virtual_base_layout_lookups
			 << " virtual_base_layout_probes="
			 << semantic.virtual_base_layout_probes
			 << " direct_base_validation_visits="
			 << semantic.direct_base_validation_visits
			 << " class_zero_offset_subobject_visits="
			 << semantic.class_zero_offset_subobject_visits
			 << " special_member_fact_lookups="
			 << semantic.special_member_fact_lookups
			 << " special_member_subobject_visits="
			 << semantic.special_member_subobject_visits
			 << " constructor_member_action_visits="
			 << semantic.constructor_member_action_visits
			 << " constructor_base_action_visits="
			 << semantic.constructor_base_action_visits
			 << " constructor_delegation_action_visits="
			 << semantic.constructor_delegation_action_visits
			 << " destructor_subobject_action_visits="
			 << semantic.destructor_subobject_action_visits
			 << " lexical_cleanup_action_visits="
			 << semantic.lexical_cleanup_action_visits
			 << " unwind_cleanup_scope_visits="
			 << semantic.unwind_cleanup_scope_visits
			 << " unwind_cleanup_action_visits="
			 << semantic.unwind_cleanup_action_visits
			 << " enclosing_lifetime_queries="
			 << semantic.enclosing_lifetime_queries
			 << " initializer_list_lifetime_queries="
			 << semantic.initializer_list_lifetime_queries
			 << " temporary_dependency_visits="
			 << semantic.temporary_dependency_visits
			 << " materialized_demand_visits="
			 << semantic.materialized_demand_visits
			 << " nonthrowing_action_visits="
			 << semantic.nonthrowing_action_visits
			 << " runtime_initializer_visits="
			 << semantic.runtime_initializer_visits
			 << " static_constant_initializer_visits="
			 << semantic.static_constant_initializer_visits
			 << " static_constant_dependency_edges="
			 << semantic.static_constant_dependency_edges
			 << " empty_constructor_chain_requests="
			 << semantic.empty_constructor_chain_requests
			 << " empty_constructor_chain_cache_hits="
			 << semantic.empty_constructor_chain_cache_hits
			 << " empty_constructor_chain_entity_visits="
			 << semantic.empty_constructor_chain_entity_visits
			 << " empty_constructor_chain_dependency_edges="
			 << semantic.empty_constructor_chain_dependency_edges
			 << " empty_destructor_chain_visits="
			 << semantic.empty_destructor_chain_visits
			 << " empty_destructor_chain_cache_hits="
			 << semantic.empty_destructor_chain_cache_hits
			 << " namespace_object_actions="
			 << semantic.namespace_object_actions
			 << " lookup_queries=" << semantic.lookup_queries
			 << " lookup_scope_visits=" << semantic.lookup_scope_visits
			 << " lookup_edge_visits=" << semantic.lookup_edge_visits
			 << " lookup_cache_hits=" << semantic.lookup_cache_hits
			 << " lookup_cache_misses=" << semantic.lookup_cache_misses
			 << " lookup_cache_invalidations="
			 << semantic.lookup_cache_invalidations
			 << " lookup_cache_dependency_edges="
			 << semantic.lookup_cache_dependency_edges
			 << " lookup_cache_invalidation_pushes="
			 << semantic.lookup_cache_invalidation_pushes
			 << " base_path_queries=" << semantic.base_path_queries
			 << " base_path_cache_hits="
			 << semantic.base_path_cache_hits
			 << " base_path_cache_misses="
			 << semantic.base_path_cache_misses
			 << " base_path_edge_visits="
			 << semantic.base_path_edge_visits
			 << " virtual_base_path_visits="
			 << semantic.virtual_base_path_visits
			 << " associated_scope_visits="
			 << semantic.associated_scope_visits
			 << " associated_declaration_visits="
			 << semantic.associated_declaration_visits
			 << " function_candidate_index_visits="
			 << semantic.function_candidate_index_visits
			 << " overload_candidates=" << semantic.overload_candidates
			 << " overload_order_comparisons="
			 << semantic.overload_order_comparisons
			 << " conversion_checks=" << semantic.conversion_checks
			 << " call_conversion_cache_hits="
			 << semantic.call_conversion_cache_hits
			 << " call_conversion_cache_misses="
			 << semantic.call_conversion_cache_misses
			 << " braced_fact_cache_hits=" << semantic.braced_fact_cache_hits
			 << " braced_fact_cache_misses=" << semantic.braced_fact_cache_misses
			 << " function_signature_lookups="
			 << semantic.function_signature_lookups
			 << " polymorphic_classes=" << semantic.polymorphic_classes
			 << " virtual_slots=" << semantic.virtual_slots
			 << " virtual_signature_lookups="
			 << semantic.virtual_signature_lookups
			 << " virtual_overrides=" << semantic.virtual_overrides
			 << " polymorphic_virtual_view_lookups="
			 << semantic.polymorphic_virtual_view_lookups
			 << " polymorphic_virtual_view_merges="
			 << semantic.polymorphic_virtual_view_merges
			 << " virtual_slot_lookups="
			 << semantic.virtual_slot_lookups
			 << " vtable_demands=" << semantic.vtable_demands
			 << " access_checks=" << semantic.access_checks
			 << " access_path_visits=" << semantic.access_path_visits
			 << " access_grant_probes=" << semantic.access_grant_probes
			 << " template_specialization_requests="
			 << semantic.template_specialization_requests
			 << " template_specialization_cache_hits="
			 << semantic.template_specialization_cache_hits
			 << " function_template_default_materializations="
			 << semantic.function_template_default_materializations
			 << " function_template_default_request_cache_hits="
			 << semantic.function_template_default_request_cache_hits
			 << " function_template_default_failure_cache_hits="
			 << semantic.function_template_default_failure_cache_hits
			 << " function_template_exception_specification_requests="
			 << semantic.function_template_exception_specification_requests
			 << " function_template_exception_specification_cache_hits="
			 << semantic.function_template_exception_specification_cache_hits
			 << " function_template_exception_specification_evaluations="
			 << semantic.function_template_exception_specification_evaluations
			 << " template_argument_list_requests="
			 << semantic.template_argument_list_requests
			 << " template_argument_list_cache_hits="
			 << semantic.template_argument_list_cache_hits
			 << " template_argument_list_index_probes="
			 << semantic.template_argument_list_index_probes
			 << " template_partition_requests="
			 << semantic.template_partition_requests
			 << " template_partition_cache_hits="
			 << semantic.template_partition_cache_hits
			 << " template_partition_index_probes="
			 << semantic.template_partition_index_probes
			 << " function_template_result_identity_requests="
			 << semantic.function_template_result_identity_requests
			 << " function_template_result_identity_cache_hits="
			 << semantic.function_template_result_identity_cache_hits
			 << " function_template_result_identity_index_probes="
			 << semantic.function_template_result_identity_index_probes
			 << " function_template_result_identity_atom_visits="
			 << semantic.function_template_result_identity_atom_visits
			 << " function_template_result_identity_syntax_visits="
			 << semantic.function_template_result_identity_syntax_visits
			 << " function_template_result_identity_environment_probes="
			 << semantic.function_template_result_identity_environment_probes
			 << " function_template_result_identity_alias_expansions="
			 << semantic.function_template_result_identity_alias_expansions
			 << " template_partial_candidates="
			 << semantic.template_partial_candidates
			 << " template_partial_order_comparisons="
			 << semantic.template_partial_order_comparisons
			 << " template_partial_shape_materializations="
			 << semantic.template_partial_shape_materializations
			 << " template_partial_shape_cache_hits="
			 << semantic.template_partial_shape_cache_hits;
}

void report_lowir_lowering_stats(const cppgm::LowIRLoweringStats & stats)
{
	const cppgm::SemanticAnalysisStats & semantic = stats.semantic;
	cerr << " template_partial_deduction_visits="
				 << semantic.template_partial_deduction_visits
				 << " function_template_deduction_visits="
				 << semantic.function_template_deduction_visits
				 << " lambda_closure_requests="
				 << semantic.lambda_closure_requests
				 << " lambda_closure_cache_hits="
				 << semantic.lambda_closure_cache_hits
				 << " lambda_capture_summary_requests="
				 << semantic.lambda_capture_summary_requests
				 << " lambda_capture_summary_cache_hits="
				 << semantic.lambda_capture_summary_cache_hits
				 << " lambda_capture_syntax_visits="
				 << semantic.lambda_capture_syntax_visits
				 << " lambda_capture_name_uses="
				 << semantic.lambda_capture_name_uses
				 << " constexpr_call_requests="
			 << semantic.constexpr_call_requests
				 << " constexpr_call_cache_hits="
			 << semantic.constexpr_call_cache_hits
			 << " constant_conversion_fact_requests="
			 << semantic.constant_conversion_fact_requests
			 << " constant_conversion_fact_cache_hits="
			 << semantic.constant_conversion_fact_cache_hits
			 << " constexpr_local_index_probes="
			 << semantic.constexpr_local_index_probes
			 << " constexpr_scope_index_probes="
			 << semantic.constexpr_scope_index_probes
			 << " constexpr_object_projection_visits="
			 << semantic.constexpr_object_projection_visits
			 << " constexpr_step_visits="
			 << semantic.constexpr_step_visits
			 << " constexpr_max_depth="
			 << semantic.constexpr_max_depth
			 << " constexpr_peak_locals="
			 << semantic.constexpr_peak_locals
			 << " constexpr_scratch_peak_nodes="
			 << semantic.constexpr_scratch_peak_nodes
			 << " demand_worklist_pushes=" << semantic.demand_worklist_pushes
			 << " demanded_function_emissions="
			 << semantic.demanded_function_emissions
			 << " default_constructor_emissions="
			 << semantic.default_constructor_emissions
			 << " demand_requests=" << semantic.demand_requests
			 << " demand_unique_edges=" << semantic.demand_unique_edges
			 << " demand_root_edges=" << semantic.demand_root_edges
			 << " demand_dependency_edges="
			 << semantic.demand_dependency_edges
			 << " demand_evaluated="
			 << semantic.demand_evaluated_use_requests
			 << " demand_retained_calls="
			 << semantic.demand_retained_call_requests
			 << " demand_addresses=" << semantic.demand_address_requests
			 << " demand_lifecycle=" << semantic.demand_lifecycle_requests
			 << " demand_vtable=" << semantic.demand_vtable_requests
			 << " demand_static_lifecycle="
			 << semantic.demand_static_lifecycle_requests
			 << " demand_exception_cleanup="
			 << semantic.demand_exception_cleanup_requests
			 << " demand_explicit_instantiation="
			 << semantic.demand_explicit_instantiation_requests
			 << " demand_abi_support="
			 << semantic.demand_abi_support_requests
			 << " functions=" << stats.functions
			 << " globals=" << stats.globals
			 << " blocks=" << stats.blocks
			 << " instructions=" << stats.instructions
			 << " binding_index_probes=" << stats.binding_index_probes
			 << " slot_implicit_object_fact_reads="
			 << stats.slot_implicit_object_fact_reads
			 << " virtual_calls=" << stats.virtual_calls
			 << " vptr_stores=" << stats.vptr_stores
			 << " virtual_base_boundary_scan_nodes="
			 << stats.virtual_base_boundary_scan_nodes
			 << " virtual_base_boundary_facts="
			 << stats.virtual_base_boundary_facts
			 << " virtual_base_call_arguments="
			 << stats.virtual_base_call_arguments
			 << " virtual_base_boundary_binding_steps="
			 << stats.virtual_base_boundary_binding_steps
			 << " virtual_base_boundary_binding_cache_hits="
			 << stats.virtual_base_boundary_binding_cache_hits
			 << " virtual_base_boundary_binding_table_growth="
			 << stats.virtual_base_boundary_binding_table_growth
			 << " vtable_offset_rows=" << stats.vtable_offset_rows
			 << " vtable_slots=" << stats.vtable_slots
			 << " vtable_thunk_requests=" << stats.vtable_thunk_requests
			 << " vtable_thunk_cache_hits="
			 << stats.vtable_thunk_cache_hits
			 << " vtable_thunk_index_probes="
			 << stats.vtable_thunk_index_probes
			 << " deleting_destructors=" << stats.deleting_destructors
			 << " rtti_graph_nodes_visited="
			 << stats.rtti_graph_nodes_visited
			 << " rtti_demand_requests=" << stats.rtti_demand_requests
			 << " rtti_types_demanded=" << stats.rtti_types_demanded
			 << " rtti_symbol_lookups=" << stats.rtti_symbol_lookups
			 << " rtti_base_dependency_visits="
			 << stats.rtti_base_dependency_visits
			 << " cleanup_dispatch_probes="
			 << stats.cleanup_dispatch_probes
			 << " cleanup_dispatch_cache_hits="
			 << stats.cleanup_dispatch_cache_hits
			 << " cleanup_dispatch_entries="
			 << stats.cleanup_dispatch_entries
			 << " conditional_lifetime_slots="
			 << stats.conditional_lifetime_slots
			 << " conditional_lifetime_marks="
			 << stats.conditional_lifetime_marks
				<< " branch_cleanup_actions="
				<< stats.branch_cleanup_actions
				<< " statement_scheduler_entries="
				<< stats.statement_scheduler_entries
				<< " statement_scheduler_nested_entries="
				<< stats.statement_scheduler_nested_entries
				<< " statement_scheduler_tasks="
				<< stats.statement_scheduler_tasks
				<< " statement_scheduler_peak_tasks="
				<< stats.statement_scheduler_peak_tasks
				<< " exception_selector_resets="
			 << stats.exception_selector_resets
			 << " exception_selector_table_growth="
			 << stats.exception_selector_table_growth
			 << " exception_selector_assignments="
			 << stats.exception_selector_assignments
			 << " force_inline_candidates=" << stats.force_inline_candidates
			 << " force_inline_recursive_candidates="
			 << stats.force_inline_recursive_candidates
			 << " force_inline_call_probes=" << stats.force_inline_call_probes
			 << " force_inline_calls=" << stats.force_inline_calls
			 << " force_inline_blocks=" << stats.force_inline_blocks
			 << " force_inline_cloned_instructions="
			 << stats.force_inline_cloned_instructions
			 << " typed_storage_bytes=" << stats.typed_storage_bytes
			 << " semantic_peak_stage_bytes="
			 << semantic.peak_stage_storage_bytes
			 << " output_bytes=" << stats.output_bytes
			 << " preprocess_ns="
			 << semantic.preprocessing.elapsed_nanoseconds
			 << " parse_ns=" << semantic.parse_nanoseconds
			 << " semantic_ns=" << semantic.analysis_nanoseconds
			 << " frontend_ns=" << semantic.elapsed_nanoseconds
			 << " lowering_ns=" << stats.lowering_nanoseconds
			 << " render_ns=" << stats.render_nanoseconds << '\n';
}

int run_driver_mode(const vector<string> & args)
{
  const DriverInvocation invocation = parse_driver_invocation(args);
  const string target = normalize_native_target(invocation.target);
  switch(invocation.mode) {
  case DriverMode::Query:
		return run_query_driver(invocation.query);
  case DriverMode::Preprocess:
		return run_preprocess_driver(invocation);
  case DriverMode::Compile:
    return run_compile_driver(invocation, target);
  case DriverMode::Link:
    return run_link_driver(invocation, target);
  }
  throw logic_error("unreachable driver mode");
}

int run_cppgm(const vector<string> & raw_args)
{
  if(has_arg(raw_args, "--batch-stdin")) {
    return run_not_implemented_batch_mode();
  }

  if(has_help_arg(raw_args)) {
    cout << cppgm_help_text();
    return EXIT_SUCCESS;
  }

  vector<string> args = raw_args;
  const EmitMode mode = parse_emit_mode(args);

  switch(mode) {
  case EmitMode::Ast:
    return run_emit_ast_mode(args);
  case EmitMode::Types:
    return run_emit_types_mode(args);
  case EmitMode::Semantics:
    return run_emit_semantics_mode(args);
  case EmitMode::LowIR:
    return run_emit_lowir_mode(args);
  case EmitMode::None:
    return run_driver_mode(args);
  }

  throw logic_error("unreachable emit mode");
}

}  // namespace

int main(int argc, char ** argv)
{
  try
  {
    return run_cppgm(collect_args(argc, argv));
  }
  catch(const NotImplementedException & e)
  {
    cerr << "ERROR: " << e.what() << endl;
    return CPPGM_EXIT_NOT_IMPLEMENTED;
  }
  catch(const exception & e)
  {
    cerr << "ERROR: " << e.what() << endl;
    return EXIT_FAILURE;
  }
}
