#include "pa12_semantic_detail.h"

#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

bool IsClassEntity(const EntityRecord& entity)
{
	return entity.flavor == NAMED_STRUCT || entity.flavor == NAMED_CLASS ||
		entity.flavor == NAMED_UNION;
}

BindingId SelectedConversionFunction(const CallConversionFact& conversion)
{
	if (conversion.conversion_function != kNoBinding)
		return conversion.conversion_function;
	if (conversion.constructor != kNoBinding) return conversion.constructor;
	return conversion.constructor_argument_conversion_function;
}

}

ExpressionInfo SemanticAnalyzer::MakeBuiltinTraitOperand(TypeId type) const
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

bool SemanticAnalyzer::BuiltinConversionIsUsable(
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

bool SemanticAnalyzer::BuiltinConversionIsNonthrowing(
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

bool SemanticAnalyzer::EvaluateBuiltinConstructibility(
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

bool SemanticAnalyzer::EvaluateBuiltinConvertibility(
	TypeId source_type, TypeId target)
{
	const bool source_void = IsVoid(source_type);
	const bool target_void = IsVoid(target);
	if (source_void || target_void) return source_void && target_void;
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

bool SemanticAnalyzer::BuiltinDefaultConstructionIsNonthrowing(EntityId entity)
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

bool SemanticAnalyzer::BuiltinConstructionIsNonthrowing(TypeId target,
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

bool SemanticAnalyzer::BuiltinConstructionIsTrivial(TypeId target,
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

bool SemanticAnalyzer::EvaluateBuiltinAssignability(TypeId target,
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

bool SemanticAnalyzer::BuiltinAssignmentIsNonthrowing(BindingId selected,
	const std::vector<CallConversionFact>& argument_conversions)
{
	if (selected != kNoBinding && !FunctionIsNonthrowing(selected)) return false;
	for (std::size_t i = 0; i < argument_conversions.size(); ++i)
		if (!BuiltinConversionIsNonthrowing(argument_conversions[i])) return false;
	return true;
}

bool SemanticAnalyzer::BuiltinAssignmentIsTrivial(BindingId selected,
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

bool SemanticAnalyzer::EvaluateBuiltinTriviallyCopyable(TypeId type) const
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

bool SemanticAnalyzer::EvaluateBuiltinNothrowCopy(TypeId type)
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

}
}
