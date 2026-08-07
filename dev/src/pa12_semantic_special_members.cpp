#include "pa12_semantic_detail.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

namespace
{

bool IsAssignmentSpecialMember(SpecialMemberKind kind)
{
	return kind == SPECIAL_MEMBER_COPY_ASSIGNMENT ||
		kind == SPECIAL_MEMBER_MOVE_ASSIGNMENT;
}

EntityId SubobjectClass(const Program& program, TypeId type,
	bool* is_const, bool* is_reference)
{
	*is_const = false;
	*is_reference = false;
	const TypeRecord* record = &program.types.Get(type);
	if (record->kind == TYPE_LVALUE_REFERENCE ||
		record->kind == TYPE_RVALUE_REFERENCE)
	{
		*is_reference = true;
		return kNoEntity;
	}
	while (record->kind == TYPE_ARRAY || record->kind == TYPE_QUALIFIED)
	{
		if (record->kind == TYPE_QUALIFIED &&
			(record->cv & CV_CONST) != 0)
			*is_const = true;
		type = record->child;
		record = &program.types.Get(type);
	}
	if (record->kind != TYPE_NAMED) return kNoEntity;
	const NamedFlavor flavor = program.entities[record->entity].flavor;
	return flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
		flavor == NAMED_UNION ? record->entity : kNoEntity;
}

}

void SemanticAnalyzer::RegisterClassSpecialMember(BindingId binding)
{
	binding = program_->bindings[binding].canonical;
	FunctionInfo& function = GetMutableFunction(binding);
	const BindingRecord& declaration = program_->bindings[binding];
	const EntityId entity = declaration.member_owner;
	if (entity == kNoEntity || function.member_owner == kNoType) return;
	const TypeRecord& function_type = program_->types.Get(function.type);
	if (function_type.kind != TYPE_FUNCTION ||
		function_type.parameter_count != 1)
		return;
	const TypeId parameter = program_->types.Parameters(function.type)[0];
	const TypeRecord& reference = program_->types.Get(parameter);
	if (reference.kind != TYPE_LVALUE_REFERENCE &&
		reference.kind != TYPE_RVALUE_REFERENCE)
		return;
	if (program_->types.RemoveTopCv(reference.child) !=
		program_->entities[entity].type)
		return;

	SpecialMemberKind kind = SPECIAL_MEMBER_NONE;
	if (function.constructor)
		kind = reference.kind == TYPE_LVALUE_REFERENCE ?
			SPECIAL_MEMBER_COPY_CONSTRUCTOR :
			SPECIAL_MEMBER_MOVE_CONSTRUCTOR;
	else if (declaration.operator_kind == OPERATOR_ASSIGN)
		kind = reference.kind == TYPE_LVALUE_REFERENCE ?
			SPECIAL_MEMBER_COPY_ASSIGNMENT :
			SPECIAL_MEMBER_MOVE_ASSIGNMENT;
	if (kind == SPECIAL_MEMBER_NONE) return;

	if (class_special_members_.size() <= entity)
		class_special_members_.resize(static_cast<std::size_t>(entity) + 1);
	ClassSpecialMemberFacts& facts = class_special_members_[entity];
	BindingId* slot = kind == SPECIAL_MEMBER_COPY_CONSTRUCTOR ?
		&facts.copy_constructor : kind == SPECIAL_MEMBER_MOVE_CONSTRUCTOR ?
		&facts.move_constructor : kind == SPECIAL_MEMBER_COPY_ASSIGNMENT ?
		&facts.copy_assignment : &facts.move_assignment;
	if (*slot != kNoBinding && *slot != binding)
		throw std::runtime_error("duplicate class special member");
	*slot = binding;
	function.special_member = kind;
	if (function.constructor)
	{
		function.defaulted_special_member = function.defaulted_constructor;
		function.deleted_special_member = function.deleted_constructor;
	}
	if (function.implicit_special_member) return;
	if (kind == SPECIAL_MEMBER_COPY_CONSTRUCTOR)
		facts.user_copy_constructor = true;
	else if (kind == SPECIAL_MEMBER_MOVE_CONSTRUCTOR)
		facts.user_move_constructor = true;
	else if (kind == SPECIAL_MEMBER_COPY_ASSIGNMENT)
		facts.user_copy_assignment = true;
	else facts.user_move_assignment = true;
}

void SemanticAnalyzer::ConfigureAssignmentSpecialMember(BindingId binding,
	NodeId initializer)
{
	binding = program_->bindings[binding].canonical;
	RegisterClassSpecialMember(binding);
	FunctionInfo& function = GetMutableFunction(binding);
	if (!IsAssignmentSpecialMember(function.special_member)) return;
	const NodeId special = initializer == kNoNode ? kNoNode :
		FindChild(initializer, "special-initializer");
	if (special == kNoNode) return;
	const std::string spelling = arena_->Payload(special);
	const bool defaulted = spelling == "default";
	const bool deleted = spelling == "delete";
	if (!defaulted && !deleted)
		throw std::runtime_error("invalid assignment special initializer");
	function.defaulted_special_member =
		function.defaulted_special_member || defaulted;
	function.deleted_special_member =
		function.deleted_special_member || deleted;
	function.defined = true;
	function.deferred = !function.deleted_special_member;
	BindingRecord& declaration = program_->bindings[binding];
	declaration.inline_function = declaration.inline_function || defaulted;
}

bool SemanticAnalyzer::AnalyzeQualifiedAssignmentStatement(NodeId node,
	ScopeId scope, std::uint32_t output_parent)
{
	const NodeId specifiers = FindChild(node, "decl-specifier-seq");
	const NodeId list = FindChild(node, "init-declarator-list");
	const NodeId specifier = specifiers == kNoNode ? kNoNode :
		FirstSemanticChild(specifiers);
	if (specifier == kNoNode || list == kNoNode) return false;
	const std::string spelling = PayloadSource(specifier);
	if (spelling.find("::operator=") == std::string::npos) return false;
	const NodeId item = FirstSemanticChild(list);
	const NodeId declarator = item == kNoNode ? kNoNode :
		FindChild(item, "declarator");
	const NodeId nested = declarator == kNoNode ? kNoNode :
		FindChild(declarator, "nested-declarator");
	const NodeId argument_declarator = nested == kNoNode ? kNoNode :
		FirstSemanticChild(nested);
	const NodeId identifier = argument_declarator == kNoNode ? kNoNode :
		FindChild(argument_declarator, "identifier");
	if (identifier == kNoNode)
		throw std::runtime_error(
			"unsupported qualified assignment argument syntax");

	EntityId naming_class = kNoEntity;
	const std::vector<BindingId> candidates =
		FunctionCandidates(scope, spelling, &naming_class);
	if (candidates.empty())
		throw std::runtime_error("qualified assignment operator was not found");
	const NameId argument_name =
		program_->names.Intern(arena_->Payload(identifier));
	const LookupResult found_argument =
		program_->LookupName(scope, argument_name, LOOKUP_ORDINARY);
	if (found_argument.ordinary == kNoBinding)
		throw std::runtime_error("qualified assignment argument was not found");
	const BindingRecord& argument_binding =
		program_->bindings[found_argument.ordinary];
	ExpressionInfo argument;
	argument.type = argument_binding.type;
	argument.category = VALUE_LVALUE;
	argument.binding = found_argument.ordinary;
	argument.node = MakeDump(DUMP_ID_EXPRESSION, argument.type,
		VALUE_LVALUE, argument_name, argument.binding);
	++expression_count_;

	const NameId this_name = program_->names.Intern("this");
	const LookupResult found_this =
		program_->LookupName(scope, this_name, LOOKUP_ORDINARY);
	if (found_this.ordinary == kNoBinding)
		throw std::runtime_error("qualified assignment has no object");
	ExpressionInfo object;
	object.type = EffectiveType(program_->bindings[found_this.ordinary].type);
	object.category = VALUE_LVALUE;
	object.binding = found_this.ordinary;
	object.node = MakeDump(DUMP_ID_EXPRESSION, object.type,
		VALUE_LVALUE, this_name, object.binding);
	++expression_count_;

	std::vector<NodeId> argument_syntax(1, identifier);
	std::vector<ExpressionInfo> arguments(1, argument);
	ObjectConversionFact object_conversion;
	std::vector<CallConversionFact> argument_conversions;
	const BindingId selected = SelectOverload(scope, argument_syntax,
		arguments, candidates, &object, &object_conversion,
		&argument_conversions);
	const ExpressionInfo call = BuildResolvedCall(selected, scope,
		argument_syntax, arguments, &object, kNoType, naming_class,
		&object_conversion, &argument_conversions);
	const std::uint32_t statement = MakeDump(DUMP_EXPRESSION_STATEMENT);
	dump_.Add(statement, call.node);
	dump_.Add(output_parent, statement);
	return true;
}

BindingId SemanticAnalyzer::AssignmentForSubobject(TypeId type,
	SpecialMemberKind kind) const
{
	++special_member_fact_lookups_;
	bool is_const = false;
	bool is_reference = false;
	const EntityId entity = SubobjectClass(
		*program_, type, &is_const, &is_reference);
	(void)is_const;
	(void)is_reference;
	if (entity == kNoEntity || entity >= class_special_members_.size())
		return kNoBinding;
	const ClassSpecialMemberFacts& facts = class_special_members_[entity];
	if (kind == SPECIAL_MEMBER_MOVE_ASSIGNMENT &&
		facts.move_assignment != kNoBinding)
		return facts.move_assignment;
	return facts.copy_assignment;
}

void SemanticAnalyzer::EvaluateSynthesizedAssignment(EntityId entity,
	SpecialMemberKind kind, bool* deleted, bool* trivial,
	bool* nonthrowing) const
{
	if (!IsAssignmentSpecialMember(kind))
		throw std::logic_error("assignment status requested for another member");
	*deleted = false;
	*trivial = true;
	*nonthrowing = true;
	const EntityRecord& owner = program_->entities[entity];
	const auto visit = [this, entity, kind, deleted, trivial, nonthrowing](
		TypeId type)
	{
		++special_member_subobject_visits_;
		bool is_const = false;
		bool is_reference = false;
		const EntityId subobject = SubobjectClass(
			*program_, type, &is_const, &is_reference);
		if (is_const || is_reference)
		{
			*deleted = true;
			return;
		}
		if (subobject == kNoEntity) return;
		const BindingId selected = AssignmentForSubobject(type, kind);
		if (selected == kNoBinding)
		{
			*deleted = true;
			return;
		}
		const FunctionInfo& function = GetFunction(selected);
		if (function.deleted_special_member ||
			!CanAccessMember(selected, subobject, entity))
			*deleted = true;
		if (!function.trivial_special_member) *trivial = false;
		if (!program_->bindings[selected].nonthrowing) *nonthrowing = false;
	};
	if (owner.direct_base != kNoEntity)
		visit(program_->entities[owner.direct_base].type);
	if (entity < entity_data_members_.size())
		for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
			visit(program_->bindings[entity_data_members_[entity][i]].type);
}

BindingId SemanticAnalyzer::DeclareImplicitAssignment(EntityId entity,
	SpecialMemberKind kind)
{
	if (!IsAssignmentSpecialMember(kind))
		throw std::logic_error("cannot declare non-assignment helper");
	bool deleted = false;
	bool trivial = false;
	bool nonthrowing = false;
	EvaluateSynthesizedAssignment(
		entity, kind, &deleted, &trivial, &nonthrowing);
	const ClassSpecialMemberFacts& existing = class_special_members_[entity];
	if (kind == SPECIAL_MEMBER_COPY_ASSIGNMENT &&
		(existing.user_move_constructor || existing.user_move_assignment))
		deleted = true;

	const EntityRecord& owner = program_->entities[entity];
	TypeId source = owner.type;
	const TypeKind reference_kind = kind == SPECIAL_MEMBER_COPY_ASSIGNMENT ?
		TYPE_LVALUE_REFERENCE : TYPE_RVALUE_REFERENCE;
	if (kind == SPECIAL_MEMBER_COPY_ASSIGNMENT)
		source = program_->types.Qualify(source, CV_CONST);
	const TypeId parameter_type =
		program_->types.Reference(reference_kind, source);
	const TypeId result_type =
		program_->types.Reference(TYPE_LVALUE_REFERENCE, owner.type);
	std::vector<TypeId> parameter_types(1, parameter_type);
	const TypeId function_type =
		program_->types.Function(result_type, parameter_types, false);
	const NameId name = program_->names.Intern("operator=");
	const NameId parameter_name = program_->names.Intern("other");
	std::vector<ParameterInfo> parameters;
	parameters.push_back(ParameterInfo(
		parameter_name, parameter_type, parameter_type));
	const BindingId assignment = DeclareFunction(owner.member_scope, name,
		function_type, parameters, true, false, STORAGE_CLASS_NONE,
		LANGUAGE_LINKAGE_CPP, nonthrowing);
	BindingRecord& declaration = program_->bindings[assignment];
	declaration.member_owner = entity;
	declaration.access = ACCESS_PUBLIC;
	declaration.inline_function = true;
	FunctionInfo& function = GetMutableFunction(assignment);
	function.member_owner = owner.type;
	function.special_member = kind;
	function.implicit_special_member = true;
	function.defaulted_special_member = true;
	function.deleted_special_member = deleted;
	function.trivial_special_member = trivial;
	function.deferred = !deleted;
	ClassSpecialMemberFacts& facts = class_special_members_[entity];
	if (kind == SPECIAL_MEMBER_COPY_ASSIGNMENT)
		facts.copy_assignment = assignment;
	else facts.move_assignment = assignment;
	return assignment;
}

void SemanticAnalyzer::CompleteClassSpecialMembers(EntityId entity)
{
	if (class_special_members_.size() <= entity)
		class_special_members_.resize(static_cast<std::size_t>(entity) + 1);
	ClassSpecialMemberFacts& facts = class_special_members_[entity];
	if (facts.copy_assignment == kNoBinding)
		DeclareImplicitAssignment(entity, SPECIAL_MEMBER_COPY_ASSIGNMENT);
	else
	{
		FunctionInfo& function = GetMutableFunction(facts.copy_assignment);
		if (function.defaulted_special_member &&
			!function.deleted_special_member)
		{
			bool deleted = false;
			bool trivial = false;
			bool nonthrowing = false;
			EvaluateSynthesizedAssignment(entity,
				SPECIAL_MEMBER_COPY_ASSIGNMENT,
				&deleted, &trivial, &nonthrowing);
			function.deleted_special_member = deleted;
			function.trivial_special_member = trivial;
			program_->bindings[facts.copy_assignment].nonthrowing = nonthrowing;
			function.deferred = !deleted;
		}
	}

	const bool implicit_move = facts.move_assignment == kNoBinding &&
		!facts.user_copy_constructor && !facts.user_move_constructor &&
		!facts.user_copy_assignment && !facts.user_move_assignment &&
		!program_->entities[entity].has_user_declared_destructor;
	if (implicit_move)
		DeclareImplicitAssignment(entity, SPECIAL_MEMBER_MOVE_ASSIGNMENT);
	else if (facts.move_assignment != kNoBinding)
	{
		FunctionInfo& function = GetMutableFunction(facts.move_assignment);
		if (function.defaulted_special_member &&
			!function.deleted_special_member)
		{
			bool deleted = false;
			bool trivial = false;
			bool nonthrowing = false;
			EvaluateSynthesizedAssignment(entity,
				SPECIAL_MEMBER_MOVE_ASSIGNMENT,
				&deleted, &trivial, &nonthrowing);
			function.deleted_special_member = deleted;
			function.trivial_special_member = trivial;
			program_->bindings[facts.move_assignment].nonthrowing = nonthrowing;
			function.deferred = !deleted;
		}
	}
}

void SemanticAnalyzer::AddSynthesizedAssignmentBody(
	const FunctionInfo& function, const std::vector<BindingId>& parameters,
	std::uint32_t body)
{
	if (!IsAssignmentSpecialMember(function.special_member) ||
		parameters.size() != 1 || function.deleted_special_member)
		throw std::logic_error("invalid synthesized assignment body");
	const EntityId entity = program_->bindings[function.binding].member_owner;
	const EntityRecord& owner = program_->entities[entity];
	const std::uint32_t assignment = MakeDump(
		DUMP_SPECIAL_MEMBER_ASSIGNMENT_ACTION,
		owner.type, VALUE_LVALUE, 0, function.binding);
	dump_.nodes[assignment].object_binding = parameters[0];

	if (function.trivial_special_member &&
		owner.direct_base == kNoEntity && !owner.empty_class)
	{
		const std::uint32_t step = MakeDump(
			DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, owner.type);
		dump_.Add(assignment, step);
	}
	else
	{
		if (owner.direct_base != kNoEntity)
		{
			++special_member_subobject_visits_;
			const EntityRecord& base =
				program_->entities[owner.direct_base];
			const BindingId selected = AssignmentForSubobject(
				base.type, function.special_member);
			if (selected == kNoBinding)
				throw std::logic_error("synthesized base assignment is missing");
			if (!base.empty_class || !GetFunction(selected).trivial_special_member)
			{
				const std::uint32_t step = MakeDump(
					DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, base.type);
				dump_.nodes[step].selected_binding = selected;
				dump_.Add(assignment, step);
				DemandFunction(selected);
			}
		}
		if (entity < entity_data_members_.size())
			for (std::size_t i = 0;
				i < entity_data_members_[entity].size(); ++i)
			{
				++special_member_subobject_visits_;
				const BindingId member = entity_data_members_[entity][i];
				const TypeId type = program_->bindings[member].type;
				const BindingId selected =
					AssignmentForSubobject(type, function.special_member);
				bool is_const = false;
				bool is_reference = false;
				const EntityId member_entity = SubobjectClass(
					*program_, type, &is_const, &is_reference);
				if (member_entity != kNoEntity && selected == kNoBinding)
					throw std::logic_error(
						"synthesized member assignment is missing");
				if (member_entity != kNoEntity &&
					program_->entities[member_entity].empty_class &&
					GetFunction(selected).trivial_special_member)
					continue;
				const std::uint32_t step = MakeDump(
					DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, type,
					VALUE_NONE, 0, member);
				dump_.nodes[step].selected_binding = selected;
				dump_.Add(assignment, step);
				if (selected != kNoBinding) DemandFunction(selected);
			}
	}
	const std::uint32_t statement = MakeDump(DUMP_RETURN_STATEMENT);
	dump_.Add(statement, assignment);
	dump_.Add(body, statement);
	++expression_count_;
}

}
}
