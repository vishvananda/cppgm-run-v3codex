#include "lowir/io/line_table_debug.h"

#include <cctype>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

namespace lowir_line_table_debug {
namespace {

lowir_model::InstructionDebugLocation source_location(
    lowir_model::StringId file, size_t line, size_t column)
{
	lowir_model::InstructionDebugLocation result;
	result.file = file;
	result.line = line;
	result.column = column;
	return result;
}

size_t first_source_column(const string & line)
{
	const size_t found = line.find_first_not_of(" \t");
	return found == string::npos ? 1 : found + 1;
}

}  // namespace

void attach_line_table_debug(lowir_model::LowirProgram * program,
	const string & path, const string & source)
{
	const lowir_model::StringId debug_file = program->strings.intern(path);
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
		const string& lowir_name = lowir_model::lowir_symbol_name(
			*program, function.symbol);
		string source_name = lowir_name;
		const size_t separator = source_name.find("__");
		if(separator != string::npos) source_name.erase(separator);
		size_t function_line = 0;
		WordOccurrence function_occurrence;
		if(find_word(source_name, 0, true, false, &function_occurrence))
			function_line = function_occurrence.line;
		function.debug_location = source_location(debug_file, function_line + 1,
			first_source_column(lines[function_line]));
		unordered_set<string> parameters;
		for(size_t i = 0; i < function.params.size(); ++i)
			parameters.insert(lowir_model::lowir_parameter_name(
				*program, function.params[i]));
		struct LocalLocation { size_t line = 0; size_t statement = 1; size_t rhs = 1; };
		unordered_map<string, LocalLocation> locals;
		for(size_t i = 0; i < function.slots.size(); ++i) {
			const string name = lowir_model::lowir_slot_name(
				program->strings, function, function.slots[i]);
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
			source_location(debug_file, return_line + 1,
				first_source_column(lines[return_line]));
		unordered_set<string> used_value_names = parameters;
		for(size_t i = 0; i < function.value_names.size(); ++i) {
			const lowir_model::PresentationName presentation =
				lowir_model::lowir_value_presentation(
					function, lowir_model::ValueId(static_cast<uint32_t>(i)));
			if(presentation.valid()) used_value_names.insert(
				lowir_model::lowir_value_name(program->strings, function,
					lowir_model::ValueId(static_cast<uint32_t>(i))));
		}
		vector<size_t> debug_value_ordinals(function.slot_names.size(), 0);
		for(size_t b = 0; b < function.blocks.size(); ++b) {
			vector<lowir_model::Instruction> with_debug;
			for(size_t j = 0; j < function.blocks[b].instructions.size(); ++j) {
				lowir_model::Instruction ins = function.blocks[b].instructions[j];
				if(ins.kind == lowir_model::Instruction::IK_RETURN)
					ins.debug_location = return_loc;
					else if(ins.kind == lowir_model::Instruction::IK_STORE &&
							ins.second.kind == lowir_model::Operand::OP_SLOT) {
						const string slot = lowir_model::lowir_slot_name(
							program->strings, function, ins.second.slot);
					if(parameters.count(slot)) ins.debug_location = function_loc;
					else if(locals.count(slot)) {
						const LocalLocation & loc = locals[slot];
						ins.debug_location = source_location(debug_file, loc.line + 1,
							loc.statement);
							lowir_model::Instruction copy;
							copy.kind = lowir_model::Instruction::IK_COPY;
							copy.type = ins.type;
							string debug_name;
							do {
								debug_name = "dbg_" + slot + "__" + to_string(
									++debug_value_ordinals[ins.second.slot]);
							} while(!used_value_names.insert(debug_name).second);
							copy.dest = lowir_model::append_lowir_value(function,
								program->strings.intern(debug_name),
								copy.type, true);
						copy.first = ins.first;
						copy.debug_location = ins.debug_location;
							lowir_model::Operand debug_value;
							debug_value.kind = lowir_model::Operand::OP_TEMP;
							debug_value.value = copy.dest;
						ins.first = std::move(debug_value);
						with_debug.push_back(copy);
					}
					} else if(ins.kind == lowir_model::Instruction::IK_LOAD &&
							  ins.first.kind == lowir_model::Operand::OP_SLOT) {
						const string slot = lowir_model::lowir_slot_name(
							program->strings, function, ins.first.slot);
					if(locals.count(slot)) ins.debug_location = return_loc;
					else if(!locals.empty()) {
						const LocalLocation & loc = locals.begin()->second;
						ins.debug_location = source_location(debug_file, loc.line + 1,
							loc.statement);
					}
				} else if(ins.kind == lowir_model::Instruction::IK_BINARY &&
						  !locals.empty()) {
					const LocalLocation & loc = locals.begin()->second;
					ins.debug_location = source_location(debug_file, loc.line + 1, loc.rhs);
				}
				with_debug.push_back(ins);
			}
			function.blocks[b].instructions.swap(with_debug);
		}
	}
}

}  // namespace lowir_line_table_debug
