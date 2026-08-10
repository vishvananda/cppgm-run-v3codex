#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::AccessIsBaseOf(EntityId base, EntityId derived) const
{
	if (base == kNoEntity || derived == kNoEntity ||
		base >= program_->entities.size() || derived >= program_->entities.size())
		return false;
	++access_path_visits_;
	return program_->QueryBasePath(derived, base, 0, 0);
}

bool SemanticAnalyzer::HasClassPrivilege(EntityId owner) const
{
	if (owner == kNoEntity) return false;
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
	{
		if (context == owner) return true;
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | context;
		++access_grant_probes_;
		if (friend_class_grants_.Find(key)) return true;
	}
	if (current_function_context_ != kNoBinding)
	{
		const BindingId function = program_->bindings[
			current_function_context_].canonical;
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | function;
		++access_grant_probes_;
		if (friend_function_grants_.Find(key)) return true;
	}
	return false;
}

bool SemanticAnalyzer::HasDerivedClassPrivilege(EntityId base) const
{
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (AccessIsBaseOf(base, context)) return true;
	return false;
}

bool SemanticAnalyzer::HasProtectedObjectPrivilege(EntityId owner,
	EntityId object_class) const
{
	if (owner == kNoEntity || object_class == kNoEntity) return false;
	bool privileged = false;
	for (EntityId current = object_class; current != kNoEntity;
		current = program_->entities[current].direct_base)
	{
		++access_path_visits_;
		if (HasClassPrivilege(current)) privileged = true;
		if (current == owner) return privileged;
	}
	return false;
}

bool SemanticAnalyzer::CanAccessMember(BindingId member,
	EntityId naming_class, EntityId object_class) const
{
	++access_checks_;
	if (member == kNoBinding || member >= program_->bindings.size()) return false;
	const BindingRecord& binding = program_->bindings[member];
	const BindingRecord& declaration =
		program_->bindings[binding.canonical];
	const bool object_member = declaration.non_static_data_member ||
		(declaration.kind == BIND_FUNCTION &&
		 declaration.member_owner != kNoEntity &&
		 !declaration.static_member_function);
	if (binding.access_owner != kNoEntity)
	{
		if (binding.access == ACCESS_PUBLIC) return true;
		if (binding.access == ACCESS_PRIVATE)
			return HasClassPrivilege(binding.access_owner);
		const bool permitted = HasClassPrivilege(binding.access_owner) ||
			HasDerivedClassPrivilege(binding.access_owner);
		return permitted && (!object_member || object_class == kNoEntity ||
			HasProtectedObjectPrivilege(binding.access_owner, object_class));
	}
	const EntityId owner = binding.member_owner;
	if (owner == kNoEntity) return true;
	if (naming_class == kNoEntity) naming_class = owner;
	if (binding.access == ACCESS_PUBLIC)
	{
		bool all_public = false;
		if (program_->QueryBasePath(
			naming_class, owner, 0, &all_public) && all_public)
			return true;
	}
	EntityId privilege_anchor = kNoEntity;
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (AccessIsBaseOf(naming_class, context))
		{
			privilege_anchor = context;
			break;
		}
	for (EntityId current = naming_class; current != owner;
		current = program_->entities[current].direct_base)
	{
		if (current == kNoEntity) return false;
		++access_path_visits_;
		if (HasClassPrivilege(current)) privilege_anchor = current;
		const EntityRecord& derived = program_->entities[current];
		if (derived.direct_base == kNoEntity) return false;
		if (derived.base_access == ACCESS_PUBLIC) continue;
		if (HasClassPrivilege(current)) continue;
		if (derived.base_access == ACCESS_PROTECTED &&
			privilege_anchor != kNoEntity &&
			AccessIsBaseOf(current, privilege_anchor)) continue;
		return false;
	}
	if (HasClassPrivilege(owner)) privilege_anchor = owner;
	if (binding.access == ACCESS_PUBLIC) return true;
	if (binding.access == ACCESS_PRIVATE)
		return HasClassPrivilege(owner);
	const bool permitted = HasClassPrivilege(owner) ||
		(privilege_anchor != kNoEntity &&
		 AccessIsBaseOf(owner, privilege_anchor)) ||
		HasDerivedClassPrivilege(owner);
	return permitted && (!object_member || object_class == kNoEntity ||
		HasProtectedObjectPrivilege(owner, object_class));
}

bool SemanticAnalyzer::BaseConversionAllowed(EntityId derived,
	EntityId base) const
{
	++access_checks_;
	if (base == derived) return false;
	std::size_t distance = 0;
	bool all_public = false;
	++access_path_visits_;
	if (!program_->QueryBasePath(
		derived, base, &distance, &all_public) || distance == 0) return false;
	if (all_public) return true;
	EntityId privilege_anchor = kNoEntity;
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (AccessIsBaseOf(derived, context))
		{
			privilege_anchor = context;
			break;
		}
	for (EntityId current = derived; current != base;
		current = program_->entities[current].direct_base)
	{
		if (current == kNoEntity) return false;
		++access_path_visits_;
		if (HasClassPrivilege(current)) privilege_anchor = current;
		const AccessKind access = program_->entities[current].base_access;
		if (access == ACCESS_PUBLIC) continue;
		if (HasClassPrivilege(current)) continue;
		if (access == ACCESS_PROTECTED && privilege_anchor != kNoEntity &&
			AccessIsBaseOf(current, privilege_anchor)) continue;
		return false;
	}
	return true;
}

std::size_t SemanticAnalyzer::BaseConversionDistance(TypeId source,
	TypeId target) const
{
	const TypeRecord source_top = program_->types.Get(source);
	const TypeRecord target_top = program_->types.Get(target);
	if (source_top.kind == TYPE_LVALUE_REFERENCE ||
		source_top.kind == TYPE_RVALUE_REFERENCE) source = source_top.child;
	if (target_top.kind == TYPE_LVALUE_REFERENCE ||
		target_top.kind == TYPE_RVALUE_REFERENCE) target = target_top.child;
	source = program_->types.RemoveTopCv(source);
	target = program_->types.RemoveTopCv(target);
	const TypeRecord source_core = program_->types.Get(source);
	const TypeRecord target_core = program_->types.Get(target);
	if (source_core.kind == TYPE_POINTER && target_core.kind == TYPE_POINTER)
	{
		source = program_->types.RemoveTopCv(source_core.child);
		target = program_->types.RemoveTopCv(target_core.child);
	}
	const EntityId derived = EntityOf(source);
	const EntityId base = EntityOf(target);
	if (derived == kNoEntity || base == kNoEntity)
		return std::numeric_limits<std::size_t>::max();
	std::size_t distance = 0;
	++access_path_visits_;
	return program_->QueryBasePath(derived, base, &distance, 0) ? distance :
		std::numeric_limits<std::size_t>::max();
}

std::size_t SemanticAnalyzer::BaseProjectionCount(TypeId source,
	TypeId target) const
{
	const std::size_t distance = BaseConversionDistance(source, target);
	return distance == std::numeric_limits<std::size_t>::max() ? distance :
		distance == 0 ? 0 : 1;
}

ConversionRank SemanticAnalyzer::MemberObjectConversion(
	const ExpressionInfo& source, TypeId target, BindingId member) const
{
	const ConversionRank ordinary = Conversion(source, target);
	if (ordinary == CONVERSION_STANDARD)
	{
		const TypeId from = Decay(source.type);
		const TypeId to = program_->types.RemoveTopCv(target);
		const TypeRecord source_pointer = program_->types.Get(from);
		const TypeRecord target_pointer = program_->types.Get(to);
		if (source_pointer.kind == TYPE_POINTER &&
			target_pointer.kind == TYPE_POINTER &&
			SimilarUnqualified(source_pointer.child, target_pointer.child))
			return CONVERSION_EXACT;
	}
	if (ordinary != CONVERSION_INVALID || member == kNoBinding ||
		member >= program_->bindings.size() ||
		program_->bindings[member].access_owner == kNoEntity)
		return ordinary;
	const TypeId from = Decay(source.type);
	const TypeId to = program_->types.RemoveTopCv(target);
	const TypeRecord source_pointer = program_->types.Get(from);
	const TypeRecord target_pointer = program_->types.Get(to);
	if (source_pointer.kind != TYPE_POINTER ||
		target_pointer.kind != TYPE_POINTER) return CONVERSION_INVALID;
	const EntityId derived = EntityOf(source_pointer.child);
	const EntityId base = EntityOf(target_pointer.child);
	if (derived == kNoEntity || base == kNoEntity || derived == base ||
		!AccessIsBaseOf(base, derived)) return CONVERSION_INVALID;
	const TypeRecord source_cv = program_->types.Get(source_pointer.child);
	const TypeRecord target_cv = program_->types.Get(target_pointer.child);
	const std::uint8_t scv = source_cv.kind == TYPE_QUALIFIED ?
		source_cv.cv : CV_NONE;
	const std::uint8_t tcv = target_cv.kind == TYPE_QUALIFIED ?
		target_cv.cv : CV_NONE;
	return (scv & ~tcv) == 0 ?
		CONVERSION_DERIVED_TO_BASE : CONVERSION_INVALID;
}

ExpressionInfo SemanticAnalyzer::ApplyMemberObjectTarget(
	ExpressionInfo value, TypeId target, BindingId member,
	const ObjectConversionFact* conversion_fact)
{
	const std::uint32_t object = ExpressionObject(value);
	const std::uint32_t complete_object = ExpressionCompleteObject(value);
	const std::uint32_t object_address = ExpressionAddress(value);
	const ConversionRank conversion = conversion_fact ? conversion_fact->rank :
		MemberObjectConversion(value, target, member);
	if (conversion != CONVERSION_DERIVED_TO_BASE)
	{
		value = ApplyTarget(value, target);
		if (object != kNoConstexprObject)
			SetExpressionSubobject(&value, object, complete_object);
		return value;
	}
	const std::size_t projections = conversion_fact ?
		conversion_fact->base_projection_count :
		BaseProjectionCount(value.type, target);
	if (projections == std::numeric_limits<std::size_t>::max() ||
		projections > std::numeric_limits<std::uint32_t>::max())
		throw std::logic_error("using member has no bounded base path");
	const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
		target, VALUE_PRVALUE);
	dump_.nodes[cast].base_projection_count =
		static_cast<std::uint32_t>(projections);
	dump_.Add(cast, value.node);
	value.node = cast;
	value.type = target;
	value.category = VALUE_PRVALUE;
	value.binding = kNoBinding;
	value.constant = false;
	value.constexpr_object = kNoConstexprObject;
	value.constexpr_complete_object = kNoConstexprObject;
	if (object != kNoConstexprObject && member != kNoBinding &&
		member < program_->bindings.size())
	{
		const EntityId owner = program_->bindings[member].member_owner;
		std::uint64_t projection_offset = 0;
		const std::uint32_t projected = owner == kNoEntity ?
			kNoConstexprObject : ProjectConstexprObject(
				object, program_->entities[owner].type, &projection_offset);
		if (projected != kNoConstexprObject)
		{
			if (object_address != kNoConstexprAddress &&
				projection_offset <= static_cast<std::uint64_t>(
					std::numeric_limits<std::int64_t>::max()))
			{
				const std::uint32_t projected_address = OffsetConstexprAddress(
					object_address, static_cast<std::int64_t>(projection_offset),
					false);
				if (projected_address != kNoConstexprAddress)
					SetExpressionAddress(&value, projected_address);
			}
			SetExpressionSubobject(&value, projected, complete_object);
		}
	}
	++expression_count_;
	RecordExpressionFacts(value);
	return value;
}

bool SemanticAnalyzer::ApplyQualifiedMemberNamingTarget(ExpressionInfo* value,
	EntityId naming_class, BindingId member)
{
	if (!value || naming_class == kNoEntity || member == kNoBinding ||
		member >= program_->bindings.size()) return false;
	const FunctionInfo& function = GetFunction(member);
	const EntityId function_owner = EntityOf(function.member_owner);
	TypeId source = program_->types.RemoveTopCv(EffectiveType(value->type));
	const TypeRecord source_record = program_->types.Get(source);
	if (source_record.kind == TYPE_POINTER)
		source = program_->types.RemoveTopCv(source_record.child);
	const EntityId source_class = EntityOf(source);
	if (source_class == kNoEntity || function_owner == kNoEntity ||
		source_class == naming_class || naming_class == function_owner ||
		!program_->IsBaseOf(naming_class, source_class) ||
		!program_->IsBaseOf(function_owner, naming_class)) return false;
	TypeId naming_type = program_->entities[naming_class].type;
	const TypeRecord function_type = program_->types.Get(function.type);
	if ((function_type.cv & CV_CONST) != 0)
		naming_type = program_->types.Qualify(naming_type, CV_CONST);
	if ((function_type.cv & CV_VOLATILE) != 0)
		naming_type = program_->types.Qualify(naming_type, CV_VOLATILE);
	const TypeId target = program_->types.Pointer(naming_type);
	ObjectConversionFact conversion;
	conversion.rank = MemberObjectConversion(*value, target, member);
	const std::size_t projections = BaseProjectionCount(value->type, target);
	if (conversion.rank != CONVERSION_DERIVED_TO_BASE ||
		projections == std::numeric_limits<std::size_t>::max() ||
		projections > std::numeric_limits<std::uint32_t>::max()) return false;
	conversion.base_projection_count =
		static_cast<std::uint32_t>(projections);
	*value = ApplyMemberObjectTarget(*value, target, member, &conversion);
	return true;
}

ExpressionInfo SemanticAnalyzer::AnalyzeCast(NodeId node, ScopeId scope)
{
	const NodeId type_id = FindChild(node, "type-id");
	if (type_id == kNoNode) throw std::runtime_error("cast without type-id");
	NodeId operand_node = kNoNode;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge))
		if (arena_->EdgeChild(edge) != type_id) operand_node = arena_->EdgeChild(edge);
	ExpressionInfo parenthesized_call;
	if (arena_->Payload(node).compare(0, 10, "OP_LPAREN:") == 0 &&
		AnalyzeParenthesizedFunctionTemplateCast(
			type_id, operand_node, scope, &parenthesized_call))
		return parenthesized_call;
	ExpressionInfo parenthesized_binary;
	if (arena_->Payload(node).compare(0, 10, "OP_LPAREN:") == 0 &&
		AnalyzeParenthesizedValueBinaryCast(
			type_id, operand_node, scope, &parenthesized_binary))
		return parenthesized_binary;
	++class_template_completion_suppressed_depth_;
	TypeId target = kNoType;
	try
	{
		target = BuildTypeId(type_id, scope);
	}
	catch (...)
	{
		--class_template_completion_suppressed_depth_;
		throw;
	}
	--class_template_completion_suppressed_depth_;
	// Expression analysis can intern more types and reallocate TypeTable storage.
	// Keep the cast shape by value across the recursive operand analysis.
	const TypeRecord target_record = program_->types.Get(target);
	const TypeId unqualified_target = program_->types.RemoveTopCv(target);
	const TypeRecord unqualified_target_record =
		program_->types.Get(unqualified_target);
	const bool function_pointer_target =
		unqualified_target_record.kind == TYPE_POINTER &&
		program_->types.IsFunction(unqualified_target_record.child);
	ExpressionInfo operand = AnalyzeExpression(operand_node, scope,
		program_->types.IsFunction(EffectiveType(target)) ||
		function_pointer_target ||
		unqualified_target_record.kind == TYPE_MEMBER_POINTER ?
		target : kNoType);
	const std::string cast_kind = arena_->Payload(node);
	TypeId constructed_target = target;
	if (target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE)
		constructed_target = target_record.child;
	constructed_target = program_->types.RemoveTopCv(constructed_target);
	const EntityId constructed_entity = EntityOf(constructed_target);
	if (constructed_entity != kNoEntity &&
		target_record.kind != TYPE_LVALUE_REFERENCE &&
		target_record.kind != TYPE_RVALUE_REFERENCE)
		EnsureClassDefinition(constructed_target);
	const TypeId operand_object_type = program_->types.RemoveTopCv(
		EffectiveType(operand.type));
	const EntityId operand_entity = EntityOf(operand_object_type);
	const bool static_reference_downcast =
		(target_record.kind == TYPE_LVALUE_REFERENCE ||
		 target_record.kind == TYPE_RVALUE_REFERENCE) &&
		cast_kind.find("STATIC") != std::string::npos &&
		operand_entity != kNoEntity && constructed_entity != kNoEntity &&
		program_->IsBaseOf(operand_entity, constructed_entity);
	const bool static_reference_base_cast =
		(target_record.kind == TYPE_LVALUE_REFERENCE ||
		 target_record.kind == TYPE_RVALUE_REFERENCE) &&
		cast_kind.find("STATIC") != std::string::npos &&
		operand_entity != kNoEntity && constructed_entity != kNoEntity &&
		operand_entity != constructed_entity &&
		program_->IsBaseOf(constructed_entity, operand_entity);
	const bool direct_reference_cast =
		(target_record.kind == TYPE_LVALUE_REFERENCE ||
		 target_record.kind == TYPE_RVALUE_REFERENCE) &&
		Conversion(operand, target) != CONVERSION_INVALID;
	const bool constructor_cast = constructed_entity != kNoEntity &&
		(program_->entities[constructed_entity].flavor == NAMED_STRUCT ||
		 program_->entities[constructed_entity].flavor == NAMED_CLASS ||
		 program_->entities[constructed_entity].flavor == NAMED_UNION) &&
		(cast_kind.find("STATIC") != std::string::npos ||
		 cast_kind.compare(0, 10, "OP_LPAREN:") == 0) &&
		!direct_reference_cast &&
		!static_reference_downcast &&
		!static_reference_base_cast &&
		(program_->types.RemoveTopCv(EffectiveType(operand.type)) !=
			constructed_target ||
		 (target_record.kind != TYPE_LVALUE_REFERENCE &&
		  target_record.kind != TYPE_RVALUE_REFERENCE));
	if (constructor_cast)
	{
		ExpressionInfo initialized;
		initialized.node = BuildClassValueConstructorAction(
			constructed_target, operand, false, true);
		initialized.type = constructed_target;
		initialized.category = VALUE_PRVALUE;
		SetExpressionDumpObject(&initialized);
		initialized = MaterializeTemporary(initialized);
		if (target_record.kind == TYPE_LVALUE_REFERENCE ||
			target_record.kind == TYPE_RVALUE_REFERENCE)
		{
			initialized.type = target;
			initialized.category = target_record.kind == TYPE_LVALUE_REFERENCE ?
				VALUE_LVALUE : VALUE_XVALUE;
		}
		return initialized;
	}
	if (EntityOf(operand.type) != kNoEntity &&
		ConvertingFunction(operand, target, true).rank != CONVERSION_INVALID)
		return ApplyExplicitConversion(operand, target);
	if (!IsVoid(target) && !IsArithmetic(target) && !IsPointer(target) &&
		!IsNullptr(target) && target_record.kind != TYPE_LVALUE_REFERENCE &&
		target_record.kind != TYPE_RVALUE_REFERENCE &&
		target_record.kind != TYPE_MEMBER_POINTER &&
		program_->types.Get(program_->types.RemoveTopCv(target)).kind != TYPE_NAMED)
		throw std::runtime_error("unsupported cast target");
	const ValueCategory category = target_record.kind == TYPE_LVALUE_REFERENCE ?
		VALUE_LVALUE : target_record.kind == TYPE_RVALUE_REFERENCE ?
		VALUE_XVALUE : VALUE_PRVALUE;
	if (target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE)
	{
		if (static_reference_base_cast)
		{
			operand.category = target_record.kind == TYPE_LVALUE_REFERENCE ?
				VALUE_LVALUE : VALUE_XVALUE;
			return ApplyTarget(operand, target);
		}
		const ConversionRank reference_conversion = Conversion(operand, target);
		const bool explicit_rvalue = target_record.kind == TYPE_RVALUE_REFERENCE &&
			SimilarUnqualified(EffectiveType(operand.type), target_record.child);
		const bool explicit_cv_lvalue =
			target_record.kind == TYPE_LVALUE_REFERENCE &&
			operand.category == VALUE_LVALUE &&
			SimilarUnqualified(EffectiveType(operand.type), target_record.child) &&
			(cast_kind.find("CONST") != std::string::npos ||
			 cast_kind.compare(0, 10, "OP_LPAREN:") == 0);
		if (!explicit_rvalue && !explicit_cv_lvalue &&
			!static_reference_downcast &&
			reference_conversion == CONVERSION_INVALID)
			throw std::runtime_error("invalid reference cast");
		if (reference_conversion == CONVERSION_DERIVED_TO_BASE)
			return ApplyTarget(operand, target);
		if (static_reference_downcast)
		{
			const std::uint32_t complete = ExpressionCompleteObject(operand);
			const std::uint32_t projected = ProjectConstexprObject(
				complete, constructed_target);
			if (projected != kNoConstexprObject)
				SetExpressionSubobject(&operand, projected, complete);
		}
		operand.type = target;
		operand.category = category;
		if (dump_.nodes[operand.node].kind == DUMP_TEMPORARY_OBJECT)
		{
			// A reference cast changes the expression type, not the storage type
			// of the already materialized object.
			dump_.nodes[operand.node].reference_call_materialization = true;
		}
		else
		{
			dump_.nodes[operand.node].type = target;
			dump_.nodes[operand.node].category = category;
			if (dump_.nodes[operand.node].kind == DUMP_CALL_EXPRESSION)
				dump_.nodes[operand.node].reference_call_materialization = true;
		}
		return operand;
	}
	if (target_record.kind == TYPE_MEMBER_POINTER)
	{
		operand.type = target;
		dump_.nodes[operand.node].type = target;
		return operand;
	}
	const EntityId source_entity = EntityOf(operand.type);
	const EntityId target_entity = EntityOf(target);
	const bool source_enum = source_entity != kNoEntity &&
		(program_->entities[source_entity].flavor == NAMED_ENUM ||
		 program_->entities[source_entity].flavor == NAMED_ENUM_CLASS);
	const bool target_enum = target_entity != kNoEntity &&
		(program_->entities[target_entity].flavor == NAMED_ENUM ||
		 program_->entities[target_entity].flavor == NAMED_ENUM_CLASS);
	const bool permits_reinterpretation =
		cast_kind.compare(0, 10, "OP_LPAREN:") == 0 ||
		cast_kind.find("REINTER") != std::string::npos;
	const TypeId decayed_operand_type = Decay(operand.type);
	if (cast_kind.find("STATIC") != std::string::npos && IsPointer(target) &&
		IsPointer(decayed_operand_type))
	{
		const TypeRecord source_pointer = program_->types.Get(Decay(operand.type));
		const TypeRecord target_pointer = program_->types.Get(
			program_->types.RemoveTopCv(target));
		const EntityId derived = EntityOf(source_pointer.child);
		const EntityId base = EntityOf(target_pointer.child);
		if (derived != kNoEntity && base != kNoEntity &&
			program_->IsBaseOf(base, derived) &&
			!BaseConversionAllowed(derived, base))
			throw std::runtime_error("inaccessible base conversion");
	}
	const bool valid = IsVoid(target) ||
		(IsArithmetic(target) && IsArithmetic(operand.type)) ||
		(target_enum && (IsIntegral(operand.type, true) ||
			IsFloating(operand.type))) ||
		(source_enum && (IsIntegral(target, true) || IsFloating(target))) ||
		(IsPointer(target) && (IsPointer(decayed_operand_type) ||
			IsNullptr(operand.type) || operand.integer_literal_zero)) ||
		(target == program_->types.Fundamental(FUND_BOOL) &&
			(IsPointer(decayed_operand_type) || IsNullptr(operand.type))) ||
		(IsNullptr(target) && (IsNullptr(operand.type) ||
			operand.integer_literal_zero)) ||
		(permits_reinterpretation &&
			((IsPointer(target) && IsIntegral(operand.type)) ||
			 (IsIntegral(target) && IsPointer(decayed_operand_type))));
	if (!valid) throw std::runtime_error("invalid explicit conversion");
	const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION, target,
		VALUE_PRVALUE, program_->names.Intern(arena_->Payload(node)));
	if (cast_kind.find("REINTER") == std::string::npos && IsPointer(target) &&
		IsPointer(decayed_operand_type))
	{
		const TypeRecord source_pointer = program_->types.Get(Decay(operand.type));
		const TypeRecord target_pointer = program_->types.Get(
			program_->types.RemoveTopCv(target));
		const EntityId derived = EntityOf(source_pointer.child);
		const EntityId base = EntityOf(target_pointer.child);
		if (derived != kNoEntity && base != kNoEntity && derived != base &&
			program_->IsBaseOf(base, derived))
		{
			const std::size_t projections =
				BaseProjectionCount(operand.type, target);
			if (projections == std::numeric_limits<std::size_t>::max() ||
				projections > std::numeric_limits<std::uint32_t>::max())
				throw std::logic_error("cast has no bounded base path");
			dump_.nodes[cast].base_projection_count =
				static_cast<std::uint32_t>(projections);
		}
	}
	dump_.Add(cast, operand.node);
	ExpressionInfo result;
	result.node = cast;
	result.type = target;
	result.constant = operand.constant;
	result.value = operand.value;
	if (result.constant && IsIntegral(target, true) &&
		IsIntegral(operand.type, true))
		result.value = NormalizeIntegralConstant(target, result.value);
	++expression_count_;
	return result;
}

void SemanticAnalyzer::AppendParenthesizedCallArguments(NodeId node,
	std::vector<NodeId>* arguments) const
{
	if (arena_->IsTag(node, "binary-expression") &&
		PayloadSource(node) == ",")
	{
		for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
			edge = arena_->NextEdge(edge))
			AppendParenthesizedCallArguments(
				arena_->EdgeChild(edge), arguments);
		return;
	}
	arguments->push_back(node);
}

bool SemanticAnalyzer::AnalyzeParenthesizedFunctionTemplateCast(
	NodeId type_id, NodeId operand, ScopeId scope, ExpressionInfo* result)
{
	const NodeId specifiers = FindChild(type_id, "type-specifier-seq");
	const NodeId name = specifiers == kNoNode ? kNoNode :
		FirstSemanticChild(specifiers);
	if (name == kNoNode || !arena_->IsTag(name, "type-name") ||
		operand == kNoNode || !arena_->IsTag(operand, "parenthesized-expression"))
		return false;
	const std::string spelling = PayloadSource(name);
	NamePath structured_base;
	std::vector<TypeId> explicit_arguments;
	const bool explicit_id = ParseExplicitTemplateArguments(
		name, scope, &structured_base, &explicit_arguments);
	if ((explicit_id ? FindFunctionTemplates(scope, structured_base) :
		FindFunctionTemplates(scope, spelling)).empty()) return false;
	const NodeId argument_root = FirstSemanticChild(operand);
	std::vector<NodeId> argument_syntax;
	if (argument_root != kNoNode)
		AppendParenthesizedCallArguments(argument_root, &argument_syntax);
	std::vector<ExpressionInfo> arguments;
	arguments.reserve(argument_syntax.size());
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
		arguments.push_back(AnalyzeExpression(argument_syntax[i], scope));
	DeduceFunctionTemplates(scope, spelling, arguments, name);
	const std::vector<BindingId> candidates =
		FunctionCallCandidates(scope, spelling, 0, name);
	if (candidates.empty())
		throw std::runtime_error(
			"parenthesized function template has no specialization");
	const BindingId selected = SelectOverload(
		scope, argument_syntax, arguments, candidates);
	*result = BuildResolvedCall(selected, scope, argument_syntax,
		arguments, 0, kNoType);
	return true;
}

bool SemanticAnalyzer::AnalyzeParenthesizedValueBinaryCast(
	NodeId type_id, NodeId operand, ScopeId scope, ExpressionInfo* result)
{
	const NodeId specifiers = FindChild(type_id, "type-specifier-seq");
	const NodeId name = specifiers == kNoNode ? kNoNode :
		FirstSemanticChild(specifiers);
	if (name == kNoNode || !arena_->IsTag(name, "type-name") ||
		operand == kNoNode || !arena_->IsTag(operand, "unary-expression"))
		return false;
	const std::string operation = PayloadSource(operand);
	if (operation != "+" && operation != "-") return false;
	const std::string spelling = PayloadSource(name);
	const LookupResult found = FindChild(name, "structured-type-name") != kNoNode ?
		LookupStructuredName(name, scope, LOOKUP_ORDINARY) :
		LookupSpelling(scope, spelling, LOOKUP_ORDINARY);
	if (found.ordinary == kNoBinding) return false;
	const BindingKind kind = program_->bindings[found.ordinary].kind;
	if (kind != BIND_VARIABLE && kind != BIND_PARAMETER &&
		kind != BIND_ENUMERATOR)
		return false;
	const NodeId right_syntax = FirstSemanticChild(operand);
	if (right_syntax == kNoNode) return false;
	ExpressionInfo left = AnalyzeNamedValue(spelling, scope, kNoType, name);
	if (left.constant && left.category == VALUE_LVALUE &&
		IsIntegral(left.type, true))
	{
		left.type = program_->types.RemoveTopCv(EffectiveType(left.type));
		left.category = VALUE_PRVALUE;
		left.binding = kNoBinding;
		left.node = MakeDump(DUMP_LITERAL, left.type, VALUE_PRVALUE,
			InternNumber(left.value));
		dump_.nodes[left.node].constant = true;
		dump_.nodes[left.node].constant_value = left.value;
		++expression_count_;
	}
	ExpressionInfo right = AnalyzeExpression(right_syntax, scope);
	*result = BuildBinaryExpression(operation, arena_->Payload(operand),
		name, right_syntax, left, right, scope);
	return true;
}

ExpressionInfo SemanticAnalyzer::AnalyzeImplicitDataMember(
	BindingId member_binding, ScopeId scope, TypeId target,
	EntityId naming_class)
{
	const BindingRecord& binding = program_->bindings[member_binding];
	if (!constexpr_frames_.empty() && binding.non_static_data_member &&
		(constexpr_frames_.back().receiver_object != kNoConstexprObject ||
		 constexpr_frames_.back().receiver_address != kNoConstexprAddress))
	{
		if (!CanAccessMember(member_binding, naming_class))
			throw std::runtime_error("inaccessible implicit data member");
		ExpressionInfo result;
		result.node = MakeDump(DUMP_MEMBER_EXPRESSION, binding.type,
			VALUE_LVALUE, binding.name, member_binding);
		result.type = binding.type;
		result.category = VALUE_LVALUE;
		result.binding = member_binding;
		const std::uint32_t receiver_address =
			constexpr_frames_.back().receiver_address;
		if (receiver_address != kNoConstexprAddress &&
			binding.member_offset <= static_cast<std::uint64_t>(
				std::numeric_limits<std::int64_t>::max()))
		{
			const std::uint32_t address = OffsetConstexprAddress(receiver_address,
				static_cast<std::int64_t>(binding.member_offset), true,
				static_cast<std::int64_t>(program_->SizeOf(
					EffectiveType(binding.type))));
			if (address != kNoConstexprAddress)
				SetExpressionLvalueAddress(&result, address);
		}
		const ConstexprObjectElement* element =
			constexpr_frames_.back().receiver_object == kNoConstexprObject ? 0 :
			ConstexprClassMemberAt(
				constexpr_frames_.back().receiver_object, member_binding);
		if (element)
			SetExpressionObjectElement(&result, *element);
		RecordExpressionFacts(result);
		++expression_count_;
		return ApplyTarget(result, target);
	}
	const NameId this_name = program_->names.Intern("this");
	const LookupResult this_lookup =
		program_->LookupName(scope, this_name, LOOKUP_ORDINARY);
	if (this_lookup.ordinary == kNoBinding)
		throw std::runtime_error("non-static member requires an object");
	const BindingRecord& this_binding =
		program_->bindings[this_lookup.ordinary];
	TypeId member_type = binding.type;
	TypeId object_type = EffectiveType(this_binding.type);
	const TypeRecord object_pointer = program_->types.Get(
		program_->types.RemoveTopCv(object_type));
	const EntityId object_class = object_pointer.kind == TYPE_POINTER ?
		EntityOf(object_pointer.child) : kNoEntity;
	if (!CanAccessMember(member_binding, naming_class, object_class))
		throw std::runtime_error("inaccessible implicit data member");
	if (object_pointer.kind == TYPE_POINTER)
	{
		const TypeRecord pointee = program_->types.Get(object_pointer.child);
		if (pointee.kind == TYPE_QUALIFIED && !binding.mutable_member)
			member_type = program_->types.Qualify(member_type, pointee.cv);
	}
	const std::uint32_t object = MakeDump(DUMP_ID_EXPRESSION,
		this_binding.type, VALUE_LVALUE, this_name, this_lookup.ordinary);
	const std::uint32_t member = MakeDump(DUMP_MEMBER_EXPRESSION,
		member_type, VALUE_LVALUE, binding.name, member_binding);
	if (current_class_context_ == kNoEntity)
		throw std::logic_error("implicit member has no class context");
	const std::size_t projections = BaseProjectionCount(
		program_->entities[current_class_context_].type,
		program_->entities[binding.member_owner].type);
	if (projections == std::numeric_limits<std::size_t>::max() ||
		projections > std::numeric_limits<std::uint32_t>::max())
		throw std::logic_error("implicit member has no bounded base path");
	dump_.nodes[member].base_projection_count =
		static_cast<std::uint32_t>(projections);
	dump_.Add(member, object);
	ExpressionInfo result;
	result.node = member;
	result.type = member_type;
	result.category = VALUE_LVALUE;
	result.binding = member_binding;
	expression_count_ += 2;
	return ApplyTarget(result, target);
}

ExpressionInfo SemanticAnalyzer::AnalyzeConditional(NodeId node, ScopeId scope)
{
	std::vector<NodeId> children;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge)) children.push_back(arena_->EdgeChild(edge));
	if (children.size() != 3) throw std::runtime_error("invalid conditional");
	ExpressionInfo condition = AnalyzeExpression(children[0], scope);
	if (!IsArithmetic(condition.type) && !IsPointer(Decay(condition.type)) &&
		!IsNullptr(condition.type))
		condition = ApplyExplicitConversion(condition,
			program_->types.Fundamental(FUND_BOOL));
	const bool known_condition = condition.constant;
	const bool condition_truth = known_condition &&
		ScalarTruth(ExpressionScalar(condition));
	const bool suppress_yes = known_condition && !condition_truth;
	const bool suppress_no = known_condition && condition_truth;
	if (suppress_yes) ++constant_evaluation_suppressed_depth_;
	ExpressionInfo yes;
	try
	{
		yes = AnalyzeExpression(children[1], scope);
	}
	catch (...)
	{
		if (suppress_yes) --constant_evaluation_suppressed_depth_;
		throw;
	}
	if (suppress_yes) --constant_evaluation_suppressed_depth_;
	if (suppress_no) ++constant_evaluation_suppressed_depth_;
	ExpressionInfo no;
	try
	{
		no = AnalyzeExpression(children[2], scope);
	}
	catch (...)
	{
		if (suppress_no) --constant_evaluation_suppressed_depth_;
		throw;
	}
	if (suppress_no) --constant_evaluation_suppressed_depth_;
	TypeId type = kNoType;
	ValueCategory category = VALUE_PRVALUE;
	const TypeId yes_object = program_->types.RemoveTopCv(
		EffectiveType(yes.type));
	const TypeId no_object = program_->types.RemoveTopCv(
		EffectiveType(no.type));
	const EntityId conditional_entity = EntityOf(yes_object);
	const bool class_conditional = conditional_entity != kNoEntity &&
		(program_->entities[conditional_entity].flavor == NAMED_STRUCT ||
		 program_->entities[conditional_entity].flavor == NAMED_CLASS ||
		 program_->entities[conditional_entity].flavor == NAMED_UNION);
	if (yes_object == no_object && class_conditional)
	{
		const bool same_glvalue = yes.category == no.category &&
			(yes.category == VALUE_LVALUE || yes.category == VALUE_XVALUE) &&
			!(yes.category == VALUE_XVALUE &&
			  dump_.nodes[yes.node].kind == DUMP_TEMPORARY_OBJECT &&
			  dump_.nodes[no.node].kind == DUMP_TEMPORARY_OBJECT);
		if (!same_glvalue)
			return BuildClassConditional(
				condition.node, yes, no, yes_object, false);
		std::uint8_t cv = CV_NONE;
		const TypeRecord& yes_top = program_->types.Get(EffectiveType(yes.type));
		const TypeRecord& no_top = program_->types.Get(EffectiveType(no.type));
		if (yes_top.kind == TYPE_QUALIFIED) cv |= yes_top.cv;
		if (no_top.kind == TYPE_QUALIFIED) cv |= no_top.cv;
		type = cv == CV_NONE ? yes_object : program_->types.Qualify(yes_object, cv);
		category = yes.category;
	}
	else if (EffectiveType(yes.type) == EffectiveType(no.type))
	{
		type = EffectiveType(yes.type);
		category = yes.category == no.category ? yes.category : VALUE_PRVALUE;
	}
	else if (IsArithmetic(yes.type) && IsArithmetic(no.type))
		type = CommonArithmeticType(yes.type, no.type);
	else if (EntityOf(yes.type) != kNoEntity &&
		EntityOf(no.type) != kNoEntity && yes.category == no.category &&
		(yes.category == VALUE_LVALUE || yes.category == VALUE_XVALUE))
	{
		const EntityId yes_entity = EntityOf(yes.type);
		const EntityId no_entity = EntityOf(no.type);
		if (yes_entity != kNoEntity && no_entity != kNoEntity &&
			BaseConversionAllowed(yes_entity, no_entity))
		{
			type = no.type;
			category = yes.category;
			yes = ApplyTarget(yes, program_->types.Reference(
				category == VALUE_LVALUE ? TYPE_LVALUE_REFERENCE :
				TYPE_RVALUE_REFERENCE, type));
		}
		else if (yes_entity != kNoEntity && no_entity != kNoEntity &&
			BaseConversionAllowed(no_entity, yes_entity))
		{
			type = yes.type;
			category = yes.category;
			no = ApplyTarget(no, program_->types.Reference(
				category == VALUE_LVALUE ? TYPE_LVALUE_REFERENCE :
				TYPE_RVALUE_REFERENCE, type));
		}
	}
	else if (IsPointer(Decay(yes.type)) &&
		(IsNullptr(no.type) || no.integer_literal_zero)) type = Decay(yes.type);
	else if (IsPointer(Decay(no.type)) &&
		(IsNullptr(yes.type) || yes.integer_literal_zero)) type = Decay(no.type);
	else if (IsPointer(Decay(yes.type)) && IsPointer(Decay(no.type)))
	{
		if (Conversion(yes, Decay(no.type)) != CONVERSION_INVALID)
			type = Decay(no.type);
		else if (Conversion(no, Decay(yes.type)) != CONVERSION_INVALID)
			type = Decay(yes.type);
	}
	if (type == kNoType) throw std::runtime_error("incompatible conditional arms");
	if (IsPointer(type))
	{
		if (yes.integer_literal_zero || IsNullptr(yes.type))
			SetExpressionAddress(&yes, NullConstexprAddress());
		else yes = ApplyTarget(yes, type);
		if (no.integer_literal_zero || IsNullptr(no.type))
			SetExpressionAddress(&no, NullConstexprAddress());
		else no = ApplyTarget(no, type);
	}
	const std::uint32_t expression = MakeDump(DUMP_CONDITIONAL_EXPRESSION,
		type, category);
	dump_.Add(expression, condition.node);
	dump_.Add(expression, yes.node);
	dump_.Add(expression, no.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = type;
	result.category = category;
	result.constant = constant_evaluation_suppressed_depth_ == 0 &&
		condition.constant &&
		(condition_truth ? yes.constant : no.constant);
	if (result.constant)
	{
		const ExpressionInfo& selected = condition_truth ? yes : no;
		if ((IsIntegral(selected.type, true) || IsFloating(selected.type)) &&
			(IsIntegral(type, true) || IsFloating(type)))
			SetExpressionScalar(&result, ConvertScalarConstant(
				selected.type, type, ExpressionScalar(selected)));
		else if (ExpressionAddress(selected) != kNoConstexprAddress)
			SetExpressionAddress(&result, ExpressionAddress(selected));
		else if (selected.constexpr_lvalue_address != kNoConstexprAddress)
			SetExpressionLvalueAddress(
				&result, selected.constexpr_lvalue_address);
		else if (ExpressionObject(selected) != kNoConstexprObject)
			SetExpressionSubobject(&result, ExpressionObject(selected),
				ExpressionCompleteObject(selected));
		else result.value = selected.value;
	}
	++expression_count_;
	return result;
}

}
}
