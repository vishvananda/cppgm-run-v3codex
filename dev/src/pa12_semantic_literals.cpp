#include "pa12_semantic_detail.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

std::int64_t SemanticAnalyzer::ParseInteger(const std::string& spelling) const
{
	const std::size_t quote = spelling.find('\'');
	if (quote != std::string::npos)
	{
		const std::size_t close = spelling.rfind('\'');
		if (close == quote || close + 1 != spelling.size())
			throw std::runtime_error("invalid character literal");
		unsigned long long value = 0;
		std::size_t count = 0;
		for (std::size_t i = quote + 1; i < close; ++i)
		{
			unsigned int character = static_cast<unsigned char>(spelling[i]);
			if (spelling[i] == '\\')
			{
				if (++i >= close)
					throw std::runtime_error("invalid character escape");
				const char escaped = spelling[i];
				if (escaped == 'a') character = 7;
				else if (escaped == 'b') character = 8;
				else if (escaped == 'f') character = 12;
				else if (escaped == 'n') character = 10;
				else if (escaped == 'r') character = 13;
				else if (escaped == 't') character = 9;
				else if (escaped == 'v') character = 11;
				else if (escaped == 'x')
				{
					character = 0;
					std::size_t digits = 0;
					while (i + 1 < close)
					{
						const char digit = spelling[i + 1];
						const int nibble = digit >= '0' && digit <= '9' ? digit - '0' :
							digit >= 'a' && digit <= 'f' ? digit - 'a' + 10 :
							digit >= 'A' && digit <= 'F' ? digit - 'A' + 10 : -1;
						if (nibble < 0) break;
						character = (character << 4) |
							static_cast<unsigned int>(nibble);
						++i;
						++digits;
					}
					if (digits == 0)
						throw std::runtime_error(
							"empty hexadecimal character escape");
				}
				else if (escaped >= '0' && escaped <= '7')
				{
					character = static_cast<unsigned int>(escaped - '0');
					for (std::size_t digits = 1; digits < 3 && i + 1 < close &&
						spelling[i + 1] >= '0' && spelling[i + 1] <= '7';
						++digits)
						character = (character << 3) |
							static_cast<unsigned int>(spelling[++i] - '0');
				}
				else character = static_cast<unsigned char>(escaped);
			}
			value = (value << 8) | (character & 0xffU);
			++count;
		}
		if (count == 0 || value > static_cast<unsigned long long>(INT64_MAX))
			throw std::runtime_error("character literal outside PA12 range");
		return static_cast<std::int64_t>(value);
	}
	std::size_t last = spelling.size();
	while (last != 0 && (spelling[last - 1] == 'u' ||
		spelling[last - 1] == 'U' || spelling[last - 1] == 'l' ||
		spelling[last - 1] == 'L')) --last;
	const std::string digits = spelling.substr(0, last);
	errno = 0;
	char* end = 0;
	const unsigned long long value = std::strtoull(digits.c_str(), &end, 0);
	if (errno == ERANGE || end == digits.c_str() || *end != '\0' ||
		value > static_cast<unsigned long long>(INT64_MAX))
		throw std::runtime_error("integer literal outside PA12 range");
	return static_cast<std::int64_t>(value);
}

NameId SemanticAnalyzer::InternNumber(std::int64_t value)
{
	return program_->names.Intern(std::to_string(value));
}

std::int64_t SemanticAnalyzer::ApplyConstantBinary(
	const std::string& operation, std::int64_t left, std::int64_t right) const
{
	if (operation == "+") return left + right;
	if (operation == "-") return left - right;
	if (operation == "*") return left * right;
	if (operation == "/")
	{
		if (right == 0) throw std::runtime_error("division by zero");
		return left / right;
	}
	if (operation == "%")
	{
		if (right == 0) throw std::runtime_error("division by zero");
		return left % right;
	}
	if (operation == "<<") return left << right;
	if (operation == ">>") return left >> right;
	if (operation == "&") return left & right;
	if (operation == "|") return left | right;
	if (operation == "^") return left ^ right;
	if (operation == "==") return left == right;
	if (operation == "!=") return left != right;
	if (operation == "<") return left < right;
	if (operation == ">") return left > right;
	if (operation == "<=") return left <= right;
	if (operation == ">=") return left >= right;
	if (operation == "&&") return left && right;
	if (operation == "||") return left || right;
	if (operation == ",") return right;
	throw std::runtime_error("unsupported constant binary operator");
}

ExpressionInfo SemanticAnalyzer::MakeLiteral(TypeId type, NameId text,
	ValueCategory category)
{
	ExpressionInfo result;
	result.type = type;
	result.category = category;
	result.node = MakeDump(DUMP_LITERAL, type, category, text);
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::MakeStringLiteral(
	const std::string& spelling, std::size_t* character_count)
{
	if (spelling.size() < 2 || spelling[0] != '"' ||
		spelling[spelling.size() - 1] != '"')
		throw std::runtime_error("invalid string literal spelling");
	std::size_t count = 1;
	for (std::size_t i = 1; i + 1 < spelling.size(); ++i)
	{
		if (spelling[i] == '\\' && i + 2 < spelling.size())
		{
			++i;
			if (spelling[i] == 'x')
				while (i + 2 < spelling.size() &&
					((spelling[i + 1] >= '0' && spelling[i + 1] <= '9') ||
					 (spelling[i + 1] >= 'a' && spelling[i + 1] <= 'f') ||
					 (spelling[i + 1] >= 'A' && spelling[i + 1] <= 'F'))) ++i;
			else if (spelling[i] >= '0' && spelling[i] <= '7')
				for (int digits = 1; digits < 3 && i + 2 < spelling.size() &&
					spelling[i + 1] >= '0' && spelling[i + 1] <= '7';
					++digits) ++i;
		}
		++count;
	}
	if (character_count) *character_count = count - 1;
	const TypeId element = program_->types.Qualify(
		program_->types.Fundamental(FUND_CHAR), CV_CONST);
	return MakeLiteral(program_->types.Array(element, count),
		program_->names.Intern(spelling), VALUE_LVALUE);
}

bool SemanticAnalyzer::TryAnalyzeUserDefinedStringLiteral(
	const std::string& spelling, ScopeId scope, TypeId target,
	ExpressionInfo* result)
{
	if (spelling.empty() || spelling[0] != '"') return false;
	std::size_t close = 1;
	for (; close < spelling.size(); ++close)
	{
		if (spelling[close] == '\\')
		{
			if (++close >= spelling.size())
				throw std::runtime_error("unterminated string literal escape");
			continue;
		}
		if (spelling[close] == '"') break;
	}
	if (close >= spelling.size() || close + 1 == spelling.size()) return false;
	const std::string suffix = spelling.substr(close + 1);
	if (suffix.empty() || suffix[0] != '_')
		throw std::runtime_error("invalid user-defined literal suffix");
	const std::string function_name = "operator\"\"" + suffix;
	const std::vector<BindingId> candidates =
		FunctionCandidates(scope, function_name);
	if (candidates.empty())
		throw std::runtime_error("user-defined literal operator not found");
	std::size_t character_count = 0;
	std::vector<ExpressionInfo> arguments;
	arguments.push_back(MakeStringLiteral(
		spelling.substr(0, close + 1), &character_count));
	ExpressionInfo count = MakeLiteral(
		program_->types.Fundamental(FUND_INT),
		InternNumber(static_cast<std::int64_t>(character_count)));
	count.constant = true;
	count.value = static_cast<std::int64_t>(character_count);
	RecordExpressionFacts(count);
	arguments.push_back(count);
	const std::vector<NodeId> argument_syntax(2, kNoNode);
	std::vector<CallConversionFact> argument_conversions;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, 0, 0, &argument_conversions);
	const FunctionInfo& function = GetFunction(selected);
	const TypeRecord function_type = program_->types.Get(function.type);
	if (function_type.parameter_count != 2)
		throw std::runtime_error(
			"string literal operator requires two parameters");
	const TypeId size_type = program_->types.Parameters(function.type)[1];
	if (arguments[1].type != size_type)
	{
		const std::uint32_t conversion = MakeDump(DUMP_CAST_EXPRESSION,
			size_type, VALUE_PRVALUE);
		dump_.Add(conversion, arguments[1].node);
		arguments[1].node = conversion;
		arguments[1].type = size_type;
		argument_conversions[1].rank = CONVERSION_EXACT;
		++expression_count_;
	}
	*result = BuildResolvedCall(selected, scope, argument_syntax, arguments,
		0, target, kNoEntity, 0, &argument_conversions);
	return true;
}

ExpressionInfo SemanticAnalyzer::AnalyzeThisExpression(ScopeId scope)
{
	const NameId name = program_->names.Intern("this");
	const LookupResult found = program_->LookupName(
		scope, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding)
		throw std::runtime_error("this outside member function");
	const BindingRecord& binding = program_->bindings[found.ordinary];
	ExpressionInfo result;
	result.type = EffectiveType(binding.type);
	result.category = VALUE_PRVALUE;
	result.binding = found.ordinary;
	result.node = MakeDump(DUMP_ID_EXPRESSION, result.type,
		VALUE_PRVALUE, name, found.ordinary);
	++expression_count_;
	return result;
}

}
}
