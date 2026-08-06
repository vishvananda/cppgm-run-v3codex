#include "pa12_semantic_detail.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <stdexcept>
#include <string>

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
