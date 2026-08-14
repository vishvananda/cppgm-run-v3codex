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

void SemanticAnalyzer::InheritConstructors(EntityId entity,
	const std::vector<BindingId>& constructors)
{
	EntityRecord& derived = program_->entities[entity];
	derived.is_aggregate = false;
	if (entity_constructors_.size() <= entity)
		entity_constructors_.resize(static_cast<std::size_t>(entity) + 1);
	for (std::size_t i = 0; i < constructors.size(); ++i)
	{
		const FunctionInfo source = GetFunction(constructors[i]);
		if (!source.constructor || source.parameters.empty()) continue;
		const FunctionSignatureKey signature_key(derived.member_scope,
			derived.identity_name, source.signature);
		++function_signature_lookups_;
		if (function_declarations_.Find(signature_key) != kNoBinding)
			continue;
		const BindingRecord source_binding =
			program_->bindings[source.binding];
		const BindingId source_base_entry =
			EnsureConstructorBaseEntry(source.binding);
		const BindingId inherited = DeclareFunction(derived.member_scope,
			derived.identity_name, source.type, source.parameters, true, false,
			STORAGE_CLASS_NONE, source_binding.language_linkage,
			source_binding.nonthrowing);
		BindingRecord& binding = program_->bindings[inherited];
		binding.member_owner = entity;
		binding.access_owner = source_binding.access_owner != kNoEntity ?
			source_binding.access_owner : source_binding.member_owner;
		binding.access = source_binding.access;
		binding.constructor = true;
		FunctionInfo& info = GetMutableFunction(inherited);
		info.member_owner = derived.type;
		info.constructor = true;
		info.explicit_constructor = source.explicit_constructor;
		info.deleted_constructor = source.deleted_constructor;
		info.inherited_constructor_source = source_base_entry;
		if (source.exception_specification_state !=
				EXCEPTION_SPECIFICATION_FIXED &&
			source.exception_specification_state !=
				EXCEPTION_SPECIFICATION_SUCCEEDED)
			info.exception_specification_state =
				EXCEPTION_SPECIFICATION_DEFERRED;
		info.deferred = !info.deleted_constructor;
		entity_constructors_[entity].push_back(inherited);
		std::size_t required = info.parameters.size();
		while (required != 0 &&
			info.parameters[required - 1].default_argument != kNoNode) --required;
		if (!info.deleted_constructor && required == 0)
			derived.default_constructible = true;
	}
}

bool SemanticAnalyzer::TryInheritConstructors(EntityId entity, ScopeId scope,
	ScopeId target_owner, NameId target_name, bool names_owner_alias,
	const std::vector<BindingId>& constructors,
	const std::vector<std::size_t>& template_patterns)
{
	if (entity == kNoEntity) return false;
	bool constructor_set = false;
	for (std::size_t base_ordinal = 0;
		base_ordinal < program_->entities[entity].direct_base_count;
		++base_ordinal)
	{
		const EntityId base = program_->DirectBase(
			entity, base_ordinal).entity;
		if (target_owner == program_->entities[base].member_scope &&
			(target_name == program_->entities[base].identity_name ||
			 names_owner_alias))
		{
			constructor_set = true;
			break;
		}
	}
	for (std::size_t i = 0; i < constructors.size(); ++i)
		constructor_set = constructor_set &&
			GetFunction(constructors[i]).constructor;
	for (std::size_t i = 0; i < template_patterns.size(); ++i)
		constructor_set = constructor_set &&
			template_patterns[i] < function_templates_.size() &&
			function_templates_[template_patterns[i]].constructor_template;
	if (!constructor_set) return false;

	InheritConstructors(entity, constructors);
	const NameId derived_name = program_->entities[entity].identity_name;
	const std::uint64_t key =
		(static_cast<std::uint64_t>(scope) << 32) | derived_name;
	CompactIndexSequence& inherited = template_function_sets_.Ensure(key);
	for (std::size_t i = 0; i < template_patterns.size(); ++i)
		if (!inherited.Contains(template_patterns[i]))
			inherited.Push(template_patterns[i]);
	if (!template_patterns.empty())
		program_->PublishFunctionTemplateName(scope, derived_name);
	return true;
}

BindingId SemanticAnalyzer::EnsureConstructorBaseEntry(BindingId constructor)
{
	constructor = program_->bindings[constructor].canonical;
	if (program_->bindings[constructor].constructor_base_entry)
	{
		program_->bindings[constructor].lifecycle_base_entry = constructor;
		return constructor;
	}
	if (constructor_base_entry_by_binding_.size() <= constructor)
		constructor_base_entry_by_binding_.resize(
			static_cast<std::size_t>(constructor) + 1, kNoBinding);
	if (constructor_base_entry_by_binding_[constructor] != kNoBinding &&
		constructor_base_entry_by_binding_[constructor] != constructor)
	{
		const BindingId base_entry =
			constructor_base_entry_by_binding_[constructor];
		program_->bindings[constructor].lifecycle_base_entry = base_entry;
		return base_entry;
	}
	const BindingRecord source_binding = program_->bindings[constructor];
	const FunctionInfo source_info = GetFunction(constructor);
	if (!source_binding.constructor || !source_info.constructor)
		throw std::logic_error(
			"constructor base entry requested for non-constructor");
	const NameId generated_name = program_->names.Intern(
		"__cppgm_constructor_base_" + std::to_string(constructor));
	const BindingId base_entry = program_->AddBinding(source_binding.owner,
		BIND_FUNCTION, generated_name, source_binding.type, false, 0,
		NAMED_NONE, 0, kNoBinding, false);
	BindingRecord& binding = program_->bindings[base_entry];
	binding.member_owner = source_binding.member_owner;
	binding.access_owner = source_binding.access_owner;
	binding.qualified_name = source_binding.qualified_name;
	binding.overload_ordinal = source_binding.overload_ordinal;
	binding.template_argument_list = source_binding.template_argument_list;
	binding.template_argument_begin = source_binding.template_argument_begin;
	binding.template_argument_count = source_binding.template_argument_count;
	binding.function_template_abi_recipe =
		source_binding.function_template_abi_recipe;
	binding.abi_tag_begin = source_binding.abi_tag_begin;
	binding.abi_tag_count = source_binding.abi_tag_count;
	binding.language_linkage = source_binding.language_linkage;
	binding.storage_class = source_binding.storage_class;
	binding.access = source_binding.access;
	binding.nonthrowing = source_binding.nonthrowing;
	binding.unnamed_namespace_linkage =
		source_binding.unnamed_namespace_linkage;
	binding.inline_function = source_binding.inline_function;
	binding.force_inline = source_binding.force_inline;
	binding.weak_odr = source_binding.weak_odr;
	binding.weak_symbol = source_binding.weak_symbol;
	binding.object_output_root = source_binding.object_output_root;
	binding.explicit_instantiation_suppressed =
		source_binding.explicit_instantiation_suppressed;
	binding.static_member_function = source_binding.static_member_function;
	binding.constructor = true;
	binding.constructor_base_entry = true;
	binding.lifecycle_base_entry = base_entry;

	FunctionInfo info = source_info;
	info.binding = base_entry;
	info.complete_constructor = source_info.complete_constructor == kNoBinding ?
		constructor : source_info.complete_constructor;
	info.ordinary_visible = false;
	info.demand_state = 0;
	if (function_fact_by_binding_.size() <= base_entry)
		function_fact_by_binding_.resize(
			static_cast<std::size_t>(base_entry) + 1, kNoDumpEdge);
	function_fact_by_binding_[base_entry] =
		static_cast<std::uint32_t>(functions_.size());
	functions_.push_back(info);
	constructor_base_entry_by_binding_[constructor] = base_entry;
	program_->bindings[constructor].lifecycle_base_entry = base_entry;
	return base_entry;
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
		function_type.parameter_count == 0)
		return;
	if (function.constructor)
	{
		for (std::size_t i = 1; i < function_type.parameter_count; ++i)
			if (i >= function.parameters.size() ||
				function.parameters[i].default_argument == kNoNode)
				return;
	}
	else if (function_type.parameter_count != 1) return;
	const TypeId parameter = program_->types.Parameters(function.type)[0];
	const TypeRecord& reference = program_->types.Get(parameter);
	const bool by_reference = reference.kind == TYPE_LVALUE_REFERENCE ||
		reference.kind == TYPE_RVALUE_REFERENCE;
	const TypeId parameter_object = program_->types.RemoveTopCv(
		by_reference ? reference.child : parameter);
	if (parameter_object != program_->entities[entity].type) return;
	if (function.constructor && !by_reference) return;

	SpecialMemberKind kind = SPECIAL_MEMBER_NONE;
	if (function.constructor)
		kind = reference.kind == TYPE_LVALUE_REFERENCE ?
			SPECIAL_MEMBER_COPY_CONSTRUCTOR :
			SPECIAL_MEMBER_MOVE_CONSTRUCTOR;
	else if (declaration.operator_kind == OPERATOR_ASSIGN)
		kind = !by_reference || reference.kind == TYPE_LVALUE_REFERENCE ?
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
	{
		const TypeRecord& prior_type =
			program_->types.Get(GetFunction(*slot).type);
		if (GetFunction(*slot).type == function.type)
			throw std::runtime_error("duplicate class special member");
		if (prior_type.cv != CV_NONE && function_type.cv == CV_NONE)
			*slot = binding;
	}
	else *slot = binding;
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
	NodeId initializer, bool defaulted_inline)
{
	binding = program_->bindings[binding].canonical;
	RegisterClassSpecialMember(binding);
	FunctionInfo& function = GetMutableFunction(binding);
	const NodeId special = initializer == kNoNode ? kNoNode :
		FindChild(initializer, "special-initializer");
	if (!IsAssignmentSpecialMember(function.special_member))
	{
		if (special == kNoNode) return;
		const std::string spelling = arena_->Payload(special);
		if (spelling == "default")
			throw std::runtime_error(
				"only a special member function may be defaulted");
		if (spelling == "delete")
		{
			function.deleted_function = true;
			function.defined = true;
			return;
		}
		throw std::runtime_error("invalid assignment function initializer");
	}
	if (special == kNoNode) return;
	const std::string spelling = arena_->Payload(special);
	const bool defaulted = spelling == "default";
	const bool deleted = spelling == "delete";
	if (!defaulted && !deleted)
		throw std::runtime_error("invalid assignment special initializer");
	function.defaulted_special_member =
		function.defaulted_special_member || defaulted;
	function.user_provided_special_member =
		function.user_provided_special_member ||
		(defaulted && !defaulted_inline);
	function.deleted_special_member =
		function.deleted_special_member || deleted;
	function.defined = true;
	function.deferred = !function.deleted_special_member;
	BindingRecord& declaration = program_->bindings[binding];
	declaration.inline_function = declaration.inline_function ||
		(defaulted && defaulted_inline);
	const EntityId entity = declaration.member_owner;
	if (defaulted && entity != kNoEntity &&
		program_->entities[entity].layout_complete)
	{
		if (!defaulted_inline)
		{
			const TypeRecord& function_type =
				program_->types.Get(function.type);
			const TypeRecord& result_type =
				program_->types.Get(function_type.child);
			if (function.parameters.size() != 1 ||
				function.parameters[0].default_argument != kNoNode ||
				result_type.kind != TYPE_LVALUE_REFERENCE ||
				result_type.child != program_->entities[entity].type)
				throw std::runtime_error(
					"explicitly defaulted assignment has the wrong type");
		}
		bool implicitly_deleted = false;
		bool trivial = false;
		bool nonthrowing = false;
		EvaluateSynthesizedAssignment(entity, function.special_member,
			&implicitly_deleted, &trivial, &nonthrowing);
		function.deleted_special_member = implicitly_deleted;
		function.trivial_special_member = trivial &&
			!function.user_provided_special_member;
		function.synthesized_storage_copy = trivial;
		if (!function.user_provided_special_member)
			declaration.nonthrowing = nonthrowing;
		function.deferred = !implicitly_deleted;
		if (!defaulted_inline && implicitly_deleted)
			throw std::runtime_error(
				"out-of-class defaulted assignment is deleted");
	}
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
		FunctionCandidates(scope, spelling, &naming_class, specifier);
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

BindingId SemanticAnalyzer::ConstructorForSubobject(TypeId type,
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
	if (kind == SPECIAL_MEMBER_MOVE_CONSTRUCTOR &&
		facts.move_constructor != kNoBinding)
		return facts.move_constructor;
	return facts.copy_constructor;
}

void SemanticAnalyzer::EvaluateSynthesizedConstructor(EntityId entity,
	SpecialMemberKind kind, bool* deleted, bool* trivial,
	bool* nonthrowing) const
{
	if (kind != SPECIAL_MEMBER_COPY_CONSTRUCTOR &&
		kind != SPECIAL_MEMBER_MOVE_CONSTRUCTOR)
		throw std::logic_error(
			"constructor status requested for another member");
	*deleted = false;
	*trivial = !program_->entities[entity].polymorphic_class &&
		program_->entities[entity].virtual_base_count == 0;
	*nonthrowing = true;
	const EntityRecord& owner = program_->entities[entity];
	const bool union_object = owner.flavor == NAMED_UNION;
	const auto visit = [this, entity, kind, deleted, trivial, nonthrowing,
		union_object](
		TypeId type)
	{
		++special_member_subobject_visits_;
		bool is_const = false;
		bool is_reference = false;
		const EntityId subobject = SubobjectClass(
			*program_, type, &is_const, &is_reference);
		(void)is_const;
		(void)is_reference;
		if (subobject == kNoEntity) return;
		const BindingId selected = ConstructorForSubobject(type, kind);
		if (selected == kNoBinding)
		{
			*deleted = true;
			return;
		}
		const FunctionInfo& function = GetFunction(selected);
		if (function.deleted_constructor ||
			function.deleted_special_member ||
			!CanAccessMember(selected, subobject, entity))
			*deleted = true;
		if (!function.trivial_special_member)
		{
			*trivial = false;
			if (union_object) *deleted = true;
		}
		if (!program_->bindings[selected].nonthrowing) *nonthrowing = false;
	};
	for (std::size_t i = 0; i < owner.direct_base_count; ++i)
		visit(program_->entities[
			program_->DirectBase(entity, i).entity].type);
	if (entity < entity_data_members_.size())
		for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
			visit(program_->bindings[entity_data_members_[entity][i]].type);
}

void SemanticAnalyzer::EvaluateSynthesizedAssignment(EntityId entity,
	SpecialMemberKind kind, bool* deleted, bool* trivial,
	bool* nonthrowing) const
{
	if (!IsAssignmentSpecialMember(kind))
		throw std::logic_error("assignment status requested for another member");
	*deleted = false;
	*trivial = !program_->entities[entity].polymorphic_class &&
		program_->entities[entity].virtual_base_count == 0;
	*nonthrowing = true;
	const EntityRecord& owner = program_->entities[entity];
	const bool union_object = owner.flavor == NAMED_UNION;
	const auto visit = [this, entity, kind, deleted, trivial, nonthrowing,
		union_object](
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
		if (!function.trivial_special_member)
		{
			*trivial = false;
			if (union_object) *deleted = true;
		}
		if (!program_->bindings[selected].nonthrowing) *nonthrowing = false;
	};
	for (std::size_t i = 0; i < owner.direct_base_count; ++i)
		visit(program_->entities[
			program_->DirectBase(entity, i).entity].type);
	if (entity < entity_data_members_.size())
		for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
			visit(program_->bindings[entity_data_members_[entity][i]].type);
}

void SemanticAnalyzer::ConfigureSynthesizedStoragePrefix(EntityId entity,
	FunctionInfo* function) const
{
	function->synthesized_prefix_size = 0;
	function->synthesized_prefix_alignment = 0;
	function->synthesized_prefix_members = 0;
	function->synthesized_memberwise_copy = false;
	if (function->deleted_special_member) return;
	const bool assignment = IsAssignmentSpecialMember(
		function->special_member);
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t i = 0; i < owner.direct_base_count; ++i)
	{
		const TypeId base_type = program_->entities[
			program_->DirectBase(entity, i).entity].type;
		const BindingId selected = assignment ?
			AssignmentForSubobject(base_type, function->special_member) :
			ConstructorForSubobject(base_type, function->special_member);
		if (selected == kNoBinding ||
			!GetFunction(selected).trivial_special_member)
			return;
	}
	if (entity >= entity_data_members_.size()) return;
	const std::vector<BindingId>& members = entity_data_members_[entity];
	for (std::size_t i = 0; i < members.size(); ++i)
	{
		const BindingRecord& member = program_->bindings[members[i]];
		bool is_const = false;
		bool is_reference = false;
		const EntityId subobject = SubobjectClass(
			*program_, member.type, &is_const, &is_reference);
		(void)is_const;
		const bool empty_subobject = subobject != kNoEntity &&
			program_->entities[subobject].empty_class;
		if (is_reference || empty_subobject)
		{
			function->synthesized_memberwise_copy = true;
			if (member.member_offset == 0) return;
			function->synthesized_prefix_size = member.member_offset;
			function->synthesized_prefix_alignment =
				static_cast<std::uint32_t>(owner.object_alignment);
			function->synthesized_prefix_members =
				static_cast<std::uint32_t>(i);
			return;
		}
		const BindingId selected = assignment ?
			AssignmentForSubobject(member.type, function->special_member) :
			ConstructorForSubobject(member.type, function->special_member);
		if (selected == kNoBinding ||
			GetFunction(selected).trivial_special_member)
			continue;
		if (member.member_offset == 0) return;
		function->synthesized_prefix_size = member.member_offset;
		function->synthesized_prefix_alignment =
			static_cast<std::uint32_t>(owner.object_alignment);
		function->synthesized_prefix_members =
			static_cast<std::uint32_t>(i);
		return;
	}
}

BindingId SemanticAnalyzer::DeclareImplicitCopyMoveConstructor(
	EntityId entity, SpecialMemberKind kind)
{
	if (kind != SPECIAL_MEMBER_COPY_CONSTRUCTOR &&
		kind != SPECIAL_MEMBER_MOVE_CONSTRUCTOR)
		throw std::logic_error("cannot declare non-constructor helper");
	bool deleted = false;
	bool trivial = false;
	bool nonthrowing = false;
	EvaluateSynthesizedConstructor(
		entity, kind, &deleted, &trivial, &nonthrowing);
	const ClassSpecialMemberFacts& existing = class_special_members_[entity];
	if (kind == SPECIAL_MEMBER_COPY_CONSTRUCTOR &&
		(existing.user_move_constructor || existing.user_move_assignment))
		deleted = true;

	const EntityRecord& owner = program_->entities[entity];
	TypeId source = owner.type;
	const TypeKind reference_kind = kind == SPECIAL_MEMBER_COPY_CONSTRUCTOR ?
		TYPE_LVALUE_REFERENCE : TYPE_RVALUE_REFERENCE;
	if (kind == SPECIAL_MEMBER_COPY_CONSTRUCTOR)
		source = program_->types.Qualify(source, CV_CONST);
	const TypeId parameter_type =
		program_->types.Reference(reference_kind, source);
	std::vector<TypeId> parameter_types(1, parameter_type);
	const TypeId function_type = program_->types.Function(
		program_->types.Fundamental(FUND_VOID), parameter_types, false);
	const NameId parameter_name = program_->names.Intern("other");
	std::vector<ParameterInfo> parameters;
	parameters.push_back(ParameterInfo(
		parameter_name, parameter_type, parameter_type));
	const BindingId constructor = DeclareFunction(owner.member_scope,
		owner.identity_name, function_type, parameters, true, false,
		STORAGE_CLASS_NONE, LANGUAGE_LINKAGE_CPP, nonthrowing, false);
	BindingRecord& declaration = program_->bindings[constructor];
	declaration.member_owner = entity;
	declaration.access = ACCESS_PUBLIC;
	declaration.constructor = true;
	declaration.inline_function = true;
	FunctionInfo& function = GetMutableFunction(constructor);
	function.member_owner = owner.type;
	function.constructor = true;
	function.defaulted_constructor = true;
	function.deleted_constructor = deleted;
	function.special_member = kind;
	function.implicit_special_member = true;
	function.defaulted_special_member = true;
	function.deleted_special_member = deleted;
	function.trivial_special_member = trivial;
	function.deferred = !deleted;
	if (entity_constructors_.size() <= entity)
		entity_constructors_.resize(static_cast<std::size_t>(entity) + 1);
	entity_constructors_[entity].push_back(constructor);
	ClassSpecialMemberFacts& facts = class_special_members_[entity];
	if (kind == SPECIAL_MEMBER_COPY_CONSTRUCTOR)
		facts.copy_constructor = constructor;
	else facts.move_constructor = constructor;
	return constructor;
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
	if (facts.copy_constructor == kNoBinding)
		DeclareImplicitCopyMoveConstructor(
			entity, SPECIAL_MEMBER_COPY_CONSTRUCTOR);
	else
	{
		FunctionInfo& function = GetMutableFunction(facts.copy_constructor);
		if (function.defaulted_special_member &&
			!function.deleted_special_member)
		{
			bool deleted = false;
			bool trivial = false;
			bool nonthrowing = false;
			EvaluateSynthesizedConstructor(entity,
				SPECIAL_MEMBER_COPY_CONSTRUCTOR,
				&deleted, &trivial, &nonthrowing);
			function.deleted_constructor = deleted;
			function.deleted_special_member = deleted;
			function.trivial_special_member = trivial;
			program_->bindings[facts.copy_constructor].nonthrowing = nonthrowing;
			function.deferred = !deleted;
		}
	}

	const bool implicit_move_constructor =
		facts.move_constructor == kNoBinding &&
		!facts.user_copy_constructor && !facts.user_move_constructor &&
		!facts.user_copy_assignment && !facts.user_move_assignment &&
		!program_->entities[entity].has_user_declared_destructor;
	if (implicit_move_constructor)
		DeclareImplicitCopyMoveConstructor(
			entity, SPECIAL_MEMBER_MOVE_CONSTRUCTOR);
	else if (facts.move_constructor != kNoBinding)
	{
		FunctionInfo& function = GetMutableFunction(facts.move_constructor);
		if (function.defaulted_special_member &&
			!function.deleted_special_member)
		{
			bool deleted = false;
			bool trivial = false;
			bool nonthrowing = false;
			EvaluateSynthesizedConstructor(entity,
				SPECIAL_MEMBER_MOVE_CONSTRUCTOR,
				&deleted, &trivial, &nonthrowing);
			function.deleted_constructor = deleted;
			function.deleted_special_member = deleted;
			function.trivial_special_member = trivial;
			program_->bindings[facts.move_constructor].nonthrowing = nonthrowing;
			function.deferred = !deleted;
		}
	}

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
			function.trivial_special_member = trivial &&
				!function.user_provided_special_member;
			function.synthesized_storage_copy = trivial;
			if (!function.user_provided_special_member)
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
			function.trivial_special_member = trivial &&
				!function.user_provided_special_member;
			function.synthesized_storage_copy = trivial;
			if (!function.user_provided_special_member)
				program_->bindings[facts.move_assignment].nonthrowing = nonthrowing;
			function.deferred = !deleted;
		}
	}
	const FunctionInfo& copy = GetFunction(facts.copy_constructor);
	const bool nontrivial_copy = copy.deleted_constructor ||
		copy.deleted_special_member || !copy.trivial_special_member;
	const bool nontrivial_move = facts.move_constructor == kNoBinding ||
		!GetFunction(facts.move_constructor).trivial_special_member;
	// PA17 may use a direct boundary when either available transfer is trivial.
	EntityRecord& class_record = program_->entities[entity];
	class_record.indirect_class_value_abi =
		(nontrivial_copy && nontrivial_move) ||
		!class_record.trivial_destructor;
	if (!class_record.layout_complete)
		throw std::logic_error("class boundary ABI requires completed layout");
	const std::size_t object_size =
		static_cast<std::size_t>(class_record.object_size);
	const bool dependent_empty_value = class_record.empty_class &&
		class_record.template_argument_count != 0 &&
		((class_record.enclosing_class == kNoEntity &&
		  class_record.default_constructible) ||
		 !class_record.indirect_class_value_abi);
	class_record.indirect_class_result_abi = !dependent_empty_value &&
		(object_size > 16 ||
		 ((object_size < 16 || (object_size == 16 &&
		   class_record.template_argument_count != 0)) &&
		  class_record.indirect_class_value_abi));
	// Publish parameter passing beside result passing and transfer/lifecycle
	// facts so lowering consumes class ABI decisions by entity identity.
	const bool direct_derived_payload = class_record.has_direct_base &&
		class_record.template_argument_count == 0 &&
		class_record.trivial_destructor && object_size <= 16;
	class_record.indirect_class_parameter_abi =
		class_record.indirect_class_value_abi && !direct_derived_payload;
	ConfigureSynthesizedStoragePrefix(
		entity, &GetMutableFunction(facts.copy_constructor));
	if (facts.move_constructor != kNoBinding)
		ConfigureSynthesizedStoragePrefix(
			entity, &GetMutableFunction(facts.move_constructor));
	ConfigureSynthesizedStoragePrefix(
		entity, &GetMutableFunction(facts.copy_assignment));
	if (facts.move_assignment != kNoBinding)
		ConfigureSynthesizedStoragePrefix(
			entity, &GetMutableFunction(facts.move_assignment));
}

void SemanticAnalyzer::AddSynthesizedConstructorBody(
	const FunctionInfo& function, const std::vector<BindingId>& parameters,
	std::uint32_t body)
{
	if ((function.special_member != SPECIAL_MEMBER_COPY_CONSTRUCTOR &&
		 function.special_member != SPECIAL_MEMBER_MOVE_CONSTRUCTOR) ||
		parameters.size() != 1 || function.deleted_special_member)
		throw std::logic_error("invalid synthesized constructor body");
	const EntityId entity = program_->bindings[function.binding].member_owner;
	const EntityRecord& owner = program_->entities[entity];
	const std::uint32_t construction = MakeDump(
		DUMP_SPECIAL_MEMBER_CONSTRUCTION_ACTION, owner.type);
	dump_.nodes[construction].object_binding = parameters[0];
	if (function.synthesized_prefix_size != 0)
	{
		const std::uint32_t prefix = MakeDump(
			DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, owner.type);
		dump_.nodes[prefix].storage_size =
			function.synthesized_prefix_size;
		dump_.nodes[prefix].storage_alignment =
			function.synthesized_prefix_alignment;
		dump_.Add(construction, prefix);
	}

	if ((function.trivial_special_member ||
		function.synthesized_storage_copy) &&
		!function.synthesized_memberwise_copy && !owner.empty_class)
	{
		const std::uint32_t step = MakeDump(
			DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, owner.type);
		dump_.Add(construction, step);
	}
	else
	{
		if (function.synthesized_prefix_size == 0)
		{
			for (std::size_t base_ordinal = 0;
				base_ordinal < owner.direct_base_count; ++base_ordinal)
			{
			++special_member_subobject_visits_;
				const DirectBaseEdge& edge = program_->DirectBase(
					entity, base_ordinal);
				if (program_->bindings[function.binding].constructor_base_entry &&
					edge.virtual_base)
				{
					const BindingId selected = ConstructorForSubobject(
						program_->entities[edge.entity].type,
						function.special_member);
					if (selected != kNoBinding &&
						!GetFunction(selected).trivial_special_member)
						DemandFunction(EnsureConstructorBaseEntry(selected));
					continue;
				}
				const EntityRecord& base =
					program_->entities[edge.entity];
			BindingId selected = ConstructorForSubobject(
				base.type, function.special_member);
			if (selected == kNoBinding)
				throw std::logic_error(
					"synthesized base constructor is missing");
			const FunctionInfo& selected_function = GetFunction(selected);
			if (!base.empty_class || !selected_function.trivial_special_member)
			{
				const std::uint32_t step = MakeDump(
					DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, base.type);
				dump_.nodes[step].base_projection_count = 1;
				dump_.nodes[step].base_projection_offset = edge.offset;
				dump_.nodes[step].has_base_projection_offset = true;
				if (!selected_function.trivial_special_member)
				{
					selected = EnsureConstructorBaseEntry(selected);
					dump_.nodes[step].selected_binding = selected;
					if (!GetFunction(selected).defined)
						GetMutableFunction(selected).deferred = true;
					DemandFunction(selected);
				}
				dump_.Add(construction, step);
			}
			}
		}
		if (entity < entity_data_members_.size())
			for (std::size_t i = function.synthesized_prefix_members;
				i < entity_data_members_[entity].size(); ++i)
			{
				++special_member_subobject_visits_;
				const BindingId member = entity_data_members_[entity][i];
				const TypeId type = program_->bindings[member].type;
				BindingId selected = ConstructorForSubobject(
					type, function.special_member);
				bool is_const = false;
				bool is_reference = false;
				const EntityId member_entity = SubobjectClass(
					*program_, type, &is_const, &is_reference);
				(void)is_const;
				(void)is_reference;
				if (member_entity != kNoEntity && selected == kNoBinding)
					throw std::logic_error(
						"synthesized member constructor is missing");
				if (member_entity != kNoEntity &&
					program_->entities[member_entity].empty_class &&
					GetFunction(selected).trivial_special_member)
					continue;
				const std::uint32_t step = MakeDump(
					DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, type,
					VALUE_NONE, 0, member);
				if (selected != kNoBinding &&
					!GetFunction(selected).trivial_special_member)
				{
					dump_.nodes[step].selected_binding = selected;
					if (!GetFunction(selected).defined)
						GetMutableFunction(selected).deferred = true;
					DemandFunction(selected);
				}
				dump_.Add(construction, step);
			}
	}
	const std::uint32_t statement = MakeDump(DUMP_EXPRESSION_STATEMENT);
	dump_.Add(statement, construction);
	dump_.Add(body, statement);
	if (owner.polymorphic_class)
		dump_.Add(body, MakeDump(DUMP_VPTR_INITIALIZATION_ACTION, owner.type));
	++expression_count_;
}

void SemanticAnalyzer::DemandSynthesizedConstructorDependencies(
	BindingId constructor)
{
	const FunctionInfo& function = GetFunction(constructor);
	if (!function.defaulted_special_member && !function.implicit_constructor)
		return;
	if (function.special_member != SPECIAL_MEMBER_COPY_CONSTRUCTOR &&
		function.special_member != SPECIAL_MEMBER_MOVE_CONSTRUCTOR)
		return;
	const EntityId entity = program_->bindings[constructor].member_owner;
	const EntityRecord& owner = program_->entities[entity];
	for (std::size_t base_ordinal = 0;
		base_ordinal < owner.direct_base_count; ++base_ordinal)
	{
		BindingId selected = ConstructorForSubobject(
			program_->entities[program_->DirectBase(
				entity, base_ordinal).entity].type,
			function.special_member);
		if (selected != kNoBinding &&
			!GetFunction(selected).trivial_special_member)
			DemandFunction(EnsureConstructorBaseEntry(selected));
	}
	if (entity >= entity_data_members_.size()) return;
	for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
	{
		const BindingId selected = ConstructorForSubobject(
			program_->bindings[entity_data_members_[entity][i]].type,
			function.special_member);
		if (selected != kNoBinding &&
			!GetFunction(selected).trivial_special_member)
			DemandFunction(selected);
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
	if (function.synthesized_prefix_size != 0)
	{
		const std::uint32_t prefix = MakeDump(
			DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, owner.type);
		dump_.nodes[prefix].storage_size =
			function.synthesized_prefix_size;
		dump_.nodes[prefix].storage_alignment =
			function.synthesized_prefix_alignment;
		dump_.Add(assignment, prefix);
	}
	bool has_bit_fields = false;
	if (entity < entity_data_members_.size())
		for (std::size_t i = 0; i < entity_data_members_[entity].size(); ++i)
			if (program_->bindings[
				entity_data_members_[entity][i]].bit_field)
			{
				has_bit_fields = true;
				break;
			}

	if ((function.trivial_special_member ||
		 function.synthesized_storage_copy) &&
		owner.direct_base == kNoEntity && !owner.empty_class &&
		!function.synthesized_memberwise_copy && !has_bit_fields)
	{
		const std::uint32_t step = MakeDump(
			DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, owner.type);
		dump_.Add(assignment, step);
	}
	else
	{
		if (function.synthesized_prefix_size == 0)
		{
			for (std::size_t base_ordinal = 0;
				base_ordinal < owner.direct_base_count; ++base_ordinal)
			{
			++special_member_subobject_visits_;
			const DirectBaseEdge& edge = program_->DirectBase(
				entity, base_ordinal);
			const EntityRecord& base =
				program_->entities[edge.entity];
			const BindingId selected = AssignmentForSubobject(
				base.type, function.special_member);
			if (selected == kNoBinding)
				throw std::logic_error("synthesized base assignment is missing");
			if (!base.empty_class || !GetFunction(selected).trivial_special_member)
			{
				const std::uint32_t step = MakeDump(
					DUMP_SPECIAL_MEMBER_SUBOBJECT_ACTION, base.type);
				dump_.nodes[step].base_projection_count = 1;
				dump_.nodes[step].base_projection_offset = edge.offset;
				dump_.nodes[step].has_base_projection_offset = true;
				dump_.nodes[step].selected_binding = selected;
				dump_.Add(assignment, step);
				if (!GetFunction(selected).defined)
					GetMutableFunction(selected).deferred = true;
				DemandFunction(selected);
			}
			}
		}
		if (entity < entity_data_members_.size())
			for (std::size_t i = function.synthesized_prefix_members;
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
				if (program_->bindings[member].bit_field)
				{
					if (i != function.synthesized_prefix_members)
					{
						const BindingRecord& previous = program_->bindings[
							entity_data_members_[entity][i - 1]];
						const BindingRecord& current =
							program_->bindings[member];
						if (previous.bit_field &&
							previous.member_offset == current.member_offset &&
							previous.bit_storage_bits == current.bit_storage_bits)
							continue;
					}
					dump_.nodes[step].storage_unit_transfer = true;
				}
				dump_.nodes[step].selected_binding = selected;
				dump_.Add(assignment, step);
				if (selected != kNoBinding &&
					!GetFunction(selected).trivial_special_member)
				{
					if (!GetFunction(selected).defined)
						GetMutableFunction(selected).deferred = true;
					DemandFunction(selected);
				}
			}
	}
	const std::uint32_t statement = MakeDump(DUMP_RETURN_STATEMENT);
	dump_.Add(statement, assignment);
	dump_.Add(body, statement);
	++expression_count_;
}

void SemanticAnalyzer::AnalyzeOutOfClassSpecialMember(NodeId node,
	ScopeId scope, ScopeId declaration_scope, bool defer_demand)
{
	const NodeId declarator = FindChild(node, "declarator");
	if (declarator == kNoNode)
		throw std::runtime_error(
			"out-of-class special member is missing its declarator");
	const NodeId member_specifiers = FindChild(node, "member-specifiers");
	if (FindChild(declarator, "virt-specifier") != kNoNode)
		throw std::runtime_error(
			"virt-specifier is only allowed in a class definition");
	if (member_specifiers != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(member_specifiers);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			if (PayloadSource(arena_->EdgeChild(edge)) == "virtual")
				throw std::runtime_error(
					"virtual specifier is only allowed in a class definition");
	const NamePath path = DeclaratorNamePath(declarator);
	if (!path.global && path.Size() <= 1)
		throw std::runtime_error(
			"unqualified special member definition outside a class");
	ScopeId structured_owner = kNoScope;
	const NodeId structure = DeclaratorNameStructure(declarator);
	if (declaration_scope == kNoScope && structure != kNoNode)
		(void)LookupStructuredName(structure, scope,
			LOOKUP_ORDINARY, &structured_owner);
	const ScopeId owner = declaration_scope == kNoScope ?
		(structured_owner != kNoScope ? structured_owner :
			ResolveOwner(scope, path)) :
		program_->ParentScope(declaration_scope);
	const EntityId entity = owner == kNoScope ? kNoEntity :
		program_->EntityForScope(owner);
	if (entity == kNoEntity)
		throw std::runtime_error("special member owner is not a class");
	const std::string terminal = program_->names.Get(path.Last());
	const std::string class_name = program_->names.Get(
		program_->entities[entity].identity_name);
	const NodeId conversion_type = FindChild(declarator, "conversion-type-id");
	const bool conversion_definition = conversion_type != kNoNode;
	const bool constructor_definition = terminal == class_name;
	const bool destructor_definition = terminal == "~" + class_name;
	if (!constructor_definition && !destructor_definition &&
		!conversion_definition)
		throw std::runtime_error(
			"qualified special member definition has an invalid name");

	const EntityId previous_class = current_class_context_;
	current_class_context_ = entity;
	const ScopeId semantic_scope = declaration_scope == kNoScope ?
		owner : declaration_scope;
	const TypeId conversion_target = conversion_definition ?
		BuildTypeId(conversion_type, semantic_scope) :
		program_->types.Fundamental(FUND_VOID);
	const DeclaratorInfo parsed = BuildDeclarator(declarator,
		conversion_target, semantic_scope, false, true);
	BindingId special = kNoBinding;
	if (conversion_definition && entity < entity_conversion_functions_.size())
		for (std::size_t i = 0;
			i < entity_conversion_functions_[entity].size(); ++i)
		{
			const BindingId candidate =
				entity_conversion_functions_[entity][i];
			const FunctionInfo& candidate_info = GetFunction(candidate);
			if (candidate_info.conversion_target == conversion_target &&
				candidate_info.type == parsed.type)
			{
				special = candidate;
				break;
			}
		}
	if (conversion_definition)
	{
		if (special == kNoBinding)
			throw std::runtime_error(
				"qualified conversion definition has no declaration");
		if (GetFunction(special).defined)
			throw std::runtime_error("duplicate function definition");
		GetMutableFunction(special).defined = true;
	}
	else special = DeclareFunction(owner, path.Last(),
		parsed.type, parsed.parameters, true, false, STORAGE_CLASS_NONE,
		current_language_linkage_,
		IsNonthrowing(declarator, parsed.parameter_scope));
	ConfigureFunctionExceptionSpecification(
		special, declarator, parsed.parameter_scope);
	FunctionInfo& info = GetMutableFunction(special);
	if (parsed.parameters.size() != info.parameters.size())
		throw std::logic_error(
			"special member definition parameter fact mismatch");
	for (std::size_t i = 0; i < parsed.parameters.size(); ++i)
		if (parsed.parameters[i].name != 0)
			info.parameters[i].name = parsed.parameters[i].name;
	info.lexical_scope = semantic_scope;
	if (defer_demand)
		program_->bindings[special].inline_function = true;
	bool inline_specifier = false;
	if (member_specifiers != kNoNode)
		for (std::uint32_t edge = arena_->FirstEdge(member_specifiers);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			if (PayloadSource(arena_->EdgeChild(edge)) == "inline")
				inline_specifier = true;
	PublishInlineFunctionFacts(special, inline_specifier || defer_demand);
	if (info.member_owner != program_->entities[entity].type)
		throw std::runtime_error(
			"qualified special member definition has no member declaration");
	if ((constructor_definition && !info.constructor) ||
		(destructor_definition && !info.destructor) ||
		(conversion_definition && !info.conversion_function))
		throw std::runtime_error(
			"qualified special member definition has no matching kind");
	if (conversion_definition && info.conversion_target != conversion_target)
		throw std::runtime_error(
			"qualified conversion definition has a mismatched target");
	ValidateFunctionRefQualifier(special);
	const NodeId initializer = FindChild(node, "initializer");
	const NodeId special_initializer = initializer == kNoNode ? kNoNode :
		FindChild(initializer, "special-initializer");
	const bool defaulted = special_initializer != kNoNode &&
		arena_->Payload(special_initializer) == "default";
	const bool deleted = special_initializer != kNoNode &&
		arena_->Payload(special_initializer) == "delete";
	info.definition_body = FunctionDefinitionPart(node, "compound-statement");
	info.function_try_block = FindChild(node, "function-try-block");
	if (conversion_definition)
	{
		if (defaulted)
			throw std::runtime_error("conversion function cannot be defaulted");
		BindingRecord& binding = program_->bindings[special];
		binding.conversion_function = true;
		binding.conversion_target = conversion_target;
		info.deleted_special_member =
			info.deleted_special_member || deleted;
		info.deferred = !info.deleted_special_member;
		if (!defer_demand)
		{
			if (host_object_emission_ && inline_specifier)
				QueueFunctionDefinitionValidation(special);
			else DemandFunction(special);
		}
		current_class_context_ = previous_class;
		return;
	}
	if (constructor_definition)
	{
		if (info.complete_constructor == kNoBinding)
			info.complete_constructor = info.binding;
		info.constructor_initializer =
			FunctionDefinitionPart(node, "ctor-initializer");
		info.defaulted_constructor = info.defaulted_constructor || defaulted;
		info.deleted_constructor = info.deleted_constructor || deleted;
		info.defaulted_special_member =
			info.defaulted_special_member || defaulted;
		info.deleted_special_member =
			info.deleted_special_member || deleted;
		if (defaulted)
			CompleteOutOfClassDefaultedConstructor(entity, special);
	}
	else
	{
		info.defaulted_destructor = info.defaulted_destructor || defaulted;
		info.deleted_destructor = info.deleted_destructor || deleted;
		if (defaulted)
		{
			CompleteDefaultedDestructor(entity, special);
			if (info.deleted_destructor)
				throw std::runtime_error(
					"out-of-class defaulted destructor is deleted");
		}
	}
	info.deferred = !(constructor_definition ? info.deleted_constructor :
		info.deleted_destructor);
	if (constructor_definition)
	{
		ValidateConstexprConstructorDefinition(info);
		program_->entities[entity].has_user_provided_constructor = true;
		bool shared_base_entry = program_->entities[entity].direct_base == kNoEntity;
		if (!shared_base_entry && (defer_demand || inline_specifier))
			shared_base_entry = ConstructorSubobjectsAreEmpty(special);
		if ((defer_demand || inline_specifier) && shared_base_entry)
		{
			if (constructor_base_entry_by_binding_.size() <= special)
				constructor_base_entry_by_binding_.resize(
					static_cast<std::size_t>(special) + 1, kNoBinding);
			constructor_base_entry_by_binding_[special] = special;
			program_->bindings[special].lifecycle_base_entry = special;
		}
		else (void)EnsureConstructorBaseEntry(special);
	}
	else
	{
		program_->entities[entity].has_user_declared_destructor = true;
		program_->entities[entity].trivial_destructor = false;
		(void)EnsureDestructorBaseEntry(special);
	}
	if (!defer_demand)
	{
		if (host_object_emission_ && inline_specifier)
			QueueFunctionDefinitionValidation(special);
		else DemandFunction(special);
	}
	current_class_context_ = previous_class;
}

}
}
