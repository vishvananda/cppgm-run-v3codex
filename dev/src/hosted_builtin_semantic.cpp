#include "pa12_semantic_detail.h"

#include "hosted_builtin_registry.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

TypeId RemoveCv(TypeTable* types, TypeId type, std::uint8_t removed)
{
	const TypeRecord record = types->Get(type);
	if (record.kind == TYPE_ARRAY)
	{
		const TypeId child = RemoveCv(types, record.child, removed);
		if (child == record.child) return type;
		return record.dependent_bound_parameter == kNoTemplateParameter ?
			types->Array(child, record.bound) :
			types->DependentArray(child, record.dependent_bound_type,
				record.dependent_bound_parameter);
	}
	if (record.kind != TYPE_QUALIFIED) return type;
	const std::uint8_t remaining =
		static_cast<std::uint8_t>(record.cv & ~removed);
	return remaining == CV_NONE ? record.child :
		types->Qualify(record.child, remaining);
}

TypeId RemoveReference(TypeTable* types, TypeId type)
{
	const TypeRecord record = types->Get(type);
	return record.kind == TYPE_LVALUE_REFERENCE ||
		record.kind == TYPE_RVALUE_REFERENCE ? record.child : type;
}

EntityId DirectNamedEntity(const TypeTable& types, TypeId type)
{
	const TypeRecord shape = types.Get(types.RemoveTopCv(type));
	return shape.kind == TYPE_NAMED ? shape.entity : kNoEntity;
}

bool IsFundamentalIntegral(const TypeRecord& record)
{
	if (record.kind == TYPE_BITINT) return true;
	return record.kind == TYPE_FUNDAMENTAL &&
		record.fundamental != FUND_VOID &&
		record.fundamental != FUND_NULLPTR_T &&
		record.fundamental != FUND_FLOAT &&
		record.fundamental != FUND_DOUBLE &&
		record.fundamental != FUND_LONG_DOUBLE &&
		record.fundamental != FUND_FLOAT16 &&
		record.fundamental != FUND_FLOAT32 &&
		record.fundamental != FUND_FLOAT32X &&
		record.fundamental != FUND_FLOAT64 &&
		record.fundamental != FUND_FLOAT64X &&
		record.fundamental != FUND_STDFLOAT128 &&
		record.fundamental != FUND_FLOAT128;
}

bool IsFundamentalFloating(const TypeRecord& record)
{
	return record.kind == TYPE_FUNDAMENTAL &&
		(record.fundamental == FUND_FLOAT ||
		 record.fundamental == FUND_DOUBLE ||
		 record.fundamental == FUND_LONG_DOUBLE ||
		 record.fundamental == FUND_FLOAT16 ||
		 record.fundamental == FUND_FLOAT32 ||
		 record.fundamental == FUND_FLOAT32X ||
		 record.fundamental == FUND_FLOAT64 ||
		 record.fundamental == FUND_FLOAT64X ||
		 record.fundamental == FUND_STDFLOAT128 ||
		 record.fundamental == FUND_FLOAT128);
}

bool IsSignedFundamental(FundamentalKind kind)
{
	return kind == FUND_SIGNED_CHAR || kind == FUND_SHORT_INT ||
		kind == FUND_INT || kind == FUND_LONG_INT ||
		kind == FUND_LONG_LONG_INT || kind == FUND_INT128 ||
		kind == FUND_FLOAT || kind == FUND_DOUBLE ||
		kind == FUND_LONG_DOUBLE || kind == FUND_FLOAT16 ||
		kind == FUND_FLOAT32 || kind == FUND_FLOAT32X ||
		kind == FUND_FLOAT64 || kind == FUND_FLOAT64X ||
		kind == FUND_STDFLOAT128 || kind == FUND_FLOAT128;
}

FundamentalKind SignednessKind(FundamentalKind kind, bool make_unsigned)
{
	switch (kind)
	{
	case FUND_CHAR:
	case FUND_SIGNED_CHAR:
	case FUND_UNSIGNED_CHAR:
		return make_unsigned ? FUND_UNSIGNED_CHAR : FUND_SIGNED_CHAR;
	case FUND_SHORT_INT:
	case FUND_UNSIGNED_SHORT_INT:
	case FUND_CHAR16_T:
		return make_unsigned ? FUND_UNSIGNED_SHORT_INT : FUND_SHORT_INT;
	case FUND_INT:
	case FUND_UNSIGNED_INT:
	case FUND_WCHAR_T:
	case FUND_CHAR32_T:
		return make_unsigned ? FUND_UNSIGNED_INT : FUND_INT;
	case FUND_LONG_INT:
	case FUND_UNSIGNED_LONG_INT:
		return make_unsigned ? FUND_UNSIGNED_LONG_INT : FUND_LONG_INT;
	case FUND_LONG_LONG_INT:
	case FUND_UNSIGNED_LONG_LONG_INT:
		return make_unsigned ? FUND_UNSIGNED_LONG_LONG_INT : FUND_LONG_LONG_INT;
	case FUND_INT128:
	case FUND_UINT128:
		return make_unsigned ? FUND_UINT128 : FUND_INT128;
	default: throw std::runtime_error("invalid signedness transform operand");
	}
}

bool IsEnumEntity(const EntityRecord& entity)
{
	return entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS;
}

bool IsClassEntity(const EntityRecord& entity)
{
	return entity.flavor == NAMED_STRUCT || entity.flavor == NAMED_CLASS ||
		entity.flavor == NAMED_UNION;
}

}

TypeId SemanticAnalyzer::BuildBuiltinTransformType(NodeId node, ScopeId scope)
{
	using namespace hosted_builtin;
	const TypeTransformKind transform =
		FindTypeTransform(PayloadSource(node));
	if (transform == TYPE_TRANSFORM_NONE)
		throw std::runtime_error("unsupported builtin type transform");
	const NodeId operand = FindChild(node, "type-id");
	TypeId type = BuildTypeId(operand, scope);
	if (CandidateSubstitutionFailed() || type == kNoType) return kNoType;
	TypeTable* types = &program_->types;
	if (transform == TYPE_TRANSFORM_REMOVE_CONST)
		return RemoveCv(types, type, CV_CONST);
	if (transform == TYPE_TRANSFORM_REMOVE_VOLATILE)
		return RemoveCv(types, type, CV_VOLATILE);
	if (transform == TYPE_TRANSFORM_REMOVE_CV)
		return RemoveCv(types, type, CV_CONST | CV_VOLATILE);
	if (transform == TYPE_TRANSFORM_REMOVE_REFERENCE)
		return RemoveReference(types, type);
	if (transform == TYPE_TRANSFORM_REMOVE_CVREF)
		return RemoveCv(types, RemoveReference(types, type),
			CV_CONST | CV_VOLATILE);
	if (transform == TYPE_TRANSFORM_REMOVE_POINTER)
	{
		const TypeId unqualified = types->RemoveTopCv(type);
		const TypeRecord record = types->Get(unqualified);
		return record.kind == TYPE_POINTER ? record.child : type;
	}
	if (transform == TYPE_TRANSFORM_REMOVE_ALL_EXTENTS)
	{
		while (types->Get(type).kind == TYPE_ARRAY)
			type = types->Get(type).child;
		return type;
	}
	if (transform == TYPE_TRANSFORM_ADD_POINTER)
	{
		type = RemoveReference(types, type);
		const TypeId pointer = types->TryPointer(type);
		return pointer == kNoType ? type : pointer;
	}
	if (transform == TYPE_TRANSFORM_ADD_LVALUE_REFERENCE ||
		transform == TYPE_TRANSFORM_ADD_RVALUE_REFERENCE)
	{
		const TypeId reference = types->TryReference(
			transform == TYPE_TRANSFORM_ADD_LVALUE_REFERENCE ?
				TYPE_LVALUE_REFERENCE : TYPE_RVALUE_REFERENCE, type);
		return reference == kNoType ? type : reference;
	}
	if (transform == TYPE_TRANSFORM_DECAY)
	{
		type = RemoveCv(types, RemoveReference(types, type),
			CV_CONST | CV_VOLATILE);
		const TypeRecord shape = types->Get(type);
		if (shape.kind == TYPE_ARRAY) return types->Pointer(shape.child);
		if (shape.kind == TYPE_FUNCTION) return types->Pointer(type);
		return type;
	}
	if (transform == TYPE_TRANSFORM_UNDERLYING_TYPE)
	{
		const TypeId unqualified = types->RemoveTopCv(type);
		const TypeRecord shape = types->Get(unqualified);
		if (shape.kind == TYPE_NAMED &&
			IsEnumEntity(program_->entities[shape.entity]))
			return program_->entities[shape.entity].underlying;
		if (FunctionTemplateTypeIsDependent(type)) return type;
		throw std::runtime_error("underlying type operand is not an enum");
	}
	if (transform == TYPE_TRANSFORM_MAKE_SIGNED ||
		transform == TYPE_TRANSFORM_MAKE_UNSIGNED)
	{
		const TypeRecord top = types->Get(type);
		const std::uint8_t cv = top.kind == TYPE_QUALIFIED ? top.cv : CV_NONE;
		TypeId base = types->RemoveTopCv(type);
		TypeRecord shape = types->Get(base);
		if (shape.kind == TYPE_NAMED && IsEnumEntity(program_->entities[shape.entity]))
		{
			base = program_->entities[shape.entity].underlying;
			shape = types->Get(types->RemoveTopCv(base));
		}
		if (shape.kind != TYPE_FUNDAMENTAL)
		{
			if (FunctionTemplateTypeIsDependent(type)) return type;
			throw std::runtime_error("invalid signedness transform operand");
		}
		base = types->Fundamental(SignednessKind(shape.fundamental,
			transform == TYPE_TRANSFORM_MAKE_UNSIGNED));
		return cv == CV_NONE ? base : types->Qualify(base, cv);
	}
	throw std::logic_error("unhandled builtin type transform");
}

ExpressionInfo SemanticAnalyzer::AnalyzeBuiltinTypeTrait(
	NodeId node, ScopeId scope)
{
	using namespace hosted_builtin;
	const TypeTraitKind trait = FindTypeTrait(PayloadSource(node));
	if (trait == TYPE_TRAIT_NONE)
		throw std::runtime_error("unsupported builtin type trait");
	std::vector<TypeId> operands;
	bool dependent = false;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
	{
		const NodeId holder = arena_->EdgeChild(edge);
		if (!arena_->IsTag(holder, "builtin-type-operand")) continue;
		const NodeId type_id = FindChild(holder, "type-id");
		NodeId declarator = FindChild(type_id, "abstract-declarator");
		if (declarator == kNoNode)
			declarator = FindChild(type_id, "declarator");
		const bool expansion =
			FindChild(holder, "type-pack-expansion") != kNoNode ||
			(declarator != kNoNode &&
			 FindChild(declarator, "parameter-pack") != kNoNode);
		if (expansion)
		{
			std::vector<ScopeId> element_scopes;
			if (ExpandPackElementScopes(type_id, scope, &element_scopes))
			{
				for (std::size_t i = 0; i < element_scopes.size(); ++i)
				{
					const TypeId element =
						BuildTypeId(type_id, element_scopes[i]);
					if (CandidateSubstitutionFailed() || element == kNoType)
						return ExpressionInfo();
					operands.push_back(element);
					dependent = dependent ||
						FunctionTemplateTypeIsDependent(element);
				}
				continue;
			}
			if (CandidateSubstitutionFailed()) return ExpressionInfo();
		}
		const TypeId type = BuildTypeId(type_id, scope);
		if (CandidateSubstitutionFailed() || type == kNoType)
			return ExpressionInfo();
		operands.push_back(type);
		dependent = dependent || FunctionTemplateTypeIsDependent(type) ||
			expansion;
	}
	if (operands.empty())
		throw std::runtime_error("builtin type trait has no operands");
	bool value = false;
	std::int64_t integral_value = 0;
	TypeId result_type = program_->types.Fundamental(FUND_BOOL);
	if (!dependent)
	{
		if (trait != TYPE_TRAIT_IS_SAME)
			for (std::size_t i = 0; i < operands.size(); ++i)
			{
				const EntityId entity = DirectNamedEntity(program_->types, operands[i]);
				if (entity != kNoEntity &&
					IsClassEntity(program_->entities[entity]))
					EnsureClassDefinition(operands[i]);
			}
		const TypeId first = operands[0];
		const TypeId unqualified = program_->types.RemoveTopCv(first);
		const TypeRecord shape = program_->types.Get(unqualified);
		const EntityId entity = DirectNamedEntity(program_->types, first);
		const EntityRecord* named = entity == kNoEntity ? 0 :
			&program_->entities[entity];
		if (trait == TYPE_TRAIT_IS_POLYMORPHIC && named &&
			IsClassEntity(*named) && !named->complete)
			throw std::runtime_error(
				"polymorphic trait requires a complete class type");
		if (trait == TYPE_TRAIT_IS_BASE_OF && operands.size() == 2)
		{
			const EntityId base = EntityOf(operands[0]);
			const EntityId derived = EntityOf(operands[1]);
			if (base != derived && derived != kNoEntity &&
				IsClassEntity(program_->entities[derived]) &&
				!program_->entities[derived].complete)
				throw std::runtime_error(
					"base-of trait requires a complete derived type");
		}
		if (trait == TYPE_TRAIT_ARRAY_RANK && operands.size() == 1)
		{
			TypeId ranked = program_->types.RemoveTopCv(first);
			while (program_->types.Get(ranked).kind == TYPE_ARRAY)
			{
				++integral_value;
				ranked = program_->types.RemoveTopCv(
					program_->types.Get(ranked).child);
			}
			result_type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
		}
		else if (trait == TYPE_TRAIT_IS_SAME && operands.size() == 2)
			value = operands[0] == operands[1];
		else if (trait == TYPE_TRAIT_IS_POINTER && operands.size() == 1)
			value = shape.kind == TYPE_POINTER;
		else if (trait == TYPE_TRAIT_IS_REFERENCE && operands.size() == 1)
			value = shape.kind == TYPE_LVALUE_REFERENCE ||
				shape.kind == TYPE_RVALUE_REFERENCE;
		else if (trait == TYPE_TRAIT_IS_FUNCTION && operands.size() == 1)
			value = shape.kind == TYPE_FUNCTION;
		else if (trait == TYPE_TRAIT_IS_MEMBER_POINTER && operands.size() == 1)
			value = shape.kind == TYPE_MEMBER_POINTER;
		else if (trait == TYPE_TRAIT_IS_MEMBER_FUNCTION_POINTER &&
			operands.size() == 1)
			value = shape.kind == TYPE_MEMBER_POINTER &&
				program_->types.Get(shape.child).kind == TYPE_FUNCTION;
		else if (trait == TYPE_TRAIT_IS_MEMBER_OBJECT_POINTER &&
			operands.size() == 1)
			value = shape.kind == TYPE_MEMBER_POINTER &&
				program_->types.Get(shape.child).kind != TYPE_FUNCTION;
		else if (trait == TYPE_TRAIT_IS_INTEGRAL && operands.size() == 1)
			value = IsFundamentalIntegral(shape);
		else if (trait == TYPE_TRAIT_IS_FLOATING_POINT && operands.size() == 1)
			value = IsFundamentalFloating(shape);
		else if (trait == TYPE_TRAIT_IS_SIGNED && operands.size() == 1)
			value = shape.kind == TYPE_FUNDAMENTAL &&
				IsSignedFundamental(shape.fundamental);
		else if (trait == TYPE_TRAIT_IS_ENUM && operands.size() == 1)
			value = named && IsEnumEntity(*named);
		else if (trait == TYPE_TRAIT_IS_UNION && operands.size() == 1)
			value = named && named->flavor == NAMED_UNION;
		else if (trait == TYPE_TRAIT_IS_CLASS && operands.size() == 1)
			value = named && IsClassEntity(*named) &&
				named->flavor != NAMED_UNION;
		else if (trait == TYPE_TRAIT_IS_SCALAR && operands.size() == 1)
			value = IsFundamentalIntegral(shape) || IsFundamentalFloating(shape) ||
				(shape.kind == TYPE_FUNDAMENTAL &&
				 shape.fundamental == FUND_NULLPTR_T) ||
				shape.kind == TYPE_POINTER || shape.kind == TYPE_MEMBER_POINTER ||
				shape.kind == TYPE_COMPLEX ||
				(named && IsEnumEntity(*named));
		else if (trait == TYPE_TRAIT_IS_EMPTY && operands.size() == 1)
			value = named && IsClassEntity(*named) && named->empty_class;
		else if (trait == TYPE_TRAIT_IS_AGGREGATE && operands.size() == 1)
			value = named && IsClassEntity(*named) && named->is_aggregate;
		else if (trait == TYPE_TRAIT_IS_ABSTRACT && operands.size() == 1)
			value = named && IsClassEntity(*named) && named->abstract_class;
		else if (trait == TYPE_TRAIT_IS_POLYMORPHIC && operands.size() == 1)
			value = named && IsClassEntity(*named) && named->polymorphic_class;
		else if ((trait == TYPE_TRAIT_IS_DESTRUCTIBLE ||
			trait == TYPE_TRAIT_IS_TRIVIALLY_DESTRUCTIBLE) && operands.size() == 1)
			value = shape.kind == TYPE_LVALUE_REFERENCE ||
				shape.kind == TYPE_RVALUE_REFERENCE || IsFundamentalIntegral(shape) ||
				IsFundamentalFloating(shape) || shape.kind == TYPE_COMPLEX ||
				shape.kind == TYPE_POINTER ||
				shape.kind == TYPE_MEMBER_POINTER ||
				(named && named->destructible &&
				 (trait == TYPE_TRAIT_IS_DESTRUCTIBLE || named->trivial_destructor));
		else if (trait == TYPE_TRAIT_IS_TRIVIALLY_COPYABLE &&
			operands.size() == 1)
			value = (named && IsClassEntity(*named)) ?
				EvaluateBuiltinTriviallyCopyable(first) :
				IsFundamentalIntegral(shape) || IsFundamentalFloating(shape) ||
				shape.kind == TYPE_COMPLEX || shape.kind == TYPE_POINTER ||
				shape.kind == TYPE_MEMBER_POINTER;
		else if ((trait == TYPE_TRAIT_IS_TRIVIAL || trait == TYPE_TRAIT_IS_POD ||
			trait == TYPE_TRAIT_IS_STANDARD_LAYOUT ||
			trait == TYPE_TRAIT_IS_LITERAL_TYPE) && operands.size() == 1)
			value = EvaluateBuiltinTrivialLayoutTrait(
				trait, first, shape, named);
		else if ((trait == TYPE_TRAIT_IS_CONSTRUCTIBLE ||
			trait == TYPE_TRAIT_IS_NOTHROW_CONSTRUCTIBLE ||
			trait == TYPE_TRAIT_IS_TRIVIALLY_CONSTRUCTIBLE))
		{
			BindingId selected = kNoBinding;
			std::vector<CallConversionFact> conversions;
			value = EvaluateBuiltinConstructibility(
				operands, &selected, &conversions);
			if (value && trait == TYPE_TRAIT_IS_NOTHROW_CONSTRUCTIBLE)
				value = BuiltinConstructionIsNonthrowing(
					operands[0], selected, conversions);
			else if (value && trait == TYPE_TRAIT_IS_TRIVIALLY_CONSTRUCTIBLE)
				value = BuiltinConstructionIsTrivial(
					operands[0], selected, conversions);
		}
		else if ((trait == TYPE_TRAIT_IS_ASSIGNABLE ||
			trait == TYPE_TRAIT_IS_NOTHROW_ASSIGNABLE ||
			trait == TYPE_TRAIT_IS_TRIVIALLY_ASSIGNABLE) && operands.size() == 2)
		{
			BindingId selected = kNoBinding;
			std::vector<CallConversionFact> conversions;
			value = EvaluateBuiltinAssignability(
				operands[0], operands[1], scope, &selected, &conversions);
			if (value && trait == TYPE_TRAIT_IS_NOTHROW_ASSIGNABLE)
				value = BuiltinAssignmentIsNonthrowing(
					selected, conversions);
			else if (value && trait == TYPE_TRAIT_IS_TRIVIALLY_ASSIGNABLE)
				value = BuiltinAssignmentIsTrivial(selected, conversions);
		}
		else if (trait == TYPE_TRAIT_IS_CONVERTIBLE && operands.size() == 2)
			value = EvaluateBuiltinConvertibility(operands[0], operands[1]);
		else if (trait == TYPE_TRAIT_IS_BASE_OF && operands.size() == 2)
		{
			const EntityId base = EntityOf(operands[0]);
			const EntityId derived = EntityOf(operands[1]);
			value = base != kNoEntity && derived != kNoEntity &&
				(base == derived || AccessIsBaseOf(base, derived));
		}
		else if (trait == TYPE_TRAIT_IS_COMPLETE_OR_UNBOUNDED &&
			operands.size() == 1)
			value = shape.kind == TYPE_ARRAY ? shape.bound == 0 :
				shape.kind != TYPE_FUNCTION && !IsVoid(first) &&
				(!named || named->complete);
		else if (trait == TYPE_TRAIT_HAS_TRIVIAL_CONSTRUCTOR &&
			operands.size() == 1)
			value = !named || named->trivial_default_constructor;
		else if (trait == TYPE_TRAIT_HAS_NOTHROW_COPY && operands.size() == 1)
			value = EvaluateBuiltinNothrowCopy(first);
		else if (trait == TYPE_TRAIT_HAS_VIRTUAL_DESTRUCTOR &&
			operands.size() == 1)
		{
			const BindingId destructor = named && IsClassEntity(*named) ?
				DestructorForType(first) : kNoBinding;
			value = destructor != kNoBinding &&
				program_->bindings[destructor].virtual_function;
		}
		else if (trait == TYPE_TRAIT_IS_FINAL && operands.size() == 1)
			value = named && IsClassEntity(*named) && named->final_class;
		else if (trait == TYPE_TRAIT_REFERENCE_BINDS_TO_TEMPORARY ||
			trait == TYPE_TRAIT_REFERENCE_CONSTRUCTS_FROM_TEMPORARY)
			value = false;
		else
			throw std::runtime_error("unsupported builtin type trait operands");
	}
	if (trait == TYPE_TRAIT_ARRAY_RANK)
	{
		if (operands.size() != 1)
			throw std::runtime_error("unsupported builtin type trait operands");
		result_type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	}
	else integral_value = value ? 1 : 0;
	ExpressionInfo result = MakeLiteral(result_type,
		program_->names.Intern(std::to_string(integral_value)));
	result.constant = true;
	result.value = integral_value;
	dump_.nodes[result.node].template_parameter_constant = dependent;
	RecordExpressionFacts(result);
	return result;
}

}
}
