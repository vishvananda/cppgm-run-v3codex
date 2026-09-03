#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"
#include "support/scoped_state.h"

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace cppgm
{
namespace semantic
{

bool Analyzer::AccessIsBaseOf(EntityId base, EntityId derived) const
{
	if (base == kNoEntity || derived == kNoEntity ||
		base >= program_->entities.size() || derived >= program_->entities.size())
		return false;
	++access_path_visits_;
	return program_->QueryBasePath(derived, base, 0, 0);
}

bool Analyzer::HasClassPrivilege(EntityId owner) const
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
	if (current_class_template_access_principal_ != kNoEntity)
	{
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) |
			current_class_template_access_principal_;
		++access_grant_probes_;
		if (friend_class_grants_.Find(key)) return true;
	}
	for (BindingId function = current_function_context_;
		function != kNoBinding;)
	{
		function = program_->bindings[function].canonical;
		const EntityId member_owner = program_->bindings[function].member_owner;
		for (EntityId context = member_owner; context != kNoEntity;
			context = program_->entities[context].enclosing_class)
		{
			if (context == owner) return true;
			const std::uint64_t class_key =
				(static_cast<std::uint64_t>(owner) << 32) | context;
			++access_grant_probes_;
			if (friend_class_grants_.Find(class_key)) return true;
		}
		const std::uint64_t key =
			(static_cast<std::uint64_t>(owner) << 32) | function;
		++access_grant_probes_;
		if (friend_function_grants_.Find(key)) return true;
		if (function >= function_fact_by_binding_.size() ||
			function_fact_by_binding_[function] == kNoDumpEdge) break;
		function = GetFunction(function).lexical_access_function;
	}
	return false;
}

bool Analyzer::HasDerivedClassPrivilege(EntityId base) const
{
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (AccessIsBaseOf(base, context)) return true;
	for (BindingId function = current_function_context_;
		function != kNoBinding;)
	{
		function = program_->bindings[function].canonical;
		for (EntityId context = program_->bindings[function].member_owner;
			context != kNoEntity;
			context = program_->entities[context].enclosing_class)
			if (AccessIsBaseOf(base, context)) return true;
		if (function >= function_fact_by_binding_.size() ||
			function_fact_by_binding_[function] == kNoDumpEdge) break;
		function = GetFunction(function).lexical_access_function;
	}
	return false;
}

bool Analyzer::HasProtectedObjectPrivilege(EntityId owner,
	EntityId object_class) const
{
	if (owner == kNoEntity || object_class == kNoEntity) return false;
	if (protected_object_unprivileged_marks_.size() <
		program_->entities.size())
	{
		protected_object_unprivileged_marks_.resize(
			program_->entities.size(), 0);
		protected_object_privileged_marks_.resize(
			program_->entities.size(), 0);
	}
	if (protected_object_path_generation_ ==
		std::numeric_limits<std::uint32_t>::max())
	{
		std::fill(protected_object_unprivileged_marks_.begin(),
			protected_object_unprivileged_marks_.end(), 0);
		std::fill(protected_object_privileged_marks_.begin(),
			protected_object_privileged_marks_.end(), 0);
		protected_object_path_generation_ = 0;
	}
	const std::uint32_t generation = ++protected_object_path_generation_;
	protected_object_path_scratch_.clear();
	protected_object_path_scratch_.push_back(
		std::make_pair(object_class, false));
	while (!protected_object_path_scratch_.empty())
	{
		EntityId current = protected_object_path_scratch_.back().first;
		bool privileged = protected_object_path_scratch_.back().second;
		protected_object_path_scratch_.pop_back();
		++access_path_visits_;
		if (HasClassPrivilege(current)) privileged = true;
		if (current == owner)
		{
			if (privileged) return true;
			continue;
		}
		std::vector<std::uint32_t>& marks = privileged ?
			protected_object_privileged_marks_ :
			protected_object_unprivileged_marks_;
		if (marks[current] == generation) continue;
		marks[current] = generation;
		const EntityRecord& record = program_->entities[current];
		for (std::size_t base = 0; base < record.direct_base_count; ++base)
			protected_object_path_scratch_.push_back(std::make_pair(
				program_->DirectBase(current, base).entity, privileged));
	}
	return false;
}

bool Analyzer::CanAccessMember(BindingId member,
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
	// A class-scope using-declaration owns the access it republishes, while the
	// canonical member still belongs to its original class.  Treat that access
	// owner as the effective member owner so a later naming class can traverse
	// inheritance (including a private edge for which it has friendship) before
	// applying the using-declaration's access.
	const EntityId owner = binding.access_owner != kNoEntity ?
		binding.access_owner : binding.member_owner;
	if (owner == kNoEntity) return true;
	if (naming_class == kNoEntity) naming_class = owner;
	bool all_public = false;
	bool ambiguous_path = false;
	const bool has_base_path = program_->QueryBasePath(
		naming_class, owner, 0, &all_public, 0, &ambiguous_path);
	if (binding.access == ACCESS_PUBLIC && has_base_path &&
		(!ambiguous_path || !object_member) && all_public)
		return true;
	if (binding.access == ACCESS_PRIVATE && has_base_path &&
		(!ambiguous_path || !object_member) && all_public &&
		HasClassPrivilege(owner))
		return true;
	EntityId privilege_anchor = kNoEntity;
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (AccessIsBaseOf(naming_class, context))
		{
			privilege_anchor = context;
			break;
		}
	if (!has_base_path || (ambiguous_path && object_member)) return false;
	if (!program_->QueryBasePath(naming_class, owner, 0, 0, 0,
		0, &access_base_path_scratch_)) return false;
	EntityId current = naming_class;
	for (std::size_t step = 0;
		step < access_base_path_scratch_.size(); ++step)
	{
		++access_path_visits_;
		if (HasClassPrivilege(current)) privilege_anchor = current;
		const EntityId derived_class = current;
		const DirectBaseEdge& edge = program_->DirectBase(
			current, access_base_path_scratch_[step]);
		current = edge.entity;
		if (edge.access == ACCESS_PUBLIC) continue;
		if (HasClassPrivilege(derived_class)) continue;
		if (edge.access == ACCESS_PROTECTED &&
			privilege_anchor != kNoEntity &&
			AccessIsBaseOf(derived_class, privilege_anchor)) continue;
		return false;
	}
	if (current != owner) return false;
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

bool Analyzer::BaseConversionAllowed(EntityId derived,
	EntityId base) const
{
	++access_checks_;
	if (base == derived) return false;
	std::size_t distance = 0;
	bool all_public = false;
	bool ambiguous = false;
	++access_path_visits_;
	if (!program_->QueryBasePath(
		derived, base, &distance, &all_public, 0, &ambiguous) ||
		distance == 0 || ambiguous) return false;
	if (all_public) return true;
	if (!program_->QueryBasePath(derived, base, 0, 0, 0, 0,
		&access_base_path_scratch_)) return false;
	EntityId privilege_anchor = kNoEntity;
	for (EntityId context = current_class_context_; context != kNoEntity;
		context = program_->entities[context].enclosing_class)
		if (AccessIsBaseOf(derived, context))
		{
			privilege_anchor = context;
			break;
		}
	EntityId current = derived;
	for (std::size_t step = 0;
		step < access_base_path_scratch_.size(); ++step)
	{
		++access_path_visits_;
		if (HasClassPrivilege(current)) privilege_anchor = current;
		const EntityId derived_class = current;
		const DirectBaseEdge& edge = program_->DirectBase(
			current, access_base_path_scratch_[step]);
		const AccessKind access = edge.access;
		current = edge.entity;
		if (access == ACCESS_PUBLIC) continue;
		if (HasClassPrivilege(derived_class)) continue;
		if (access == ACCESS_PROTECTED && privilege_anchor != kNoEntity &&
			AccessIsBaseOf(derived_class, privilege_anchor)) continue;
		return false;
	}
	return current == base;
}

std::size_t Analyzer::BaseConversionDistance(TypeId source,
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
	bool ambiguous = false;
	++access_path_visits_;
	return program_->QueryBasePath(
		derived, base, &distance, 0, 0, &ambiguous) && !ambiguous ? distance :
		std::numeric_limits<std::size_t>::max();
}

std::size_t Analyzer::BaseProjectionCount(TypeId source,
	TypeId target, std::uint64_t* offset) const
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
	std::uint64_t selected_offset = 0;
	bool ambiguous = false;
	++access_path_visits_;
	if (!program_->QueryBasePath(
		derived, base, &distance, 0, &selected_offset, &ambiguous) || ambiguous)
		return std::numeric_limits<std::size_t>::max();
	if (offset) *offset = selected_offset;
	return distance == 0 ? 0 : 1;
}

ConversionRank Analyzer::MemberObjectConversion(
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

ExpressionInfo Analyzer::ApplyMemberObjectTarget(
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
	std::uint64_t projection_offset = 0;
	const std::size_t selected_projections = BaseProjectionCount(
		value.type, target, &projection_offset);
	const std::size_t projections = conversion_fact ?
		conversion_fact->base_projection_count : selected_projections;
	if (projections == std::numeric_limits<std::size_t>::max() ||
		projections > std::numeric_limits<std::uint32_t>::max())
		ThrowInternalCompilerError("using member has no bounded base path");
	const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION,
		target, VALUE_PRVALUE);
	dump_.nodes[cast].base_projection_count =
		static_cast<std::uint32_t>(projections);
	dump_.nodes[cast].base_projection_offset = projection_offset;
	dump_.nodes[cast].has_base_projection_offset = true;
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

bool Analyzer::ApplyQualifiedMemberNamingTarget(ExpressionInfo* value,
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
	std::uint64_t projection_offset = 0;
	const std::size_t projections = BaseProjectionCount(
		value->type, target, &projection_offset);
	if (conversion.rank != CONVERSION_DERIVED_TO_BASE ||
		projections == std::numeric_limits<std::size_t>::max() ||
		projections > std::numeric_limits<std::uint32_t>::max()) return false;
	conversion.base_projection_count =
		static_cast<std::uint32_t>(projections);
	conversion.base_projection_offset = projection_offset;
	*value = ApplyMemberObjectTarget(*value, target, member, &conversion);
	return true;
}

ExpressionInfo Analyzer::AnalyzeCast(NodeId node, ScopeId scope)
{
	const NodeId type_id = FindChild(node, ::cppgm::syntax::STAG_TYPE_ID);
	if (type_id == kNoNode) ThrowSemanticError("cast without type-id");
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
	TypeId target = kNoType;
	{
		ScopedCounterIncrement suppressed(
			&class_template_completion_suppressed_depth_);
		target = BuildTypeId(type_id, scope);
	}
	if (CandidateSubstitutionFailed() || target == kNoType)
		return ExpressionInfo();
	// Expression analysis can intern more types and reallocate TypeTable storage.
	// Keep the cast shape by value across the recursive operand analysis.
	const TypeRecord target_record = program_->types.Get(target);
	const TypeId unqualified_target = program_->types.RemoveTopCv(target);
	const TypeRecord unqualified_target_record =
		program_->types.Get(unqualified_target);
	const bool function_pointer_target =
		unqualified_target_record.kind == TYPE_POINTER &&
		program_->types.IsFunction(unqualified_target_record.child);
	const bool compound_literal =
		arena_->IsTag(operand_node, ::cppgm::syntax::STAG_BRACED_INIT_LIST);
	const bool c_style_cast =
		arena_->Payload(node).compare(0, 10, "OP_LPAREN:") == 0;
	ExpressionInfo operand = AnalyzeExpression(operand_node, scope,
		program_->types.IsFunction(EffectiveType(target)) ||
		(function_pointer_target &&
		 (!c_style_cast || arena_->IsTag(operand_node, ::cppgm::syntax::STAG_ID_EXPRESSION))) ||
		unqualified_target_record.kind == TYPE_MEMBER_POINTER || compound_literal ?
		target : kNoType);
	if (CandidateSubstitutionFailed()) return ExpressionInfo();
	if (compound_literal && EntityOf(target) != kNoEntity &&
		dump_.nodes[operand.node].kind == DUMP_BRACED_INIT_LIST)
	{
		operand.node = BuildAggregateConstructionAction(
			target, operand.node, true);
		operand.category = VALUE_PRVALUE;
		return MaterializeTemporary(operand);
	}
	if (compound_literal) return operand;
	const std::string cast_kind = arena_->Payload(node);
	ExpressionInfo dynamic_result;
	if (cast_kind.find("DYNAMIC") != std::string::npos &&
		TryAnalyzeDynamicCast(target, operand, &dynamic_result))
		return dynamic_result;
	TypeId constructed_target = target;
	if (target_record.kind == TYPE_LVALUE_REFERENCE ||
		target_record.kind == TYPE_RVALUE_REFERENCE)
		constructed_target = target_record.child;
	constructed_target = program_->types.RemoveTopCv(constructed_target);
	const EntityId constructed_entity = EntityOf(constructed_target);
	const TypeId operand_object_type = program_->types.RemoveTopCv(
		EffectiveType(operand.type));
	const EntityId operand_entity = EntityOf(operand_object_type);
	if (constructed_entity != kNoEntity &&
		((target_record.kind != TYPE_LVALUE_REFERENCE &&
		  target_record.kind != TYPE_RVALUE_REFERENCE) ||
		 (arena_->Payload(node).find("STATIC") != std::string::npos &&
		  operand_entity != kNoEntity && operand_entity != constructed_entity)))
		EnsureClassDefinition(constructed_target);
	const bool static_reference_downcast =
		(target_record.kind == TYPE_LVALUE_REFERENCE ||
		 target_record.kind == TYPE_RVALUE_REFERENCE) &&
		cast_kind.find("STATIC") != std::string::npos &&
		operand_entity != kNoEntity && constructed_entity != kNoEntity &&
		operand_entity != constructed_entity &&
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
	const bool direct_rvalue_reclassification =
		target_record.kind == TYPE_RVALUE_REFERENCE &&
		SimilarUnqualified(EffectiveType(operand.type), target_record.child);
	const bool reinterpret_reference_cast =
		(target_record.kind == TYPE_LVALUE_REFERENCE ||
		 target_record.kind == TYPE_RVALUE_REFERENCE) &&
		cast_kind.find("REINTER") != std::string::npos &&
		operand.category != VALUE_PRVALUE && operand.category != VALUE_NONE;
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
	if (!direct_reference_cast && !direct_rvalue_reclassification &&
		!reinterpret_reference_cast &&
		!static_reference_downcast && !static_reference_base_cast &&
		EntityOf(operand.type) != kNoEntity &&
		ConvertingFunction(operand, target, true).rank != CONVERSION_INVALID)
		return ApplyExplicitConversion(operand, target);
	const TypeId explicit_source_type = program_->types.RemoveTopCv(
		EffectiveType(operand.type));
	const TypeKind explicit_source_kind =
		program_->types.Get(explicit_source_type).kind;
	const TypeId explicit_target_type = program_->types.RemoveTopCv(target);
	const TypeKind explicit_target_kind =
		program_->types.Get(explicit_target_type).kind;
	const bool vector_target = explicit_target_kind == TYPE_VECTOR;
	if (!IsVoid(target) && !IsArithmetic(target) && !IsPointer(target) &&
		!IsNullptr(target) && target_record.kind != TYPE_LVALUE_REFERENCE &&
		target_record.kind != TYPE_RVALUE_REFERENCE &&
		target_record.kind != TYPE_MEMBER_POINTER &&
		explicit_target_kind != TYPE_NAMED && !vector_target)
		ThrowSemanticError("unsupported cast target");
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
			!reinterpret_reference_cast &&
			!static_reference_downcast &&
			reference_conversion == CONVERSION_INVALID)
			ThrowSemanticError("invalid reference cast");
		if (reference_conversion == CONVERSION_DERIVED_TO_BASE)
			return ApplyTarget(operand, target);
		if (static_reference_downcast)
		{
			const std::uint32_t complete = ExpressionCompleteObject(operand);
			std::uint64_t projection_offset = 0;
			if (!program_->QueryBasePath(constructed_entity, operand_entity,
				0, 0, &projection_offset))
				ThrowInternalCompilerError("reference downcast has no base path");
			if (projection_offset != 0)
			{
				const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION, target,
					category,
					program_->names.UseInterned(arena_->PayloadId(node)));
				dump_.nodes[cast].base_projection_count = 1;
				dump_.nodes[cast].base_projection_offset = projection_offset;
				dump_.nodes[cast].has_base_projection_offset = true;
				dump_.nodes[cast].inverse_base_projection = true;
				dump_.Add(cast, operand.node);
				operand.node = cast;
			}
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
	const bool equal_width_vector_cast =
		(vector_target || explicit_source_kind == TYPE_VECTOR) &&
		(vector_target || IsArithmetic(explicit_target_type)) &&
		(explicit_source_kind == TYPE_VECTOR ||
		 IsArithmetic(explicit_source_type)) &&
		program_->SizeOf(explicit_target_type) ==
			program_->SizeOf(explicit_source_type);
	if ((cast_kind.compare(0, 10, "OP_LPAREN:") == 0 ||
		 cast_kind.find("STATIC") != std::string::npos) &&
		IsPointer(target) && IsPointer(decayed_operand_type))
	{
		const TypeRecord source_pointer =
			program_->types.Get(decayed_operand_type);
		const TypeRecord target_pointer = program_->types.Get(
			program_->types.RemoveTopCv(target));
		const EntityId source_class = EntityOf(source_pointer.child);
		const EntityId target_class = EntityOf(target_pointer.child);
		if (source_class != kNoEntity && target_class != kNoEntity &&
			program_->IsBaseOf(source_class, target_class) &&
			program_->HasVirtualBasePath(target_class, source_class))
			return CandidateExpressionFailure(
				"invalid downcast through virtual base");
	}
	if (cast_kind.find("STATIC") != std::string::npos && IsPointer(target) &&
		IsPointer(decayed_operand_type))
	{
		const TypeRecord source_pointer = program_->types.Get(Decay(operand.type));
		const TypeRecord target_pointer = program_->types.Get(
			program_->types.RemoveTopCv(target));
		const EntityId derived = EntityOf(source_pointer.child);
		const EntityId base = EntityOf(target_pointer.child);
		if (derived != kNoEntity && base != kNoEntity && derived != base &&
			program_->IsBaseOf(base, derived) &&
			!BaseConversionAllowed(derived, base))
			return CandidateExpressionFailure("inaccessible base conversion");
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
		equal_width_vector_cast ||
		(permits_reinterpretation &&
			((IsPointer(target) && IsIntegral(operand.type)) ||
			 (IsIntegral(target) && IsPointer(decayed_operand_type))));
	if (!valid) return CandidateExpressionFailure(
		"invalid explicit conversion");
	const std::uint32_t cast = MakeDump(DUMP_CAST_EXPRESSION, target,
		VALUE_PRVALUE, program_->names.UseInterned(arena_->PayloadId(node)));
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
			std::uint64_t projection_offset = 0;
			const std::size_t projections =
				BaseProjectionCount(operand.type, target, &projection_offset);
			if (projections == std::numeric_limits<std::size_t>::max() ||
				projections > std::numeric_limits<std::uint32_t>::max())
				ThrowInternalCompilerError("cast has no bounded base path");
			dump_.nodes[cast].base_projection_count =
				static_cast<std::uint32_t>(projections);
			dump_.nodes[cast].base_projection_offset = projection_offset;
			dump_.nodes[cast].has_base_projection_offset = true;
		}
		else if (derived != kNoEntity && base != kNoEntity &&
			derived != base && program_->IsBaseOf(derived, base))
		{
			std::uint64_t projection_offset = 0;
			if (!program_->QueryBasePath(base, derived, 0, 0,
				&projection_offset))
				ThrowInternalCompilerError("pointer downcast has no base path");
			if (projection_offset != 0)
			{
				dump_.nodes[cast].base_projection_count = 1;
				dump_.nodes[cast].base_projection_offset = projection_offset;
				dump_.nodes[cast].has_base_projection_offset = true;
				dump_.nodes[cast].inverse_base_projection = true;
			}
		}
	}
	dump_.Add(cast, operand.node);
	if (program_->types.RemoveTopCv(target) !=
			program_->types.Fundamental(FUND_BOOL) &&
		IsIntegral(target, true) && IsIntegral(operand.type, true) &&
		program_->SizeOf(program_->types.RemoveTopCv(target)) <
		program_->SizeOf(program_->types.RemoveTopCv(EffectiveType(operand.type))))
		dump_.nodes[operand.node].integer_narrowing_conversion = true;
	ExpressionInfo result;
	result.node = cast;
	result.type = target;
	const std::uint32_t operand_address = ExpressionAddress(operand);
	if (IsPointer(target) && operand_address != kNoConstexprAddress)
		SetExpressionAddress(&result, operand_address);
	else if (operand.constant &&
		(IsIntegral(target, true) || IsFloating(target)) &&
		(IsIntegral(operand.type, true) || IsFloating(operand.type)))
	{
		SetExpressionScalar(&result, ConvertScalarConstant(
			operand.type, target, ExpressionScalar(operand)));
	}
	else
	{
		result.constant = operand.constant;
		result.value = operand.value;
		result.integral_high = operand.integral_high;
		result.integral_high_valid = operand.integral_high_valid;
	}
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

void Analyzer::AppendParenthesizedCallArguments(NodeId node,
	std::vector<NodeId>* arguments) const
{
	if (arena_->IsTag(node, ::cppgm::syntax::STAG_BINARY_EXPRESSION) &&
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

bool Analyzer::AnalyzeParenthesizedFunctionTemplateCast(
	NodeId type_id, NodeId operand, ScopeId scope, ExpressionInfo* result)
{
	const NodeId specifiers = FindChild(type_id, ::cppgm::syntax::STAG_TYPE_SPECIFIER_SEQ);
	const NodeId name = specifiers == kNoNode ? kNoNode :
		FirstSemanticChild(specifiers);
	if (name == kNoNode || !arena_->IsTag(name, ::cppgm::syntax::STAG_TYPE_NAME) ||
		operand == kNoNode || !arena_->IsTag(operand, ::cppgm::syntax::STAG_PARENTHESIZED_EXPRESSION))
		return false;
	const std::string spelling = PayloadSource(name);
	NamePath structured_base;
	std::vector<TypeId> explicit_arguments;
	const bool explicit_id = ParseExplicitTemplateArguments(
		name, scope, &structured_base, &explicit_arguments);
	if ((explicit_id ? FindFunctionTemplates(scope, structured_base) :
		FindFunctionTemplates(scope, SyntaxNamePath(name))).empty()) return false;
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
		ThrowSemanticError(
			"parenthesized function template has no specialization");
	const BindingId selected = SelectOverload(
		scope, argument_syntax, arguments, candidates);
	*result = BuildResolvedCall(selected, scope, argument_syntax,
		arguments, 0, kNoType);
	return true;
}

bool Analyzer::AnalyzeParenthesizedValueBinaryCast(
	NodeId type_id, NodeId operand, ScopeId scope, ExpressionInfo* result)
{
	const NodeId specifiers = FindChild(type_id, ::cppgm::syntax::STAG_TYPE_SPECIFIER_SEQ);
	const NodeId name = specifiers == kNoNode ? kNoNode :
		FirstSemanticChild(specifiers);
	if (name == kNoNode || !arena_->IsTag(name, ::cppgm::syntax::STAG_TYPE_NAME) ||
		operand == kNoNode || !arena_->IsTag(operand, ::cppgm::syntax::STAG_UNARY_EXPRESSION))
		return false;
	const std::string operation = PayloadSource(operand);
	if (operation != "+" && operation != "-") return false;
	const std::string spelling = PayloadSource(name);
	const LookupResult found = LookupSyntaxName(name, scope, LOOKUP_ORDINARY);
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
			InternScalar(left.type, ExpressionScalar(left)));
		dump_.nodes[left.node].constant = true;
		dump_.nodes[left.node].constant_value = left.value;
		dump_.nodes[left.node].constant_high =
			ExpressionScalar(left).integral_high;
		++expression_count_;
	}
	ExpressionInfo right = AnalyzeExpression(right_syntax, scope);
	*result = BuildBinaryExpression(operation, arena_->Payload(operand),
		name, right_syntax, left, right, scope);
	return true;
}

ExpressionInfo Analyzer::AnalyzeImplicitDataMember(
	BindingId member_binding, ScopeId scope, TypeId target,
	EntityId naming_class)
{
	const BindingRecord& binding = program_->bindings[member_binding];
	const BindingLayoutFact& layout = program_->BindingLayout(binding);
	if (!constexpr_frames_.empty() && binding.non_static_data_member &&
		(constexpr_frames_.back().receiver_object != kNoConstexprObject ||
		 constexpr_frames_.back().receiver_address != kNoConstexprAddress))
	{
		if (!CanAccessMember(member_binding, naming_class))
			ThrowSemanticError("inaccessible implicit data member");
		ExpressionInfo result;
		result.node = MakeDump(DUMP_MEMBER_EXPRESSION, binding.type,
			VALUE_LVALUE, binding.name, member_binding);
		result.type = binding.type;
		result.category = VALUE_LVALUE;
		result.binding = member_binding;
		const std::uint32_t receiver_address =
			constexpr_frames_.back().receiver_address;
		if (receiver_address != kNoConstexprAddress &&
			layout.member_offset <= static_cast<std::uint64_t>(
				std::numeric_limits<std::int64_t>::max()))
		{
			const std::uint32_t address = OffsetConstexprAddress(receiver_address,
				static_cast<std::int64_t>(layout.member_offset), true,
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
	// An unqualified non-static member in an unevaluated operand still has its
	// declared lvalue type, but does not require a runtime implicit object.  Keep
	// the canonical member binding as the expression fact; no lowering can be
	// demanded for this operand.
	if (unevaluated_depth_ != 0 && current_class_context_ != kNoEntity &&
		binding.member_owner != kNoEntity &&
		program_->IsBaseOf(binding.member_owner, current_class_context_))
	{
		ExpressionInfo result;
		result.node = MakeDump(DUMP_MEMBER_EXPRESSION, binding.type,
			VALUE_LVALUE, binding.name, member_binding);
		result.type = binding.type;
		result.category = VALUE_LVALUE;
		result.binding = member_binding;
		RecordExpressionFacts(result);
		++expression_count_;
		return ApplyTarget(result, target);
	}
	const NameId this_name = program_->names.Intern("this");
	const LookupResult this_lookup =
		program_->LookupName(scope, this_name, LOOKUP_ORDINARY);
	if (this_lookup.ordinary == kNoBinding)
		ThrowSemanticError("non-static member requires an object");
	TypeId member_type = binding.type;
	const ExpressionInfo this_expression = AnalyzeThisExpression(scope);
	TypeId object_type = EffectiveType(this_expression.type);
	const TypeRecord object_pointer = program_->types.Get(
		program_->types.RemoveTopCv(object_type));
	const EntityId object_class = object_pointer.kind == TYPE_POINTER ?
		EntityOf(object_pointer.child) : kNoEntity;
	if (!CanAccessMember(member_binding, naming_class, object_class))
		ThrowSemanticError("inaccessible implicit data member");
	if (object_pointer.kind == TYPE_POINTER)
	{
		const TypeRecord pointee = program_->types.Get(object_pointer.child);
		if (pointee.kind == TYPE_QUALIFIED && !binding.mutable_member)
			member_type = program_->types.Qualify(member_type, pointee.cv);
	}
	const std::uint32_t member = MakeDump(DUMP_MEMBER_EXPRESSION,
		member_type, VALUE_LVALUE, binding.name, member_binding);
	if (object_class == kNoEntity)
		ThrowInternalCompilerError("implicit member has no class context");
	std::uint64_t projection_offset = 0;
	const std::size_t projections = BaseProjectionCount(
		program_->entities[object_class].type,
		program_->entities[binding.member_owner].type, &projection_offset);
	if (projections == std::numeric_limits<std::size_t>::max() ||
		projections > std::numeric_limits<std::uint32_t>::max())
		ThrowInternalCompilerError("implicit member has no bounded base path");
	const bool captured_object = current_function_context_ != kNoBinding &&
		GetFunction(program_->bindings[
			current_function_context_].canonical).
			lambda_this_capture_member == this_expression.binding;
	if (captured_object &&
		projections == std::numeric_limits<std::uint32_t>::max())
		ThrowSemanticResourceLimit("captured object projection count overflow");
	dump_.nodes[member].base_projection_count = static_cast<std::uint32_t>(
		projections + (captured_object ? 1 : 0));
	dump_.nodes[member].base_projection_offset = projection_offset;
	dump_.nodes[member].has_base_projection_offset = !captured_object;
	dump_.Add(member, this_expression.node);
	ExpressionInfo result;
	result.node = member;
	result.type = member_type;
	result.category = VALUE_LVALUE;
	result.binding = member_binding;
	++expression_count_;
	return ApplyTarget(result, target);
}

ExpressionInfo Analyzer::AnalyzeConditional(NodeId node, ScopeId scope)
{
	std::vector<NodeId> children;
	for (std::uint32_t edge = arena_->FirstEdge(node); edge != kNoEdge;
		edge = arena_->NextEdge(edge)) children.push_back(arena_->EdgeChild(edge));
	if (children.size() != 3)
		ThrowInternalCompilerError("invalid conditional");
	ExpressionInfo condition = AnalyzeExpression(children[0], scope);
	const bool class_condition = IsClassObjectType(condition.type);
	if (!IsArithmetic(condition.type) && !IsPointer(Decay(condition.type)) &&
		!IsNullptr(condition.type))
		condition = ApplyContextualBool(condition);
	const bool known_condition = condition.constant;
	const bool condition_truth = known_condition &&
		ScalarTruth(ExpressionScalar(condition));
	const bool suppress_yes = known_condition && !condition_truth;
	const bool suppress_no = known_condition && condition_truth;
	ExpressionInfo yes;
	{
		ScopedCounterIncrement suppressed(
			&constant_evaluation_suppressed_depth_, suppress_yes);
		yes = AnalyzeExpression(children[1], scope);
	}
	ExpressionInfo no;
	{
		ScopedCounterIncrement suppressed(
			&constant_evaluation_suppressed_depth_, suppress_no);
		no = AnalyzeExpression(children[2], scope);
	}
	ApplyConditionalClassConversion(&yes, &no);
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
	const bool yes_throw = dump_.nodes[yes.node].kind == DUMP_THROW_EXPRESSION;
	const bool no_throw = dump_.nodes[no.node].kind == DUMP_THROW_EXPRESSION;
	if (yes_throw && !no_throw)
	{
		if (IsClassObjectType(no.type) && no.category == VALUE_PRVALUE)
			return BuildClassConditional(
				condition.node, yes, no, no.type, false);
		type = no.type;
		category = no.category;
	}
	else if (no_throw && !yes_throw)
	{
		if (IsClassObjectType(yes.type) && yes.category == VALUE_PRVALUE)
			return BuildClassConditional(
				condition.node, yes, no, yes.type, false);
		type = yes.type;
		category = yes.category;
	}
	else if (yes_object == no_object && class_conditional)
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
	else if (yes_object == no_object &&
		(IsArithmetic(yes.type) || IsPointer(yes.type) ||
		 IsMemberPointer(yes.type) || IsNullptr(yes.type)) &&
		(yes.category != no.category || yes.category == VALUE_PRVALUE))
	{
		// When either scalar arm undergoes lvalue-to-rvalue conversion, its
		// top-level cv-qualification is discarded before the conditional's
		// same-type test.  In particular, an enum must not be promoted merely
		// because its other arm is a const lvalue of that enum type.
		type = yes_object;
		category = VALUE_PRVALUE;
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
	else if (IsMemberPointer(yes.type) &&
		(IsNullptr(no.type) || no.integer_literal_zero))
		type = program_->types.RemoveTopCv(EffectiveType(yes.type));
	else if (IsMemberPointer(no.type) &&
		(IsNullptr(yes.type) || yes.integer_literal_zero))
		type = program_->types.RemoveTopCv(EffectiveType(no.type));
	else if (IsPointer(Decay(yes.type)) && IsPointer(Decay(no.type)))
	{
		if (Conversion(yes, Decay(no.type)) != CONVERSION_INVALID)
			type = Decay(no.type);
		else if (Conversion(no, Decay(yes.type)) != CONVERSION_INVALID)
			type = Decay(yes.type);
	}
	else if (IsMemberPointer(yes.type) && IsMemberPointer(no.type))
	{
		const TypeId yes_type = program_->types.RemoveTopCv(
			EffectiveType(yes.type));
		const TypeId no_type = program_->types.RemoveTopCv(
			EffectiveType(no.type));
		if (Conversion(yes, no_type) != CONVERSION_INVALID) type = no_type;
		else if (Conversion(no, yes_type) != CONVERSION_INVALID) type = yes_type;
	}
	if (type == kNoType &&
		(FunctionTemplateTypeIsDependent(yes.type) ||
		 FunctionTemplateTypeIsDependent(no.type)))
	{
		// A retained template shape cannot decide conversions between distinct
		// dependent arms.  Specialization reanalyzes this syntax with concrete
		// types, so carry one canonical dependent result through the shape pass.
		type = DependentFunctionTemplateResultShape();
		category = VALUE_PRVALUE;
	}
	if (type == kNoType)
		ThrowSemanticError("incompatible conditional arms at " +
			arena_->SourceFile(node) + ":" +
			std::to_string(arena_->SourceLine(node)) + ":" +
			std::to_string(arena_->SourceColumn(node)) + " (" +
			program_->RenderType(yes.type) + " and " +
			program_->RenderType(no.type) + ") in " +
			(current_function_context_ == kNoBinding ? std::string("<namespace>") :
			 program_->names.Get(program_->bindings[current_function_context_].name)) +
			" (" + arena_->Tag(children[1]) + ": " +
			PayloadSource(children[1]) + "; " + arena_->Tag(children[2]) + ": " +
			PayloadSource(children[2]) + ")");
	if (IsPointer(type))
	{
		if (yes.integer_literal_zero || IsNullptr(yes.type))
			SetExpressionAddress(&yes, NullConstexprAddress());
		else yes = ApplyTarget(yes, type);
		if (no.integer_literal_zero || IsNullptr(no.type))
			SetExpressionAddress(&no, NullConstexprAddress());
		else no = ApplyTarget(no, type);
	}
	else if (IsMemberPointer(type))
	{
		yes = ApplyTarget(yes, type);
		no = ApplyTarget(no, type);
	}
	if (class_condition && known_condition &&
		dump_.nodes[condition.node].kind == DUMP_LITERAL)
	{
		ExpressionInfo selected = condition_truth ? yes : no;
		selected = ApplyTarget(selected, type);
		selected.category = category;
		return selected;
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
		{
			SetExpressionScalar(&result, ConvertScalarConstant(
				selected.type, type, ExpressionScalar(selected)));
		}
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
