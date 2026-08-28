#include "preprocess/expressions/control_expression.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <vector>

#include "preprocess/tokens/IPPTokenStream.h"
#include "preprocess/tokens/pp_tokenizer.h"

namespace cppgm
{
namespace
{

struct Value
{
	std::uint64_t bits;
	bool is_unsigned;

	Value(std::uint64_t value = 0, bool unsigned_type = false)
		: bits(value), is_unsigned(unsigned_type)
	{}
};

std::int64_t SignedBits(std::uint64_t bits)
{
	std::int64_t value = 0;
	std::memcpy(&value, &bits, sizeof(value));
	return value;
}

std::uint64_t SignedEncoding(std::int64_t value)
{
	std::uint64_t bits = 0;
	std::memcpy(&bits, &value, sizeof(bits));
	return bits;
}

bool IsSignedIntegral(FundamentalType type)
{
	switch (type)
	{
	case FT_SIGNED_CHAR:
	case FT_SHORT_INT:
	case FT_INT:
	case FT_LONG_INT:
	case FT_LONG_LONG_INT:
	case FT_WCHAR_T:
	case FT_CHAR:
	case FT_BOOL:
		return true;
	default:
		return false;
	}
}

bool IsUnsignedIntegral(FundamentalType type)
{
	switch (type)
	{
	case FT_UNSIGNED_CHAR:
	case FT_UNSIGNED_SHORT_INT:
	case FT_UNSIGNED_INT:
	case FT_UNSIGNED_LONG_INT:
	case FT_UNSIGNED_LONG_LONG_INT:
	case FT_CHAR16_T:
	case FT_CHAR32_T:
		return true;
	default:
		return false;
	}
}

bool DecodeIntegralLiteral(FundamentalType type, const void* data,
	std::size_t size, Value* result)
{
	const bool signed_type = IsSignedIntegral(type);
	if ((!signed_type && !IsUnsignedIntegral(type)) || size == 0 || size > 8)
		return false;
	const unsigned char* bytes = static_cast<const unsigned char*>(data);
	std::uint64_t bits = 0;
	for (std::size_t i = 0; i < size; ++i)
		bits |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
	if (signed_type && size < 8 &&
		(bits & (std::uint64_t(1) << (size * 8 - 1))) != 0)
		bits |= ~std::uint64_t(0) << (size * 8);
	*result = Value(bits, !signed_type);
	return true;
}

enum TokenCategory : unsigned char
{
	TOKEN_VALUE,
	TOKEN_IDENTIFIER,
	TOKEN_SIMPLE
};

struct Token
{
	std::uint64_t bits;
	SimpleTokenKind simple;
	TokenCategory category;
	bool value_is_unsigned;
	bool is_defined_operator;
	bool definition_value;

	Token()
		: bits(0), simple(OP_COMMA), category(TOKEN_VALUE),
		  value_is_unsigned(false), is_defined_operator(false),
		  definition_value(false)
	{}
};

enum NodeKind : unsigned char
{
	NODE_LITERAL,
	NODE_UNARY,
	NODE_BINARY,
	NODE_CONDITIONAL
};

struct Node
{
	std::uint64_t literal_bits;
	std::uint32_t first;
	std::uint32_t second;
	std::uint32_t third;
	SimpleTokenKind operation;
	NodeKind kind;
	bool is_unsigned;

	Node()
		: literal_bits(0), first(0), second(0), third(0),
		  operation(OP_COMMA), kind(NODE_LITERAL), is_unsigned(false)
	{}
};

typedef std::uint32_t NodeId;
const NodeId kNoNode = std::numeric_limits<NodeId>::max();

bool IsUnaryOperator(SimpleTokenKind kind)
{
	return kind == OP_PLUS || kind == OP_MINUS || kind == OP_LNOT ||
		kind == OP_COMPL;
}

int BinaryPrecedence(SimpleTokenKind kind)
{
	switch (kind)
	{
	case OP_LOR: return 1;
	case OP_LAND: return 2;
	case OP_BOR: return 3;
	case OP_XOR: return 4;
	case OP_AMP: return 5;
	case OP_EQ:
	case OP_NE: return 6;
	case OP_LT:
	case OP_GT:
	case OP_LE:
	case OP_GE: return 7;
	case OP_LSHIFT:
	case OP_RSHIFT: return 8;
	case OP_PLUS:
	case OP_MINUS: return 9;
	case OP_STAR:
	case OP_DIV:
	case OP_MOD: return 10;
	default: return 0;
	}
}

enum ParserOperatorKind : unsigned char
{
	PARSER_LPAREN,
	PARSER_QUESTION,
	PARSER_UNARY,
	PARSER_BINARY,
	PARSER_CONDITIONAL
};

struct ParserOperator
{
	SimpleTokenKind operation;
	ParserOperatorKind kind;

	ParserOperator(SimpleTokenKind token_operation,
		ParserOperatorKind token_kind)
		: operation(token_operation), kind(token_kind)
	{}
};

class Parser
{
public:
	Parser(const std::vector<Token>& tokens, std::vector<Node>* nodes,
		std::vector<ParserOperator>* operators, std::vector<NodeId>* operands,
		ControlExpressionStats* stats)
		: tokens_(tokens), nodes_(*nodes), operators_(*operators),
		  operands_(*operands), stats_(stats), valid_(tokens.size() < kNoNode)
	{
		nodes_.clear();
		operators_.clear();
		operands_.clear();
	}

	NodeId Parse()
	{
		bool expect_operand = true;
		std::size_t position = 0;
		while (valid_ && position < tokens_.size())
		{
			const Token& token = tokens_[position];
			if (expect_operand)
			{
				if (token.category == TOKEN_SIMPLE &&
					IsUnaryOperator(token.simple))
				{
					PushOperator(ParserOperator(token.simple, PARSER_UNARY));
					++position;
					continue;
				}
				if (token.category == TOKEN_SIMPLE && token.simple == OP_LPAREN)
				{
					PushOperator(ParserOperator(OP_LPAREN, PARSER_LPAREN));
					++position;
					continue;
				}
				const NodeId operand = ParsePrimary(&position);
				if (operand == kNoNode)
					break;
				PushOperand(operand);
				ReducePrefixOperators();
				expect_operand = false;
				continue;
			}

			if (token.category != TOKEN_SIMPLE)
			{
				valid_ = false;
				break;
			}
			if (token.simple == OP_RPAREN)
			{
				if (!CloseParenthesis())
					break;
				++position;
				ReducePrefixOperators();
				continue;
			}
			if (token.simple == OP_QMARK)
			{
				ReduceForIncoming(0, false);
				if (!valid_)
					break;
				PushOperator(ParserOperator(OP_QMARK, PARSER_QUESTION));
				++position;
				expect_operand = true;
				continue;
			}
			if (token.simple == OP_COLON)
			{
				if (!BeginConditionalFalseExpression())
					break;
				++position;
				expect_operand = true;
				continue;
			}
			const int precedence = BinaryPrecedence(token.simple);
			if (precedence == 0)
			{
				valid_ = false;
				break;
			}
			ReduceForIncoming(precedence, true);
			if (!valid_)
				break;
			PushOperator(ParserOperator(token.simple, PARSER_BINARY));
			++position;
			expect_operand = true;
		}

		if (!valid_ || expect_operand)
			return kNoNode;
		while (valid_ && !operators_.empty())
			ReduceTop();
		if (!valid_ || operands_.size() != 1)
			return kNoNode;
		return operands_.back();
	}

private:
	NodeId StoreNode(const Node& node)
	{
		if (nodes_.size() >= kNoNode)
		{
			valid_ = false;
			return kNoNode;
		}
		nodes_.push_back(node);
		return static_cast<NodeId>(nodes_.size() - 1);
	}

	NodeId AddLiteral(const Value& value)
	{
		Node node;
		node.kind = NODE_LITERAL;
		node.literal_bits = value.bits;
		node.is_unsigned = value.is_unsigned;
		return StoreNode(node);
	}

	NodeId AddUnary(SimpleTokenKind operation, NodeId operand)
	{
		if (operand == kNoNode)
			return kNoNode;
		Node node;
		node.kind = NODE_UNARY;
		node.operation = operation;
		node.first = operand;
		node.is_unsigned = operation == OP_LNOT ? false :
			nodes_[operand].is_unsigned;
		return StoreNode(node);
	}

	NodeId AddBinary(SimpleTokenKind operation, NodeId left, NodeId right)
	{
		if (left == kNoNode || right == kNoNode)
			return kNoNode;
		Node node;
		node.kind = NODE_BINARY;
		node.operation = operation;
		node.first = left;
		node.second = right;
		if (operation == OP_LSHIFT || operation == OP_RSHIFT)
			node.is_unsigned = nodes_[left].is_unsigned;
		else if (operation == OP_LT || operation == OP_GT ||
			operation == OP_LE || operation == OP_GE ||
			operation == OP_EQ || operation == OP_NE ||
			operation == OP_LAND || operation == OP_LOR)
			node.is_unsigned = false;
		else
			node.is_unsigned = nodes_[left].is_unsigned ||
				nodes_[right].is_unsigned;
		return StoreNode(node);
	}

	NodeId AddConditional(NodeId condition, NodeId true_expression,
		NodeId false_expression)
	{
		if (condition == kNoNode || true_expression == kNoNode ||
			false_expression == kNoNode)
			return kNoNode;
		Node node;
		node.kind = NODE_CONDITIONAL;
		node.first = condition;
		node.second = true_expression;
		node.third = false_expression;
		node.is_unsigned = nodes_[true_expression].is_unsigned ||
			nodes_[false_expression].is_unsigned;
		return StoreNode(node);
	}

	bool IsSimple(std::size_t position, SimpleTokenKind kind) const
	{
		return position < tokens_.size() &&
			tokens_[position].category == TOKEN_SIMPLE &&
			tokens_[position].simple == kind;
	}

	NodeId ParsePrimary(std::size_t* position)
	{
		if (*position >= tokens_.size())
		{
			valid_ = false;
			return kNoNode;
		}
		const Token token = tokens_[*position];
		if (token.category == TOKEN_VALUE)
		{
			++*position;
			return AddLiteral(Value(token.bits, token.value_is_unsigned));
		}
		if (token.category == TOKEN_IDENTIFIER &&
			!token.is_defined_operator)
		{
			++*position;
			return AddLiteral(Value(token.bits, token.value_is_unsigned));
		}
		if (token.category == TOKEN_IDENTIFIER && token.is_defined_operator)
		{
			++*position;
			const bool parenthesized = IsSimple(*position, OP_LPAREN);
			if (parenthesized)
				++*position;
			if (*position >= tokens_.size() ||
				tokens_[*position].category != TOKEN_IDENTIFIER)
			{
				valid_ = false;
				return kNoNode;
			}
			const bool definition_value =
				tokens_[*position].definition_value;
			++*position;
			if (parenthesized && !IsSimple(*position, OP_RPAREN))
			{
				valid_ = false;
				return kNoNode;
			}
			if (parenthesized)
				++*position;
			return AddLiteral(Value(definition_value ? 1 : 0, false));
		}
		valid_ = false;
		return kNoNode;
	}

	void PushOperator(const ParserOperator& operation)
	{
		operators_.push_back(operation);
		if (stats_)
			stats_->peak_parser_operators = std::max(
				stats_->peak_parser_operators, operators_.size());
	}

	void PushOperand(NodeId operand)
	{
		if (operand == kNoNode)
		{
			valid_ = false;
			return;
		}
		operands_.push_back(operand);
		if (stats_)
			stats_->peak_parser_operands = std::max(
				stats_->peak_parser_operands, operands_.size());
	}

	void ReduceTop()
	{
		if (operators_.empty())
		{
			valid_ = false;
			return;
		}
		const ParserOperator operation = operators_.back();
		operators_.pop_back();
		if (operation.kind == PARSER_UNARY)
		{
			if (operands_.empty())
			{
				valid_ = false;
				return;
			}
			const NodeId operand = operands_.back();
			operands_.pop_back();
			PushOperand(AddUnary(operation.operation, operand));
			return;
		}
		if (operation.kind == PARSER_BINARY)
		{
			if (operands_.size() < 2)
			{
				valid_ = false;
				return;
			}
			const NodeId right = operands_.back();
			operands_.pop_back();
			const NodeId left = operands_.back();
			operands_.pop_back();
			PushOperand(AddBinary(operation.operation, left, right));
			return;
		}
		if (operation.kind == PARSER_CONDITIONAL)
		{
			if (operands_.size() < 3)
			{
				valid_ = false;
				return;
			}
			const NodeId false_expression = operands_.back();
			operands_.pop_back();
			const NodeId true_expression = operands_.back();
			operands_.pop_back();
			const NodeId condition = operands_.back();
			operands_.pop_back();
			PushOperand(AddConditional(condition, true_expression,
				false_expression));
			return;
		}
		valid_ = false;
	}

	void ReducePrefixOperators()
	{
		while (valid_ && !operators_.empty() &&
			operators_.back().kind == PARSER_UNARY)
			ReduceTop();
	}

	void ReduceForIncoming(int precedence, bool reduce_equal)
	{
		while (valid_ && !operators_.empty())
		{
			const ParserOperator& top = operators_.back();
			if (top.kind == PARSER_LPAREN || top.kind == PARSER_QUESTION)
				return;
			const int top_precedence = top.kind == PARSER_CONDITIONAL ? 0 :
				(top.kind == PARSER_UNARY ? 11 :
				 BinaryPrecedence(top.operation));
			if (top_precedence < precedence ||
				(top_precedence == precedence && !reduce_equal))
				return;
			ReduceTop();
		}
	}

	bool CloseParenthesis()
	{
		while (valid_ && !operators_.empty() &&
			operators_.back().kind != PARSER_LPAREN)
		{
			if (operators_.back().kind == PARSER_QUESTION)
			{
				valid_ = false;
				return false;
			}
			ReduceTop();
		}
		if (!valid_ || operators_.empty())
		{
			valid_ = false;
			return false;
		}
		operators_.pop_back();
		return true;
	}

	bool BeginConditionalFalseExpression()
	{
		while (valid_ && !operators_.empty() &&
			operators_.back().kind != PARSER_QUESTION)
		{
			if (operators_.back().kind == PARSER_LPAREN)
			{
				valid_ = false;
				return false;
			}
			ReduceTop();
		}
		if (!valid_ || operators_.empty())
		{
			valid_ = false;
			return false;
		}
		operators_.back().kind = PARSER_CONDITIONAL;
		operators_.back().operation = OP_QMARK;
		return true;
	}

	const std::vector<Token>& tokens_;
	std::vector<Node>& nodes_;
	std::vector<ParserOperator>& operators_;
	std::vector<NodeId>& operands_;
	ControlExpressionStats* stats_;
	bool valid_;
};

bool ShiftCount(const Value& value, unsigned int* count)
{
	if (value.is_unsigned)
	{
		if (value.bits >= 64)
			return false;
		*count = static_cast<unsigned int>(value.bits);
		return true;
	}
	const std::int64_t signed_value = SignedBits(value.bits);
	if (signed_value < 0 || signed_value >= 64)
		return false;
	*count = static_cast<unsigned int>(signed_value);
	return true;
}

bool CheckedAdd(std::int64_t left, std::int64_t right,
	std::int64_t* result)
{
	const std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
	const std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
	if ((right > 0 && left > maximum - right) ||
		(right < 0 && left < minimum - right))
		return false;
	*result = left + right;
	return true;
}

bool CheckedSubtract(std::int64_t left, std::int64_t right,
	std::int64_t* result)
{
	const std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
	const std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
	if ((right > 0 && left < minimum + right) ||
		(right < 0 && left > maximum + right))
		return false;
	*result = left - right;
	return true;
}

bool CheckedMultiply(std::int64_t left, std::int64_t right,
	std::int64_t* result)
{
	const std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
	const std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
	if (left > 0)
	{
		if ((right > 0 && left > maximum / right) ||
			(right < 0 && right < minimum / left))
			return false;
	}
	else if (left < 0)
	{
		if ((right > 0 && left < minimum / right) ||
			(right < 0 && left < maximum / right))
			return false;
	}
	*result = left * right;
	return true;
}

bool ApplyUnary(SimpleTokenKind operation, const Value& operand,
	Value* result)
{
	switch (operation)
	{
	case OP_PLUS:
		*result = operand;
		return true;
	case OP_MINUS:
		if (operand.is_unsigned)
			*result = Value(std::uint64_t(0) - operand.bits, true);
		else
		{
			const std::int64_t value = SignedBits(operand.bits);
			if (value == std::numeric_limits<std::int64_t>::min())
				return false;
			*result = Value(SignedEncoding(-value), false);
		}
		return true;
	case OP_LNOT:
		*result = Value(operand.bits == 0 ? 1 : 0, false);
		return true;
	case OP_COMPL:
		*result = Value(~operand.bits, operand.is_unsigned);
		return true;
	default:
		return false;
	}
}

bool ApplyBinary(SimpleTokenKind operation, const Value& original_left,
	const Value& original_right, Value* result)
{
	if (operation == OP_LSHIFT || operation == OP_RSHIFT)
	{
		unsigned int count = 0;
		if (!ShiftCount(original_right, &count))
			return false;
		if (operation == OP_LSHIFT)
			*result = Value(original_left.bits << count,
				original_left.is_unsigned);
		else if (original_left.is_unsigned ||
			SignedBits(original_left.bits) >= 0 || count == 0)
			*result = Value(original_left.bits >> count,
				original_left.is_unsigned);
		else
			*result = Value((original_left.bits >> count) |
				(~std::uint64_t(0) << (64 - count)), false);
		return true;
	}
	if (operation == OP_LAND || operation == OP_LOR)
	{
		*result = Value(original_right.bits == 0 ? 0 : 1, false);
		return true;
	}

	const bool unsigned_type = original_left.is_unsigned ||
		original_right.is_unsigned;
	const Value left(original_left.bits, unsigned_type);
	const Value right(original_right.bits, unsigned_type);
	if (operation == OP_EQ || operation == OP_NE)
	{
		const bool equal = left.bits == right.bits;
		*result = Value((operation == OP_EQ) == equal ? 1 : 0, false);
		return true;
	}
	if (operation == OP_LT || operation == OP_GT || operation == OP_LE ||
		operation == OP_GE)
	{
		bool comparison = false;
		if (unsigned_type)
		{
			if (operation == OP_LT) comparison = left.bits < right.bits;
			if (operation == OP_GT) comparison = left.bits > right.bits;
			if (operation == OP_LE) comparison = left.bits <= right.bits;
			if (operation == OP_GE) comparison = left.bits >= right.bits;
		}
		else
		{
			const std::int64_t signed_left = SignedBits(left.bits);
			const std::int64_t signed_right = SignedBits(right.bits);
			if (operation == OP_LT) comparison = signed_left < signed_right;
			if (operation == OP_GT) comparison = signed_left > signed_right;
			if (operation == OP_LE) comparison = signed_left <= signed_right;
			if (operation == OP_GE) comparison = signed_left >= signed_right;
		}
		*result = Value(comparison ? 1 : 0, false);
		return true;
	}
	if (operation == OP_AMP || operation == OP_XOR || operation == OP_BOR)
	{
		std::uint64_t bits = left.bits & right.bits;
		if (operation == OP_XOR) bits = left.bits ^ right.bits;
		if (operation == OP_BOR) bits = left.bits | right.bits;
		*result = Value(bits, unsigned_type);
		return true;
	}

	if (unsigned_type)
	{
		switch (operation)
		{
		case OP_PLUS: *result = Value(left.bits + right.bits, true); return true;
		case OP_MINUS: *result = Value(left.bits - right.bits, true); return true;
		case OP_STAR: *result = Value(left.bits * right.bits, true); return true;
		case OP_DIV:
			if (right.bits == 0) return false;
			*result = Value(left.bits / right.bits, true);
			return true;
		case OP_MOD:
			if (right.bits == 0) return false;
			*result = Value(left.bits % right.bits, true);
			return true;
		default: return false;
		}
	}

	const std::int64_t signed_left = SignedBits(left.bits);
	const std::int64_t signed_right = SignedBits(right.bits);
	std::int64_t signed_result = 0;
	switch (operation)
	{
	case OP_PLUS:
		if (!CheckedAdd(signed_left, signed_right, &signed_result)) return false;
		break;
	case OP_MINUS:
		if (!CheckedSubtract(signed_left, signed_right, &signed_result))
			return false;
		break;
	case OP_STAR:
		if (!CheckedMultiply(signed_left, signed_right, &signed_result))
			return false;
		break;
	case OP_DIV:
		if (signed_right == 0 ||
			(signed_left == std::numeric_limits<std::int64_t>::min() &&
			 signed_right == -1))
			return false;
		signed_result = signed_left / signed_right;
		break;
	case OP_MOD:
		if (signed_right == 0 ||
			(signed_left == std::numeric_limits<std::int64_t>::min() &&
			 signed_right == -1))
			return false;
		signed_result = signed_left % signed_right;
		break;
	default:
		return false;
	}
	*result = Value(SignedEncoding(signed_result), false);
	return true;
}

struct EvaluationFrame
{
	Value left;
	NodeId node;
	unsigned char state;

	explicit EvaluationFrame(NodeId node_id)
		: left(), node(node_id), state(0)
	{}
};

class Evaluator
{
public:
	Evaluator(const std::vector<Node>& nodes,
		std::vector<EvaluationFrame>* frames, std::vector<Value>* values,
		ControlExpressionStats* stats)
		: nodes_(nodes), frames_(*frames), values_(*values), stats_(stats)
	{}

	bool Evaluate(NodeId root, Value* result)
	{
		frames_.clear();
		values_.clear();
		Push(root);
		while (!frames_.empty())
		{
			EvaluationFrame& frame = frames_.back();
			const Node& node = nodes_[frame.node];
			if (frame.state == 0)
			{
				if (stats_)
					++stats_->evaluation_visits;
				if (node.kind == NODE_LITERAL)
				{
					values_.push_back(Value(node.literal_bits,
						node.is_unsigned));
					frames_.pop_back();
					continue;
				}
				frame.state = 1;
				Push(node.first);
				continue;
			}
			if (frame.state == 1)
			{
				if (values_.empty())
					return false;
				const Value first = values_.back();
				values_.pop_back();
				if (node.kind == NODE_UNARY)
				{
					Value unary_result;
					if (!ApplyUnary(node.operation, first, &unary_result))
						return false;
					frames_.pop_back();
					values_.push_back(unary_result);
					continue;
				}
				if (node.kind == NODE_CONDITIONAL)
				{
					frame.state = 2;
					if (stats_)
						++stats_->skipped_subexpressions;
					Push(first.bits != 0 ? node.second : node.third);
					continue;
				}
				frame.left = first;
				if ((node.operation == OP_LAND && first.bits == 0) ||
					(node.operation == OP_LOR && first.bits != 0))
				{
					if (stats_)
						++stats_->skipped_subexpressions;
					const Value logical_result(
						node.operation == OP_LOR ? 1 : 0, false);
					frames_.pop_back();
					values_.push_back(logical_result);
					continue;
				}
				frame.state = 2;
				Push(node.second);
				continue;
			}

			if (values_.empty())
				return false;
			Value second = values_.back();
			values_.pop_back();
			if (node.kind == NODE_CONDITIONAL)
			{
				second.is_unsigned = node.is_unsigned;
				frames_.pop_back();
				values_.push_back(second);
				continue;
			}
			Value binary_result;
			if (!ApplyBinary(node.operation, frame.left, second,
				&binary_result))
				return false;
			frames_.pop_back();
			values_.push_back(binary_result);
		}
		if (values_.size() != 1)
			return false;
		*result = values_.back();
		return true;
	}

private:
	void Push(NodeId node)
	{
		frames_.push_back(EvaluationFrame(node));
		if (stats_)
			stats_->peak_evaluation_frames = std::max(
				stats_->peak_evaluation_frames, frames_.size());
	}

	const std::vector<Node>& nodes_;
	std::vector<EvaluationFrame>& frames_;
	std::vector<Value>& values_;
	ControlExpressionStats* stats_;
};

void WriteValue(std::ostream& output, const Value& value)
{
	if (value.is_unsigned)
	{
		output << value.bits << "u\n";
		return;
	}
	if ((value.bits & (std::uint64_t(1) << 63)) == 0)
		output << value.bits << '\n';
	else
		output << '-' << (~value.bits + 1) << '\n';
}

class LineTokenCollector : public IPostTokenStream
{
public:
	LineTokenCollector(std::ostream& output, DefinedIdentifierQuery is_defined,
		ControlExpressionStats* stats)
		: output_(&output), is_defined_(is_defined), stats_(stats),
		  line_invalid_(false)
	{}

	explicit LineTokenCollector(ControlExpressionStats* stats)
		: output_(0), is_defined_(0), stats_(stats), line_invalid_(false)
	{}

	void EmitInvalid(const std::string& source)
	{
		(void)source;
		InvalidateLine();
	}

	void EmitSimple(const std::string& source, SimpleTokenKind kind)
	{
		Token token;
		// PA1 classifies the named operators new/delete as preprocessing
		// punctuators.  Preserve that origin here: unlike ordinary keywords,
		// they are not identifier_or_keyword operands in the PA3 grammar.
		if (kind >= KW_ALIGNAS && kind <= KW_WHILE && source != "new" &&
			source != "delete")
		{
			token.category = TOKEN_IDENTIFIER;
			token.bits = kind == KW_TRUE ? 1 : 0;
			token.definition_value = QueryDefinition(source);
		}
		else
		{
			token.category = TOKEN_SIMPLE;
			token.simple = kind;
		}
		AddToken(token);
	}

	void EmitIdentifier(const std::string& source)
	{
		Token token;
		token.category = TOKEN_IDENTIFIER;
		token.is_defined_operator = source == "defined";
		token.definition_value = QueryDefinition(source);
		AddToken(token);
	}

	void EmitLiteral(const std::string& source, FundamentalType type,
		const void* data, std::size_t size)
	{
		(void)source;
		Value value;
		if (!DecodeIntegralLiteral(type, data, size, &value))
		{
			InvalidateLine();
			return;
		}
		Token token;
		token.category = TOKEN_VALUE;
		token.bits = value.bits;
		token.value_is_unsigned = value.is_unsigned;
		AddToken(token);
	}

	void EmitLiteralArray(const std::string& source, std::size_t elements,
		FundamentalType type, const void* data, std::size_t size)
	{
		(void)source;
		(void)elements;
		(void)type;
		(void)data;
		(void)size;
		InvalidateLine();
	}

	void EmitUserDefinedCharacter(const std::string& source,
		const std::string& suffix, FundamentalType type,
		const void* data, std::size_t size)
	{
		(void)source;
		(void)suffix;
		(void)type;
		(void)data;
		(void)size;
		InvalidateLine();
	}

	void EmitUserDefinedString(const std::string& source,
		const std::string& suffix, std::size_t elements,
		FundamentalType type, const void* data, std::size_t size)
	{
		(void)source;
		(void)suffix;
		(void)elements;
		(void)type;
		(void)data;
		(void)size;
		InvalidateLine();
	}

	void EmitUserDefinedInteger(const std::string& source,
		const std::string& suffix, const std::string& prefix)
	{
		(void)source;
		(void)suffix;
		(void)prefix;
		InvalidateLine();
	}

	void EmitUserDefinedFloating(const std::string& source,
		const std::string& suffix, const std::string& prefix)
	{
		(void)source;
		(void)suffix;
		(void)prefix;
		InvalidateLine();
	}

	void EmitEof()
	{
		if (output_)
			*output_ << "eof\n";
	}

	bool EvaluateExpression(bool* truth)
	{
		if (stats_)
		{
			++stats_->logical_lines;
			++stats_->nonempty_lines;
		}
		Value result;
		const bool ok = EvaluateTokens(&result);
		if (truth)
			*truth = ok && result.bits != 0;
		ResetLine();
		if (!ok && stats_)
			++stats_->error_lines;
		return ok;
	}

	void FinishLine()
	{
		if (stats_)
			++stats_->logical_lines;
		if (!line_invalid_ && tokens_.empty())
		{
			ObserveStorage();
			return;
		}
		if (stats_)
			++stats_->nonempty_lines;
		Value result;
		const bool error = !EvaluateTokens(&result);
		if (!error)
			WriteValue(*output_, result);
		if (error)
		{
			*output_ << "error\n";
			if (stats_)
				++stats_->error_lines;
		}
		ResetLine();
	}

private:
	bool EvaluateTokens(Value* result)
	{
		if (line_invalid_ || tokens_.empty())
			return false;
		Parser parser(tokens_, &nodes_, &parser_operators_,
			&parser_operands_, stats_);
		const NodeId root = parser.Parse();
		if (stats_)
			stats_->syntax_nodes += nodes_.size();
		ObserveStorage();
		if (root == kNoNode)
			return false;
		Evaluator evaluator(nodes_, &evaluation_frames_,
			&evaluation_values_, stats_);
		return evaluator.Evaluate(root, result);
	}

	void ResetLine()
	{
		ObserveStorage();
		tokens_.clear();
		nodes_.clear();
		parser_operators_.clear();
		parser_operands_.clear();
		evaluation_frames_.clear();
		evaluation_values_.clear();
		line_invalid_ = false;
	}

	bool QueryDefinition(const std::string& source) const
	{
		return is_defined_ != 0 && is_defined_(source);
	}

	void AddToken(const Token& token)
	{
		if (line_invalid_)
			return;
		tokens_.push_back(token);
		if (stats_)
			stats_->peak_line_tokens = std::max(stats_->peak_line_tokens,
				tokens_.size());
		ObserveStorage();
	}

	void InvalidateLine()
	{
		line_invalid_ = true;
		tokens_.clear();
	}

	void ObserveStorage()
	{
		if (!stats_)
			return;
		stats_->peak_line_nodes = std::max(stats_->peak_line_nodes,
			nodes_.size());
		const std::size_t storage = tokens_.capacity() * sizeof(Token) +
			nodes_.capacity() * sizeof(Node) +
			parser_operators_.capacity() * sizeof(ParserOperator) +
			parser_operands_.capacity() * sizeof(NodeId) +
			evaluation_frames_.capacity() * sizeof(EvaluationFrame) +
			evaluation_values_.capacity() * sizeof(Value);
		stats_->peak_line_storage_bytes = std::max(
			stats_->peak_line_storage_bytes, storage);
	}

	std::ostream* output_;
	DefinedIdentifierQuery is_defined_;
	ControlExpressionStats* stats_;
	std::vector<Token> tokens_;
	std::vector<Node> nodes_;
	std::vector<ParserOperator> parser_operators_;
	std::vector<NodeId> parser_operands_;
	std::vector<EvaluationFrame> evaluation_frames_;
	std::vector<Value> evaluation_values_;
	bool line_invalid_;
};

struct ControllingExpressionEvaluatorImpl
{
	explicit ControllingExpressionEvaluatorImpl(ControlExpressionStats* stats)
		: collector(stats)
	{}

	LineTokenCollector collector;
};

class LogicalLinePostTokenStream : public IPPTokenStream
{
public:
	LogicalLinePostTokenStream(std::ostream& output,
		DefinedIdentifierQuery is_defined, ControlExpressionStats* stats)
		: collector_(output, is_defined, stats),
		  post_tokens_(collector_, stats ? &stats->tokenization : 0),
		  line_started_(false)
	{}

	void emit_whitespace_sequence()
	{
		line_started_ = true;
		post_tokens_.emit_whitespace_sequence();
	}

	void emit_new_line()
	{
		post_tokens_.FlushPendingTokens();
		collector_.FinishLine();
		post_tokens_.emit_new_line();
		line_started_ = false;
	}

	void emit_header_name(const std::string& data)
	{
		line_started_ = true;
		post_tokens_.emit_header_name(data);
	}

	void emit_identifier(const std::string& data)
	{
		line_started_ = true;
		post_tokens_.emit_identifier(data);
	}

	void emit_pp_number(const std::string& data)
	{
		line_started_ = true;
		post_tokens_.emit_pp_number(data);
	}

	void emit_character_literal(const std::string& data)
	{
		line_started_ = true;
		post_tokens_.emit_character_literal(data);
	}

	void emit_user_defined_character_literal(const std::string& data)
	{
		line_started_ = true;
		post_tokens_.emit_user_defined_character_literal(data);
	}

	void emit_string_literal(const std::string& data)
	{
		line_started_ = true;
		post_tokens_.emit_string_literal(data);
	}

	void emit_user_defined_string_literal(const std::string& data)
	{
		line_started_ = true;
		post_tokens_.emit_user_defined_string_literal(data);
	}

	void emit_preprocessing_op_or_punc(const std::string& data)
	{
		line_started_ = true;
		post_tokens_.emit_preprocessing_op_or_punc(data);
	}

	void emit_non_whitespace_char(const std::string& data)
	{
		line_started_ = true;
		post_tokens_.emit_non_whitespace_char(data);
	}

	void emit_eof()
	{
		post_tokens_.FlushPendingTokens();
		if (line_started_)
			collector_.FinishLine();
		post_tokens_.emit_eof();
	}

private:
	LineTokenCollector collector_;
	PostTokenizationSession post_tokens_;
	bool line_started_;
};

}

struct ControllingExpressionEvaluator::Impl
	: ControllingExpressionEvaluatorImpl
{
	explicit Impl(ControlExpressionStats* stats)
		: ControllingExpressionEvaluatorImpl(stats)
	{}
};

ControlExpressionStats::ControlExpressionStats()
	: logical_lines(0), nonempty_lines(0), error_lines(0), syntax_nodes(0),
	  evaluation_visits(0), skipped_subexpressions(0), peak_line_tokens(0),
	  peak_line_nodes(0), peak_parser_operators(0), peak_parser_operands(0),
	  peak_evaluation_frames(0),
	  peak_line_storage_bytes(0), elapsed_nanoseconds(0)
{}

void EvaluateControllingExpressions(const std::string& source,
	std::ostream& output, DefinedIdentifierQuery is_defined,
	ControlExpressionStats* stats)
{
	const std::chrono::steady_clock::time_point start = stats ?
		std::chrono::steady_clock::now() :
		std::chrono::steady_clock::time_point();
	if (stats)
		*stats = ControlExpressionStats();
	LogicalLinePostTokenStream lines(output, is_defined, stats);
	TokenizePreprocessingFile(source, lines,
		stats ? &stats->tokenization.preprocessing : 0);
	if (stats)
		stats->elapsed_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				std::chrono::steady_clock::now() - start).count());
}

ControllingExpressionEvaluator::ControllingExpressionEvaluator(
	ControlExpressionStats* stats)
	: impl_(new Impl(stats))
{}

ControllingExpressionEvaluator::~ControllingExpressionEvaluator()
{
	delete impl_;
}

bool ControllingExpressionEvaluator::Finish(bool* value)
{
	return impl_->collector.EvaluateExpression(value);
}

void ControllingExpressionEvaluator::EmitInvalid(const std::string& source)
{ impl_->collector.EmitInvalid(source); }
void ControllingExpressionEvaluator::EmitSimple(const std::string& source,
	SimpleTokenKind kind)
{ impl_->collector.EmitSimple(source, kind); }
void ControllingExpressionEvaluator::EmitIdentifier(const std::string& source)
{ impl_->collector.EmitIdentifier(source); }
void ControllingExpressionEvaluator::EmitLiteral(const std::string& source,
	FundamentalType type, const void* data, std::size_t size)
{ impl_->collector.EmitLiteral(source, type, data, size); }
void ControllingExpressionEvaluator::EmitLiteralArray(
	const std::string& source, std::size_t elements, FundamentalType type,
	const void* data, std::size_t size)
{ impl_->collector.EmitLiteralArray(source, elements, type, data, size); }
void ControllingExpressionEvaluator::EmitUserDefinedCharacter(
	const std::string& source, const std::string& suffix,
	FundamentalType type, const void* data, std::size_t size)
{ impl_->collector.EmitUserDefinedCharacter(source, suffix, type, data, size); }
void ControllingExpressionEvaluator::EmitUserDefinedString(
	const std::string& source, const std::string& suffix,
	std::size_t elements, FundamentalType type, const void* data,
	std::size_t size)
{ impl_->collector.EmitUserDefinedString(source, suffix, elements, type, data, size); }
void ControllingExpressionEvaluator::EmitUserDefinedInteger(
	const std::string& source, const std::string& suffix,
	const std::string& prefix)
{ impl_->collector.EmitUserDefinedInteger(source, suffix, prefix); }
void ControllingExpressionEvaluator::EmitUserDefinedFloating(
	const std::string& source, const std::string& suffix,
	const std::string& prefix)
{ impl_->collector.EmitUserDefinedFloating(source, suffix, prefix); }
void ControllingExpressionEvaluator::EmitEof()
{ impl_->collector.EmitEof(); }

}
