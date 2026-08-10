#include "pa12_semantic_detail.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{
bool SyntaxUsesAnyIdentifier(const SyntaxArena& arena, NodeId node,
	const std::unordered_set<NameId>& identifiers)
{
	if (identifiers.count(arena.SemanticPayloadId(node)) != 0) return true;
	for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		if (SyntaxUsesAnyIdentifier(arena, arena.EdgeChild(edge), identifiers)) return true;
	return false;
}
}

NodeId SemanticAnalyzer::FindChild(NodeId node, const char* tag) const
{
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (arena_->IsTag(child, tag)) return child;
	}
	return kNoNode;
}
NodeId SemanticAnalyzer::FirstSemanticChild(NodeId node) const
{
	const std::uint32_t edge = arena_->FirstEdge(node);
	return edge == kNoEdge ? kNoNode : arena_->EdgeChild(edge);
}
std::string SemanticAnalyzer::PayloadSource(NodeId node) const
{
	return arena_->SemanticPayload(node);
}
NamePath SemanticAnalyzer::ParseNamePath(const std::string& spelling)
{
	NamePath result;
	std::size_t first = 0;
	result.global = spelling.size() >= 2 && spelling[0] == ':' &&
		spelling[1] == ':';
	if (result.global) first = 2;
	std::size_t conversion_terminal = std::string::npos;
	if (spelling.compare(first, 9, "operator ") == 0)
		conversion_terminal = first;
	else
	{
		const std::size_t separator = spelling.find("::operator ", first);
		if (separator != std::string::npos)
			conversion_terminal = separator + 2;
	}
	while (first < spelling.size())
	{
		std::size_t separator = std::string::npos;
		if (first != conversion_terminal)
		{
			std::size_t angle_depth = 0;
			for (std::size_t scan = first; scan + 1 < spelling.size(); ++scan)
			{
				if (spelling[scan] == '<') ++angle_depth;
				else if (spelling[scan] == '>' && angle_depth != 0) --angle_depth;
				else if (spelling[scan] == ':' && spelling[scan + 1] == ':' &&
					angle_depth == 0)
				{
					separator = scan;
					break;
				}
			}
		}
		const std::size_t last = separator == std::string::npos ?
			spelling.size() : separator;
		if (last == first) throw std::runtime_error("invalid qualified name");
		if (first == conversion_terminal)
		{
			std::string terminal;
			terminal.reserve(last - first);
			for (std::size_t i = first; i < last; ++i)
				if (!std::isspace(static_cast<unsigned char>(spelling[i])))
					terminal += spelling[i];
			result.Push(program_->names.Intern(terminal));
		}
		else result.Push(
			program_->names.InternRange(spelling, first, last - first));
		if (separator == std::string::npos) break;
		first = separator + 2;
	}
	return result;
}
std::uint32_t SemanticAnalyzer::MakeDump(DumpKind kind, TypeId type,
	ValueCategory category, NameId text, BindingId binding)
{
	const std::uint32_t node = dump_.Make(kind);
	DumpNode& record = dump_.nodes[node];
	record.type = type;
	record.category = category;
	record.text = text;
	record.binding = binding;
	if (kind == DUMP_VARIABLE && binding != kNoBinding)
	{
		if (variable_node_by_binding_.size() <= binding)
			variable_node_by_binding_.resize(
				static_cast<std::size_t>(binding) + 1, kNoDumpEdge);
		variable_node_by_binding_[binding] = node;
	}
	return node;
}
TypeId SemanticAnalyzer::EffectiveType(TypeId type) const
{
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE ? record.child : type;
}

bool SemanticAnalyzer::IsVoid(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental == FUND_VOID;
}

bool SemanticAnalyzer::IsNullptr(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental == FUND_NULLPTR_T;
}

bool SemanticAnalyzer::IsConst(TypeId type) const
{
	type = EffectiveType(type);
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_QUALIFIED && (record.cv & CV_CONST) != 0;
}

FundamentalKind SemanticAnalyzer::FundamentalOf(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	if (record.kind != TYPE_FUNDAMENTAL)
		throw std::logic_error("fundamental kind requested for non-fundamental");
	return record.fundamental;
}

bool SemanticAnalyzer::IsIntegral(TypeId type, bool allow_scoped_enum) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_FUNDAMENTAL)
		return record.fundamental != FUND_VOID &&
			record.fundamental != FUND_NULLPTR_T &&
			record.fundamental != FUND_FLOAT &&
			record.fundamental != FUND_DOUBLE &&
			record.fundamental != FUND_LONG_DOUBLE;
	if (record.kind != TYPE_NAMED) return false;
	const NamedFlavor flavor = program_->entities[record.entity].flavor;
	return flavor == NAMED_ENUM || (allow_scoped_enum &&
		flavor == NAMED_ENUM_CLASS);
}

bool SemanticAnalyzer::IsFloating(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	return record.kind == TYPE_FUNDAMENTAL &&
		(record.fundamental == FUND_FLOAT ||
		 record.fundamental == FUND_DOUBLE ||
		 record.fundamental == FUND_LONG_DOUBLE);
}

bool SemanticAnalyzer::IsArithmetic(TypeId type) const
{
	return IsIntegral(type) || IsFloating(type);
}

bool SemanticAnalyzer::IsPointer(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	return program_->types.Get(type).kind == TYPE_POINTER;
}

int SemanticAnalyzer::IntegralRank(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_->entities[record.entity];
		return entity.underlying == kNoType ? 3 : IntegralRank(entity.underlying);
	}
	switch (record.fundamental)
	{
	case FUND_BOOL: return 0;
	case FUND_CHAR: case FUND_SIGNED_CHAR: case FUND_UNSIGNED_CHAR: return 1;
	case FUND_SHORT_INT: case FUND_UNSIGNED_SHORT_INT:
	case FUND_CHAR16_T: return 2;
	case FUND_INT: case FUND_UNSIGNED_INT: case FUND_WCHAR_T:
	case FUND_CHAR32_T: return 3;
	case FUND_LONG_INT: case FUND_UNSIGNED_LONG_INT: return 4;
	case FUND_LONG_LONG_INT: case FUND_UNSIGNED_LONG_LONG_INT: return 5;
	default: return -1;
	}
}

TypeId SemanticAnalyzer::IntegralPromotionType(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_NAMED)
	{
		const EntityRecord& entity = program_->entities[record.entity];
		if (entity.flavor == NAMED_ENUM_CLASS) return type;
		type = entity.underlying;
	}
	if (IntegralRank(type) < 3)
		return program_->types.Fundamental(FUND_INT);
	return type;
}

TypeId SemanticAnalyzer::CommonArithmeticType(TypeId left, TypeId right) const
{
	left = program_->types.RemoveTopCv(EffectiveType(left));
	right = program_->types.RemoveTopCv(EffectiveType(right));
	if (IsFloating(left) || IsFloating(right))
	{
		if ((IsFloating(left) && FundamentalOf(left) == FUND_LONG_DOUBLE) ||
			(IsFloating(right) && FundamentalOf(right) == FUND_LONG_DOUBLE))
			return program_->types.Fundamental(FUND_LONG_DOUBLE);
		if ((IsFloating(left) && FundamentalOf(left) == FUND_DOUBLE) ||
			(IsFloating(right) && FundamentalOf(right) == FUND_DOUBLE))
			return program_->types.Fundamental(FUND_DOUBLE);
		return program_->types.Fundamental(FUND_FLOAT);
	}
	left = IntegralPromotionType(left);
	right = IntegralPromotionType(right);
	if (left == right) return left;
	const int left_rank = IntegralRank(left);
	const int right_rank = IntegralRank(right);
	const bool left_unsigned = IsUnsignedIntegral(left);
	const bool right_unsigned = IsUnsignedIntegral(right);
	if (left_unsigned == right_unsigned)
		return left_rank > right_rank ? left : right;
	const TypeId unsigned_type = left_unsigned ? left : right;
	const TypeId signed_type = left_unsigned ? right : left;
	const int unsigned_rank = left_unsigned ? left_rank : right_rank;
	const int signed_rank = left_unsigned ? right_rank : left_rank;
	if (unsigned_rank >= signed_rank) return unsigned_type;
	if (IntegralWidth(signed_type) > IntegralWidth(unsigned_type))
		return signed_type;
	switch (FundamentalOf(signed_type))
	{
	case FUND_INT:
		return program_->types.Fundamental(FUND_UNSIGNED_INT);
	case FUND_LONG_INT:
		return program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	case FUND_LONG_LONG_INT:
		return program_->types.Fundamental(FUND_UNSIGNED_LONG_LONG_INT);
	default:
		throw std::logic_error(
			"usual arithmetic conversion has no unsigned counterpart");
	}
}

TypeId SemanticAnalyzer::Decay(TypeId type) const
{
	type = EffectiveType(type);
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_ARRAY) return program_->types.Pointer(record.child);
	if (record.kind == TYPE_FUNCTION) return program_->types.Pointer(type);
	return program_->types.RemoveTopCv(type);
}

TypeId SemanticAnalyzer::AdjustParameterType(TypeId type)
{
	const TypeRecord record = program_->types.Get(type);
	if (record.kind == TYPE_ARRAY) return program_->types.Pointer(record.child);
	if (record.kind == TYPE_FUNCTION) return program_->types.Pointer(type);
	return program_->types.RemoveTopCv(type);
}

bool SemanticAnalyzer::SimilarUnqualified(TypeId source, TypeId target) const
{
	source = program_->types.RemoveTopCv(source);
	target = program_->types.RemoveTopCv(target);
	if (source == target) return true;
	const TypeRecord a = program_->types.Get(source);
	const TypeRecord b = program_->types.Get(target);
	if (a.kind != b.kind) return false;
	switch (a.kind)
	{
	case TYPE_POINTER:
	case TYPE_LVALUE_REFERENCE:
	case TYPE_RVALUE_REFERENCE:
		return SimilarUnqualified(a.child, b.child);
	case TYPE_ARRAY:
		return a.bound == b.bound && SimilarUnqualified(a.child, b.child);
	case TYPE_FUNCTION:
		if (a.parameter_count != b.parameter_count || a.variadic != b.variadic ||
			!SimilarUnqualified(a.child, b.child)) return false;
		for (std::size_t i = 0; i < a.parameter_count; ++i)
			if (!SimilarUnqualified(program_->types.Parameters(source)[i],
				program_->types.Parameters(target)[i])) return false;
		return true;
	default: return false;
	}
}

bool SemanticAnalyzer::QualificationConversion(TypeId source,
	TypeId target) const
{
	std::vector<std::uint8_t> source_cv;
	std::vector<std::uint8_t> target_cv;
	TypeId a = source;
	TypeId b = target;
	while (true)
	{
		const TypeRecord ar = program_->types.Get(a);
		const TypeRecord br = program_->types.Get(b);
		if (ar.kind != TYPE_POINTER || br.kind != TYPE_POINTER) break;
		a = ar.child;
		b = br.child;
		std::uint8_t acv = CV_NONE;
		std::uint8_t bcv = CV_NONE;
		if (program_->types.Get(a).kind == TYPE_QUALIFIED)
		{
			acv = program_->types.Get(a).cv;
			a = program_->types.Get(a).child;
		}
		if (program_->types.Get(b).kind == TYPE_QUALIFIED)
		{
			bcv = program_->types.Get(b).cv;
			b = program_->types.Get(b).child;
		}
		source_cv.push_back(acv);
		target_cv.push_back(bcv);
	}
	if (!SimilarUnqualified(a, b) || source_cv.size() != target_cv.size())
		return false;
	for (std::size_t i = 0; i < source_cv.size(); ++i)
	{
		if ((source_cv[i] & ~target_cv[i]) != 0) return false;
		if (i > 0 && source_cv[i] != target_cv[i])
			for (std::size_t j = 0; j < i; ++j)
				if ((target_cv[j] & CV_CONST) == 0) return false;
	}
	return true;
}

ConversionRank SemanticAnalyzer::Conversion(TypeId source,
	ValueCategory category, bool integer_zero, TypeId target) const
{
	++conversion_checks_;
	const TypeRecord target_record = program_->types.Get(target);
	if (target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE)
	{
		const bool lvalue_reference =
			target_record.kind == TYPE_LVALUE_REFERENCE;
		if (lvalue_reference && category != VALUE_LVALUE &&
			!IsConst(target_record.child)) return CONVERSION_INVALID;
		TypeId from = program_->types.RemoveTopCv(EffectiveType(source));
		TypeId to = program_->types.RemoveTopCv(target_record.child);
		if (from == to)
		{
			if (!lvalue_reference && category == VALUE_LVALUE)
				return CONVERSION_INVALID;
			const TypeRecord source_top = program_->types.Get(EffectiveType(source));
			const TypeRecord target_top = program_->types.Get(target_record.child);
			const std::uint8_t source_cv = source_top.kind == TYPE_QUALIFIED ?
				source_top.cv : CV_NONE;
			const std::uint8_t target_cv = target_top.kind == TYPE_QUALIFIED ?
				target_top.cv : CV_NONE;
			if ((source_cv & ~target_cv) != 0) return CONVERSION_INVALID;
			return lvalue_reference && category != VALUE_LVALUE ?
				CONVERSION_STANDARD : CONVERSION_EXACT;
		}
		if (QualificationConversion(EffectiveType(source), target_record.child))
			return !lvalue_reference && category == VALUE_LVALUE ?
				CONVERSION_INVALID : CONVERSION_EXACT;
		const EntityId source_entity = EntityOf(from);
		const EntityId target_entity = EntityOf(to);
		const bool derived_to_base = source_entity != kNoEntity &&
			target_entity != kNoEntity &&
			BaseConversionAllowed(source_entity, target_entity);
		const bool reference_related =
			SimilarUnqualified(EffectiveType(source), target_record.child) ||
			derived_to_base;
		if (!reference_related &&
			(!lvalue_reference || IsConst(target_record.child)))
		{
			const ConversionRank temporary = Conversion(source, category,
				integer_zero, target_record.child);
			if (temporary != CONVERSION_INVALID) return temporary;
		}
		if (derived_to_base)
		{
			const TypeRecord source_top = program_->types.Get(EffectiveType(source));
			const TypeRecord target_top = program_->types.Get(target_record.child);
			const std::uint8_t source_cv = source_top.kind == TYPE_QUALIFIED ?
				source_top.cv : CV_NONE;
			const std::uint8_t target_cv = target_top.kind == TYPE_QUALIFIED ?
				target_top.cv : CV_NONE;
			if ((source_cv & ~target_cv) == 0)
				return CONVERSION_DERIVED_TO_BASE;
		}
		if (IsArithmetic(from) && IsArithmetic(to) &&
			(IsConst(target_record.child) || !lvalue_reference))
		{
			const EntityId target_entity = EntityOf(to);
			if (target_entity != kNoEntity &&
				(program_->entities[target_entity].flavor == NAMED_ENUM ||
				 program_->entities[target_entity].flavor == NAMED_ENUM_CLASS))
				return CONVERSION_INVALID;
			return lvalue_reference ? CONVERSION_BOOLEAN : CONVERSION_STANDARD;
		}
		return CONVERSION_INVALID;
	}

	TypeId from = Decay(source);
	TypeId to = program_->types.RemoveTopCv(target);
	if (from == to) return CONVERSION_EXACT;
	const EntityId derived_object = EntityOf(from);
	const EntityId base_object = EntityOf(to);
	if (derived_object != kNoEntity && base_object != kNoEntity &&
		BaseConversionAllowed(derived_object, base_object))
		return CONVERSION_DERIVED_TO_BASE;
	if (IsNullptr(to) && integer_zero) return CONVERSION_STANDARD;
	if (IsPointer(to) && (IsNullptr(from) || integer_zero))
		return CONVERSION_STANDARD;
	if (IsPointer(from) && IsPointer(to))
	{
		const TypeRecord source_pointer = program_->types.Get(from);
		const TypeRecord target_pointer = program_->types.Get(to);
		const EntityId derived = EntityOf(source_pointer.child);
		const EntityId base = EntityOf(target_pointer.child);
		if (derived != kNoEntity && base != kNoEntity &&
			BaseConversionAllowed(derived, base))
		{
			const TypeRecord source_cv = program_->types.Get(source_pointer.child);
			const TypeRecord target_cv = program_->types.Get(target_pointer.child);
			const std::uint8_t scv = source_cv.kind == TYPE_QUALIFIED ?
				source_cv.cv : CV_NONE;
			const std::uint8_t tcv = target_cv.kind == TYPE_QUALIFIED ?
				target_cv.cv : CV_NONE;
			if ((scv & ~tcv) == 0) return CONVERSION_DERIVED_TO_BASE;
		}
		TypeId target_pointee = program_->types.RemoveTopCv(target_pointer.child);
		const TypeRecord pointee = program_->types.Get(target_pointee);
		if (pointee.kind == TYPE_FUNDAMENTAL &&
			pointee.fundamental == FUND_VOID)
		{
			const TypeRecord source_cv = program_->types.Get(source_pointer.child);
			const TypeRecord target_cv = program_->types.Get(target_pointer.child);
			const std::uint8_t scv = source_cv.kind == TYPE_QUALIFIED ?
				source_cv.cv : CV_NONE;
			const std::uint8_t tcv = target_cv.kind == TYPE_QUALIFIED ?
				target_cv.cv : CV_NONE;
			return (scv & ~tcv) == 0 ? CONVERSION_STANDARD : CONVERSION_INVALID;
		}
		return QualificationConversion(from, to) ?
			CONVERSION_STANDARD : CONVERSION_INVALID;
	}
	if (IsPointer(from) && to == program_->types.Fundamental(FUND_BOOL))
		return CONVERSION_BOOLEAN;
	if (IsArithmetic(from) && IsArithmetic(to))
	{
		const EntityId target_entity = EntityOf(to);
		if (target_entity != kNoEntity &&
			(program_->entities[target_entity].flavor == NAMED_ENUM ||
			 program_->entities[target_entity].flavor == NAMED_ENUM_CLASS))
			return CONVERSION_INVALID;
		if (IsIntegral(from) && IsIntegral(to) &&
			IntegralPromotionType(from) == to)
			return CONVERSION_PROMOTION;
		return CONVERSION_STANDARD;
	}
	return CONVERSION_INVALID;
}

ConversionRank SemanticAnalyzer::Conversion(const ExpressionInfo& source,
	TypeId target) const
{
	return Conversion(source.type, source.category,
		source.integer_literal_zero, target);
}

ExpressionInfo SemanticAnalyzer::ApplyTarget(ExpressionInfo value,
	TypeId target, ConversionRank known_conversion)
{
	if (target == kNoType)
	{
		RecordExpressionFacts(value);
		return value;
	}
	const ConversionRank conversion = known_conversion == CONVERSION_INVALID ?
		Conversion(value, target) : known_conversion;
	if (conversion == CONVERSION_INVALID)
	{
		const CallConversionFact implicit =
			CallConversion(value, target, 0, 0);
		if (implicit.rank != CONVERSION_INVALID)
			return ApplyCallArgument(value, target, &implicit);
		throw std::runtime_error("invalid implicit conversion from " +
			program_->RenderType(value.type) + " to " +
			program_->RenderType(target));
	}
	const TypeRecord target_record = program_->types.Get(target);
	const TypeId nonreference = target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE ? target_record.child : target;
	const TypeId conversion_target =
		program_->types.RemoveTopCv(nonreference);
	const TypeId conversion_source =
		program_->types.RemoveTopCv(EffectiveType(value.type));
	const TypeRecord& conversion_target_record =
		program_->types.Get(conversion_target);
	const bool target_is_bool =
		conversion_target_record.kind == TYPE_FUNDAMENTAL &&
		conversion_target_record.fundamental == FUND_BOOL;
	const TypeRecord& conversion_source_record =
		program_->types.Get(conversion_source);
	const bool reference_target = target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE;
	const std::uint32_t source_object = ExpressionObject(value);
	const std::uint32_t source_complete_object =
		ExpressionCompleteObject(value);
	std::uint32_t source_address = ExpressionAddress(value);
	if (source_address == kNoConstexprAddress && value.constant &&
		IsNullptr(conversion_source))
		source_address = NullConstexprAddress();
	if (source_address == kNoConstexprAddress &&
		((!reference_target &&
		  (conversion_source_record.kind == TYPE_ARRAY ||
		   conversion_source_record.kind == TYPE_FUNCTION)) ||
		 (reference_target && (constant_expression_required_depth_ != 0 ||
		  constexpr_evaluation_depth_ != 0))))
		source_address = LvalueAddress(&value);
	const bool source_is_bool =
		conversion_source_record.kind == TYPE_FUNDAMENTAL &&
		conversion_source_record.fundamental == FUND_BOOL;
	const EntityId conversion_source_entity = EntityOf(conversion_source);
	const bool source_is_enum = conversion_source_entity != kNoEntity &&
		(program_->entities[conversion_source_entity].flavor == NAMED_ENUM ||
		 program_->entities[conversion_source_entity].flavor == NAMED_ENUM_CLASS);
	if (target_is_bool && !source_is_bool)
		dump_.nodes[value.node].boolean_conversion = true;
	if (source_is_enum && conversion_source != conversion_target &&
		IsArithmetic(conversion_target))
		dump_.nodes[value.node].enum_arithmetic_conversion = true;
	if (!target_is_bool && IsIntegral(conversion_source, true) &&
		IsIntegral(conversion_target, true) &&
		program_->SizeOf(conversion_target) < program_->SizeOf(conversion_source))
		dump_.nodes[value.node].integer_narrowing_conversion = true;
	if (conversion == CONVERSION_DERIVED_TO_BASE)
	{
		const std::uint32_t object = ExpressionObject(value);
		const std::uint32_t complete_object = ExpressionCompleteObject(value);
		const bool binds_temporary =
			target_record.kind == TYPE_LVALUE_REFERENCE &&
			value.category != VALUE_LVALUE;
		if (binds_temporary && EntityOf(value.type) != kNoEntity &&
			dump_.nodes[value.node].kind != DUMP_TEMPORARY_OBJECT)
			value = MaterializeTemporary(value);
		const std::size_t projections =
			BaseProjectionCount(value.type, nonreference);
		if (projections == std::numeric_limits<std::size_t>::max() ||
			projections > std::numeric_limits<std::uint32_t>::max())
			throw std::logic_error(
				"derived conversion has no bounded base path");
		const ValueCategory category = binds_temporary ? VALUE_PRVALUE :
			target_record.kind == TYPE_LVALUE_REFERENCE ? VALUE_LVALUE :
			target_record.kind == TYPE_RVALUE_REFERENCE ?
			VALUE_XVALUE : VALUE_PRVALUE;
		const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
			nonreference, category);
		dump_.nodes[cast].base_projection_count =
			static_cast<std::uint32_t>(projections);
		dump_.Add(cast, value.node);
		value.node = cast;
		value.type = nonreference;
		value.category = category;
		value.binding = kNoBinding;
		value.constant = false;
		value.constexpr_object = kNoConstexprObject;
		value.constexpr_complete_object = kNoConstexprObject;
		std::uint64_t projection_offset = 0;
		const std::uint32_t projected = ProjectConstexprObject(
			object, nonreference, &projection_offset);
		if (projected != kNoConstexprObject)
		{
			SetExpressionSubobject(&value, projected, complete_object);
			if (source_address != kNoConstexprAddress &&
				projection_offset <= static_cast<std::uint64_t>(
					std::numeric_limits<std::int64_t>::max()))
				source_address = OffsetConstexprAddress(source_address,
					static_cast<std::int64_t>(projection_offset), false);
		}
		++expression_count_;
	}
	if (reference_target &&
		!SimilarUnqualified(EffectiveType(value.type), target_record.child) &&
		conversion != CONVERSION_DERIVED_TO_BASE)
	{
		const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
			nonreference, VALUE_PRVALUE);
		dump_.Add(cast, value.node);
		value.node = cast;
		value.type = nonreference;
		value.category = VALUE_PRVALUE;
		value.binding = kNoBinding;
		value.constant = false;
		value.constexpr_object = kNoConstexprObject;
		value.constexpr_complete_object = kNoConstexprObject;
		++expression_count_;
	}
	if (value.integer_literal_zero &&
		(IsPointer(nonreference) || IsNullptr(nonreference)))
	{
		value.type = program_->types.RemoveTopCv(nonreference);
		dump_.nodes[value.node].type = value.type;
		source_address = NullConstexprAddress();
	}
	if (reference_target &&
		program_->types.RemoveTopCv(EffectiveType(value.type)) !=
			program_->types.RemoveTopCv(target_record.child) &&
		IsArithmetic(value.type) && IsArithmetic(target_record.child))
	{
		const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
			target_record.child, VALUE_PRVALUE);
		dump_.Add(cast, value.node);
		value.node = cast;
		value.type = target_record.child;
		value.category = VALUE_PRVALUE;
		++expression_count_;
	}
	if (reference_target && source_address != kNoConstexprAddress)
		SetExpressionLvalueAddress(&value, source_address);
	else if (IsPointer(conversion_target) &&
		source_address != kNoConstexprAddress)
	{
		SetExpressionAddress(&value, source_address);
		if (source_object != kNoConstexprObject &&
			source_complete_object != kNoConstexprObject)
			SetExpressionSubobject(&value, source_object,
				source_complete_object);
	}
	else if (target_is_bool &&
		(IsPointer(Decay(conversion_source)) || IsNullptr(conversion_source)) &&
		source_address != kNoConstexprAddress)
	{
		const ConstexprAddressValue* address =
			ConstexprAddressAt(source_address);
		SetExpressionScalar(&value, ConstexprScalarValue(
			static_cast<std::int64_t>(address &&
				address->kind != CONSTEXPR_ADDRESS_NULL)));
	}
	if (value.constant &&
		(IsIntegral(conversion_source, true) || IsFloating(conversion_source)) &&
		(IsIntegral(conversion_target, true) || IsFloating(conversion_target)))
		SetExpressionScalar(&value, ConvertScalarConstant(conversion_source,
			conversion_target, ExpressionScalar(value)));
	RecordExpressionFacts(value);
	return value;
}

bool SemanticAnalyzer::IsModifiableLvalue(const ExpressionInfo& value) const
{
	return value.category == VALUE_LVALUE && !IsConst(value.type) &&
		!program_->types.IsFunction(EffectiveType(value.type)) &&
		!IsVoid(value.type);
}
ExpressionInfo SemanticAnalyzer::AnalyzeExpression(NodeId node, ScopeId scope,
	TypeId target)
{
	if (node == kNoNode) throw std::runtime_error("missing expression");
	ExpressionInfo prepared;
	if (ReusePreparedBracedExpression(node, target, &prepared)) return prepared;
	if (arena_->IsTag(node, "parenthesized-expression"))
		return AnalyzeExpression(FirstSemanticChild(node), scope, target);
	if (arena_->IsTag(node, "literal"))
	{
		std::string spelling = arena_->Payload(node);
		if (spelling.compare(0, 11, "TT_LITERAL:") == 0)
			spelling.erase(0, 11);
		ExpressionInfo result;
		if (spelling.find('"') != std::string::npos)
		{
			if (!spelling.empty() && spelling[0] == '"' &&
				TryAnalyzeUserDefinedStringLiteral(
				spelling, scope, target, &result)) return result;
			result = MakeStringLiteral(spelling);
		}
		else
		{
			if (spelling.find('\'') == std::string::npos &&
				TryAnalyzeUserDefinedNumericLiteral(
					spelling, scope, target, &result)) return result;
			result = MakeBuiltinScalarLiteral(spelling, node);
		}
		return ApplyTarget(result, target);
	}
	if (arena_->IsTag(node, "keyword-literal"))
	{
		const std::string spelling = PayloadSource(node);
		ExpressionInfo result;
		if (spelling == "this")
			result = AnalyzeThisExpression(scope);
		else if (spelling == "nullptr")
		{
			result = MakeLiteral(program_->types.Fundamental(FUND_NULLPTR_T),
				program_->names.Intern(arena_->Payload(node)));
			result.constant = true;
			result.value = 0;
		}
		else if (spelling == "true" || spelling == "false")
		{
			result = MakeLiteral(program_->types.Fundamental(FUND_BOOL),
				program_->names.Intern(arena_->Payload(node)));
			result.constant = true;
			result.value = spelling == "true";
		}
		else throw std::runtime_error("unsupported keyword literal");
		return ApplyTarget(result, target);
	}
	if (arena_->IsTag(node, "id-expression"))
	{
		const std::string spelling = arena_->Payload(node);
		ExpressionInfo local;
		if (FindChild(node, "structured-type-name") == kNoNode &&
			spelling.find("::") == std::string::npos &&
			TryAnalyzeConstexprLocal(spelling, target, &local))
			return local;
		ExpressionInfo function_id;
		if (AnalyzeFunctionId(node, scope, target, &function_id))
			return function_id;
		return AnalyzeNamedValue(spelling, scope, target, node);
	}
	if (arena_->IsTag(node, "call-expression"))
		return AnalyzeCall(node, scope, target);
	if (arena_->IsTag(node, "unary-expression") ||
		arena_->IsTag(node, "postfix-expression"))
		return ApplyTarget(AnalyzeUnary(node, scope, target), target);
	if (arena_->IsTag(node, "binary-expression"))
		return ApplyTarget(AnalyzeBinary(node, scope), target);
	if (arena_->IsTag(node, "assignment-expression"))
		return ApplyTarget(AnalyzeAssignment(node, scope), target);
	if (arena_->IsTag(node, "cast-expression"))
		return ApplyTarget(AnalyzeCast(node, scope), target);
	if (arena_->IsTag(node, "conditional-expression"))
		return ApplyTarget(AnalyzeConditional(node, scope), target);
	if (arena_->IsTag(node, "subscript-expression"))
		return ApplyTarget(AnalyzeSubscript(node, scope), target);
	if (arena_->IsTag(node, "sizeof-expression"))
		return ApplyTarget(AnalyzeSizeof(node, scope), target);
	if (arena_->IsTag(node, "sizeof-pack-expression")) return ApplyTarget(AnalyzeSizeofPackExpression(node, scope), target);
	if (arena_->IsTag(node, "type-trait-expression") &&
		(PayloadSource(node) == "alignof" || PayloadSource(node) == "__alignof"))
		return ApplyTarget(AnalyzeSizeof(node, scope), target);
	if (arena_->IsTag(node, "type-trait-expression") &&
		PayloadSource(node) == "noexcept")
		return ApplyTarget(AnalyzeNoexcept(node, scope), target);
	if (arena_->IsTag(node, "braced-init-list"))
		return AnalyzeBracedInit(node, scope, target);
	if (arena_->IsTag(node, "new-expression")) return AnalyzeNewExpression(node, scope, target);
	if (arena_->IsTag(node, "delete-expression"))
		return AnalyzeDeleteExpression(node, scope, target);
	if (arena_->IsTag(node, "member-expression"))
		return ApplyTarget(AnalyzeMember(node, scope), target);
	throw std::runtime_error("unsupported PA12 expression: " + arena_->Tag(node));
}

BindingId SemanticAnalyzer::SelectOverload(ScopeId scope,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments,
	const std::vector<BindingId>& candidates,
	const ExpressionInfo* object, ObjectConversionFact* object_conversion,
	std::vector<CallConversionFact>* argument_conversions)
{
	const std::size_t explicit_arity = argument_syntax.size();
	const std::size_t arity = explicit_arity + (object ? 1 : 0);
	if (arity != 0 && candidates.size() >
		std::numeric_limits<std::size_t>::max() / arity)
		throw std::runtime_error("overload conversion table is too large");
	std::vector<ConversionRank> ranks(candidates.size() * arity,
		CONVERSION_ELLIPSIS);
	std::vector<std::size_t> base_distances(candidates.size() * arity,
		std::numeric_limits<std::size_t>::max());
	std::vector<ConversionRank> actual_object_ranks(candidates.size(),
		CONVERSION_INVALID);
	std::vector<std::size_t> actual_object_distances(candidates.size(),
		std::numeric_limits<std::size_t>::max());
	std::vector<CallConversionFact> conversions(
		candidates.size() * explicit_arity);
	CallConversionTable conversion_cache;
	std::vector<bool> viable(candidates.size(), true);
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		++overload_candidates_;
		const FunctionInfo& function = GetFunction(candidates[c]);
		const TypeRecord function_type = program_->types.Get(function.type);
		const std::size_t explicit_offset = object ? 1 : 0;
		if (function.member_owner != kNoType)
		{
			if (!object)
			{
				viable[c] = false;
				continue;
			}
			if (!RefQualifierViable(*object, function_type))
			{
				viable[c] = false;
				continue;
			}
			TypeId object_type = function.member_owner;
			if ((function_type.cv & CV_CONST) != 0)
				object_type = program_->types.Qualify(object_type, CV_CONST);
			if ((function_type.cv & CV_VOLATILE) != 0)
				object_type = program_->types.Qualify(object_type, CV_VOLATILE);
			const ConversionRank object_rank = MemberObjectConversion(*object,
				program_->types.Pointer(object_type), candidates[c]);
			actual_object_ranks[c] = object_rank;
			if (object_rank == CONVERSION_DERIVED_TO_BASE)
				actual_object_distances[c] = BaseConversionDistance(
					object->type, program_->types.Pointer(object_type));
			std::size_t selection_distance = actual_object_distances[c];
			const ConversionRank selection_rank =
				MemberCandidateSelectionRank(
					*object, candidates[c], object_rank, &selection_distance);
			ranks[c * arity] = selection_rank;
			if (selection_rank == CONVERSION_DERIVED_TO_BASE)
				base_distances[c * arity] = selection_distance;
			if (object_rank == CONVERSION_INVALID)
			{
				viable[c] = false;
				continue;
			}
		}
		else if (object)
			ranks[c * arity] = CONVERSION_EXACT;
		std::size_t required_parameters = function_type.parameter_count;
		while (required_parameters != 0 &&
			required_parameters <= function.parameters.size() &&
			function.parameters[required_parameters - 1].default_argument != kNoNode)
			--required_parameters;
		if (explicit_arity < required_parameters ||
			(!function_type.variadic &&
			 explicit_arity > function_type.parameter_count))
		{
			viable[c] = false;
			continue;
		}
		const TypeId* parameter_data = program_->types.Parameters(function.type);
		std::vector<TypeId> parameters;
		if (function_type.parameter_count != 0)
			parameters.assign(parameter_data,
				parameter_data + function_type.parameter_count);
		for (std::size_t a = 0; a < explicit_arity; ++a)
		{
			ConversionRank rank = CONVERSION_ELLIPSIS;
			if (a < function_type.parameter_count)
			{
				if (arguments[a].type != kNoType)
				{
					const CallConversionFact conversion = CallConversion(
						arguments[a], parameters[a], &conversion_cache, a);
					conversions[c * explicit_arity + a] = conversion;
					rank = conversion.rank;
				}
				else
				{
					std::vector<BindingId> argument_functions = FunctionCandidates(
							scope, arena_->Payload(argument_syntax[a]), 0,
							argument_syntax[a]);
					TypeId desired = program_->types.RemoveTopCv(parameters[a]);
					if (program_->types.Get(desired).kind == TYPE_POINTER)
						desired = program_->types.Get(desired).child;
					const std::vector<BindingId> target_templates =
						FunctionTemplateTargetCandidates(scope,
							arena_->Payload(argument_syntax[a]), desired, argument_syntax[a]);
					for (std::size_t f = 0; f < target_templates.size(); ++f)
						if (std::find(argument_functions.begin(),
							argument_functions.end(), target_templates[f]) ==
							argument_functions.end())
							argument_functions.push_back(target_templates[f]);
					std::size_t matches = 0;
					for (std::size_t f = 0; f < argument_functions.size(); ++f)
						if (GetFunction(argument_functions[f]).type == desired)
							++matches;
					rank = matches == 1 ?
						CONVERSION_EXACT : CONVERSION_INVALID;
				}
			}
			ranks[c * arity + explicit_offset + a] = rank;
			if (rank == CONVERSION_DERIVED_TO_BASE)
				base_distances[c * arity + explicit_offset + a] =
					BaseConversionDistance(arguments[a].type, parameters[a]);
			if (rank == CONVERSION_INVALID) viable[c] = false;
		}
	}
	const auto better = [this, &ranks, &base_distances, &conversions,
		&candidates, &arguments, arity, explicit_arity, object](
		std::size_t left, std::size_t right) -> bool
	{
		++overload_order_comparisons_;
		bool no_worse = true;
		bool strictly_better = false;
		for (std::size_t a = 0; a < arity; ++a)
		{
			const ConversionRank left_rank = ranks[left * arity + a];
			const ConversionRank right_rank = ranks[right * arity + a];
			const std::size_t left_distance =
				base_distances[left * arity + a];
			const std::size_t right_distance =
				base_distances[right * arity + a];
			if (left_rank > right_rank ||
				(left_rank == right_rank &&
				 left_rank == CONVERSION_DERIVED_TO_BASE &&
				 left_distance > right_distance))
				no_worse = false;
			if (left_rank < right_rank ||
				(left_rank == right_rank &&
				 left_rank == CONVERSION_DERIVED_TO_BASE &&
				 left_distance < right_distance))
				strictly_better = true;
			if (left_rank == CONVERSION_USER_DEFINED &&
				right_rank == CONVERSION_USER_DEFINED &&
				a >= (object ? 1u : 0u))
			{
				const std::size_t argument = a - (object ? 1u : 0u);
				const int preference = CompareCallConversions(
					conversions[left * explicit_arity + argument],
					conversions[right * explicit_arity + argument]);
				if (preference < 0) no_worse = false;
				if (preference > 0) strictly_better = true;
			}
		}
		if (!no_worse) return false;
		if (strictly_better) return true;
		const FunctionInfo& left_function = GetFunction(candidates[left]);
		const FunctionInfo& right_function = GetFunction(candidates[right]);
		const TypeRecord& left_type =
			program_->types.Get(left_function.type);
		const TypeRecord& right_type =
			program_->types.Get(right_function.type);
		const TypeId* left_parameters =
			program_->types.Parameters(left_function.type);
		const TypeId* right_parameters =
			program_->types.Parameters(right_function.type);
		for (std::size_t a = 0; a < explicit_arity; ++a)
		{
			if (a >= left_type.parameter_count ||
				a >= right_type.parameter_count)
				continue;
			const int preference = CompareReferenceBindings(
				arguments[a], left_parameters[a], right_parameters[a]);
			if (preference != 0) return preference > 0;
		}
		if (object && left_function.member_owner != kNoType &&
			right_function.member_owner != kNoType)
		{
			const int preference = CompareImplicitObjectBindings(
				object->category, program_->types.Get(left_function.type),
				program_->types.Get(right_function.type));
			if (preference != 0) return preference > 0;
		}
		const int template_preference = CompareFunctionTemplateConstraints(
			left_function, right_function);
		if (template_preference != 0) return template_preference > 0;
		return !left_function.template_specialization &&
			right_function.template_specialization;
	};

	std::size_t viable_count = 0;
	std::size_t champion = candidates.size();
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		if (!viable[c]) continue;
		++viable_count;
		if (champion == candidates.size())
		{
			champion = c;
			continue;
		}
		if (better(c, champion)) champion = c;
	}
	if (viable_count == 0) throw std::runtime_error("no viable overload");
	if (viable_count != 1)
		for (std::size_t other = 0; other < candidates.size(); ++other)
		{
			if (other == champion || !viable[other]) continue;
			if (!better(champion, other))
				throw std::runtime_error("ambiguous overload");
		}
	if (object_conversion && object)
	{
		object_conversion->rank = actual_object_ranks[champion];
		if (object_conversion->rank == CONVERSION_DERIVED_TO_BASE)
		{
			const std::size_t projections = actual_object_distances[champion];
			if (projections == std::numeric_limits<std::size_t>::max() ||
				projections > std::numeric_limits<std::uint32_t>::max())
				throw std::logic_error(
					"selected object conversion has no bounded base path");
			object_conversion->base_projection_count =
				static_cast<std::uint32_t>(projections);
		}
	}
	if (argument_conversions)
	{
		argument_conversions->clear();
		argument_conversions->reserve(explicit_arity);
		for (std::size_t a = 0; a < explicit_arity; ++a)
			argument_conversions->push_back(
				conversions[champion * explicit_arity + a]);
	}
	return candidates[champion];
}

ExpressionInfo SemanticAnalyzer::BuildResolvedCall(BindingId selected,
	ScopeId scope, const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>& arguments,
	const ExpressionInfo* object, TypeId target, EntityId naming_class,
	const ObjectConversionFact* object_conversion,
	const std::vector<CallConversionFact>* argument_conversions,
	bool suppress_virtual_dispatch)
{
	if (GetFunction(selected).deleted_special_member)
		throw std::runtime_error("selected special member is deleted");
	EntityId object_class = kNoEntity;
	if (object)
	{
		TypeId object_type = program_->types.RemoveTopCv(
			EffectiveType(object->type));
		const TypeRecord object_shape = program_->types.Get(object_type);
		if (object_shape.kind == TYPE_POINTER) object_type = object_shape.child;
		object_class = EntityOf(object_type);
	}
	if (!CanAccessMember(selected, naming_class, object_class))
		throw std::runtime_error("inaccessible member function");
	const FunctionInfo function = GetFunction(selected);
	const bool nonstatic_member = function.member_owner != kNoType &&
		!program_->bindings[selected].static_member_function;
	const TypeRecord function_type = program_->types.Get(function.type);
	const TypeId* parameter_data = program_->types.Parameters(function.type);
	std::vector<TypeId> parameters;
	if (function_type.parameter_count != 0)
		parameters.assign(parameter_data,
			parameter_data + function_type.parameter_count);
	const TypeId result_type = function_type.child;
	EnsureClassDefinition(result_type);
	const TypeRecord returned = program_->types.Get(result_type);
	const ValueCategory category = returned.kind == TYPE_LVALUE_REFERENCE ?
		VALUE_LVALUE : returned.kind == TYPE_RVALUE_REFERENCE ?
			VALUE_XVALUE : VALUE_PRVALUE;
	const TypeId callable_type = function.member_owner == kNoType ?
		function.type : AdaptMemberFunctionType(selected);
	const BindingId emission_binding = program_->bindings[selected].canonical;
	const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
		result_type, category, 0, emission_binding);
	const std::uint32_t virtual_slot = VirtualSlotFor(selected);
	if (!suppress_virtual_dispatch && object && virtual_slot != kNoDumpEdge)
	{
		dump_.nodes[call].virtual_call = true;
		dump_.nodes[call].virtual_slot = virtual_slot;
	}
	dump_.nodes[call].user_conversion_call = function.conversion_function;
	dump_.nodes[call].explicit_user_conversion_call =
		function.conversion_function && function.explicit_conversion;
	const std::uint32_t callee = MakeDump(DUMP_CALLEE, callable_type,
		VALUE_NONE, function.display_name, emission_binding);
	dump_.Add(call, callee);
	ExpressionInfo converted_object;
	const ExpressionInfo* constexpr_receiver = object;
	std::vector<ExpressionInfo> constexpr_arguments;
	constexpr_arguments.reserve(function_type.parameter_count);
	if (function.member_owner != kNoType)
	{
		if (!object) throw std::logic_error("selected member call has no object");
		const TypeId object_parameter =
			program_->types.Parameters(callable_type)[0];
		ExpressionInfo qualified_object = *object;
		const bool split_qualified_projection = suppress_virtual_dispatch &&
			ApplyQualifiedMemberNamingTarget(
				&qualified_object, naming_class, selected);
		converted_object = ApplyMemberObjectTarget(
			qualified_object, object_parameter, selected,
			split_qualified_projection ? 0 : object_conversion);
		dump_.Add(call, converted_object.node);
		constexpr_receiver = &converted_object;
	}
	for (std::size_t a = 0; a < arguments.size(); ++a)
	{
		ExpressionInfo argument = arguments[a];
		if (a < function_type.parameter_count)
		{
			if (argument.type == kNoType)
				argument = AnalyzeExpression(argument_syntax[a], scope,
					parameters[a]);
			else argument = ApplyCallArgument(argument, parameters[a],
				argument_conversions && a < argument_conversions->size() ?
					&(*argument_conversions)[a] : 0);
			constexpr_arguments.push_back(argument);
		}
		dump_.Add(call, argument.node);
	}
	for (std::size_t a = arguments.size();
		a < function_type.parameter_count; ++a)
	{
		if (a >= function.parameters.size() ||
			function.parameters[a].default_argument == kNoNode)
			throw std::runtime_error("missing default argument fact");
		ExpressionInfo argument = AnalyzeExpression(
			function.parameters[a].default_argument,
			function.parameters[a].default_scope, parameters[a]);
		argument = ApplyCallArgument(argument, parameters[a]);
		dump_.Add(call, argument.node);
		constexpr_arguments.push_back(argument);
	}
	ExpressionInfo result;
	result.node = call;
	result.type = result_type;
	result.category = category;
	result.binding = selected;
	ConstexprScalarValue constexpr_value;
	bool constexpr_has_scalar = false;
	std::uint32_t constexpr_address = kNoConstexprAddress;
	std::uint32_t constexpr_object = kNoConstexprObject;
	std::uint32_t constexpr_complete_object = kNoConstexprObject;
	bool folded_call = false;
	bool evaluated_call = false;
	if (constant_evaluation_suppressed_depth_ == 0 &&
		(constant_expression_required_depth_ != 0 ||
		 constexpr_evaluation_depth_ != 0) &&
		TryEvaluateConstexprFunction(
		selected, constexpr_arguments, &constexpr_value, &constexpr_has_scalar,
		&constexpr_address,
		&constexpr_object, &constexpr_complete_object, constexpr_receiver))
	{
		evaluated_call = true;
		if (returned.kind == TYPE_LVALUE_REFERENCE ||
			returned.kind == TYPE_RVALUE_REFERENCE)
		{
			if (constexpr_has_scalar)
				SetExpressionScalar(&result,
					NormalizeScalarConstant(EffectiveType(result_type),
						constexpr_value));
			if (constexpr_object != kNoConstexprObject)
				SetExpressionSubobject(&result, constexpr_object,
					constexpr_complete_object);
			SetExpressionLvalueAddress(&result, constexpr_address);
		}
		else if (IsPointer(EffectiveType(result_type)))
		{
			SetExpressionAddress(&result, constexpr_address);
			if (constexpr_object != kNoConstexprObject)
				SetExpressionSubobject(&result, constexpr_object,
					constexpr_complete_object);
		}
		else if (constexpr_object != kNoConstexprObject)
		{
			SetExpressionObject(&result, constexpr_object);
			PublishDumpObject(call, constexpr_object);
		}
		else SetExpressionScalar(&result,
			NormalizeScalarConstant(result_type, constexpr_value));
		RecordExpressionFacts(result);
		if (constant_expression_required_depth_ != 0 &&
			preserve_constant_initializer_recipe_depth_ == 0 &&
			!(nonstatic_member &&
			  constant_initializer_required_depth_ != 0 &&
			  (!constexpr_has_scalar ||
			   local_constant_initializer_depth_ != 0)) &&
			constexpr_address == kNoConstexprAddress &&
			constexpr_object == kNoConstexprObject)
		{
			result = MakeLiteral(
				result_type, InternScalar(result_type, constexpr_value));
			SetExpressionScalar(&result,
				NormalizeScalarConstant(result_type, constexpr_value));
			RecordExpressionFacts(result);
			folded_call = true;
		}
	}
	const bool compile_time_only_call = evaluated_call &&
		constant_expression_required_depth_ != 0 &&
		!(nonstatic_member && constant_initializer_required_depth_ != 0 &&
		  (!constexpr_has_scalar ||
		   local_constant_initializer_depth_ != 0));
	if (ShouldDemandResolvedCall(
		selected, folded_call, compile_time_only_call))
		DemandFunction(selected);
	++expression_count_;
	return ApplyTarget(result, target);
}
ExpressionInfo SemanticAnalyzer::AnalyzeCall(NodeId node, ScopeId scope, TypeId target)
{
	const NodeId callee_syntax = FirstSemanticChild(node);
	if (callee_syntax == kNoNode) throw std::runtime_error("call without callee");
	NodeId direct_callee_syntax = callee_syntax;
	bool parenthesized_callee = false;
	while (arena_->IsTag(direct_callee_syntax, "parenthesized-expression")) {
		parenthesized_callee = true;
		direct_callee_syntax = FirstSemanticChild(direct_callee_syntax);
		if (direct_callee_syntax == kNoNode)
			throw std::runtime_error("empty parenthesized callee");
	}
	NodeId arguments_node = FindChild(node, "argument-list");
	if (arguments_node == kNoNode)
		arguments_node = FindChild(node, "braced-init-list");
	if (arguments_node == kNoNode)
	{
		std::uint32_t edge = arena_->FirstEdge(node);
		if (edge != kNoEdge) edge = arena_->NextEdge(edge);
		if (edge != kNoEdge) arguments_node = arena_->EdgeChild(edge);
	}
	std::vector<NodeId> argument_syntax;
	if (arguments_node != kNoNode)
		for (std::uint32_t argument = arena_->FirstEdge(arguments_node);
			argument != kNoEdge; argument = arena_->NextEdge(argument))
			argument_syntax.push_back(arena_->EdgeChild(argument));
	std::vector<ExpressionInfo> analyzed_arguments;
	bool arguments_analyzed = false;
	ExpressionInfo member_call;
	if (AnalyzeExplicitDestructorCall(callee_syntax, scope, argument_syntax,
		target, &member_call)) return member_call;
	if (AnalyzeDirectMemberCall(callee_syntax, scope, argument_syntax,
		target, &member_call)) return member_call;
	if (ExpandCallArgumentPacks(argument_syntax, scope, &argument_syntax, &analyzed_arguments)) arguments_analyzed = true;
	std::size_t constexpr_callee_local = 0;
	const bool local_callable =
		arena_->IsTag(direct_callee_syntax, "id-expression") &&
		FindChild(direct_callee_syntax, "structured-type-name") == kNoNode &&
		arena_->Payload(direct_callee_syntax).find("::") == std::string::npos &&
		FindConstexprLocal(program_->names.Intern(
			arena_->Payload(direct_callee_syntax)), &constexpr_callee_local);
	if (arena_->IsTag(direct_callee_syntax, "id-expression") &&
		!local_callable)
	{
		const std::string spelling = arena_->Payload(direct_callee_syntax);
		NamePath callee_path = StructuredNamePath(direct_callee_syntax);
		if (callee_path.Empty()) callee_path = ParseNamePath(spelling);
		const bool qualified_callee = callee_path.global || callee_path.Size() > 1;
		ExpressionInfo builtin;
		if (TryAnalyzeImmediateBuiltinCall(
			spelling, scope, argument_syntax, target, &builtin))
			return builtin;
		EntityId function_naming_class = kNoEntity;
		bool retained_lookup = false;
		std::vector<BindingId> candidates = RetainedFunctionCallCandidates(
			direct_callee_syntax, scope, spelling, &function_naming_class, &retained_lookup);
		if (!arguments_analyzed)
			for (std::size_t i = 0; i < argument_syntax.size(); ++i)
				analyzed_arguments.push_back(AnalyzeExpression(argument_syntax[i], scope));
		arguments_analyzed = true;
		const TypeId cast_type = ResolveFunctionalCastType(scope, spelling,
			direct_callee_syntax);
		const bool type_precedes_functions = FunctionalCastPrecedesFunctions(
				spelling, scope, cast_type, direct_callee_syntax, candidates);
		if (!type_precedes_functions)
			CompleteFunctionCallTemplateCandidates(direct_callee_syntax, scope,
				spelling, analyzed_arguments, retained_lookup, &candidates,
				&function_naming_class);
		if (!type_precedes_functions &&
			!parenthesized_callee && !qualified_callee)
		{
			BeginCandidateCollection();
			std::vector<BindingId> combined;
			bool suppress_adl = retained_lookup &&
				!RetainedCallAllowsArgumentDependentLookup(direct_callee_syntax);
			for (std::size_t i = 0; i < candidates.size(); ++i)
			{
				AddCandidate(candidates[i], &combined);
				if (GetFunction(candidates[i]).member_owner != kNoType)
					suppress_adl = true;
			}
			if (!suppress_adl)
				AppendArgumentDependentCandidates(program_->names.Intern(spelling),
					analyzed_arguments, &combined);
			candidates.swap(combined);
		}
		if (!type_precedes_functions && !candidates.empty())
		{
			bool has_member_candidate = false;
			for (std::size_t i = 0; i < candidates.size(); ++i)
				if (GetFunction(candidates[i]).member_owner != kNoType)
					has_member_candidate = true;
			ExpressionInfo implicit_object;
			const ExpressionInfo* object = 0;
			if (has_member_candidate)
			{
				const NameId this_name = program_->names.Intern("this");
				const LookupResult found_this = program_->LookupName(
					scope, this_name, LOOKUP_ORDINARY);
				if (found_this.ordinary != kNoBinding)
				{
					const BindingRecord& this_binding =
						program_->bindings[found_this.ordinary];
					implicit_object.type = EffectiveType(this_binding.type);
					implicit_object.category = VALUE_LVALUE;
					implicit_object.binding = found_this.ordinary;
					implicit_object.node = MakeDump(DUMP_ID_EXPRESSION,
						implicit_object.type, VALUE_LVALUE, this_name,
						found_this.ordinary);
					object = &implicit_object;
					++expression_count_;
				}
			}
			ObjectConversionFact object_conversion;
			std::vector<CallConversionFact> argument_conversions;
			const BindingId selected = SelectOverload(scope, argument_syntax,
				analyzed_arguments, candidates, object,
				object ? &object_conversion : 0, &argument_conversions);
			return BuildResolvedCall(selected, scope, argument_syntax,
				analyzed_arguments, object, target, function_naming_class,
				object ? &object_conversion : 0, &argument_conversions,
				qualified_callee);
		}
		if (cast_type != kNoType)
		{
			if (IsClassObjectType(cast_type))
				return AnalyzeClassFunctionalCast(cast_type, scope,
					argument_syntax, arguments_node, target);
			if (argument_syntax.size() > 1)
				throw std::runtime_error("too many functional cast arguments");
			if (argument_syntax.empty())
			{
				ExpressionInfo zero = MakeLiteral(cast_type,
					program_->names.Intern("0"));
				zero.constant = true; zero.value = 0;
				dump_.nodes[zero.node].value_initialization = true;
				return ApplyTarget(zero, target);
			}
			ExpressionInfo operand = analyzed_arguments[0];
			if (EntityOf(operand.type) != kNoEntity &&
				ConvertingFunction(operand, cast_type, true).rank !=
					CONVERSION_INVALID)
				return ApplyTarget(
					ApplyExplicitConversion(operand, cast_type), target);
			const TypeRecord cast_record = program_->types.Get(cast_type);
			const ValueCategory cast_category =
				cast_record.kind == TYPE_LVALUE_REFERENCE ? VALUE_LVALUE :
				cast_record.kind == TYPE_RVALUE_REFERENCE ? VALUE_XVALUE :
				VALUE_PRVALUE;
			const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
				cast_type, cast_category);
			dump_.Add(cast, operand.node);
			ExpressionInfo result;
			result.node = cast;
			result.type = cast_type;
			result.category = cast_category;
			result.constant = operand.constant;
			result.value = operand.value;
			++expression_count_;
			return ApplyTarget(result, target);
		}
		if (retained_lookup) throw std::runtime_error("retained call has no viable function");
	}
	ExpressionInfo callee = AnalyzeExpression(callee_syntax, scope);
	if (!arguments_analyzed)
	{
		for (std::size_t i = 0; i < argument_syntax.size(); ++i)
			analyzed_arguments.push_back(
				AnalyzeExpression(argument_syntax[i], scope));
		arguments_analyzed = true;
	}
	ExpressionInfo call_operator;
	if (TryAnalyzeCallOperator(scope, callee, argument_syntax,
		&analyzed_arguments, target, &call_operator))
		return call_operator;
	if (TryAnalyzeCallSurrogate(scope, callee, analyzed_arguments,
		target, &call_operator))
		return call_operator;
	TypeId function_type = program_->types.RemoveTopCv(
		EffectiveType(callee.type));
	TypeRecord callable = program_->types.Get(function_type);
	if (callable.kind == TYPE_POINTER)
	{
		function_type = callable.child;
		callable = program_->types.Get(function_type);
	}
	if (callable.kind != TYPE_FUNCTION)
		throw std::runtime_error("called object is not callable");
	if (argument_syntax.size() < callable.parameter_count || (!callable.variadic && argument_syntax.size() != callable.parameter_count))
		throw std::runtime_error("indirect call arity mismatch");
	ExpressionInfo constexpr_call; if (TryAnalyzeConstexprIndirectCall(&callee, scope, argument_syntax,
		analyzed_arguments, target, &constexpr_call)) return constexpr_call;
	const TypeId* parameter_data = program_->types.Parameters(function_type);
	std::vector<TypeId> parameters;
	if (callable.parameter_count != 0)
		parameters.assign(parameter_data,
			parameter_data + callable.parameter_count);
	const TypeId result_type = callable.child;
	const TypeRecord returned = program_->types.Get(result_type);
	const ValueCategory category = returned.kind == TYPE_LVALUE_REFERENCE ?
		VALUE_LVALUE : returned.kind == TYPE_RVALUE_REFERENCE ?
		VALUE_XVALUE : VALUE_PRVALUE;
	const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
		result_type, category);
	dump_.Add(call, callee.node);
	for (std::size_t a = 0; a < argument_syntax.size(); ++a)
	{
		ExpressionInfo argument = arguments_analyzed ? analyzed_arguments[a] :
			AnalyzeExpression(argument_syntax[a], scope);
		if (a < callable.parameter_count)
			argument = ApplyCallArgument(argument, parameters[a]);
		dump_.Add(call, argument.node);
	}
	ExpressionInfo result;
	result.node = call;
	result.type = result_type;
	result.category = category;
	++expression_count_;
	return ApplyTarget(result, target);
}


ExpressionInfo SemanticAnalyzer::AnalyzeAssignment(NodeId node, ScopeId scope)
{
	const std::uint32_t first = arena_->FirstEdge(node);
	const std::uint32_t second = first == kNoEdge ? kNoEdge :
		arena_->NextEdge(first);
	if (second == kNoEdge) throw std::runtime_error("invalid assignment");
	const NodeId left_syntax = arena_->EdgeChild(first);
	const NodeId right_syntax = arena_->EdgeChild(second);
	ExpressionInfo left = AnalyzeExpression(left_syntax, scope);
	const std::string operation = PayloadSource(node);
	ExpressionInfo right = AnalyzeExpression(right_syntax, scope,
		operation == "=" && arena_->IsTag(right_syntax, "braced-init-list") ?
			EffectiveType(left.type) : kNoType);
	std::vector<NodeId> overloaded_syntax;
	overloaded_syntax.push_back(left_syntax);
	overloaded_syntax.push_back(right_syntax);
	std::vector<ExpressionInfo> overloaded_operands;
	overloaded_operands.push_back(left);
	overloaded_operands.push_back(right);
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator(operation, scope, overloaded_syntax,
		overloaded_operands, operation == "=", kNoType, &overloaded))
		return overloaded;
	(void)ApplyBuiltinAssignmentConversion(operation, left, &right);
	if (operation == "=") right = ApplyTarget(right, EffectiveType(left.type));
	if (!IsModifiableLvalue(left))
		throw std::runtime_error("assignment requires modifiable lvalue");
	const bool pointer_add = IsPointer(left.type) &&
		(operation == "+=" || operation == "-=") && IsIntegral(right.type);
	if (operation != "=")
	{
		const bool additive = operation == "+=" || operation == "-=";
		const bool arithmetic_operation = additive || operation == "*=" ||
			operation == "/=";
		const bool integral_operation = operation == "%=" ||
			operation == "<<=" || operation == ">>=" || operation == "&=" ||
			operation == "|=" || operation == "^=";
		const bool arithmetic = arithmetic_operation &&
			IsArithmetic(left.type) && IsArithmetic(right.type);
		const bool integral = integral_operation && IsIntegral(left.type) &&
			IsIntegral(right.type);
		if (!pointer_add && !arithmetic && !integral)
			throw std::runtime_error("invalid compound assignment");
	}
	const TypeId result_type = EffectiveType(left.type);
	const std::uint32_t expression = MakeDump(DUMP_ASSIGNMENT_EXPRESSION,
		result_type, VALUE_LVALUE, program_->names.Intern(arena_->Payload(node)));
	if (operation != "=" && !pointer_add)
		dump_.nodes[expression].operand_type =
			CommonArithmeticType(left.type, right.type);
	dump_.Add(expression, left.node);
	dump_.Add(expression, right.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = result_type;
	result.category = VALUE_LVALUE;
	if (constexpr_evaluation_depth_ != 0 &&
		constant_evaluation_suppressed_depth_ == 0 &&
		right.constant &&
		(IsIntegral(result_type, true) || IsFloating(result_type)))
	{
		const std::size_t local = left.constexpr_local;
		bool valid = local < constexpr_locals_.size();
		ConstexprScalarValue assigned;
		if (valid)
		{
			try
			{
				assigned = ConvertScalarConstant(
					right.type, result_type, ExpressionScalar(right));
			}
			catch (...)
			{
				valid = false;
			}
		}
		if (valid && operation != "=")
		{
			const std::string binary = operation.substr(0, operation.size() - 1);
			const TypeId operand_type = dump_.nodes[expression].operand_type != kNoType ?
				dump_.nodes[expression].operand_type : result_type;
			try
			{
				const ConstexprScalarValue left_operand = ConvertScalarConstant(
					result_type, operand_type, constexpr_locals_[local].value);
				const ConstexprScalarValue right_operand = ConvertScalarConstant(
					right.type, operand_type, ExpressionScalar(right));
				assigned = ApplyConstantScalarBinary(
					binary, left_operand, right_operand, operand_type);
			}
			catch (...)
			{
				valid = false;
			}
		}
		if (valid)
		{
			assigned = NormalizeScalarConstant(result_type, assigned);
			constexpr_locals_[local].value = assigned;
			SetExpressionScalar(&result, assigned);
			dump_.nodes[expression].constant = true;
			if (assigned.kind == CONSTEXPR_SCALAR_INTEGRAL)
				dump_.nodes[expression].constant_value = assigned.integral;
		}
	}
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeSubscript(NodeId node, ScopeId scope)
{
	const std::uint32_t first = arena_->FirstEdge(node);
	const std::uint32_t second = first == kNoEdge ? kNoEdge :
		arena_->NextEdge(first);
	if (second == kNoEdge) throw std::runtime_error("invalid subscript");
	const NodeId left_syntax = arena_->EdgeChild(first);
	const NodeId right_syntax = arena_->EdgeChild(second);
	ExpressionInfo left = AnalyzeExpression(left_syntax, scope);
	ExpressionInfo right = AnalyzeExpression(right_syntax, scope);
	std::vector<NodeId> overloaded_syntax;
	overloaded_syntax.push_back(left_syntax);
	overloaded_syntax.push_back(right_syntax);
	std::vector<ExpressionInfo> overloaded_operands;
	overloaded_operands.push_back(left);
	overloaded_operands.push_back(right);
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator("[]", scope, overloaded_syntax,
		overloaded_operands, true, kNoType, &overloaded)) return overloaded;
	(void)ApplyBuiltinBinaryConversions("[]", &left, &right);
	if (!IsPointer(Decay(left.type)) && IsPointer(Decay(right.type)))
		std::swap(left, right);
	const TypeId pointer_type = Decay(left.type);
	const TypeRecord pointer = program_->types.Get(pointer_type);
	if (pointer.kind != TYPE_POINTER || !IsIntegral(right.type))
		throw std::runtime_error("invalid subscript operands");
	const std::uint32_t expression = MakeDump(DUMP_SUBSCRIPT_EXPRESSION,
		pointer.child, VALUE_LVALUE);
	dump_.Add(expression, left.node);
	dump_.Add(expression, right.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = pointer.child;
	result.category = VALUE_LVALUE;
	std::uint32_t base_address = ExpressionAddress(left);
	if (base_address == kNoConstexprAddress &&
		program_->types.Get(program_->types.RemoveTopCv(
			EffectiveType(left.type))).kind == TYPE_ARRAY)
		base_address = LvalueAddress(&left);
	if (base_address != kNoConstexprAddress && right.constant &&
		right.constexpr_address == kNoConstexprAddress)
	{
		const std::int64_t step = static_cast<std::int64_t>(
			program_->SizeOf(pointer.child));
		const std::int64_t index = ExpressionScalar(right).integral;
		if (step == 0 || (index <=
			std::numeric_limits<std::int64_t>::max() / step &&
			index >= std::numeric_limits<std::int64_t>::min() / step))
		{
			const std::uint32_t address = OffsetConstexprAddress(base_address,
				index * step, true, step);
			if (address != kNoConstexprAddress)
				SetExpressionLvalueAddress(&result, address);
		}
	}
	if (left.string_unit_begin != kNoDumpEdge && right.constant &&
		right.value >= 0 &&
		static_cast<std::uint64_t>(right.value) < left.string_unit_count)
	{
		const std::size_t index = left.string_unit_begin +
			static_cast<std::size_t>(right.value);
		if (index >= string_literal_units_.size())
			throw std::logic_error("string literal code-unit range is invalid");
		result.constant = true;
		result.value = NormalizeIntegralConstant(
			pointer.child, string_literal_units_[index]);
		RecordExpressionFacts(result);
	}
	else if ((constant_expression_required_depth_ != 0 ||
		constexpr_evaluation_depth_ != 0) &&
		left.constexpr_object != kNoConstexprObject && right.constant &&
		right.constexpr_object == kNoConstexprObject && right.value >= 0)
	{
		const ConstexprObjectElement* element = ConstexprObjectElementAt(
			left.constexpr_object, static_cast<std::size_t>(right.value));
		if (element)
		{
			SetExpressionObjectElement(&result, *element);
			RecordExpressionFacts(result);
		}
	}
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeSizeof(NodeId node, ScopeId scope)
{
	const NodeId operand = FirstSemanticChild(node);
	if (operand == kNoNode) throw std::runtime_error("empty sizeof");
	TypeId measured = kNoType;
	if (arena_->IsTag(operand, "type-id"))
	{
		const NodeId specifiers = FindChild(operand, "type-specifier-seq");
		const NodeId name = specifiers == kNoNode ? kNoNode :
			FirstSemanticChild(specifiers);
		const NodeId declarator = FindChild(operand, "abstract-declarator");
		const NodeId clause = declarator == kNoNode ? kNoNode :
			FindChild(declarator, "parameter-clause");
		NamePath base;
		std::vector<TypeId> explicit_arguments;
		const bool ambiguous_function_call = name != kNoNode &&
			arena_->IsTag(name, "type-name") && clause != kNoNode &&
			FirstSemanticChild(clause) == kNoNode &&
			ParseExplicitTemplateArguments(
				name, scope, &base, &explicit_arguments);
		if (ambiguous_function_call)
		{
			const std::vector<std::size_t> patterns =
				FindFunctionTemplates(scope, base);
			std::vector<BindingId> candidates;
			const std::vector<ExpressionInfo> no_arguments;
			DeduceFunctionTemplatePatterns(patterns, no_arguments,
				&candidates, &explicit_arguments);
			if (candidates.size() == 1)
				measured = program_->types.Get(
					GetFunction(candidates[0]).type).child;
			else if (!candidates.empty())
				throw std::runtime_error(
					"ambiguous function template in sizeof expression");
		}
		if (measured == kNoType) measured = BuildTypeId(operand, scope);
	}
	else if (arena_->IsTag(operand, "id-expression"))
	{
		const std::string spelling = arena_->Payload(operand);
		const NodeId structure = FindChild(operand, "structured-type-name");
		const LookupResult ordinary = structure != kNoNode ?
			LookupStructuredName(operand, scope, LOOKUP_ORDINARY) :
			LookupSpelling(scope, spelling, LOOKUP_ORDINARY);
		if (ordinary.ordinary == kNoBinding)
		{
			const LookupResult type = structure != kNoNode ?
				LookupStructuredName(operand, scope, LOOKUP_TYPE) :
				LookupSpelling(scope, spelling, LOOKUP_TYPE);
			if (type.type != kNoType) measured = type.type;
		}
		else measured = EffectiveType(
			program_->bindings[ordinary.ordinary].type);
	}
	if (measured == kNoType)
	{
		++unevaluated_depth_;
		try
		{
			measured = EffectiveType(AnalyzeExpression(operand, scope).type);
		}
		catch (...)
		{
			--unevaluated_depth_;
			throw;
		}
		--unevaluated_depth_;
	}
	const bool alignment_query = arena_->IsTag(node, "type-trait-expression");
	const std::size_t value = alignment_query ? program_->AlignOf(measured) :
		program_->SizeOf(measured);
	ExpressionInfo result;
	result.type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	result.node = MakeDump(DUMP_SIZEOF_EXPRESSION, result.type, VALUE_PRVALUE);
	dump_.nodes[result.node].template_layout_constant =
		IsClassTemplateSpecializationContext(EntityOf(measured));
	result.constant = true;
	result.value = static_cast<std::int64_t>(value);
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

void SemanticAnalyzer::AnalyzeTemplate(NodeId node, ScopeId scope,
	AccessKind member_access)
{
	const NodeId clause = FindChild(node, "template-parameter-clause");
	const NodeId list = clause == kNoNode ? kNoNode :
		FindChild(clause, "template-parameter-list");
	std::vector<TemplateParameter> parameters;
	std::vector<NameId> parameter_name_list;
	std::vector<NodeId> defaults;
	ParseTemplateParameters(list, scope, &parameters,
		&parameter_name_list, &defaults);
	NodeId target = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (child != clause) target = child;
	}
	if (clause != kNoNode && list == kNoNode && target != kNoNode &&
		AnalyzeExplicitTemplateSpecialization(target, scope, member_access))
		return;
	if (target != kNoNode &&
		!arena_->IsTag(target, "template-declaration"))
		ValidateRetainedTemplateDefinition(target, scope, parameters);
	if (target != kNoNode && arena_->IsTag(target, "alias-declaration"))
	{
		RegisterAliasTemplate(target, scope, member_access, parameters);
		return;
	}
	if (target != kNoNode &&
		AnalyzeFriendClassTemplate(target, scope, parameters)) return;
	if (target != kNoNode && class_template_member_replay_depth_ == 0 &&
		AnalyzeClassTemplateMember(target, scope, parameters)) return;
	if (target != kNoNode &&
		(arena_->IsTag(target, "class-specifier") ||
		 arena_->IsTag(target, "class-forward-declaration")))
	{
		AnalyzeClassTemplate(target, scope, parameters, member_access);
		return;
	}
	if (target != kNoNode && RetainVariableTemplate(
		target, scope, parameters)) return;
	const bool special_member_template = target != kNoNode &&
		(arena_->IsTag(target, "special-member-declaration") ||
		 arena_->IsTag(target, "special-member-definition"));
	if (target == kNoNode ||
		(!arena_->IsTag(target, "simple-declaration") &&
		 !arena_->IsTag(target, "function-definition") &&
		 !special_member_template))
		throw std::runtime_error("unsupported PA12 templated declaration");
	const NodeId specifiers = FindChild(target, "decl-specifier-seq");
	if (specifiers == kNoNode && !special_member_template)
		throw std::runtime_error("invalid PA12 function template");
	const bool definition = arena_->IsTag(target, "function-definition") ||
		arena_->IsTag(target, "special-member-definition");
	const NodeId declarators = definition ? kNoNode :
		FindChild(target, "init-declarator-list");
	if (!definition && declarators == kNoNode && !special_member_template)
		throw std::runtime_error("invalid PA12 function template");
	std::vector<NodeId> pattern_declarators;
	if (definition || special_member_template)
	{
		const NodeId declarator = FindChild(target, "declarator");
		if (declarator == kNoNode)
			throw std::runtime_error("invalid PA12 function template definition");
		pattern_declarators.push_back(declarator);
	}
	else
	{
		for (std::uint32_t edge = arena_->FirstEdge(declarators); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId declarator =
				FindChild(arena_->EdgeChild(edge), "declarator");
			if (declarator != kNoNode) pattern_declarators.push_back(declarator);
		}
	}
	while (function_template_shape_parameters_.size() < parameters.size())
	{
		std::ostringstream generated;
		generated << "__function_template_parameter_shape_"
			<< function_template_shape_parameters_.size();
		const NameId name = program_->names.Intern(generated.str());
		const EntityId entity = program_->NewEntity(name,
			NAMED_TYPENAME_PARAMETER, false, kNoType,
			program_->GlobalScope(), name);
		function_template_shape_parameters_.push_back(
			program_->types.Named(entity));
	}
	std::unordered_set<NameId> parameter_names;
	for (std::size_t i = 0; i < parameters.size(); ++i)
		if (parameters[i].name != 0)
			parameter_names.insert(parameters[i].name);
	bool deferred_dependent_result = false;
	for (std::uint32_t edge = specifiers == kNoNode ? kNoEdge :
		arena_->FirstEdge(specifiers);
		edge != kNoEdge && !deferred_dependent_result;
		edge = arena_->NextEdge(edge))
	{
		const NodeId specifier = arena_->EdgeChild(edge);
		const NodeId structured = FindChild(specifier, "structured-type-name");
		const bool deferred_shape =
			arena_->IsTag(specifier, "decltype-specifier") ||
			(arena_->IsTag(specifier, "decl-specifier") &&
			 FirstSemanticChild(specifier) != kNoNode) ||
			(structured != kNoNode && StructuredNamePath(structured).Size() > 1);
		if (deferred_shape &&
			SyntaxUsesAnyIdentifier(*arena_, specifier, parameter_names))
			deferred_dependent_result = true;
	}
	TypeId dependent_result_shape = kNoType;
	if (deferred_dependent_result)
	{
		if (function_template_dependent_result_shape_ == kNoType)
		{
			const NameId shape_name = program_->names.Intern(
				"__function_template_dependent_result_shape");
			const EntityId shape = program_->NewEntity(shape_name,
				NAMED_TYPENAME_PARAMETER, false, kNoType,
				program_->GlobalScope(), shape_name);
			function_template_dependent_result_shape_ =
				program_->types.Named(shape);
		}
		dependent_result_shape = function_template_dependent_result_shape_;
	}
	for (std::size_t i = 0; i < pattern_declarators.size(); ++i)
	{
		const NodeId declarator = pattern_declarators[i];
		const NodeId exception_qualifier =
			FindChild(declarator, "function-qualifier");
		const NodeId exception_expression = exception_qualifier == kNoNode ?
			kNoNode : FirstSemanticChild(exception_qualifier);
		const bool dependent_exception_specification =
			exception_expression != kNoNode && SyntaxUsesAnyIdentifier(
				*arena_, exception_expression, parameter_names);
		RegisterFunctionTemplatePattern(target, scope, member_access,
			parameters, specifiers, declarator, definition,
			special_member_template, dependent_result_shape,
			dependent_exception_specification);
	}
}
void SemanticAnalyzer::AnalyzeNamespace(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	std::string spelling = arena_->Payload(node);
	const bool unnamed = spelling.empty() || spelling == "<unnamed>";
	if (unnamed) spelling = "<unnamed>";
	const NameId name = program_->names.Intern(spelling);
	const bool is_inline = FindChild(node, "inline") != kNoNode;
	const ScopeId child = program_->OpenNamespace(scope, name, is_inline);
	program_->SetScopeEmissionName(child, unnamed ?
		program_->names.Intern("_GLOBAL__N_1") : name);
	if (scope_prefixes_.size() <= child)
	{
		scope_prefixes_.resize(static_cast<std::size_t>(child) + 1, 0);
		scope_prefix_segments_.resize(static_cast<std::size_t>(child) + 1, 0);
		scope_parents_.resize(static_cast<std::size_t>(child) + 1, kNoScope);
		scope_prefixes_[child] = unnamed ? ScopePrefixId(scope) :
			std::numeric_limits<NameId>::max();
		scope_prefix_segments_[child] = unnamed ? 0 : name;
		scope_parents_[child] = scope;
	}
	if (unnamed) program_->AddUsingEdge(scope, child);
	const std::uint32_t output_node = MakeDump(DUMP_NAMESPACE, kNoType,
		VALUE_NONE, name);
	dump_.Add(output_parent, output_node);
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId declaration = arena_->EdgeChild(edge);
		if (IsDeclaration(declaration))
			AnalyzeDeclaration(declaration, child, output_node, false);
	}
}

void SemanticAnalyzer::AnalyzeDeclaration(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool local)
{
	if (arena_->IsTag(node, "empty-declaration")) return;
	if (arena_->IsTag(node, "layout-pack-push"))
	{
		const std::int64_t parsed = ParseInteger(arena_->Payload(node));
		if (parsed <= 0 ||
			(static_cast<std::uint64_t>(parsed) &
			 (static_cast<std::uint64_t>(parsed) - 1)) != 0)
			throw std::runtime_error("invalid layout packing alignment");
		pack_alignment_stack_.push_back(current_pack_alignment_);
		current_pack_alignment_ = static_cast<std::size_t>(parsed);
		return;
	}
	if (arena_->IsTag(node, "layout-pack-pop"))
	{
		if (pack_alignment_stack_.empty()) current_pack_alignment_ = 0;
		else
		{
			current_pack_alignment_ = pack_alignment_stack_.back();
			pack_alignment_stack_.pop_back();
		}
		return;
	}
	if (arena_->IsTag(node, "template-declaration"))
	{
		AnalyzeTemplate(node, scope);
		return;
	}
	if (arena_->IsTag(node, "static-assert-declaration"))
		return AnalyzeStaticAssert(node, scope);
	if (arena_->IsTag(node, "explicit-instantiation-declaration") ||
		arena_->IsTag(node, "explicit-instantiation-definition"))
	{
		AnalyzeExplicitInstantiation(node, scope,
			arena_->IsTag(node, "explicit-instantiation-definition"));
		return;
	}
	if (arena_->IsTag(node, "namespace-definition"))
	{
		AnalyzeNamespace(node, scope, output_parent);
		return;
	}
	if (arena_->IsTag(node, "namespace-alias-definition") ||
		arena_->IsTag(node, "using-directive") ||
		arena_->IsTag(node, "using-declaration") ||
		arena_->IsTag(node, "alias-declaration"))
	{
		AnalyzeUsing(node, scope, output_parent, local);
		return;
	}
	if (arena_->IsTag(node, "simple-declaration"))
	{
		AnalyzeSimple(node, scope, output_parent, local);
		return;
	}
	if (arena_->IsTag(node, "function-definition"))
	{
		AnalyzeFunction(node, scope, output_parent);
		return;
	}
	if (arena_->IsTag(node, "special-member-definition") ||
		arena_->IsTag(node, "special-member-declaration"))
	{
		AnalyzeOutOfClassSpecialMember(node, scope);
		return;
	}
	if (arena_->IsTag(node, "class-specifier") ||
		arena_->IsTag(node, "class-forward-declaration"))
	{
		const TypeId type = AnalyzeClass(node, scope, std::string(), false);
		const EntityId entity = EntityOf(type);
		if (local && arena_->Payload(node).empty() && entity != kNoEntity &&
			program_->entities[entity].flavor == NAMED_UNION)
		{
			std::ostringstream generated;
			generated << "__anonymous_union_storage__" << arena_->TokenFirst(node)
				<< '_' << arena_->TokenLast(node);
			const NameId storage_name = program_->names.Intern(generated.str());
			const BindingId storage = program_->AddBinding(scope, BIND_VARIABLE,
				storage_name, type);
			const std::uint32_t simple = MakeDump(DUMP_SIMPLE_DECLARATION);
			const std::uint32_t variable = MakeDump(DUMP_VARIABLE, type,
				VALUE_NONE, storage_name, storage);
			AddDefaultConstructor(variable, storage, type);
			dump_.Add(simple, variable);
			dump_.Add(output_parent, simple);
			const std::vector<BindingId>& members = entity_data_members_[entity];
			for (std::size_t i = 0; i < members.size(); ++i)
			{
				const BindingRecord source = program_->bindings[members[i]];
				if (program_->LookupDirect(scope, source.name,
					LOOKUP_ORDINARY).ordinary != kNoBinding)
					throw std::runtime_error(
						"anonymous union member conflicts in block scope");
				const BindingId injected = program_->AddBinding(scope, BIND_VARIABLE,
					source.name, source.type, source.constant, source.value,
					source.display_flavor, source.display_type_name);
				if (injected_fact_by_binding_.size() <= injected)
					injected_fact_by_binding_.resize(
						static_cast<std::size_t>(injected) + 1, kNoDumpEdge);
				injected_fact_by_binding_[injected] =
					static_cast<std::uint32_t>(injected_members_.size());
				injected_members_.push_back(
					InjectedMemberInfo(storage, members[i]));
			}
		}
		return;
	}
	if (arena_->IsTag(node, "enum-specifier"))
	{
		AnalyzeEnum(node, scope, std::string(), false);
		if (local)
		{
			const std::uint32_t simple = MakeDump(DUMP_SIMPLE_DECLARATION);
			dump_.Add(output_parent, simple);
		}
		return;
	}
	if (arena_->IsTag(node, "linkage-specification"))
	{
		const LanguageLinkage previous_linkage = current_language_linkage_;
		current_language_linkage_ = arena_->Payload(node) == "C" ?
			LANGUAGE_LINKAGE_C : LANGUAGE_LINKAGE_CPP;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (IsDeclaration(child))
				AnalyzeDeclaration(child, scope, output_parent, local);
		}
		current_language_linkage_ = previous_linkage;
		return;
	}
	throw std::runtime_error("unsupported PA12 declaration: " + arena_->Tag(node));
}

void SemanticAnalyzer::AnalyzeSimple(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool local, bool qualified_lexical_scope,
	bool demanded_template_storage)
{
	if (local && AnalyzeQualifiedAssignmentStatement(
		node, scope, output_parent))
		return;
	if (local && AnalyzeAmbiguousCallStatement(node, scope, output_parent))
		return;
	if (local && AnalyzeAmbiguousDirectInitializer(
		node, scope, output_parent))
		return;
	const NodeId specifiers = FindChild(node, "decl-specifier-seq");
	const NodeId list = FindChild(node, "init-declarator-list");
	std::string hint;
	EntityId declaration_class_context = kNoEntity;
	if (list != kNoNode)
	{
		const NodeId first = FirstSemanticChild(list);
		const NodeId declarator = first == kNoNode ? kNoNode :
			FindChild(first, "declarator");
		if (declarator != kNoNode && DeclaratorName(declarator) != 0)
			hint = program_->names.Get(DeclaratorName(declarator));
		if (declarator != kNoNode)
		{
			NamePath owner_path = DeclaratorNamePath(declarator);
			if (owner_path.global || owner_path.Size() > 1)
			{
				owner_path.Pop();
				const LookupResult owner_type = LookupPath(scope, owner_path,
					LOOKUP_TYPE);
				if (owner_type.type != kNoType)
					declaration_class_context = EntityOf(owner_type.type);
			}
		}
	}
	const EntityId previous_class_context = current_class_context_;
	if (declaration_class_context != kNoEntity)
		current_class_context_ = declaration_class_context;
	const SpecInfo spec = BuildSpecifiers(specifiers, scope, hint,
		list != kNoNode);
	if (spec.virtual_specifier)
		throw std::runtime_error(
			"virtual specifier is only allowed in a class definition");
	if (list == kNoNode)
	{
		if (local)
		{
			const std::uint32_t empty = MakeDump(DUMP_SIMPLE_DECLARATION);
			dump_.Add(output_parent, empty);
		}
		current_class_context_ = previous_class_context;
		return;
	}
	const std::uint32_t owner = local ? MakeDump(DUMP_SIMPLE_DECLARATION) :
		output_parent;
	if (local) dump_.Add(output_parent, owner);
	for (std::uint32_t edge = arena_->FirstEdge(list); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId item = arena_->EdgeChild(edge);
		const NodeId declarator = FindChild(item, "declarator");
		if (declarator == kNoNode) throw std::runtime_error("missing declarator");
		const NamePath declared_path = DeclaratorNamePath(declarator);
		const ScopeId declaration_scope =
			qualified_lexical_scope ? program_->ParentScope(scope) :
			declared_path.global || declared_path.Size() > 1 ?
				ResolveOwner(scope, declared_path) : scope;
		if (declaration_scope == kNoScope)
			throw std::runtime_error("variable owner not found");
		const ScopeId semantic_scope = qualified_lexical_scope ?
			scope : declaration_scope;
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type,
			semantic_scope);
		parsed.name = declared_path.Last();
		if (parsed.name == 0) throw std::runtime_error("unnamed declaration");
		if (spec.is_typedef)
		{
			program_->AddBinding(scope, BIND_TYPE_ALIAS, parsed.name, parsed.type);
			const std::uint32_t alias = MakeDump(DUMP_TYPE_ALIAS, parsed.type,
				VALUE_NONE, parsed.name);
			dump_.Add(owner, alias);
			continue;
		}
		if (program_->types.IsFunction(parsed.type))
		{
			AnalyzeSimpleFunctionDeclaration(item, declarator, scope,
				declaration_scope, owner, declared_path, spec, parsed);
			continue;
		}
		if (spec.is_constexpr)
			parsed.type = program_->types.Qualify(parsed.type, CV_CONST);
		const NodeId initializer_node = FindChild(item, "initializer");
		if (spec.storage_class != STORAGE_CLASS_EXTERN ||
			initializer_node != kNoNode)
		{
			EnsureClassDefinition(parsed.type);
			DemandClassTemplateMemberDefinitions(
				DestructedEntity(parsed.type));
		}
		if (spec.is_constexpr && !IsConstexprLiteralType(parsed.type))
			throw std::runtime_error(
				"constexpr variable does not have literal type");
		const LookupResult occupied =
			program_->LookupDirect(declaration_scope, parsed.name, LOOKUP_ORDINARY);
		if (occupied.ordinary != kNoBinding &&
			program_->bindings[occupied.ordinary].kind == BIND_FUNCTION)
			throw std::runtime_error("variable conflicts with function binding");
		if (qualified_lexical_scope)
			parsed.type = CompleteQualifiedStaticArrayType(
				occupied.ordinary, parsed.type);
		const BindingId binding = program_->AddBinding(declaration_scope,
			BIND_VARIABLE,
			parsed.name, parsed.type);
		PublishVariableDeclarationFacts(binding, declaration_scope,
			parsed.name, parsed.type, spec, local);
		const bool static_constant_definition = IsStaticConstantDefinition(binding, initializer_node);
		const bool constexpr_class_default =
			spec.is_constexpr && IsClassObjectType(parsed.type) &&
			!static_constant_definition;
		if (spec.is_constexpr && initializer_node == kNoNode &&
			!constexpr_class_default &&
			!static_constant_definition &&
			!(qualified_lexical_scope && program_->bindings[binding].constant))
			throw std::runtime_error("constexpr variable requires initializer");
		ExpressionInfo initializer;
		bool has_initializer = initializer_node != kNoNode;
		if (initializer_node != kNoNode)
		{
			const bool require_constant =
				ShouldProbeConstantInitialization(local, spec, parsed.type);
			const bool preserve_runtime_recipe = !local && spec.is_constexpr &&
				IsClassObjectType(parsed.type) &&
				FindChild(initializer_node, "paren-initializer") != kNoNode &&
				arena_->HasDescendantTag(initializer_node, "call-expression");
			initializer = AnalyzeConstantAwareVariableInitializer(initializer_node,
				semantic_scope, parsed.type, local, require_constant,
				preserve_runtime_recipe);
			if (program_->types.Get(parsed.type).kind == TYPE_ARRAY &&
				program_->types.Get(parsed.type).bound == 0)
			{
				parsed.type = initializer.type;
				program_->bindings[binding].type = parsed.type;
			}
			PublishConstantVariableInitializer(
				binding, parsed.type, spec, initializer);
			if (preserve_runtime_recipe)
				DemandRuntimeInitializerFunctions(initializer.node);
		}
		else if (constexpr_class_default)
		{
			initializer = AnalyzeDefaultConstexprObjectInitializer(
				parsed.type, semantic_scope, local);
			PublishConstantVariableInitializer(
				binding, parsed.type, spec, initializer);
			has_initializer = true;
		}
		PublishCanonicalBindingConstant(binding);
		const bool deferred_template_constant_storage = !has_initializer &&
			qualified_lexical_scope && program_->bindings[binding].constant &&
			!demanded_template_storage &&
			ClassTemplateHasNonTypeParameter(declaration_class_context);
		if (!has_initializer &&
			(qualified_lexical_scope || static_constant_definition) &&
			!deferred_template_constant_storage)
			has_initializer = MaterializeConstantDefinitionInitializer(
				binding, &parsed.type, &initializer);
		if (deferred_template_constant_storage) continue;
		const bool declaration_only = !local && !has_initializer &&
			program_->bindings[binding].storage_class == STORAGE_CLASS_EXTERN;
		const std::uint32_t variable = MakeDump(DUMP_VARIABLE, parsed.type,
			VALUE_NONE, parsed.name, binding);
		if (has_initializer) dump_.Add(variable, initializer.node);
		else if (!declaration_only && DestructedEntity(parsed.type) != kNoEntity)
		{
			const EntityId object = DestructedEntity(parsed.type);
			if (!qualified_lexical_scope || object == kNoEntity ||
				!program_->entities[object].trivial_default_constructor)
				AddDefaultConstructor(variable, binding, parsed.type);
		}
		dump_.Add(owner, variable);
		RegisterVariableLifetimeAndStorage(scope, local, declaration_only,
			variable, binding, parsed.type,
			has_initializer && HasConstantInitializerFact(initializer));
		if (local && has_initializer)
		{
			const TypeKind declared_kind = program_->types.Get(parsed.type).kind;
			const bool control_dependent =
				HasControlDependentTemporary(initializer.node);
			if ((declared_kind == TYPE_LVALUE_REFERENCE ||
					 declared_kind == TYPE_RVALUE_REFERENCE) &&
				!control_dependent)
			{
				std::vector<std::uint32_t> temporaries;
				CollectTemporaryObjects(initializer.node, &temporaries);
				if (!temporaries.empty())
				{
					AddTemporaryLifetimeObligation(scope, temporaries.back());
					for (std::size_t i = temporaries.size() - 1; i != 0; --i)
					{
						const std::uint32_t action =
							MakeTemporaryDestructorAction(temporaries[i - 1]);
						if (action != kNoDumpEdge) dump_.Add(owner, action);
					}
				}
			}
			else
			{
				const std::size_t edge_count = dump_.edges.size();
				AppendFullExpressionDestructionActions(initializer.node, owner);
				if (control_dependent && dump_.edges.size() != edge_count &&
					!InitializationActionsAreNonthrowing(initializer.node))
				{
					dump_.nodes[owner].full_expression_staging = true;
					MarkFullExpressionCalls(initializer.node);
				}
			}
		}
	}
	current_class_context_ = previous_class_context;
}

void SemanticAnalyzer::AnalyzeFunction(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool deferred_member_definition)
{
	const NodeId declarator = FindChild(node, "declarator");
	const NamePath path = DeclaratorNamePath(declarator);
	ScopeId structured_owner = kNoScope;
	const NodeId structure = DeclaratorNameStructure(declarator);
	if (!deferred_member_definition && structure != kNoNode &&
		(path.global || path.Size() > 1))
		(void)LookupStructuredName(structure, scope,
			LOOKUP_ORDINARY, &structured_owner);
	const ScopeId owner = deferred_member_definition ?
		program_->ParentScope(scope) :
		structured_owner != kNoScope ? structured_owner :
		ResolveOwner(scope, path);
	if (owner == kNoScope) throw std::runtime_error("function owner not found");
	const EntityId previous_class = current_class_context_;
	const EntityId declaration_class = program_->EntityForScope(owner);
	if (declaration_class != kNoEntity)
		current_class_context_ = declaration_class;
	const ScopeId semantic_scope = deferred_member_definition ? scope : owner;
	const SpecInfo spec = BuildSpecifiers(FindChild(node, "decl-specifier-seq"),
		semantic_scope, std::string(), true);
	if (spec.virtual_specifier ||
		FindChild(declarator, "virt-specifier") != kNoNode)
		throw std::runtime_error(
			"virtual specifier is only allowed in a class definition");
	DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type,
		semantic_scope);
	parsed.name = path.Last();
	if (!program_->types.IsFunction(parsed.type))
		throw std::runtime_error("function definition has non-function type");
	if (spec.is_constexpr)
		parsed.type = ApplyConstexprDeclaredFunctionType(parsed.type,
			owner, parsed.name, declaration_class);
	if (spec.is_constexpr)
		ValidateConstexprCallableType(parsed.type, false);
	const BindingId binding = DeclareFunction(owner, parsed.name,
		parsed.type, parsed.parameters, true, false, spec.storage_class,
		current_language_linkage_, IsNonthrowing(declarator, semantic_scope));
	PublishInlineFunctionFacts(
		binding, spec.inline_specifier || spec.is_constexpr);
	ValidateFunctionRefQualifier(binding);
	ValidateNonmemberOperator(binding);
	FunctionInfo& function = GetMutableFunction(binding);
	function.constexpr_function =
		function.constexpr_function || spec.is_constexpr;
	function.definition_body = FindChild(node, "compound-statement");
	if (deferred_member_definition)
	{
		if (declaration_class == kNoEntity ||
			program_->bindings[binding].member_owner != declaration_class)
			throw std::runtime_error(
				"class template member definition has no declaration");
		function.definition_body = FindChild(node, "compound-statement");
		function.lexical_scope = semantic_scope;
		function.deferred = true;
		current_class_context_ = previous_class;
		return;
	}
	if (program_->bindings[binding].virtual_function)
		MarkVtableDemand(program_->bindings[binding].member_owner);
	function.deferred = spec.is_constexpr;
	if (function.deferred)
	{
		current_class_context_ = previous_class;
		return;
	}
	const bool member = function.member_owner != kNoType;
	const TypeId output_type = member ?
		AdaptMemberFunctionType(binding) : parsed.type;
	const std::uint32_t output_node = MakeDump(DUMP_FUNCTION_DEFINITION,
		output_type, VALUE_NONE, function.display_name, binding);
	dump_.Add(output_parent, output_node);
	const ScopeId function_scope = NewScope(owner, SCOPE_FUNCTION, parsed.name,
		ScopePrefixId(owner));
	if (member)
	{
		const TypeId this_type = program_->types.Parameters(output_type)[0];
		const NameId this_name = program_->names.Intern("this");
		const BindingId this_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, this_name, this_type);
		dump_.Add(output_node, MakeDump(DUMP_PARAMETER, this_type,
			VALUE_NONE, this_name, this_binding));
	}
	for (std::size_t i = 0; i < parsed.parameters.size(); ++i)
	{
		ParameterInfo parameter = parsed.parameters[i];
		if (parameter.name == 0 && i < function.parameters.size())
			parameter.name = function.parameters[i].name;
		const BindingId parameter_binding = program_->AddBinding(function_scope,
			BIND_PARAMETER, parameter.name, ParameterBindingType(parameter));
		BindFunctionParameterPackElement(function_scope, parameter.pack_name, parameter_binding);
		const std::uint32_t parameter_node = MakeDump(DUMP_PARAMETER,
			parameter.function_type, VALUE_NONE, parameter.name, parameter_binding);
		dump_.Add(output_node, parameter_node);
		AddLifetimeObligation(function_scope, parameter_binding, parameter.function_type, false);
	}
	const TypeId previous_return = current_return_type_;
	const BindingId previous_function = current_function_context_;
	current_return_type_ = program_->types.Get(parsed.type).child;
	current_class_context_ = function.friend_of != kNoEntity ?
		function.friend_of : program_->bindings[binding].member_owner;
	current_function_context_ = program_->bindings[binding].canonical;
	const NodeId body = FindChild(node, "compound-statement");
	if (body != kNoNode)
	{
		AnalyzeCompound(body, function_scope, output_node);
		FinalizeNamedReturnSlot(output_node);
	}
	current_return_type_ = previous_return;
	current_class_context_ = previous_class;
	current_function_context_ = previous_function;
}

void SemanticAnalyzer::AnalyzeCompound(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	const ScopeId block = NewScope(scope, SCOPE_BLOCK, 0, ScopePrefixId(scope));
	const std::uint32_t compound = MakeDump(DUMP_COMPOUND_STATEMENT);
	dump_.Add(output_parent, compound);
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId child = arena_->EdgeChild(edge);
		if (IsDeclaration(child))
			AnalyzeDeclaration(child, block, compound, true);
		else AnalyzeStatement(child, block, compound);
	}
	AppendScopeDestructionActions(block, compound, CompoundCleanupStop(scope));
}

void SemanticAnalyzer::AnalyzeSubstatement(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	if (arena_->IsTag(node, "compound-statement"))
		AnalyzeCompound(node, scope, output_parent);
	else
	{
		const ScopeId child = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		if (IsDeclaration(node))
			AnalyzeDeclaration(node, child, output_parent, true);
		else AnalyzeStatement(node, child, output_parent);
		AppendScopeDestructionActions(child, output_parent, scope);
	}
}

void SemanticAnalyzer::AnalyzeCondition(NodeId node, ScopeId scope,
	std::uint32_t output_parent, bool switch_condition)
{
	const std::uint32_t condition = MakeDump(DUMP_CONDITION);
	dump_.Add(output_parent, condition);
	NodeId declaration_node = node;
	const NodeId first_child = FirstSemanticChild(node);
	if (first_child != kNoNode &&
		arena_->IsTag(first_child, "condition-declaration"))
		declaration_node = first_child;
	const NodeId specifiers = FindChild(declaration_node, "decl-specifier-seq");
	if (specifiers != kNoNode)
	{
		const NodeId declarator = FindChild(declaration_node, "declarator");
		const SpecInfo spec = BuildSpecifiers(specifiers, scope, std::string(), true);
		DeclaratorInfo parsed = BuildDeclarator(declarator, spec.type, scope);
		const BindingId binding = program_->AddBinding(scope, BIND_VARIABLE,
			parsed.name, parsed.type);
		const NodeId initializer = FindChild(declaration_node, "initializer");
		ExpressionInfo value = AnalyzeVariableInitializer(initializer,
			scope, parsed.type, true);
		if (constexpr_evaluation_depth_ != 0 && value.constant &&
			IsIntegral(parsed.type, true))
		{
			program_->bindings[binding].constant = true;
			program_->bindings[binding].value =
				NormalizeIntegralConstant(parsed.type, value.value);
		}
		const std::uint32_t declaration = MakeDump(DUMP_CONDITION_DECLARATION);
		const std::uint32_t variable = MakeDump(DUMP_VARIABLE, parsed.type,
			VALUE_NONE, parsed.name, binding);
		dump_.Add(variable, value.node);
		dump_.Add(declaration, variable);
		dump_.Add(condition, declaration);
		if (switch_condition)
		{
			if (!IsIntegral(parsed.type, true) &&
				EntityOf(parsed.type) == kNoEntity)
				throw std::runtime_error("invalid switch condition");
			if (!IsIntegral(parsed.type, true))
			{
				ExpressionInfo declared;
				declared.node = MakeDump(DUMP_ID_EXPRESSION, parsed.type,
					VALUE_LVALUE, parsed.name, binding);
				declared.type = parsed.type;
				declared.category = VALUE_LVALUE;
				declared.binding = binding;
				++expression_count_;
				const ExpressionInfo converted = ApplyExplicitConversion(declared,
					program_->types.Fundamental(FUND_INT));
				dump_.Add(condition, converted.node);
			}
		}
		else if (!IsArithmetic(parsed.type) && !IsPointer(parsed.type))
		{
			ExpressionInfo declared;
			declared.node = MakeDump(DUMP_ID_EXPRESSION, parsed.type,
				VALUE_LVALUE, parsed.name, binding);
			declared.type = parsed.type;
			declared.category = VALUE_LVALUE;
			declared.binding = binding;
			++expression_count_;
			const ExpressionInfo converted = ApplyExplicitConversion(declared,
				program_->types.Fundamental(FUND_BOOL));
			dump_.Add(condition, converted.node);
		}
		RegisterConditionLifetime(scope, binding, parsed.type, value, condition);
		return;
	}
	ExpressionInfo value = AnalyzeExpression(FirstSemanticChild(node), scope);
	if (switch_condition)
	{
		if (!IsIntegral(value.type, true))
			throw std::runtime_error("invalid switch condition");
	}
	else if (!IsArithmetic(value.type) && !IsPointer(value.type) &&
		!IsNullptr(value.type))
		value = ApplyExplicitConversion(value,
			program_->types.Fundamental(FUND_BOOL));
	dump_.Add(condition, value.node);
	AppendFullExpressionDestructionActions(value.node, condition);
	const std::uint32_t first = dump_.nodes[condition].first_edge;
	if (first != kNoDumpEdge && dump_.edges[first].next != kNoDumpEdge)
	{
		MarkFullExpressionCalls(value.node);
		AppendUnwindDestructionActions(scope, condition);
	}
}

void SemanticAnalyzer::AnalyzeStatement(NodeId node, ScopeId scope,
	std::uint32_t output_parent)
{
	if (arena_->IsTag(node, "compound-statement"))
	{
		AnalyzeCompound(node, scope, output_parent);
		return;
	}
	if (arena_->IsTag(node, "return-statement"))
	{
		AnalyzeReturnStatement(node, scope, output_parent);
		return;
	}
	if (arena_->IsTag(node, "expression-statement"))
	{
		const std::uint32_t statement = MakeDump(DUMP_EXPRESSION_STATEMENT);
		dump_.Add(output_parent, statement);
		const NodeId expression = FirstSemanticChild(node);
		if (expression != kNoNode)
		{
			ExpressionInfo value = MaterializeDiscardedClassResult(
				AnalyzeExpression(expression, scope));
			dump_.Add(statement, value.node);
			AppendFullExpressionDestructionActions(value.node, statement);
			StageNestedTemplateTemporaryCleanup(value.node, statement, scope);
		}
		return;
	}
	if (arena_->IsTag(node, "if-statement"))
	{
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(DUMP_IF_STATEMENT);
		dump_.Add(output_parent, statement);
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, false);
			else if (arena_->IsTag(child, "then") || arena_->IsTag(child, "else"))
			{
				const std::uint32_t branch = MakeDump(arena_->IsTag(child, "then") ?
					DUMP_THEN : DUMP_ELSE);
				dump_.Add(statement, branch);
				AnalyzeSubstatement(FirstSemanticChild(child), control, branch);
			}
		}
		AppendScopeDestructionActions(control, output_parent, scope);
		return;
	}
	if (arena_->IsTag(node, "while-statement") ||
		arena_->IsTag(node, "do-statement"))
	{
		const bool is_do = arena_->IsTag(node, "do-statement");
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(is_do ?
			DUMP_DO_STATEMENT : DUMP_WHILE_STATEMENT);
		dump_.Add(output_parent, statement);
		++loop_depth_;
		break_cleanup_stops_.push_back(control);
		continue_cleanup_stops_.push_back(control);
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, false);
			else AnalyzeSubstatement(child, control, statement);
		}
		continue_cleanup_stops_.pop_back();
		break_cleanup_stops_.pop_back();
		--loop_depth_;
		AppendScopeDestructionActions(control, output_parent, scope);
		return;
	}
	if (arena_->IsTag(node, "for-statement"))
	{
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(DUMP_FOR_STATEMENT);
		dump_.Add(output_parent, statement);
		++loop_depth_;
		break_cleanup_stops_.push_back(control);
		continue_cleanup_stops_.push_back(control);
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "for-init-statement"))
			{
				const std::uint32_t init = MakeDump(DUMP_FOR_INIT_STATEMENT);
				dump_.Add(statement, init);
				const NodeId value = FirstSemanticChild(child);
				if (value != kNoNode)
				{
					if (IsDeclaration(value))
						AnalyzeDeclaration(value, control, init, true);
					else dump_.Add(init, MaterializeDiscardedClassResult(
						AnalyzeExpression(value, control)).node);
					if (!IsDeclaration(value) &&
						dump_.nodes[init].first_edge != kNoDumpEdge)
					{
						const std::uint32_t expression =
							dump_.edges[dump_.nodes[init].first_edge].child;
						const std::size_t edge_count = dump_.edges.size();
						AppendFullExpressionDestructionActions(
							expression, init);
						if (dump_.edges.size() != edge_count)
						{
							MarkFullExpressionCalls(expression);
							AppendUnwindDestructionActions(control, init);
						}
					}
				}
			}
			else if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, false);
			else if (arena_->IsTag(child, "iteration"))
			{
				const std::uint32_t iteration = MakeDump(DUMP_ITERATION);
				dump_.Add(statement, iteration);
				const ExpressionInfo value = MaterializeDiscardedClassResult(
					AnalyzeExpression(FirstSemanticChild(child), control));
				dump_.Add(iteration, value.node);
				const std::size_t edge_count = dump_.edges.size();
				AppendFullExpressionDestructionActions(value.node, iteration);
				if (dump_.edges.size() != edge_count)
				{
					MarkFullExpressionCalls(value.node);
					AppendUnwindDestructionActions(control, iteration);
				}
			}
			else AnalyzeSubstatement(child, control, statement);
		}
		continue_cleanup_stops_.pop_back();
		break_cleanup_stops_.pop_back();
		--loop_depth_;
		AppendScopeDestructionActions(control, output_parent, scope);
		return;
	}
	if (arena_->IsTag(node, "switch-statement"))
	{
		const ScopeId control = NewScope(scope, SCOPE_BLOCK, 0,
			ScopePrefixId(scope));
		const std::uint32_t statement = MakeDump(DUMP_SWITCH_STATEMENT);
		dump_.Add(output_parent, statement);
		++switch_depth_;
		break_cleanup_stops_.push_back(control);
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (arena_->IsTag(child, "condition"))
				AnalyzeCondition(child, control, statement, true);
			else AnalyzeSubstatement(child, control, statement);
		}
		break_cleanup_stops_.pop_back();
		--switch_depth_;
		AppendScopeDestructionActions(control, output_parent, scope);
		return;
	}
	if (arena_->IsTag(node, "break-statement"))
	{
		if (loop_depth_ == 0 && switch_depth_ == 0)
			throw std::runtime_error("break outside loop or switch");
		const std::uint32_t statement = MakeDump(DUMP_BREAK_STATEMENT);
		dump_.Add(output_parent, statement);
		if (break_cleanup_stops_.empty())
			throw std::logic_error("break has no cleanup boundary");
		AppendScopeDestructionActions(scope, statement,
			break_cleanup_stops_.back());
		return;
	}
	if (arena_->IsTag(node, "continue-statement"))
	{
		if (loop_depth_ == 0) throw std::runtime_error("continue outside loop");
		const std::uint32_t statement = MakeDump(DUMP_CONTINUE_STATEMENT);
		dump_.Add(output_parent, statement);
		if (continue_cleanup_stops_.empty())
			throw std::logic_error("continue has no cleanup boundary");
		AppendScopeDestructionActions(scope, statement,
			continue_cleanup_stops_.back());
		return;
	}
	if (arena_->IsTag(node, "case-statement") ||
		arena_->IsTag(node, "default-statement"))
	{
		if (switch_depth_ == 0)
			throw std::runtime_error("case/default outside switch");
		const bool is_case = arena_->IsTag(node, "case-statement");
		const std::uint32_t statement = MakeDump(is_case ?
			DUMP_CASE_STATEMENT : DUMP_DEFAULT_STATEMENT);
		dump_.Add(output_parent, statement);
		bool first = true;
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
		{
			const NodeId child = arena_->EdgeChild(edge);
			if (is_case && first)
			{
				ExpressionInfo label = AnalyzeExpression(child, scope);
				if (!label.constant) throw std::runtime_error("nonconstant case label");
				dump_.Add(statement, label.node);
				first = false;
			}
			else if (IsDeclaration(child))
				AnalyzeDeclaration(child, scope, statement, true);
			else AnalyzeStatement(child, scope, statement);
		}
		return;
	}
	if (arena_->IsTag(node, "labeled-statement"))
	{
		const std::uint32_t statement = MakeDump(DUMP_LABELED_STATEMENT,
			kNoType, VALUE_NONE, program_->names.Intern(arena_->Payload(node)));
		dump_.Add(output_parent, statement);
		const NodeId child = FirstSemanticChild(node);
		if (child == kNoNode) throw std::runtime_error("label without statement");
		AnalyzeStatement(child, scope, statement);
		return;
	}
	if (arena_->IsTag(node, "goto-statement"))
	{
		dump_.Add(output_parent, MakeDump(DUMP_GOTO_STATEMENT,
			kNoType, VALUE_NONE, program_->names.Intern(arena_->Payload(node))));
		return;
	}
	if (IsDeclaration(node))
	{
		AnalyzeDeclaration(node, scope, output_parent, true);
		return;
	}
	throw std::runtime_error("unsupported PA12 statement: " + arena_->Tag(node));
}

void SemanticAnalyzer::RenderNode(std::uint32_t node, std::size_t depth)
{
	RenderLine(dump_.nodes[node], depth);
	for (std::uint32_t edge = dump_.nodes[node].first_edge;
		edge != kNoDumpEdge; edge = dump_.edges[edge].next)
		RenderNode(dump_.edges[edge].child, depth + 1);
}

void SemanticAnalyzer::Render()
{
	RenderNode(root_, 0);
}

void SemanticAnalyzer::Consume(const SyntaxArena& arena, NodeId root)
{
	arena_ = &arena;
	Program program(arena.SharedStrings());
	program_ = &program;
	scope_prefixes_.resize(static_cast<std::size_t>(program.GlobalScope()) + 1, 0);
	scope_prefix_segments_.resize(
		static_cast<std::size_t>(program.GlobalScope()) + 1, 0);
	scope_parents_.resize(static_cast<std::size_t>(program.GlobalScope()) + 1,
		kNoScope);
	program.AddBinding(program.GlobalScope(), BIND_TYPE_ALIAS,
		program.names.Intern("nullptr_t"),
		program.types.Fundamental(FUND_NULLPTR_T));
	root_ = MakeDump(DUMP_TRANSLATION_UNIT);
	(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_NEW);
	(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_DELETE);
	(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY);
	(void)EnsureBuiltinFunction(BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY);
	const std::chrono::steady_clock::time_point analysis_started =
		std::chrono::steady_clock::now();
	for (std::uint32_t edge = arena.FirstEdge(root); edge != kNoEdge;
		edge = arena.NextEdge(edge))
		AnalyzeDeclaration(arena.EdgeChild(edge), program.GlobalScope(), root_, false);
	DemandMaterializedConstructorActions(root_);
	if (function_templates_.empty() && class_templates_.empty())
		for (std::size_t i = 0; i < hidden_friend_anchor_by_entity_.size(); ++i)
			if (hidden_friend_anchor_by_entity_[i] != kNoBinding &&
				!GetFunction(hidden_friend_anchor_by_entity_[i]).constexpr_function)
				DemandFunction(hidden_friend_anchor_by_entity_[i]);
	std::size_t default_demand = 0;
	std::size_t function_demand = 0;
	std::size_t member_definition_demand = 0;
	while (member_definition_demand <
			demanded_class_template_member_definitions_.size() ||
		default_demand < demanded_default_constructor_entities_.size() ||
		function_demand < demanded_functions_.size())
	{
		while (member_definition_demand <
			demanded_class_template_member_definitions_.size())
			ApplyDemandedClassTemplateMemberDefinitions(
				demanded_class_template_member_definitions_[
					member_definition_demand++]);
		while (default_demand < demanded_default_constructor_entities_.size())
			EmitDefaultConstructor(
				demanded_default_constructor_entities_[default_demand++]);
		while (function_demand < demanded_functions_.size())
			EmitDemandedFunction(demanded_functions_[function_demand++]);
	}
	const std::chrono::steady_clock::time_point render_started =
		std::chrono::steady_clock::now();
	if (graph_consumer_) graph_consumer_->Consume(SemanticGraphView(program,
		dump_, namespace_objects_, local_static_objects_, aggregate_helpers_,
		class_polymorphism_, root_));
	if (render_output_) Render();
	if (stats_)
	{
		const std::chrono::steady_clock::time_point finished =
			std::chrono::steady_clock::now();
		stats_->semantic_nodes = dump_.nodes.size();
		stats_->semantic_edges = dump_.edges.size();
		stats_->interned_names = program.names.Size();
		stats_->canonical_types = program.types.Size();
		stats_->scopes = program.ScopeCount();
		stats_->declarations = program.bindings.size() - 1;
		stats_->expressions = expression_count_;
		stats_->class_layouts = class_layouts_;
		stats_->class_layout_member_visits = class_layout_member_visits_;
		stats_->class_zero_offset_subobject_visits =
			class_zero_offset_subobject_visits_;
		stats_->special_member_fact_lookups = special_member_fact_lookups_;
		stats_->special_member_subobject_visits =
			special_member_subobject_visits_;
		stats_->constructor_member_action_visits =
			constructor_member_action_visits_;
		stats_->constructor_base_action_visits =
			constructor_base_action_visits_;
		stats_->constructor_delegation_action_visits =
			constructor_delegation_action_visits_;
		stats_->destructor_subobject_action_visits =
			destructor_subobject_action_visits_;
		stats_->lexical_cleanup_action_visits =
			lexical_cleanup_action_visits_;
		stats_->unwind_cleanup_scope_visits =
			unwind_cleanup_scope_visits_;
		stats_->unwind_cleanup_action_visits =
			unwind_cleanup_action_visits_;
		stats_->temporary_dependency_visits = temporary_dependency_visits_;
		stats_->nonthrowing_action_visits = nonthrowing_action_visits_;
		PublishStaticConstantEvaluationStats();
		stats_->empty_destructor_chain_visits = empty_destructor_chain_visits_;
		stats_->empty_destructor_chain_cache_hits = empty_destructor_chain_cache_hits_;
		stats_->namespace_object_actions = namespace_objects_.size();
		stats_->lookup_queries = program.lookup_queries;
		stats_->lookup_scope_visits = program.lookup_scope_visits;
		stats_->lookup_edge_visits = program.lookup_edge_visits;
		stats_->lookup_cache_hits = program.lookup_cache_hits;
		stats_->lookup_cache_misses = program.lookup_cache_misses;
		stats_->lookup_cache_invalidations =
			program.lookup_cache_invalidations;
		stats_->lookup_cache_dependency_edges =
			program.lookup_cache_dependency_edges;
		stats_->lookup_cache_invalidation_pushes =
			program.lookup_cache_invalidation_pushes;
		stats_->associated_scope_visits = associated_scope_visits_;
		stats_->associated_declaration_visits =
			associated_declaration_visits_;
		stats_->overload_candidates = overload_candidates_;
		stats_->overload_order_comparisons = overload_order_comparisons_;
		stats_->conversion_checks = conversion_checks_;
		stats_->call_conversion_cache_hits = call_conversion_cache_hits_;
		stats_->call_conversion_cache_misses = call_conversion_cache_misses_;
		stats_->braced_fact_cache_hits = braced_fact_cache_hits_;
		stats_->braced_fact_cache_misses = braced_fact_cache_misses_;
		stats_->function_signature_lookups = function_signature_lookups_;
		stats_->polymorphic_classes = polymorphic_classes_;
		stats_->virtual_slots = virtual_slots_;
		stats_->virtual_signature_lookups = virtual_signature_lookups_;
		stats_->virtual_overrides = virtual_overrides_;
		stats_->virtual_slot_lookups = virtual_slot_lookups_;
		stats_->vtable_demands = vtable_demands_;
		stats_->access_checks = access_checks_;
		stats_->access_path_visits = access_path_visits_;
		stats_->access_grant_probes = access_grant_probes_;
		stats_->template_specialization_requests =
			template_specialization_requests_;
		stats_->template_specialization_cache_hits =
			template_specialization_cache_hits_;
		stats_->template_partial_candidates = template_partial_candidates_;
		stats_->template_partial_order_comparisons =
			template_partial_order_comparisons_;
		stats_->template_partial_shape_materializations =
			template_partial_shape_materializations_;
		stats_->template_partial_shape_cache_hits =
			template_partial_shape_cache_hits_;
		stats_->template_partial_deduction_visits =
			template_partial_deduction_visits_;
		stats_->constexpr_call_requests = constexpr_call_requests_;
		stats_->constexpr_call_cache_hits = constexpr_call_cache_hits_;
		stats_->constexpr_local_index_probes =
			constexpr_local_index_probes_;
		stats_->constexpr_scope_index_probes =
			constexpr_scope_index_probes_;
		stats_->constexpr_object_projection_visits =
			constexpr_object_projection_visits_;
		stats_->constexpr_step_visits = constexpr_step_visits_;
		stats_->constexpr_max_depth = constexpr_max_depth_;
		stats_->constexpr_peak_locals = constexpr_peak_locals_;
		stats_->constexpr_scratch_peak_nodes =
			constexpr_scratch_peak_nodes_;
		stats_->demand_worklist_pushes = demand_worklist_pushes_;
		stats_->demanded_function_emissions = demanded_function_emissions_;
		stats_->default_constructor_emissions = default_constructor_emissions_;
		const std::size_t shared_string_storage =
			arena.SharedStrings().StorageBytes();
		const std::size_t program_storage = program.StorageBytes();
		stats_->semantic_storage_bytes =
			(program_storage >= shared_string_storage ?
			 program_storage - shared_string_storage : program_storage) +
			dump_.StorageBytes() + SideStorageBytes();
		stats_->analysis_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				render_started - analysis_started).count());
		stats_->render_nanoseconds = static_cast<std::uint64_t>(
			std::chrono::duration_cast<std::chrono::nanoseconds>(
				finished - render_started).count());
	}
	program_ = 0;
}
}
}
