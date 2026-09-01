#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <string>
#include <vector>

namespace cppgm
{
namespace semantic
{
namespace
{

bool IsClassEntity(const EntityRecord& entity)
{
	return IsClassNamedFlavor(entity.flavor);
}

BindingId SelectedConversionFunction(const CallConversionFact& conversion)
{
	if (conversion.conversion_function != kNoBinding)
		return conversion.conversion_function;
	if (conversion.constructor != kNoBinding) return conversion.constructor;
	return conversion.constructor_argument_conversion_function;
}

}

ExpressionInfo Analyzer::MakeBuiltinTraitOperand(TypeId type) const
{
	ExpressionInfo result;
	const TypeRecord top = program_->types.Get(type);
	result.type = EffectiveType(type);
	if (program_->types.Get(result.type).kind == TYPE_FUNCTION)
		result.category = VALUE_LVALUE;
	else if (top.kind == TYPE_LVALUE_REFERENCE)
		result.category = VALUE_LVALUE;
	else result.category = VALUE_XVALUE;
	return result;
}

bool Analyzer::BuiltinConversionIsUsable(
	const CallConversionFact& conversion) const
{
	if (conversion.rank == CONVERSION_INVALID) return false;
	const BindingId bindings[] = {
		conversion.conversion_function,
		conversion.constructor,
		conversion.constructor_argument_conversion_function
	};
	for (std::size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
	{
		if (bindings[i] == kNoBinding) continue;
		const FunctionInfo& function = GetFunction(bindings[i]);
		if (function.deleted_function || function.deleted_constructor ||
			function.deleted_special_member || !CanAccessMember(bindings[i]))
			return false;
	}
	return true;
}

bool Analyzer::BuiltinConversionIsNonthrowing(
	const CallConversionFact& conversion)
{
	if (!BuiltinConversionIsUsable(conversion)) return false;
	const BindingId bindings[] = {
		conversion.conversion_function,
		conversion.constructor,
		conversion.constructor_argument_conversion_function
	};
	for (std::size_t i = 0; i < sizeof(bindings) / sizeof(bindings[0]); ++i)
		if (bindings[i] != kNoBinding &&
			!FunctionIsNonthrowing(bindings[i])) return false;
	return true;
}

bool Analyzer::EvaluateBuiltinConstructibility(
	const std::vector<TypeId>& operands, BindingId* selected,
	std::vector<CallConversionFact>* argument_conversions)
{
	*selected = kNoBinding;
	argument_conversions->clear();
	if (operands.empty()) return false;
	TypeId target = operands[0];
	const TypeRecord target_top = program_->types.Get(target);
	if (target_top.kind == TYPE_LVALUE_REFERENCE ||
		target_top.kind == TYPE_RVALUE_REFERENCE)
	{
		if (operands.size() != 2) return false;
		const ExpressionInfo source = MakeBuiltinTraitOperand(operands[1]);
		CallConversionFact conversion = CallConversion(source, target, 0, 0);
		if (!BuiltinConversionIsUsable(conversion))
			conversion = ConvertingFunction(source, target, true);
		if (!BuiltinConversionIsUsable(conversion)) return false;
		argument_conversions->push_back(conversion);
		*selected = SelectedConversionFunction(conversion);
		return true;
	}

	target = program_->types.RemoveTopCv(target);
	const TypeRecord shape = program_->types.Get(target);
	if (shape.kind == TYPE_ARRAY)
	{
		if (shape.bound == 0 || operands.size() != 1) return false;
		std::vector<TypeId> element(1, shape.child);
		return EvaluateBuiltinConstructibility(
			element, selected, argument_conversions);
	}
	if (shape.kind == TYPE_FUNCTION || IsVoid(target)) return false;

	const EntityId entity = EntityOf(target);
	if (entity != kNoEntity && IsClassEntity(program_->entities[entity]))
	{
		EnsureClassDefinition(target);
		if (!program_->entities[entity].complete ||
			program_->entities[entity].abstract_class) return false;
		std::vector<ExpressionInfo> arguments;
		arguments.reserve(operands.size() - 1);
		for (std::size_t i = 1; i < operands.size(); ++i)
			arguments.push_back(MakeBuiltinTraitOperand(operands[i]));
		const std::vector<NodeId> syntax(arguments.size(), kNoNode);
		*selected = SelectConstructor(kNoScope, syntax, arguments,
			ConstructorCandidates(entity), false, false,
			argument_conversions, true, kNoNode, target);
		if (*selected != kNoBinding) return true;
		if (arguments.size() != 1) return false;
		const CallConversionFact conversion =
			ConvertingFunction(arguments[0], target, true);
		if (!BuiltinConversionIsUsable(conversion)) return false;
		argument_conversions->assign(1, conversion);
		*selected = conversion.conversion_function;
		return true;
	}

	if (operands.size() == 1) return true;
	if (operands.size() != 2) return false;
	const ExpressionInfo source = MakeBuiltinTraitOperand(operands[1]);
	CallConversionFact conversion = CallConversion(source, target, 0, 0);
	if (!BuiltinConversionIsUsable(conversion))
		conversion = ConvertingFunction(source, target, true);
	if (!BuiltinConversionIsUsable(conversion)) return false;
	argument_conversions->push_back(conversion);
	*selected = SelectedConversionFunction(conversion);
	return true;
}

bool Analyzer::EvaluateBuiltinConvertibility(
	TypeId source_type, TypeId target)
{
	const bool source_void = IsVoid(source_type);
	const bool target_void = IsVoid(target);
	if (source_void || target_void) return source_void && target_void;
	const TypeId source_object = program_->types.RemoveTopCv(
		EffectiveType(source_type));
	const TypeId target_object_type = program_->types.RemoveTopCv(
		EffectiveType(target));
	const TypeRecord source_shape = program_->types.Get(source_object);
	const TypeRecord target_shape = program_->types.Get(target_object_type);
	if (source_shape.kind == TYPE_POINTER)
		EnsureClassDefinition(source_shape.child);
	if (target_shape.kind == TYPE_POINTER)
		EnsureClassDefinition(target_shape.child);
	ExpressionInfo source = MakeBuiltinTraitOperand(source_type);
	const TypeRecord target_top = program_->types.Get(target);
	if (program_->types.Get(source.type).kind == TYPE_FUNCTION &&
		target_top.kind == TYPE_RVALUE_REFERENCE)
		source.category = VALUE_XVALUE;
	const CallConversionFact conversion = CallConversion(source, target, 0, 0);
	if (!BuiltinConversionIsUsable(conversion)) return false;
	if (conversion.conversion_function == kNoBinding) return true;
	const TypeId converted =
		GetFunction(conversion.conversion_function).conversion_target;
	const TypeRecord converted_top = program_->types.Get(converted);
	const TypeId target_object = program_->types.RemoveTopCv(
		EffectiveType(target));
	if ((converted_top.kind != TYPE_LVALUE_REFERENCE &&
		 converted_top.kind != TYPE_RVALUE_REFERENCE) ||
		program_->types.RemoveTopCv(EffectiveType(converted)) != target_object ||
		EntityOf(target_object) == kNoEntity) return true;
	std::vector<TypeId> construction;
	construction.push_back(target);
	construction.push_back(converted);
	BindingId selected = kNoBinding;
	std::vector<CallConversionFact> argument_conversions;
	return EvaluateBuiltinConstructibility(
		construction, &selected, &argument_conversions);
}

bool Analyzer::BuiltinDefaultConstructionIsNonthrowing(EntityId entity)
{
	const std::vector<BindingId>& candidates = ConstructorCandidates(entity);
	BindingId selected = kNoBinding;
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const FunctionInfo& function = GetFunction(candidates[i]);
		if (!function.constructor || function.deleted_function ||
			function.deleted_constructor || function.deleted_special_member)
			continue;
		std::size_t required = function.parameters.size();
		while (required != 0 &&
			function.parameters[required - 1].default_argument != kNoNode)
			--required;
		if (required != 0) continue;
		if (selected != kNoBinding) return false;
		selected = candidates[i];
	}
	if (selected == kNoBinding) return false;
	const FunctionInfo& constructor = GetFunction(selected);
	if (!constructor.implicit_constructor && !constructor.defaulted_constructor)
		return FunctionIsNonthrowing(selected);
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t i = 0; i < owner.direct_base_count; ++i)
		if (!BuiltinDefaultConstructionIsNonthrowing(
			program_->DirectBase(entity, i).entity)) return false;
	if (entity >= entity_data_members_.size()) return true;
	for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
	{
		TypeId member =
			program_->bindings[entity_data_members_[entity][i]].type;
		TypeRecord shape = program_->types.Get(member);
		while (shape.kind == TYPE_ARRAY || shape.kind == TYPE_QUALIFIED)
		{
			member = shape.child;
			shape = program_->types.Get(member);
		}
		if (shape.kind == TYPE_LVALUE_REFERENCE ||
			shape.kind == TYPE_RVALUE_REFERENCE) return false;
		if (shape.kind == TYPE_NAMED &&
			IsClassEntity(program_->entities[shape.entity]) &&
			!BuiltinDefaultConstructionIsNonthrowing(shape.entity)) return false;
	}
	return true;
}

bool Analyzer::BuiltinConstructionIsNonthrowing(TypeId target,
	BindingId selected,
	const std::vector<CallConversionFact>& argument_conversions)
{
	for (std::size_t i = 0; i < argument_conversions.size(); ++i)
		if (!BuiltinConversionIsNonthrowing(argument_conversions[i])) return false;
	if (selected == kNoBinding) return true;
	const FunctionInfo& function = GetFunction(selected);
	if (!function.constructor || !function.parameters.empty())
		return FunctionIsNonthrowing(selected);
	while (program_->types.Get(target).kind == TYPE_ARRAY)
		target = program_->types.Get(target).child;
	const EntityId entity = EntityOf(target);
	return entity != kNoEntity &&
		BuiltinDefaultConstructionIsNonthrowing(entity);
}

bool Analyzer::BuiltinConstructionIsTrivial(TypeId target,
	BindingId selected,
	const std::vector<CallConversionFact>& argument_conversions) const
{
	for (std::size_t i = 0; i < argument_conversions.size(); ++i)
		if (argument_conversions[i].rank == CONVERSION_USER_DEFINED)
			return false;
	if (selected == kNoBinding) return true;
	const FunctionInfo& function = GetFunction(selected);
	if (!function.constructor) return false;
	while (program_->types.Get(target).kind == TYPE_ARRAY)
		target = program_->types.Get(target).child;
	const EntityId entity = EntityOf(target);
	if (entity == kNoEntity || !program_->entities[entity].trivial_destructor)
		return false;
	if (function.special_member != SPECIAL_MEMBER_NONE)
		return function.trivial_special_member;
	return (function.implicit_constructor || function.defaulted_constructor) &&
		program_->entities[entity].trivial_default_constructor;
}

bool Analyzer::EvaluateBuiltinAssignability(TypeId target,
	TypeId source_type, ScopeId scope, BindingId* selected,
	std::vector<CallConversionFact>* argument_conversions)
{
	*selected = kNoBinding;
	argument_conversions->clear();
	ExpressionInfo left = MakeBuiltinTraitOperand(target);
	ExpressionInfo right = MakeBuiltinTraitOperand(source_type);
	const EntityId entity = EntityOf(left.type);
	if (entity != kNoEntity && IsClassEntity(program_->entities[entity]))
	{
		EnsureClassDefinition(left.type);
		const NameId name = program_->names.Intern("operator=");
		BeginCandidateCollection();
		std::vector<BindingId> candidates;
		EntityId naming_class = kNoEntity;
		const LookupResult member = program_->LookupMember(
			entity, name, LOOKUP_ORDINARY);
		if (member.ordinary != kNoBinding &&
			program_->bindings[member.ordinary].kind == BIND_FUNCTION)
		{
			naming_class = member.naming_class;
			const std::vector<BindingId> functions =
				FunctionSet(member.ordinary);
			for (std::size_t i = 0; i < functions.size(); ++i)
				if (GetFunction(functions[i]).member_owner != kNoType)
					AddCandidate(functions[i], &candidates);
		}
		const LookupResult templates = program_->LookupMember(
			entity, name, LOOKUP_FUNCTION_TEMPLATE);
		std::vector<std::size_t> patterns;
		for (std::size_t owner = 0;
			owner < templates.FunctionTemplateOwnerCount(); ++owner)
		{
			const ScopeId template_owner =
				templates.FunctionTemplateOwnerAt(owner);
			const std::uint64_t key =
				(static_cast<std::uint64_t>(template_owner) << 32) | name;
			const CompactIndexSequence* indexed =
				template_function_sets_.Find(key);
			if (!indexed) continue;
			for (std::size_t i = 0; i < indexed->Size(); ++i)
				patterns.push_back((*indexed)[i]);
		}
		if (!patterns.empty())
		{
			associated_declaration_visits_ += patterns.size();
			const std::vector<ExpressionInfo> arguments(1, right);
			std::vector<BindingId> specializations;
			DeduceFunctionTemplatePatterns(
				patterns, arguments, &specializations);
			for (std::size_t i = 0; i < specializations.size(); ++i)
				if (GetFunction(specializations[i]).member_owner != kNoType)
					AddCandidate(specializations[i], &candidates);
			if (naming_class == kNoEntity)
				naming_class = templates.naming_class;
		}
		if (candidates.empty()) return false;
		ExpressionInfo object;
		object.type = program_->types.Pointer(EffectiveType(left.type));
		object.category = left.category;
		std::vector<NodeId> syntax(2, kNoNode);
		std::vector<ExpressionInfo> operands;
		operands.push_back(left);
		operands.push_back(right);
		bool selected_member = false;
		ObjectConversionFact object_conversion;
		*selected = SelectOperatorOverload(scope, syntax, operands,
			candidates, object, &selected_member, &object_conversion,
			argument_conversions, true);
		if (*selected == kNoBinding || !selected_member) return false;
		const FunctionInfo& function = GetFunction(*selected);
		if (function.deleted_function || function.deleted_special_member ||
			!CanAccessMember(*selected, naming_class, entity)) return false;
		for (std::size_t i = 0; i < argument_conversions->size(); ++i)
			if (!BuiltinConversionIsUsable((*argument_conversions)[i]))
				return false;
		return true;
	}

	if (!IsModifiableLvalue(left)) return false;
	const TypeRecord shape = program_->types.Get(
		program_->types.RemoveTopCv(EffectiveType(left.type)));
	if (shape.kind == TYPE_ARRAY) return false;
	const CallConversionFact conversion = CallConversion(
		right, EffectiveType(left.type), 0, 0);
	if (!BuiltinConversionIsUsable(conversion)) return false;
	argument_conversions->push_back(conversion);
	return true;
}

bool Analyzer::BuiltinAssignmentIsNonthrowing(BindingId selected,
	const std::vector<CallConversionFact>& argument_conversions)
{
	if (selected != kNoBinding && !FunctionIsNonthrowing(selected)) return false;
	for (std::size_t i = 0; i < argument_conversions.size(); ++i)
		if (!BuiltinConversionIsNonthrowing(argument_conversions[i])) return false;
	return true;
}

bool Analyzer::BuiltinAssignmentIsTrivial(BindingId selected,
	const std::vector<CallConversionFact>& argument_conversions) const
{
	for (std::size_t i = 0; i < argument_conversions.size(); ++i)
		if (argument_conversions[i].rank == CONVERSION_USER_DEFINED)
			return false;
	if (selected == kNoBinding) return true;
	const FunctionInfo& function = GetFunction(selected);
	return (function.special_member == SPECIAL_MEMBER_COPY_ASSIGNMENT ||
		function.special_member == SPECIAL_MEMBER_MOVE_ASSIGNMENT) &&
		function.trivial_special_member;
}

bool Analyzer::EvaluateBuiltinTriviallyCopyable(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity) return true;
	if (!IsClassEntity(program_->entities[entity]) ||
		!program_->entities[entity].trivial_destructor ||
		entity >= class_special_members_.size()) return false;
	const ClassSpecialMemberFacts& facts = class_special_members_[entity];
	const BindingId members[] = {
		facts.copy_constructor, facts.move_constructor,
		facts.copy_assignment, facts.move_assignment
	};
	bool eligible = false;
	for (std::size_t i = 0; i < sizeof(members) / sizeof(members[0]); ++i)
	{
		if (members[i] == kNoBinding) continue;
		const FunctionInfo& function = GetFunction(members[i]);
		if (function.deleted_function || function.deleted_constructor ||
			function.deleted_special_member) continue;
		eligible = true;
		if (!function.trivial_special_member) return false;
	}
	return eligible;
}

bool Analyzer::EvaluateBuiltinStandardLayout(TypeId type) const
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity) return true;
	const EntityRecord& owner = program_->entities[entity];
	if (!IsClassEntity(owner) || !owner.complete || owner.polymorphic_class ||
		owner.virtual_base_count != 0 ||
		entity >= entity_data_members_.size()) return false;
	bool has_access = false;
	AccessKind member_access = ACCESS_PUBLIC;
	for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
	{
		const BindingRecord& member =
			program_->bindings[entity_data_members_[entity][i]];
		if (!has_access)
		{
			member_access = member.access;
			has_access = true;
		}
		else if (member.access != member_access) return false;
		TypeId member_type = member.type;
		TypeRecord shape = program_->types.Get(member_type);
		while (shape.kind == TYPE_ARRAY || shape.kind == TYPE_QUALIFIED)
		{
			member_type = shape.child;
			shape = program_->types.Get(member_type);
		}
		if (shape.kind == TYPE_LVALUE_REFERENCE ||
			shape.kind == TYPE_RVALUE_REFERENCE) return false;
		if (shape.kind == TYPE_NAMED &&
			IsClassEntity(program_->entities[shape.entity]) &&
			!EvaluateBuiltinStandardLayout(member_type)) return false;
	}
	bool base_has_members = false;
	for (std::size_t i = 0; i < owner.direct_base_count; ++i)
	{
		const DirectBaseEdge& edge = program_->DirectBase(entity, i);
		if (edge.virtual_base ||
			!EvaluateBuiltinStandardLayout(program_->entities[edge.entity].type))
			return false;
		const bool has_members = edge.entity < entity_data_members_.size() &&
			!entity_data_members_[edge.entity].empty();
		if (has_members && (base_has_members || has_access)) return false;
		base_has_members = base_has_members || has_members;
	}
	return true;
}

bool Analyzer::EvaluateBuiltinTrivialLayoutTrait(
	hosted_builtin::TypeTraitKind trait, TypeId type,
	const TypeRecord& shape, const EntityRecord* named) const
{
	bool value = IsIntegral(type, true) || IsFloating(type) ||
		shape.kind == TYPE_COMPLEX || shape.kind == TYPE_POINTER ||
		shape.kind == TYPE_MEMBER_POINTER;
	if (!named || !IsClassEntity(*named)) return value;
	const bool copyable = EvaluateBuiltinTriviallyCopyable(type);
	if (trait == hosted_builtin::TYPE_TRAIT_IS_STANDARD_LAYOUT)
		return EvaluateBuiltinStandardLayout(type);
	if (trait == hosted_builtin::TYPE_TRAIT_IS_POD)
		return copyable && named->trivial_default_constructor &&
			EvaluateBuiltinStandardLayout(type);
	return copyable &&
		(trait == hosted_builtin::TYPE_TRAIT_IS_LITERAL_TYPE ||
		 named->trivial_default_constructor);
}

bool Analyzer::EvaluateBuiltinNothrowCopy(TypeId type)
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity) return true;
	if (!IsClassEntity(program_->entities[entity]) ||
		entity >= class_special_members_.size()) return false;
	const BindingId copy = class_special_members_[entity].copy_constructor;
	if (copy == kNoBinding) return false;
	const FunctionInfo& function = GetFunction(copy);
	return !function.deleted_function && !function.deleted_constructor &&
		!function.deleted_special_member && CanAccessMember(copy) &&
		FunctionIsNonthrowing(copy);
}

HostedTraitTemplateKind Analyzer::ClassifyHostedTraitTemplate(
	ScopeId owner, NameId name,
	const std::vector<TemplateParameter>& parameters) const
{
	if (owner == kNoScope || name == 0 || parameters.empty())
		return HOSTED_TRAIT_TEMPLATE_NONE;
	for (std::size_t i = 0; i < parameters.size(); ++i)
		if (parameters[i].kind != TEMPLATE_ARGUMENT_TYPE)
			return HOSTED_TRAIT_TEMPLATE_NONE;
	const std::string spelling = program_->names.Get(name);
	if (spelling == "__is_nothrow_invocable" &&
		owner == program_->GlobalScope())
		return HOSTED_TRAIT_TEMPLATE_NOTHROW_INVOCABLE;
	if (!program_->IsStandardNamespace(owner) ||
		parameters.size() != 1)
		return HOSTED_TRAIT_TEMPLATE_NONE;
	if (spelling == "char_traits") return HOSTED_TRAIT_TEMPLATE_CHAR_TRAITS;
	if (spelling == "is_nothrow_default_constructible")
		return HOSTED_TRAIT_TEMPLATE_NOTHROW_DEFAULT_CONSTRUCTIBLE;
	if (spelling == "is_nothrow_copy_constructible")
		return HOSTED_TRAIT_TEMPLATE_NOTHROW_COPY_CONSTRUCTIBLE;
	if (spelling == "is_nothrow_move_constructible")
		return HOSTED_TRAIT_TEMPLATE_NOTHROW_MOVE_CONSTRUCTIBLE;
	return HOSTED_TRAIT_TEMPLATE_NONE;
}

bool Analyzer::EvaluateBuiltinInvocability(
	const std::vector<TypeId>& operands, ScopeId scope, bool* nonthrowing)
{
	if (nonthrowing) *nonthrowing = false;
	if (operands.empty()) return false;
	ExpressionInfo callable = MakeBuiltinTraitOperand(operands[0]);
	const EntityId entity = EntityOf(callable.type);
	if (entity == kNoEntity) return false;
	EnsureClassDefinition(callable.type);
	const NameId name = program_->names.Intern("operator()");
	BeginCandidateCollection();
	std::vector<BindingId> candidates;
	EntityId naming_class = kNoEntity;
	const LookupResult ordinary = program_->LookupMember(
		entity, name, LOOKUP_ORDINARY);
	if (ordinary.ordinary != kNoBinding &&
		program_->bindings[ordinary.ordinary].kind == BIND_FUNCTION)
	{
		naming_class = ordinary.naming_class;
		const std::vector<BindingId> functions =
			FunctionSet(ordinary.ordinary);
		for (std::size_t i = 0; i < functions.size(); ++i)
			if (GetFunction(functions[i]).member_owner != kNoType)
				AddCandidate(functions[i], &candidates);
	}
	std::vector<ExpressionInfo> arguments;
	for (std::size_t i = 1; i < operands.size(); ++i)
		arguments.push_back(MakeBuiltinTraitOperand(operands[i]));
	const LookupResult templates = program_->LookupMember(
		entity, name, LOOKUP_FUNCTION_TEMPLATE);
	std::vector<std::size_t> patterns;
	for (std::size_t owner_index = 0;
		owner_index < templates.FunctionTemplateOwnerCount(); ++owner_index)
	{
		const ScopeId template_owner =
			templates.FunctionTemplateOwnerAt(owner_index);
		const std::uint64_t key =
			(static_cast<std::uint64_t>(template_owner) << 32) | name;
		const CompactIndexSequence* indexed =
			template_function_sets_.Find(key);
		for (std::size_t i = 0; indexed && i < indexed->Size(); ++i)
			patterns.push_back((*indexed)[i]);
	}
	if (!patterns.empty())
	{
		associated_declaration_visits_ += patterns.size();
		std::vector<BindingId> specializations;
		DeduceFunctionTemplatePatterns(patterns, arguments, &specializations);
		for (std::size_t i = 0; i < specializations.size(); ++i)
			if (GetFunction(specializations[i]).member_owner != kNoType)
				AddCandidate(specializations[i], &candidates);
		if (naming_class == kNoEntity) naming_class = templates.naming_class;
	}
	if (candidates.empty()) return false;
	std::vector<NodeId> syntax(arguments.size() + 1, kNoNode);
	std::vector<ExpressionInfo> invocation;
	invocation.reserve(arguments.size() + 1);
	invocation.push_back(callable);
	invocation.insert(invocation.end(), arguments.begin(), arguments.end());
	ExpressionInfo object;
	object.type = program_->types.Pointer(EffectiveType(callable.type));
	object.category = callable.category;
	bool selected_member = false;
	ObjectConversionFact object_conversion;
	std::vector<CallConversionFact> conversions;
	const BindingId selected = SelectOperatorOverload(scope, syntax,
		invocation, candidates, object, &selected_member, &object_conversion,
		&conversions, true);
	if (selected == kNoBinding || !selected_member) return false;
	const FunctionInfo& function = GetFunction(selected);
	if (function.deleted_function || function.deleted_special_member ||
		!CanAccessMember(selected, naming_class, entity)) return false;
	for (std::size_t i = 0; i < conversions.size(); ++i)
		if (!BuiltinConversionIsUsable(conversions[i])) return false;
	if (nonthrowing)
	{
		*nonthrowing = FunctionIsNonthrowing(selected);
		for (std::size_t i = 0; *nonthrowing && i < conversions.size(); ++i)
			*nonthrowing = BuiltinConversionIsNonthrowing(conversions[i]);
	}
	return true;
}

bool Analyzer::CompleteHostedTraitTemplateSpecialization(
	std::size_t pattern_index, BindingId specialization,
	const std::vector<TemplateArgument>& arguments)
{
	if (pattern_index >= class_templates_.size())
		ThrowInternalCompilerError("invalid hosted trait template pattern");
	const ClassTemplatePattern& pattern = class_templates_[pattern_index];
	const HostedTraitTemplateKind kind = pattern.hosted_trait_template;
	if (kind == HOSTED_TRAIT_TEMPLATE_NONE) return false;
	if (kind == HOSTED_TRAIT_TEMPLATE_CHAR_TRAITS)
	{
		if (pattern.defined) return false;
		if (arguments.size() != 1 ||
			arguments[0].kind != TEMPLATE_ARGUMENT_TYPE ||
			arguments[0].type == kNoType) return false;
		if (specialization == kNoBinding ||
			specialization >= program_->bindings.size())
			ThrowInternalCompilerError("invalid hosted char_traits specialization");
		const EntityId entity = EntityOf(program_->bindings[specialization].type);
		if (entity == kNoEntity)
			ThrowInternalCompilerError("hosted char_traits specialization has no entity");
		EntityRecord& record = program_->entities[entity];
		if (record.member_scope == kNoScope)
		{
			const ScopeId member_scope = NewScope(pattern.owner, SCOPE_CLASS,
				pattern.name, ScopePrefixId(pattern.owner));
			program_->SetEntityScope(entity, member_scope);
			program_->SetTypeName(member_scope, pattern.name, record.type);
			const BindingId injected = program_->AddBinding(member_scope,
				BIND_TYPE, pattern.name, record.type, false, 0, record.flavor);
			program_->bindings[injected].member_owner = entity;
			program_->bindings[injected].access = ACCESS_PUBLIC;
			const TypeId integer =
				program_->types.Fundamental(FUND_UNSIGNED_INT);
			const BindingId alias = program_->AddBinding(member_scope,
				BIND_TYPE_ALIAS, program_->names.Intern("int_type"), integer);
			program_->bindings[alias].member_owner = entity;
			program_->bindings[alias].access = ACCESS_PUBLIC;
			const TypeId results[] = {arguments[0].type, integer};
			const TypeId inputs[] = {integer, arguments[0].type};
			const char* names[] = {"to_char_type", "to_int_type"};
			for (std::size_t i = 0; i < 2; ++i)
			{
				const std::vector<TypeId> parameter_types(1, inputs[i]);
				const std::vector<ParameterInfo> parameters(
					1, ParameterInfo(0, inputs[i], inputs[i]));
				const TypeId function_type = program_->types.Function(
					results[i], parameter_types, false);
				const BindingId function = DeclareFunction(member_scope,
					program_->names.Intern(names[i]), function_type, parameters,
					false);
				BindingRecord& binding = program_->bindings[function];
				binding.member_owner = entity;
				binding.access = ACCESS_PUBLIC;
				binding.static_member_function = true;
				RegisterClassMemberFunction(entity, function);
			}
		}
		record.object_size = record.nonvirtual_size = 1;
		record.object_alignment = record.nonvirtual_alignment = 1;
		record.natural_alignment = 1;
		record.default_constructible = record.trivial_default_constructor = true;
		record.destructible = record.trivial_destructor = true;
		record.is_aggregate = record.empty_class = record.layout_complete = true;
		record.complete = true;
		class_template_specialization_states_[specialization] = 2;
		return true;
	}
	std::vector<TypeId> operands;
	operands.reserve(arguments.size());
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		if (arguments[i].kind != TEMPLATE_ARGUMENT_TYPE ||
			arguments[i].type == kNoType || arguments[i].IsDependent())
			return false;
		operands.push_back(arguments[i].type);
	}
	bool value = false;
	if (kind == HOSTED_TRAIT_TEMPLATE_NOTHROW_INVOCABLE)
	{
		bool nonthrowing = false;
		value = EvaluateBuiltinInvocability(
			operands, pattern.lexical_scope, &nonthrowing) && nonthrowing;
	}
	else
	{
		if (operands.size() != 1) return false;
		std::vector<TypeId> construction(1, operands[0]);
		if (kind == HOSTED_TRAIT_TEMPLATE_NOTHROW_COPY_CONSTRUCTIBLE ||
			kind == HOSTED_TRAIT_TEMPLATE_NOTHROW_MOVE_CONSTRUCTIBLE)
		{
			TypeId source = operands[0];
			if (kind == HOSTED_TRAIT_TEMPLATE_NOTHROW_COPY_CONSTRUCTIBLE)
				source = program_->types.TryQualify(source, CV_CONST);
			if (source != kNoType)
				source = program_->types.TryReference(
					kind == HOSTED_TRAIT_TEMPLATE_NOTHROW_COPY_CONSTRUCTIBLE ?
						TYPE_LVALUE_REFERENCE : TYPE_RVALUE_REFERENCE, source);
			if (source == kNoType) return false;
			construction.push_back(source);
		}
		BindingId selected = kNoBinding;
		std::vector<CallConversionFact> conversions;
		value = EvaluateBuiltinConstructibility(
			construction, &selected, &conversions) &&
			BuiltinConstructionIsNonthrowing(
				construction[0], selected, conversions);
	}

	if (specialization == kNoBinding ||
		specialization >= program_->bindings.size())
		ThrowInternalCompilerError("invalid hosted trait specialization binding");
	const EntityId entity = EntityOf(program_->bindings[specialization].type);
	if (entity == kNoEntity)
		ThrowInternalCompilerError("hosted trait specialization has no entity");
	EntityRecord& record = program_->entities[entity];
	if (record.member_scope == kNoScope)
	{
		const ScopeId member_scope = NewScope(pattern.owner, SCOPE_CLASS,
			pattern.name, ScopePrefixId(pattern.owner));
		program_->SetEntityScope(entity, member_scope);
		program_->SetTypeName(member_scope, pattern.name, record.type);
		const BindingId injected = program_->AddBinding(member_scope,
			BIND_TYPE, pattern.name, record.type, false, 0, record.flavor);
		program_->bindings[injected].member_owner = entity;
		program_->bindings[injected].access = ACCESS_PUBLIC;
		const TypeId bool_type = program_->types.Qualify(
			program_->types.Fundamental(FUND_BOOL), CV_CONST);
		const NameId value_name = program_->names.Intern("value");
		const BindingId member = program_->AddBinding(member_scope,
			BIND_VARIABLE, value_name, bool_type, true, value ? 1 : 0);
		program_->bindings[member].member_owner = entity;
		program_->bindings[member].access = ACCESS_PUBLIC;
	}
	record.object_size = 1;
	record.nonvirtual_size = 1;
	record.object_alignment = 1;
	record.nonvirtual_alignment = 1;
	record.natural_alignment = 1;
	record.default_constructible = true;
	record.trivial_default_constructor = true;
	record.destructible = true;
	record.trivial_destructor = true;
	record.is_aggregate = true;
	record.empty_class = true;
	record.layout_complete = true;
	record.complete = true;
	class_template_specialization_states_[specialization] = 2;
	return true;
}

}
}
