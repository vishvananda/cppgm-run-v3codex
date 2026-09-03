#include "semantic/analysis/analyzer.h"
#include "preprocess/tokens/post_tokenizer.h"
#include "support/exceptions.h"
#include "support/scoped_state.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{

namespace
{

bool IsFloatingLiteralSpelling(const std::string& spelling)
{
	return spelling.find('.') != std::string::npos ||
		spelling.find('p') != std::string::npos ||
		spelling.find('P') != std::string::npos ||
		((spelling.size() < 2 || spelling[0] != '0' ||
		  (spelling[1] != 'x' && spelling[1] != 'X')) &&
		 (spelling.find('e') != std::string::npos ||
		  spelling.find('E') != std::string::npos));
}

bool EndsWith(const std::string& spelling, const char* suffix)
{
	const std::size_t size = std::char_traits<char>::length(suffix);
	return spelling.size() >= size &&
		spelling.compare(spelling.size() - size, size, suffix) == 0;
}

FundamentalKind FloatingSpellingKind(const std::string& spelling,
	FundamentalKind fallback)
{
	if (EndsWith(spelling, "F16") || EndsWith(spelling, "f16"))
		return FUND_FLOAT16;
	if (EndsWith(spelling, "F32x") || EndsWith(spelling, "f32x"))
		return FUND_FLOAT32X;
	if (EndsWith(spelling, "F32") || EndsWith(spelling, "f32"))
		return FUND_FLOAT32;
	if (EndsWith(spelling, "F64x") || EndsWith(spelling, "f64x"))
		return FUND_FLOAT64X;
	if (EndsWith(spelling, "F64") || EndsWith(spelling, "f64"))
		return FUND_FLOAT64;
	if (EndsWith(spelling, "F128") || EndsWith(spelling, "f128"))
		return FUND_FLOAT128;
	return fallback;
}

std::size_t FloatingNumericEnd(const std::string& spelling)
{
	static const char* const suffixes[] = {
		"BF16", "bf16", "F128", "f128", "F32x", "f32x", "F64x", "f64x",
		"F16", "f16", "F32", "f32", "F64", "f64",
		"F", "f", "L", "l", "Q", "q"
	};
	for (std::size_t i = 0; i < sizeof(suffixes) / sizeof(suffixes[0]); ++i)
		if (EndsWith(spelling, suffixes[i]))
			return spelling.size() -
				std::char_traits<char>::length(suffixes[i]);
	return spelling.size();
}

FundamentalKind StringElementKind(FundamentalType type)
{
	switch (type)
	{
	case FT_CHAR: return FUND_CHAR;
	case FT_WCHAR_T: return FUND_WCHAR_T;
	case FT_CHAR16_T: return FUND_CHAR16_T;
	case FT_CHAR32_T: return FUND_CHAR32_T;
	default: break;
	}
	ThrowSemanticError("invalid string literal element type");
}

FundamentalKind SemanticFundamentalKind(FundamentalType type)
{
	switch (type)
	{
	case FT_SIGNED_CHAR: return FUND_SIGNED_CHAR;
	case FT_SHORT_INT: return FUND_SHORT_INT;
	case FT_INT: return FUND_INT;
	case FT_LONG_INT: return FUND_LONG_INT;
	case FT_LONG_LONG_INT: return FUND_LONG_LONG_INT;
	case FT_UNSIGNED_CHAR: return FUND_UNSIGNED_CHAR;
	case FT_UNSIGNED_SHORT_INT: return FUND_UNSIGNED_SHORT_INT;
	case FT_UNSIGNED_INT: return FUND_UNSIGNED_INT;
	case FT_UNSIGNED_LONG_INT: return FUND_UNSIGNED_LONG_INT;
	case FT_UNSIGNED_LONG_LONG_INT: return FUND_UNSIGNED_LONG_LONG_INT;
	case FT_WCHAR_T: return FUND_WCHAR_T;
	case FT_CHAR: return FUND_CHAR;
	case FT_CHAR16_T: return FUND_CHAR16_T;
	case FT_CHAR32_T: return FUND_CHAR32_T;
	case FT_BOOL: return FUND_BOOL;
	case FT_FLOAT: return FUND_FLOAT;
	case FT_DOUBLE: return FUND_DOUBLE;
	case FT_LONG_DOUBLE: return FUND_LONG_DOUBLE;
	case FT_FLOAT16: return FUND_FLOAT16;
	case FT_FLOAT32: return FUND_FLOAT32;
	case FT_FLOAT32X: return FUND_FLOAT32X;
	case FT_FLOAT64: return FUND_FLOAT64;
	case FT_FLOAT64X: return FUND_FLOAT64X;
	case FT_FLOAT128: return FUND_FLOAT128;
	case FT_VOID: return FUND_VOID;
	case FT_NULLPTR_T: return FUND_NULLPTR_T;
	}
	ThrowInternalCompilerError("unknown retained literal type");
}

std::size_t CharacterUnitCount(const std::string& spelling,
	std::size_t quote, std::size_t close)
{
	std::size_t count = 0;
	for (std::size_t i = quote + 1; i < close; ++i)
	{
		if (spelling[i] == '\\')
		{
			if (++i >= close) return 0;
			if (spelling[i] == 'x')
				while (i + 1 < close &&
					((spelling[i + 1] >= '0' && spelling[i + 1] <= '9') ||
					 (spelling[i + 1] >= 'a' && spelling[i + 1] <= 'f') ||
					 (spelling[i + 1] >= 'A' && spelling[i + 1] <= 'F'))) ++i;
			else if (spelling[i] >= '0' && spelling[i] <= '7')
				for (int digits = 1; digits < 3 && i + 1 < close &&
					spelling[i + 1] >= '0' && spelling[i + 1] <= '7';
					++digits) ++i;
		}
		++count;
	}
	return count;
}

}

bool StringLiteralTokenEnd(const std::string& spelling, std::size_t* end)
{
	std::size_t position = 0;
	if (spelling.compare(0, 2, "u8") == 0) position = 2;
	else if (!spelling.empty() &&
		(spelling[0] == 'L' || spelling[0] == 'u' || spelling[0] == 'U'))
		position = 1;
	const bool raw = position < spelling.size() && spelling[position] == 'R';
	if (raw) ++position;
	if (position >= spelling.size() || spelling[position] != '"') return false;
	if (raw)
	{
		const std::size_t delimiter_begin = position + 1;
		const std::size_t open = spelling.find('(', delimiter_begin);
		if (open == std::string::npos || open - delimiter_begin > 16)
			return false;
		const std::string close = ")" +
			spelling.substr(delimiter_begin, open - delimiter_begin) + "\"";
		const std::size_t close_position = spelling.find(close, open + 1);
		if (close_position == std::string::npos) return false;
		*end = close_position + close.size();
		return true;
	}
	for (++position; position < spelling.size(); ++position)
	{
		if (spelling[position] == '\\')
		{
			if (++position >= spelling.size()) return false;
			continue;
		}
		if (spelling[position] == '"')
		{
			*end = position + 1;
			return true;
		}
	}
	return false;
}

std::int64_t Analyzer::ParseInteger(const std::string& spelling) const
{
	if (stats_) ++stats_->semantic_integer_parses;
	const std::size_t quote = spelling.find('\'');
	if (quote != std::string::npos)
	{
		const std::size_t close = spelling.rfind('\'');
		if (close == quote || close + 1 != spelling.size())
			ThrowSemanticError("invalid character literal");
		unsigned long long value = 0;
		std::size_t count = 0;
		for (std::size_t i = quote + 1; i < close; ++i)
		{
			unsigned int character = static_cast<unsigned char>(spelling[i]);
			if (spelling[i] == '\\')
			{
				if (++i >= close)
					ThrowSemanticError("invalid character escape");
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
						ThrowSemanticError(
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
			ThrowSemanticError("character literal outside PA12 range");
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
	if (errno == ERANGE || end == digits.c_str() || *end != '\0')
		ThrowSemanticError("integer literal outside PA12 range");
	return static_cast<std::int64_t>(value);
}

NameId Analyzer::InternNumber(std::int64_t value)
{
	return program_->names.Intern(std::to_string(value));
}

std::int64_t Analyzer::ApplyConstantBinary(
	const std::string& operation, std::int64_t left, std::int64_t right,
	TypeId operand_type) const
{
	const bool integral_type = operand_type != kNoType &&
		IsIntegral(operand_type, true);
	const bool unsigned_type = integral_type &&
		IsUnsignedIntegral(operand_type);
	if (integral_type)
	{
		left = NormalizeIntegralConstant(operand_type, left);
		right = NormalizeIntegralConstant(operand_type, right);
	}
	const std::uint64_t unsigned_left = static_cast<std::uint64_t>(left);
	const std::uint64_t unsigned_right = static_cast<std::uint64_t>(right);
	const std::size_t width = integral_type ? IntegralWidth(operand_type) : 64;
	const std::int64_t signed_minimum = width == 64 ? INT64_MIN :
		- static_cast<std::int64_t>(std::uint64_t(1) << (width - 1));
	const std::int64_t signed_maximum = width == 64 ? INT64_MAX :
		static_cast<std::int64_t>(
			(std::uint64_t(1) << (width - 1)) - 1);
	if (integral_type && !unsigned_type)
	{
		const bool addition_overflow = operation == "+" &&
			((right > 0 && left > signed_maximum - right) ||
			 (right < 0 && left < signed_minimum - right));
		const bool subtraction_overflow = operation == "-" &&
			((right < 0 && left > signed_maximum + right) ||
			 (right > 0 && left < signed_minimum + right));
		bool multiplication_overflow = false;
		if (operation == "*" && left != 0 && right != 0)
		{
			if (left == -1) multiplication_overflow = right == signed_minimum;
			else if (right == -1)
				multiplication_overflow = left == signed_minimum;
			else if (left > 0)
				multiplication_overflow = right > 0 ?
					left > signed_maximum / right :
					right < signed_minimum / left;
			else multiplication_overflow = right > 0 ?
				left < signed_minimum / right :
				left < signed_maximum / right;
		}
		if (addition_overflow || subtraction_overflow ||
			multiplication_overflow)
			ThrowSemanticError("signed constant arithmetic overflow");
	}
	std::int64_t value = 0;
	if (operation == "+")
		value = static_cast<std::int64_t>(unsigned_left + unsigned_right);
	else if (operation == "-")
		value = static_cast<std::int64_t>(unsigned_left - unsigned_right);
	else if (operation == "*")
		value = static_cast<std::int64_t>(unsigned_left * unsigned_right);
	if (operation == "/")
	{
		if (right == 0) ThrowSemanticError("division by zero");
		if (unsigned_type)
			value = static_cast<std::int64_t>(unsigned_left / unsigned_right);
		else
		{
			if (left == signed_minimum && right == -1)
				ThrowSemanticError("signed division overflow");
			value = left / right;
		}
	}
	else if (operation == "%")
	{
		if (right == 0) ThrowSemanticError("division by zero");
		if (unsigned_type)
			value = static_cast<std::int64_t>(unsigned_left % unsigned_right);
		else
		{
			if (left == signed_minimum && right == -1)
				ThrowSemanticError("signed division overflow");
			value = left % right;
		}
	}
	else if (operation == "<<" || operation == ">>")
	{
		if (right < 0 || static_cast<std::uint64_t>(right) >=
			width)
			ThrowSemanticError("invalid constant shift count");
		if (operation == "<<")
		{
			if (!unsigned_type && left < 0)
				ThrowSemanticError("invalid negative constant left shift");
			const std::uint64_t maximum = width == 64 ?
				std::numeric_limits<std::uint64_t>::max() :
				(std::uint64_t(1) << width) - 1;
			if (!unsigned_type && unsigned_left > (maximum >> right))
				ThrowSemanticError("constant left shift overflow");
			value = static_cast<std::int64_t>(unsigned_left << right);
		}
		else value = unsigned_type ?
			static_cast<std::int64_t>(unsigned_left >> right) : left >> right;
	}
	else if (operation == "&") value = left & right;
	else if (operation == "|") value = left | right;
	else if (operation == "^") value = left ^ right;
	else if (operation == "==") return unsigned_left == unsigned_right;
	else if (operation == "!=") return unsigned_left != unsigned_right;
	else if (operation == "<") return unsigned_type ?
		unsigned_left < unsigned_right : left < right;
	else if (operation == ">") return unsigned_type ?
		unsigned_left > unsigned_right : left > right;
	else if (operation == "<=") return unsigned_type ?
		unsigned_left <= unsigned_right : left <= right;
	else if (operation == ">=") return unsigned_type ?
		unsigned_left >= unsigned_right : left >= right;
	else if (operation == "&&") return left && right;
	else if (operation == "||") return left || right;
	else if (operation == ",") return right;
	else if (operation != "+" && operation != "-" && operation != "*" &&
		operation != "/" && operation != "%")
		ThrowInternalCompilerError("unsupported constant binary operator");
	return integral_type ?
		NormalizeIntegralConstant(operand_type, value) : value;
}

ExpressionInfo Analyzer::MakeLiteral(TypeId type, NameId text,
	ValueCategory category)
{
	ExpressionInfo result;
	result.type = type;
	result.category = category;
	result.node = MakeDump(DUMP_LITERAL, type, category, text);
	++expression_count_;
	return result;
}

ExpressionInfo Analyzer::MakeStringLiteral(
	const std::string& spelling, std::size_t* character_count)
{
	FundamentalType decoded_type = FT_CHAR;
	std::vector<std::uint32_t> decoded;
	if (!DecodeStringLiteralCodeUnits(spelling, &decoded_type, &decoded) ||
		decoded.empty())
		ThrowSemanticError("invalid string literal spelling");
	if (string_literal_units_.size() >
		std::numeric_limits<std::uint32_t>::max() - decoded.size())
		ThrowSemanticResourceLimit("too many retained string literal code units");
	const std::uint32_t first =
		static_cast<std::uint32_t>(string_literal_units_.size());
	string_literal_units_.insert(
		string_literal_units_.end(), decoded.begin(), decoded.end());
	if (character_count) *character_count = decoded.size() - 1;
	const TypeId element = program_->types.Qualify(
		program_->types.Fundamental(StringElementKind(decoded_type)), CV_CONST);
	ExpressionInfo result = MakeLiteral(
		program_->types.Array(element, decoded.size()),
		program_->names.Intern(spelling), VALUE_LVALUE);
	result.string_unit_begin = first;
	result.string_unit_count = static_cast<std::uint32_t>(decoded.size());
	return result;
}

ExpressionInfo Analyzer::MakeBuiltinScalarLiteral(
	const std::string& spelling, NodeId syntax)
{
	FundamentalType retained_type = FT_VOID;
	std::uint64_t retained_value = 0;
	const bool retained = syntax != kNoNode && arena_->ScalarLiteralFact(
		syntax, &retained_type, &retained_value);
	const std::size_t quote = spelling.find('\'');
	if (quote != std::string::npos)
	{
		const std::size_t close = spelling.rfind('\'');
		std::int64_t value = retained ?
			static_cast<std::int64_t>(retained_value) : ParseInteger(spelling);
		FundamentalKind kind = retained ?
			SemanticFundamentalKind(retained_type) : FUND_INT;
		if (!retained && quote == 0)
		{
			const std::size_t units = CharacterUnitCount(spelling, quote, close);
			kind = units == 1 && value <= 0x7F ? FUND_CHAR : FUND_INT;
		}
		else if (!retained)
		{
			if (quote == 1 && spelling[0] == 'L') kind = FUND_WCHAR_T;
			else if (quote == 1 && spelling[0] == 'u') kind = FUND_CHAR16_T;
			else if (quote == 1 && spelling[0] == 'U') kind = FUND_CHAR32_T;
			else ThrowSemanticError("invalid character literal prefix");
		}
		const TypeId type = program_->types.Fundamental(kind);
		value = NormalizeIntegralConstant(type, value);
		ExpressionInfo result = MakeLiteral(type,
			program_->names.Intern(spelling));
		result.constant = true;
		result.value = value;
		return result;
	}
	if (IsFloatingLiteralSpelling(spelling))
	{
		const char suffix = spelling.empty() ? 0 : spelling[spelling.size() - 1];
		const FundamentalKind fallback = retained ?
			SemanticFundamentalKind(retained_type) :
			suffix == 'f' || suffix == 'F' ?
			FUND_FLOAT : suffix == 'l' || suffix == 'L' ?
			FUND_LONG_DOUBLE : FUND_DOUBLE;
		const FundamentalKind kind = FloatingSpellingKind(spelling, fallback);
		const TypeId type = program_->types.Fundamental(kind);
		const std::string numeric = spelling.substr(0, FloatingNumericEnd(spelling));
		char* end = 0;
		errno = 0;
		const long double decoded = std::strtold(numeric.c_str(), &end);
		if (!end || end != numeric.c_str() + numeric.size() || errno == ERANGE)
			ThrowSemanticError("invalid floating literal value");
		ExpressionInfo result = MakeLiteral(
			type, program_->names.Intern(spelling));
		SetExpressionScalar(&result, ConvertScalarConstant(
			type, type, ConstexprScalarValue(decoded)));
		return result;
	}
	const std::int64_t value = retained ?
		static_cast<std::int64_t>(retained_value) : ParseInteger(spelling);
	const bool has_u = spelling.find('u') != std::string::npos ||
		spelling.find('U') != std::string::npos;
	std::size_t ls = 0;
	for (std::size_t i = 0; i < spelling.size(); ++i)
		if (spelling[i] == 'l' || spelling[i] == 'L') ++ls;
	const FundamentalKind kind = retained ?
		SemanticFundamentalKind(retained_type) :
		ls > 1 ?
		(has_u ? FUND_UNSIGNED_LONG_LONG_INT : FUND_LONG_LONG_INT) :
		ls == 1 ? (has_u ? FUND_UNSIGNED_LONG_INT : FUND_LONG_INT) :
		has_u ? FUND_UNSIGNED_INT : FUND_INT;
	ExpressionInfo result = MakeLiteral(program_->types.Fundamental(kind),
		program_->names.Intern(spelling));
	result.constant = true;
	result.value = value;
	result.integer_literal_zero = value == 0;
	return result;
}

bool Analyzer::TryAnalyzeUserDefinedStringLiteral(
	const std::string& spelling, ScopeId scope, TypeId target,
	ExpressionInfo* result)
{
	std::size_t literal_end = 0;
	if (!StringLiteralTokenEnd(spelling, &literal_end) ||
		literal_end == spelling.size()) return false;
	const std::string suffix = spelling.substr(literal_end);
	if (!suffix.empty() && suffix[0] == ' ') return false;
	if (suffix.empty() || suffix[0] != '_')
		ThrowSemanticError("invalid user-defined literal suffix");
	const std::string function_name = "operator\"\"" + suffix;
	const std::vector<BindingId> candidates =
		FunctionCandidates(scope, function_name);
	if (candidates.empty())
		ThrowSemanticError("user-defined literal operator not found");
	std::size_t character_count = 0;
	std::vector<ExpressionInfo> arguments;
	arguments.push_back(MakeStringLiteral(
		spelling.substr(0, literal_end), &character_count));
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
		ThrowSemanticError(
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

bool Analyzer::TryAnalyzeUserDefinedNumericLiteral(
	const std::string& spelling, ScopeId scope, TypeId target,
	ExpressionInfo* result)
{
	const std::size_t suffix_begin = spelling.find('_');
	if (suffix_begin == std::string::npos) return false;
	if (suffix_begin == 0)
		ThrowSemanticError("invalid user-defined numeric literal");
	const std::string source = spelling.substr(0, suffix_begin);
	const std::string function_name =
		"operator\"\"" + spelling.substr(suffix_begin);
	const bool floating = IsFloatingLiteralSpelling(source);
	const TypeId cooked_parameter = program_->types.Fundamental(floating ?
		FUND_LONG_DOUBLE : FUND_UNSIGNED_LONG_LONG_INT);
	const std::vector<BindingId> ordinary =
		FunctionCandidates(scope, function_name);
	std::vector<BindingId> cooked;
	for (std::size_t i = 0; i < ordinary.size(); ++i)
	{
		const TypeRecord& type =
			program_->types.Get(GetFunction(ordinary[i]).type);
		const TypeId* parameters =
			program_->types.Parameters(GetFunction(ordinary[i]).type);
		if (type.parameter_count == 1 && parameters[0] == cooked_parameter)
			cooked.push_back(ordinary[i]);
	}
	if (!cooked.empty())
	{
		std::vector<ExpressionInfo> arguments(1,
			MakeBuiltinScalarLiteral(source));
		const std::vector<NodeId> syntax(1, kNoNode);
		std::vector<CallConversionFact> conversions;
		const BindingId selected = SelectOverload(scope, syntax, arguments,
			cooked, 0, 0, &conversions);
		*result = BuildResolvedCall(selected, scope, syntax, arguments, 0,
			target, kNoEntity, 0, &conversions);
		return true;
	}

	const TypeId character = program_->types.Fundamental(FUND_CHAR);
	std::vector<TemplateArgument> arguments;
	arguments.reserve(source.size());
	for (std::size_t i = 0; i < source.size(); ++i)
		arguments.push_back(TemplateArgument(TEMPLATE_ARGUMENT_INTEGRAL,
			character, static_cast<unsigned char>(source[i])));
	const std::vector<std::size_t> patterns =
		FindFunctionTemplates(scope, function_name);
	std::vector<BindingId> candidates;
	for (std::size_t i = 0; i < patterns.size(); ++i)
	{
		const FunctionTemplatePattern& pattern = function_templates_[patterns[i]];
		if (pattern.parameters.size() != 1 || !pattern.parameters[0].pack ||
			pattern.parameters[0].kind != TEMPLATE_ARGUMENT_INTEGRAL ||
			pattern.parameters[0].dependent_type ||
			program_->types.RemoveTopCv(pattern.parameters[0].value_type) !=
				character)
			continue;
		const BindingId candidate =
			InstantiateFunctionTemplate(patterns[i], arguments);
		if (candidate == kNoBinding) continue;
		const TypeRecord& type =
			program_->types.Get(GetFunction(candidate).type);
		if (type.parameter_count == 0 &&
			std::find(candidates.begin(), candidates.end(), candidate) ==
				candidates.end())
			candidates.push_back(candidate);
	}
	if (candidates.empty())
		ThrowSemanticError("user-defined literal operator not found");
	const std::vector<NodeId> no_syntax;
	const std::vector<ExpressionInfo> no_arguments;
	const BindingId selected = SelectOverload(scope, no_syntax, no_arguments,
		candidates, 0, 0, 0);
	*result = BuildResolvedCall(selected, scope, no_syntax, no_arguments, 0,
		target, kNoEntity, 0, 0);
	return true;
}

ExpressionInfo Analyzer::AnalyzeThisExpression(ScopeId scope)
{
	const NameId name = program_->names.Intern("this");
	const LookupResult found = program_->LookupName(
		scope, name, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding)
	{
		if (constexpr_frames_.empty())
			ThrowSemanticError("this outside member function");
		const FunctionInfo& function =
			GetFunction(constexpr_frames_.back().function);
		if (function.member_owner == kNoType)
			ThrowSemanticError("this outside member function");
		TypeId owner = function.member_owner;
		const TypeRecord& function_type = program_->types.Get(function.type);
		if (function_type.cv != CV_NONE)
			owner = program_->types.Qualify(owner, function_type.cv);
		ExpressionInfo result;
		result.type = program_->types.Pointer(owner);
		result.category = VALUE_PRVALUE;
		result.node = MakeDump(DUMP_ID_EXPRESSION, result.type,
			VALUE_PRVALUE, name);
		if (constexpr_frames_.back().receiver_address != kNoConstexprAddress)
			SetExpressionAddress(&result,
				constexpr_frames_.back().receiver_address);
		if (constexpr_frames_.back().receiver_object != kNoConstexprObject)
		{
			SetExpressionSubobject(&result,
				constexpr_frames_.back().receiver_object,
				constexpr_frames_.back().receiver_complete_object);
		}
		++expression_count_;
		return result;
	}
	if (current_function_context_ != kNoBinding)
	{
		const FunctionInfo& function = GetFunction(
			program_->bindings[current_function_context_].canonical);
		if (function.lambda_this_capture_member != kNoBinding)
		{
			const BindingRecord& storage =
				program_->bindings[found.ordinary];
			const BindingRecord& member = program_->bindings[
				function.lambda_this_capture_member];
			const TypeId type = EffectiveType(member.type);
			const std::uint32_t object = MakeDump(DUMP_ID_EXPRESSION,
				storage.type, VALUE_LVALUE, name, found.ordinary);
			const std::uint32_t projected = MakeDump(DUMP_MEMBER_EXPRESSION,
				type, VALUE_PRVALUE, member.name,
				function.lambda_this_capture_member);
			dump_.Add(projected, object);
			ExpressionInfo result;
			result.type = type;
			result.category = VALUE_PRVALUE;
			result.binding = function.lambda_this_capture_member;
			result.node = projected;
			expression_count_ += 2;
			return result;
		}
	}
	const BindingRecord& binding = program_->bindings[found.ordinary];
	ExpressionInfo result;
	result.type = EffectiveType(binding.type);
	result.category = VALUE_PRVALUE;
	result.binding = found.ordinary;
	result.node = MakeDump(DUMP_ID_EXPRESSION, result.type,
		VALUE_PRVALUE, name, found.ordinary);
	if (!constexpr_frames_.empty() &&
		constexpr_frames_.back().receiver_address != kNoConstexprAddress)
		SetExpressionAddress(&result,
			constexpr_frames_.back().receiver_address);
	if (!constexpr_frames_.empty() &&
		constexpr_frames_.back().receiver_object != kNoConstexprObject)
	{
		SetExpressionSubobject(&result,
			constexpr_frames_.back().receiver_object,
			constexpr_frames_.back().receiver_complete_object);
	}
	++expression_count_;
	return result;
}

ExpressionInfo Analyzer::AnalyzeNamedValue(
	const std::string& spelling, ScopeId scope, TypeId target, NodeId syntax)
{
	ExpressionInfo local;
	if (syntax != kNoNode &&
		FindChild(syntax, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) == kNoNode &&
		spelling.find("::") == std::string::npos &&
		TryAnalyzeConstexprLocal(spelling, target, &local))
		return local;
	LookupResult found;
	const NodeId decltype_name = syntax == kNoNode ? kNoNode :
		FindChild(syntax, ::cppgm::syntax::STAG_DECLTYPE_NAME);
	if (decltype_name != kNoNode)
	{
		const TypeId qualifier = program_->types.RemoveTopCv(
			EffectiveType(DecltypeType(
				FirstSemanticChild(decltype_name), scope)));
		EnsureClassDefinition(qualifier);
		const ScopeId carrier = program_->ScopeForType(qualifier);
		const NodeId qualified = FindChild(decltype_name, ::cppgm::syntax::STAG_QUALIFIED_NAME);
		if (carrier != kNoScope && qualified != kNoNode)
			found = LookupStructuredName(
				qualified, carrier, LOOKUP_ORDINARY);
	}
	else if (syntax != kNoNode &&
		FindChild(syntax, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) != kNoNode)
	{
		const BindingId variable_template =
			InstantiateVariableTemplate(syntax, scope);
		if (variable_template != kNoBinding)
			found.ordinary = variable_template;
		else found = LookupStructuredName(syntax, scope, LOOKUP_ORDINARY);
	}
	else found = syntax == kNoNode ?
		LookupSpelling(scope, spelling, LOOKUP_ORDINARY,
			NAME_PATH_PARSE_LITERAL) :
		LookupSyntaxName(syntax, scope, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding)
	{
		if (CandidateSubstitutionActive()) return CandidateSubstitutionFailure();
		ThrowSemanticError("unknown expression name: " + spelling);
	}
	if (program_->bindings[found.ordinary].kind == BIND_VARIABLE &&
		program_->bindings[found.ordinary].member_owner != kNoEntity &&
		!program_->bindings[found.ordinary].non_static_data_member &&
		!program_->bindings[found.ordinary].constant &&
		constant_expression_required_depth_ != 0 && unevaluated_depth_ == 0 &&
		(IsIntegral(program_->bindings[found.ordinary].type, true) ||
		 IsFloating(program_->bindings[found.ordinary].type)))
	{
		for (EntityId owner = program_->bindings[found.ordinary].member_owner;
			owner != kNoEntity; owner = program_->entities[owner].enclosing_class)
		{
			if (owner >= class_template_pattern_by_entity_.size() ||
				class_template_pattern_by_entity_[owner] == kNoDumpEdge) continue;
			const BindingId specialization =
				program_->entities[owner].declaration;
			DemandClassTemplateMemberDefinitions(owner);
			if (specialization != kNoBinding && specialization <
				class_template_member_definition_demand_states_.size())
				ApplyDemandedClassTemplateMemberDefinitions(specialization);
			break;
		}
		found = syntax == kNoNode ?
			LookupSpelling(scope, spelling, LOOKUP_ORDINARY,
				NAME_PATH_PARSE_LITERAL) :
			LookupSyntaxName(syntax, scope, LOOKUP_ORDINARY);
		if (found.ordinary == kNoBinding)
			ThrowInternalCompilerError(
				"static member definition replay lost its declaration");
	}
	std::size_t qualified_component_count = 0;
	std::size_t first_template_component =
		std::numeric_limits<std::size_t>::max();
	const NodeId structured_name = syntax == kNoNode ? kNoNode :
		FindChild(syntax, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME);
	for (std::uint32_t edge = structured_name == kNoNode ? kNoEdge :
		arena_->FirstEdge(structured_name); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId component = arena_->EdgeChild(edge);
		if (arena_->IsTag(component, ::cppgm::syntax::STAG_NAME_COMPONENT))
		{
			if (first_template_component ==
					std::numeric_limits<std::size_t>::max() &&
				FindChild(component, ::cppgm::syntax::STAG_TEMPLATE_TYPE_ARGUMENT_LIST) != kNoNode)
				first_template_component = qualified_component_count;
			++qualified_component_count;
		}
	}
	const bool intermediate_template_qualifier =
		first_template_component != std::numeric_limits<std::size_t>::max() &&
		first_template_component + 2 < qualified_component_count;
	if (intermediate_template_qualifier &&
		program_->bindings[found.ordinary].kind == BIND_VARIABLE &&
		program_->bindings[found.ordinary].member_owner != kNoEntity &&
		!program_->bindings[found.ordinary].non_static_data_member &&
		program_->bindings[found.ordinary].constant &&
		(IsIntegral(program_->bindings[found.ordinary].type, true) ||
		 IsFloating(program_->bindings[found.ordinary].type)))
	{
		for (EntityId owner = program_->bindings[found.ordinary].member_owner;
			owner != kNoEntity; owner = program_->entities[owner].enclosing_class)
		{
			if (owner >= class_template_pattern_by_entity_.size() ||
				class_template_pattern_by_entity_[owner] == kNoDumpEdge) continue;
			const BindingId specialization =
				program_->entities[owner].declaration;
			DemandClassTemplateMemberDefinitions(owner);
			if (specialization != kNoBinding && specialization <
				class_template_member_definition_demand_states_.size())
				ApplyDemandedClassTemplateMemberDefinitions(specialization);
			break;
		}
	}
	const BindingRecord& binding = program_->bindings[found.ordinary];
	if (found.ordinary < variable_template_bindings_.size() &&
		variable_template_bindings_[found.ordinary] != 0 && binding.constant &&
		(IsIntegral(binding.type, true) || IsFloating(binding.type)))
	{
		const ConstexprScalarValue scalar = BindingScalar(found.ordinary);
		ExpressionInfo result = MakeLiteral(EffectiveType(binding.type),
			InternScalar(EffectiveType(binding.type), scalar));
		SetExpressionScalar(&result, scalar);
		return ApplyTarget(result, target);
	}
	if (binding.kind == BIND_ENUMERATOR)
	{
		ExpressionInfo result = MakeLiteral(binding.type,
			InternNumber(binding.value));
		result.constant = true;
		result.value = binding.value;
		return ApplyTarget(result, target);
	}
	if (binding.kind != BIND_VARIABLE && binding.kind != BIND_PARAMETER)
		return CandidateExpressionFailure("name does not denote a value");
	if (!CanAccessMember(found.ordinary, found.naming_class))
		return CandidateExpressionFailure("inaccessible member object");
	MarkClassTemplateSpecializationUse(binding.member_owner);
	if (binding.non_static_data_member)
	{
		const TypeId target_shape = target == kNoType ? kNoType :
			program_->types.RemoveTopCv(target);
		const bool member_pointer_target = target_shape != kNoType &&
			program_->types.Get(target_shape).kind == TYPE_MEMBER_POINTER;
		if (member_pointer_target)
		{
			const TypeRecord& pointer = program_->types.Get(target_shape);
			const EntityId target_owner = EntityOf(
				static_cast<TypeId>(pointer.bound));
			if (!SimilarUnqualified(pointer.child, binding.type) ||
				(binding.member_owner != target_owner &&
				 !program_->IsBaseOf(binding.member_owner, target_owner)))
				return CandidateExpressionFailure(
					"member does not match member pointer target");
			ExpressionInfo result;
			result.type = binding.type;
			result.category = VALUE_LVALUE;
			result.binding = found.ordinary;
			result.node = MakeDump(DUMP_ID_EXPRESSION, result.type,
				result.category, binding.name, found.ordinary);
			++expression_count_;
			return result;
		}
		return AnalyzeImplicitDataMember(found.ordinary, scope, target,
			found.naming_class);
	}
	if (binding.member_owner != kNoEntity && constexpr_evaluation_depth_ == 0)
		EnsureStaticMemberStorage(found.ordinary,
			(target != kNoType && program_->types.IsReference(target)) ||
			(binding.constant &&
			 BindingObject(found.ordinary) != kNoConstexprObject &&
			 constant_expression_required_depth_ == 0));
	const std::uint32_t constant_address = binding.constant ?
		BindingAddress(found.ordinary) : kNoConstexprAddress;
	if (constant_address != kNoConstexprAddress &&
		constant_expression_required_depth_ == 0 &&
		(target == kNoType || !program_->types.IsReference(target)))
	{
		ExpressionInfo result = MaterializeConstexprAddress(
			constant_address, binding.type);
		const ConstexprAddressValue* address =
			ConstexprAddressAt(constant_address);
		result.indirect_constant_designator =
			binding.kind == BIND_PARAMETER ||
			(address && address->kind == CONSTEXPR_ADDRESS_FUNCTION);
		return ApplyTarget(result, target);
	}
	std::uint32_t injected_fact =
		found.ordinary < injected_fact_by_binding_.size() ?
		injected_fact_by_binding_[found.ordinary] : kNoDumpEdge;
	if (injected_fact == kNoDumpEdge && binding.canonical != kNoBinding &&
		binding.canonical < injected_fact_by_binding_.size())
		injected_fact = injected_fact_by_binding_[binding.canonical];
	if (injected_fact != kNoDumpEdge)
	{
		const InjectedMemberInfo& injected = injected_members_[injected_fact];
		const BindingRecord& storage = program_->bindings[injected.storage];
		const BindingRecord& member = program_->bindings[injected.member];
		const std::uint32_t storage_node = MakeDump(DUMP_ID_EXPRESSION,
			storage.type, VALUE_LVALUE, storage.name, injected.storage);
		const std::uint32_t member_node = MakeDump(DUMP_MEMBER_EXPRESSION,
			binding.type, VALUE_LVALUE, member.name, injected.member);
		dump_.Add(member_node, storage_node);
		ExpressionInfo result;
		result.node = member_node;
		result.type = binding.type;
		result.category = VALUE_LVALUE;
		expression_count_ += 2;
		return ApplyTarget(result, target);
	}
	ExpressionInfo result;
	const BindingId value_binding = binding.kind == BIND_PARAMETER ?
		binding.canonical : found.ordinary;
	const bool nonreference_nttp =
		binding.kind == BIND_PARAMETER && binding.constant &&
		!program_->types.IsReference(binding.type);
	result.type = EffectiveType(binding.type);
	result.category = nonreference_nttp ? VALUE_PRVALUE : VALUE_LVALUE;
	result.binding = value_binding;
	result.node = MakeDump(DUMP_ID_EXPRESSION, result.type,
		result.category, program_->names.Intern(spelling), value_binding);
	dump_.nodes[result.node].template_parameter_constant =
		(binding.kind == BIND_PARAMETER && binding.constant) ||
		binding.template_parameter_constant;
	if (binding.constant)
		SetExpressionBindingConstant(&result, found.ordinary);
	if (binding.kind == BIND_PARAMETER && binding.constant &&
		IsMemberPointer(binding.type) && result.binding != kNoBinding &&
		result.binding < program_->bindings.size() &&
		program_->bindings[result.binding].kind == BIND_FUNCTION)
	{
		result.indirect_constant_designator = true;
		dump_.nodes[result.node].binding = result.binding;
	}
	RecordExpressionFacts(result);
	++expression_count_;
	return ApplyTarget(result, target);
}

TypeId Analyzer::DecltypeType(NodeId node, ScopeId scope)
{
	if (node == kNoNode) ThrowSemanticError("empty decltype");
	bool parenthesized = false;
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_PARENTHESIZED_EXPRESSION))
	{
		parenthesized = true;
		node = FirstSemanticChild(node);
	}
	const bool unparenthesized_member = !parenthesized &&
		arena_->IsTag(node, ::cppgm::syntax::STAG_MEMBER_EXPRESSION);
	const bool unparenthesized_id = !parenthesized &&
		arena_->IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION);
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_ID_EXPRESSION) &&
		FindChild(node, ::cppgm::syntax::STAG_STRUCTURED_TYPE_NAME) == kNoNode &&
		arena_->Payload(node).find("::") == std::string::npos)
	{
		const LookupResult found = LookupSyntaxName(
			node, scope, LOOKUP_ORDINARY);
		if (found.ordinary == kNoBinding)
			ThrowSemanticError("decltype name not found");
		const BindingRecord& binding = program_->bindings[found.ordinary];
		if (!parenthesized || binding.kind == BIND_ENUMERATOR)
			return binding.type;
		return program_->types.Reference(TYPE_LVALUE_REFERENCE,
			EffectiveType(binding.type));
	}
	ExpressionInfo expression;
	{
		ScopedCounterIncrement unevaluated(&unevaluated_depth_);
		ScopedCounterIncrement decltype_operand(&decltype_operand_depth_);
		expression = AnalyzeExpression(node, scope);
	}
	if (CandidateSubstitutionFailed()) return kNoType;
	if ((unparenthesized_member || unparenthesized_id) &&
		expression.binding != kNoBinding)
		return program_->bindings[expression.binding].type;
	if (expression.category == VALUE_LVALUE)
		return program_->types.Reference(TYPE_LVALUE_REFERENCE,
			EffectiveType(expression.type));
	if (expression.category == VALUE_XVALUE)
		return program_->types.Reference(TYPE_RVALUE_REFERENCE,
			EffectiveType(expression.type));
	return expression.type;
}

}
}
