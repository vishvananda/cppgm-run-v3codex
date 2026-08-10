#include "pa12_semantic_detail.h"

#include <limits>
#include <stdexcept>

namespace cppgm
{
namespace pa12_semantic_detail
{

bool SemanticAnalyzer::IsNonthrowing(NodeId declarator, ScopeId scope)
{
	const NodeId qualifier = FindChild(declarator, "function-qualifier");
	if (qualifier == kNoNode) return false;
	const std::string spelling = PayloadSource(qualifier);
	if (spelling == "noexcept" || spelling == "throw()") return true;
	if (spelling.compare(0, 8, "noexcept") != 0) return false;
	const NodeId expression_node = FirstSemanticChild(qualifier);
	if (expression_node == kNoNode)
		throw std::logic_error("missing noexcept expression");
	++constant_expression_required_depth_;
	ExpressionInfo expression;
	try
	{
		expression = ApplyContextualBool(
			AnalyzeExpression(expression_node, scope));
	}
	catch (...)
	{
		--constant_expression_required_depth_;
		throw;
	}
	--constant_expression_required_depth_;
	if (!expression.constant || !IsIntegral(expression.type, true))
		throw std::runtime_error("nonconstant noexcept expression");
	return expression.value != 0;
}

bool SemanticAnalyzer::IsConstexprLiteralType(TypeId type) const
{
	const TypeRecord& top = program_->types.Get(type);
	if (top.kind == TYPE_QUALIFIED)
		return IsConstexprLiteralType(top.child);
	if (top.kind == TYPE_LVALUE_REFERENCE ||
		top.kind == TYPE_RVALUE_REFERENCE || top.kind == TYPE_POINTER ||
		top.kind == TYPE_MEMBER_POINTER)
		return true;
	if (top.kind == TYPE_ARRAY)
		return IsConstexprLiteralType(top.child);
	if (top.kind == TYPE_FUNDAMENTAL)
		return top.fundamental != FUND_VOID;
	if (top.kind != TYPE_NAMED || top.entity >= program_->entities.size())
		return false;
	const EntityRecord& entity = program_->entities[top.entity];
	if (entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS)
		return true;
	if (entity.flavor == NAMED_TYPENAME_PARAMETER ||
		!entity.complete || entity.deferred_template_completion)
		return true;
	if (!IsClassObjectType(type) || !entity.trivial_destructor) return false;
	bool literal_constructor = entity.is_aggregate;
	if (top.entity < entity_constructors_.size())
	{
		const std::vector<BindingId>& constructors =
			entity_constructors_[top.entity];
		for (std::size_t i = 0; i < constructors.size(); ++i)
		{
			const FunctionInfo& constructor = GetFunction(constructors[i]);
			if ((constructor.constexpr_function ||
				 constructor.defaulted_constructor) &&
				!constructor.deleted_constructor &&
				constructor.special_member != SPECIAL_MEMBER_COPY_CONSTRUCTOR &&
				constructor.special_member != SPECIAL_MEMBER_MOVE_CONSTRUCTOR)
			{
				literal_constructor = true;
				break;
			}
		}
	}
	if (!literal_constructor) return false;
	for (std::size_t i = 0; i < entity.direct_base_count; ++i)
		if (!IsConstexprLiteralType(program_->entities[
			program_->DirectBase(top.entity, i).entity].type)) return false;
	if (top.entity < entity_data_members_.size())
		for (std::size_t i = 0; i < entity_data_members_[top.entity].size(); ++i)
		{
			const BindingRecord& member = program_->bindings[
				entity_data_members_[top.entity][i]];
			if (IsVolatileSubobjectType(member.type) ||
				!IsConstexprLiteralType(member.type)) return false;
		}
	return true;
}

bool SemanticAnalyzer::IsVolatileSubobjectType(TypeId type) const
{
	const TypeRecord& record = program_->types.Get(type);
	if (record.kind == TYPE_QUALIFIED)
		return (record.cv & CV_VOLATILE) != 0 ||
			IsVolatileSubobjectType(record.child);
	if (record.kind == TYPE_ARRAY)
		return IsVolatileSubobjectType(record.child);
	return false;
}

bool SemanticAnalyzer::IsConstexprCallableType(TypeId type,
	bool constructor) const
{
	const TypeRecord& function = program_->types.Get(type);
	if (function.kind != TYPE_FUNCTION)
		throw std::logic_error("constexpr callable has non-function type");
	if (!constructor && !IsConstexprLiteralType(function.child)) return false;
	const TypeId* parameters = program_->types.Parameters(type);
	for (std::size_t i = 0; i < function.parameter_count; ++i)
		if (!IsConstexprLiteralType(parameters[i])) return false;
	return true;
}

TypeId SemanticAnalyzer::ApplyConstexprMemberFunctionType(TypeId type,
	EntityId owner, bool static_member)
{
	if (owner == kNoEntity || static_member) return type;
	const TypeRecord& function = program_->types.Get(type);
	if (function.kind != TYPE_FUNCTION)
		throw std::logic_error("constexpr member has non-function type");
	if ((function.cv & CV_CONST) != 0) return type;
	const TypeId* parameter_data = program_->types.Parameters(type);
	std::vector<TypeId> parameters;
	parameters.reserve(function.parameter_count);
	for (std::size_t i = 0; i < function.parameter_count; ++i)
		parameters.push_back(parameter_data[i]);
	return program_->types.Function(function.child, parameters,
		function.variadic, function.cv | CV_CONST, function.ref_qualifier);
}

TypeId SemanticAnalyzer::ApplyConstexprDeclaredFunctionType(TypeId type,
	ScopeId owner, NameId name, EntityId entity)
{
	bool static_member = false;
	if (entity != kNoEntity)
	{
		const LookupResult found =
			program_->LookupDirect(owner, name, LOOKUP_ORDINARY);
		for (std::size_t i = 0; i < found.OrdinaryCount(); ++i)
		{
			const BindingId binding = found.OrdinaryAt(i);
			if (program_->bindings[binding].kind == BIND_FUNCTION &&
				program_->bindings[binding].static_member_function &&
				GetFunction(binding).type == type)
			{
				static_member = true;
				break;
			}
		}
	}
	return ApplyConstexprMemberFunctionType(type, entity, static_member);
}

void SemanticAnalyzer::ValidateConstexprCallableType(TypeId type,
	bool constructor) const
{
	if (!IsConstexprCallableType(type, constructor))
		throw std::runtime_error(
			"constexpr callable uses a non-literal result or parameter type");
}

void SemanticAnalyzer::ValidateConstexprClassDeclarations(
	EntityId entity)
{
	if (entity == kNoEntity || entity >= program_->entities.size())
		throw std::logic_error("invalid constexpr class validation owner");
	const TypeId owner_type = program_->entities[entity].type;
	const bool template_specialization =
		IsClassTemplateSpecializationContext(entity);
	if (entity < entity_constructors_.size())
		for (std::size_t i = 0; i < entity_constructors_[entity].size(); ++i)
		{
			FunctionInfo& constructor =
				GetMutableFunction(entity_constructors_[entity][i]);
			if (constructor.constexpr_function)
			{
				if (template_specialization &&
					(!IsConstexprCallableType(constructor.type, true) ||
					 !IsConstexprLiteralType(owner_type)))
				{
					constructor.constexpr_function = false;
					continue;
				}
				ValidateConstexprCallableType(constructor.type, true);
				if (!IsConstexprLiteralType(owner_type))
					throw std::runtime_error(
						"constexpr constructor owner is not a literal type");
			}
		}
	if (entity < entity_member_functions_.size())
		for (std::size_t i = 0; i < entity_member_functions_[entity].size(); ++i)
		{
			const BindingId binding = entity_member_functions_[entity][i];
			FunctionInfo& function = GetMutableFunction(binding);
			if (!function.constexpr_function) continue;
			if (program_->bindings[binding].virtual_function)
				throw std::runtime_error(
					"constexpr function may not be virtual");
			if (template_specialization &&
				(!IsConstexprCallableType(function.type, false) ||
				 (!program_->bindings[binding].static_member_function &&
				  !IsConstexprLiteralType(owner_type))))
			{
				function.constexpr_function = false;
				continue;
			}
			ValidateConstexprCallableType(function.type, false);
			if (!program_->bindings[binding].static_member_function &&
				!IsConstexprLiteralType(owner_type))
				throw std::runtime_error(
					"constexpr member function owner is not a literal type");
		}
	if (entity < entity_conversion_functions_.size())
		for (std::size_t i = 0;
			i < entity_conversion_functions_[entity].size(); ++i)
		{
			const BindingId binding = entity_conversion_functions_[entity][i];
			FunctionInfo& function = GetMutableFunction(binding);
			if (!function.constexpr_function) continue;
			if (program_->bindings[binding].virtual_function)
				throw std::runtime_error(
					"constexpr conversion function may not be virtual");
			if (template_specialization &&
				(!IsConstexprCallableType(function.type, false) ||
				 !IsConstexprLiteralType(owner_type)))
			{
				function.constexpr_function = false;
				continue;
			}
			ValidateConstexprCallableType(function.type, false);
			if (!IsConstexprLiteralType(owner_type))
				throw std::runtime_error(
					"constexpr conversion function owner is not a literal type");
		}
}

void SemanticAnalyzer::AddLocalStaticObjectAction(std::uint32_t variable,
	BindingId object, TypeId type, std::uint32_t initializer,
	NameId source_file, std::uint32_t source_line,
	std::uint32_t source_column, bool constant_initialized)
{
	if (current_function_context_ == kNoBinding)
		throw std::logic_error("local static object has no function owner");
	const BindingId function =
		program_->bindings[current_function_context_].canonical;
	const BindingRecord& function_record = program_->bindings[function];
	const bool specialized_function =
		function_record.template_argument_count != 0 ||
		IsClassTemplateSpecializationContext(function_record.member_owner);
	if (local_static_count_by_function_.size() <= function)
		local_static_count_by_function_.resize(
			static_cast<std::size_t>(function) + 1, 0);
	std::uint32_t& declaration_ordinal =
		local_static_count_by_function_[function];
	if (declaration_ordinal == std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many local static declarations");
	const std::uint32_t ordinal = declaration_ordinal++;
	std::uint32_t destructor_action = kNoDumpEdge;
	const EntityId entity = DestructedEntity(type);
	if (entity != kNoEntity)
	{
		if (!program_->entities[entity].destructible)
			throw std::runtime_error("local static object type is not destructible");
		const BindingId destructor = DestructorForType(type);
		if (destructor == kNoBinding)
			throw std::logic_error("local static class has no destructor identity");
		if (!CanAccessMember(destructor, entity))
			throw std::runtime_error("inaccessible local static object destructor");
		if (!program_->entities[entity].trivial_destructor)
			destructor_action = MakeDestructorAction(type, destructor, object);
	}
	const bool specialized_addresses = initializer != kNoDumpEdge &&
		DemandRuntimeInitializerFunctions(initializer, true);
	const bool specialization_owned_recipe = constant_initialized &&
		specialized_function && specialized_addresses &&
		IsClassObjectType(type);
	local_static_objects_.push_back(LocalStaticObjectAction(object,
		function, type, variable, initializer, destructor_action, ordinal,
		source_file, source_line, source_column,
		constant_initialized, specialization_owned_recipe));
}

void SemanticAnalyzer::RegisterVariableLifetimeAndStorage(ScopeId scope,
	bool local, bool declaration_only, std::uint32_t variable,
	BindingId object, TypeId type, NameId source_file,
	std::uint32_t source_line, std::uint32_t source_column,
	bool constant_initialized)
{
	const StorageClass storage = program_->bindings[object].storage_class;
	if (local && storage == STORAGE_CLASS_NONE)
	{
		AddLifetimeObligation(scope, object, type);
		return;
	}
	if (local && storage == STORAGE_CLASS_STATIC)
	{
		const std::uint32_t edge = dump_.nodes[variable].first_edge;
		const std::uint32_t initializer = edge == kNoDumpEdge ?
			kNoDumpEdge : dump_.edges[edge].child;
		AddLocalStaticObjectAction(variable, object, type, initializer,
			source_file, source_line, source_column, constant_initialized);
		return;
	}
	if (!local && !declaration_only)
	{
		const std::uint32_t edge = dump_.nodes[variable].first_edge;
		const std::uint32_t initializer = edge == kNoDumpEdge ?
			kNoDumpEdge : dump_.edges[edge].child;
		AddNamespaceObjectAction(variable, object, type, initializer);
	}
}

bool SemanticAnalyzer::DemandRuntimeInitializerFunctions(
	std::uint32_t initializer, bool function_addresses_only)
{
	bool specialization_owned_address = false;
	std::vector<std::uint32_t> pending(1, initializer);
	while (!pending.empty())
	{
		const std::uint32_t current = pending.back();
		pending.pop_back();
		++runtime_initializer_visits_;
		const DumpKind kind = dump_.nodes[current].kind;
		const BindingId binding = dump_.nodes[current].binding;
		const BindingId selected_binding =
			dump_.nodes[current].selected_binding;
		const std::uint32_t first_edge = dump_.nodes[current].first_edge;
		if (!function_addresses_only &&
			(kind == DUMP_CALL_EXPRESSION ||
			kind == DUMP_CONSTRUCTOR_ACTION) &&
			binding != kNoBinding)
			DemandFunction(binding);
		else if (kind == DUMP_ID_EXPRESSION && binding != kNoBinding &&
			program_->bindings[binding].kind == BIND_FUNCTION)
		{
			const BindingRecord& addressed =
				program_->bindings[binding];
			specialization_owned_address = specialization_owned_address ||
				addressed.template_argument_count != 0 ||
				IsClassTemplateSpecializationContext(addressed.member_owner);
			DemandFunction(binding);
		}
		else if (!function_addresses_only &&
			kind == DUMP_CLASS_VALUE_TRANSFER &&
			selected_binding != kNoBinding)
			DemandFunction(selected_binding);
		for (std::uint32_t edge = first_edge;
			edge != kNoDumpEdge; edge = dump_.edges[edge].next)
			pending.push_back(dump_.edges[edge].child);
	}
	return specialization_owned_address;
}

}
}
