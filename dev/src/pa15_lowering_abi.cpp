#include "pa15_lowering_abi.h"

#include "abi_mangle.h"
#include "pa15_lowir_model.h"
#include "pa18_polymorphism_lowering.h"

#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa15_lowering_abi
{

namespace
{

bool IsTrivialLifecycleBinding(const pa11::Program& program,
	pa11::BindingId binding)
{
	using namespace pa11;
	if (binding == kNoBinding || binding >= program.bindings.size())
		return false;
	const BindingRecord& record = program.bindings[binding];
	if (record.member_owner == kNoEntity ||
		record.member_owner >= program.entities.size() ||
		record.type == kNoType || !program.types.IsFunction(record.type))
		return false;
	const TypeRecord& function = program.types.Get(record.type);
	if (function.parameter_count != 0) return false;
	return (record.constructor && !record.constructor_base_entry &&
			program.entities[record.member_owner].trivial_default_constructor) ||
		(record.destructor &&
		 program.entities[record.member_owner].trivial_destructor);
}

}

void ApplyLifecycleSymbolMetadata(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node,
	pa15_lowir_detail::TypedProgram* output,
	pa15_lowir_detail::SymbolId symbol)
{
	using namespace pa11;
	using namespace pa15_lowir_detail;
	const BindingRecord& binding = program.bindings[node.binding];
	const TypeRecord& function = program.types.Get(node.type);
	if (function.kind != TYPE_FUNCTION)
		throw std::logic_error("lifecycle ABI metadata has non-function type");
	const bool trivial_constructor = IsTrivialLifecycleBinding(
		program, node.binding) && binding.constructor &&
		!binding.constructor_base_entry && binding.member_owner != kNoEntity &&
		function.parameter_count == 1;
	const bool trivial_destructor = IsTrivialLifecycleBinding(
		program, node.binding) && binding.destructor;
	Symbol& record = output->symbols[symbol];
	record.trivial_lifecycle = trivial_constructor || trivial_destructor;
	const bool shared_inline_destructor = binding.destructor &&
		!binding.destructor_base_entry && binding.inline_function &&
		binding.member_owner != kNoEntity &&
		program.entities[binding.member_owner].virtual_base_count == 0 &&
		!binding.virtual_function;
	if (!trivial_constructor &&
		(!shared_inline_destructor || !output->host_object_emission)) return;
	const std::string alias = MangleFunction(program, node, true);
	if (!alias.empty() && alias != record.object_name)
		output->object_aliases.push_back(ObjectAlias(alias, symbol));
}

namespace
{

using namespace pa11;

std::string LambdaDiscriminator(std::uint32_t ordinal)
{
	return ordinal == 0 ? std::string() : std::to_string(ordinal - 1);
}

class AbiFactBuilder
{
	const pa11::Program& program_;
	abi_mangle::AbiFactCase& facts_;
	std::size_t next_argument_;

public:
	AbiFactBuilder(const pa11::Program& program,
		abi_mangle::AbiFactCase& facts)
		: program_(program), facts_(facts), next_argument_(0) {}

	std::string AddTypeArgument(pa11::TypeId type,
		const pa11::BindingRecord* function = 0,
		const pa11::FunctionTemplateAbiRecipe* recipe = 0)
	{
		using namespace abi_mangle;
		const std::string id = "__cppgm_abi_type_argument_" +
			std::to_string(next_argument_++);
		AbiFactRecord definition;
		definition.set_kind(ABI_FACT_RECORD_DEFINITION);
		definition.definition.id = id;
		definition.definition.set_kind(ABI_DEFINITION_TEMPLATE_ARGUMENT);
		definition.definition.template_argument.kind = ABI_TEMPLATE_ARGUMENT_TYPE;
		definition.definition.template_argument.type =
			MakeType(type, function, recipe);
		facts_.records.push_back(definition);
		return id;
	}

	std::string AddEntity(pa11::BindingId source)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (source == kNoBinding || source >= program_.bindings.size())
			throw std::logic_error("ABI template argument entity is invalid");
		source = program_.bindings[source].canonical;
		const BindingRecord& binding = program_.bindings[source];
		const std::string id = "__cppgm_abi_entity_argument_" +
			std::to_string(next_argument_++);
		AbiFactRecord definition;
		definition.set_kind(ABI_FACT_RECORD_DEFINITION);
		definition.definition.id = id;
		definition.definition.set_kind(ABI_DEFINITION_ENTITY);
		AbiEntityFact& entity = definition.definition.entity;
		if (binding.kind == BIND_FUNCTION)
		{
			entity.kind = ABI_ENTITY_FACT_FUNCTION;
			entity.function.kind = ABI_FUNCTION_TARGET_PATH;
			entity.function.qualified_name = program_.names.Get(
				binding.qualified_name != 0 ?
					binding.qualified_name : binding.name);
			const TypeRecord& function = program_.types.Get(binding.type);
			const TypeId* parameters =
				program_.types.Parameters(binding.type);
			for (std::size_t i = 0; i < function.parameter_count; ++i)
				entity.function.signature_parameter_types.push_back(
					MakeType(parameters[i]));
		}
		else
		{
			entity.kind = ABI_ENTITY_FACT_VARIABLE;
			entity.qualified_name = program_.names.Get(
				binding.qualified_name != 0 ?
					binding.qualified_name : binding.name);
			entity.internal_linkage =
				binding.storage_class == STORAGE_CLASS_STATIC &&
				binding.member_owner == kNoEntity &&
				!binding.unnamed_namespace_linkage;
		}
		facts_.records.push_back(definition);
		return id;
	}

	std::string AddTemplateArgument(std::size_t argument,
		const pa11::BindingRecord* function = 0,
		const pa11::FunctionTemplateAbiRecipe* recipe = 0)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (argument >= program_.template_arguments.size())
			throw std::logic_error("ABI template argument index is invalid");
		if (argument >= program_.canonical_template_arguments.size() ||
			program_.canonical_template_arguments[argument].kind ==
				TEMPLATE_ARGUMENT_TYPE)
			return AddTypeArgument(
				program_.template_arguments[argument], function, recipe);
		const TemplateArgument& source =
			program_.canonical_template_arguments[argument];
		if (source.kind == TEMPLATE_ARGUMENT_TEMPLATE)
		{
			const std::string id = "__cppgm_abi_template_argument_" +
				std::to_string(next_argument_++);
			AbiFactRecord definition;
			definition.set_kind(ABI_FACT_RECORD_DEFINITION);
			definition.definition.id = id;
			definition.definition.set_kind(ABI_DEFINITION_TEMPLATE_ARGUMENT);
			AbiTemplateArgument& target =
				definition.definition.template_argument;
			if (source.dependent_parameter != kNoTemplateParameter)
			{
				target.kind =
					ABI_TEMPLATE_ARGUMENT_TEMPLATE_PARAMETER_TEMPLATE;
				target.index = source.dependent_parameter;
			}
			else
			{
				// The canonical marker type carries the template's indexed
				// qualified path.  Encoding it as a type argument gives the
				// Itanium substitution order used for a template-name argument,
				// without introducing a value-expression wrapper.  Keep the
				// template-name identity separate from its qualified path: both
				// are substitution candidates in the ABI grammar.
				target.kind = ABI_TEMPLATE_ARGUMENT_TYPE;
				target.type = MakeType(source.type, function, recipe);
				target.type.substitution = id;
			}
			facts_.records.push_back(definition);
			return id;
		}
		const std::string id = "__cppgm_abi_value_argument_" +
			std::to_string(next_argument_++);
		AbiFactRecord definition;
		definition.set_kind(ABI_FACT_RECORD_DEFINITION);
		definition.definition.id = id;
		definition.definition.set_kind(ABI_DEFINITION_TEMPLATE_ARGUMENT);
		AbiTemplateArgument& target =
			definition.definition.template_argument;
		if (source.value_binding != kNoBinding)
		{
			target.kind = ABI_TEMPLATE_ARGUMENT_ENTITY;
			target.entity_ref = AddEntity(source.value_binding);
			const TypeRecord& type = program_.types.Get(
				program_.types.RemoveTopCv(source.type));
			target.address_of = type.kind == TYPE_POINTER;
		}
		else
		{
			target.kind = ABI_TEMPLATE_ARGUMENT_VALUE;
			target.value_type = MakeType(source.type, function, recipe);
			target.has_value_type = true;
			target.value = source.value;
		}
		facts_.records.push_back(definition);
		return id;
	}

	std::string AddTemplateArgumentPack(std::size_t first, std::size_t count,
		const pa11::BindingRecord* function = 0,
		const pa11::FunctionTemplateAbiRecipe* recipe = 0)
	{
		using namespace abi_mangle;
		if (first > program_.template_arguments.size() ||
			count > program_.template_arguments.size() - first)
			throw std::logic_error("ABI template argument pack range is invalid");
		const std::string id = "__cppgm_abi_pack_argument_" +
			std::to_string(next_argument_++);
		AbiFactRecord definition;
		definition.set_kind(ABI_FACT_RECORD_DEFINITION);
		definition.definition.id = id;
		definition.definition.set_kind(ABI_DEFINITION_TEMPLATE_ARGUMENT);
		definition.definition.template_argument.kind = ABI_TEMPLATE_ARGUMENT_PACK;
		for (std::size_t argument = 0; argument < count; ++argument)
			definition.definition.template_argument.argument_refs.push_back(
				AddTemplateArgument(first + argument, function, recipe));
		facts_.records.push_back(definition);
		return id;
	}

	std::string AddTemplateParameterExpression(std::size_t parameter)
	{
		using namespace abi_mangle;
		const std::string id = "__cppgm_abi_template_expression_" +
			std::to_string(next_argument_++);
		AbiFactRecord definition;
		definition.set_kind(ABI_FACT_RECORD_DEFINITION);
		definition.definition.id = id;
		definition.definition.set_kind(ABI_DEFINITION_EXPRESSION);
		definition.definition.expression.kind =
			ABI_EXPRESSION_TEMPLATE_PARAMETER;
		definition.definition.expression.index = parameter;
		facts_.records.push_back(definition);
		return id;
	}

	std::string AddLocalContext(pa11::BindingId binding)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (binding == kNoBinding || binding >= program_.bindings.size())
			throw std::logic_error("local ABI type has no function context");
		const BindingRecord& function = program_.bindings[binding];
		const TypeRecord& type = program_.types.Get(function.type);
		if (type.kind != TYPE_FUNCTION)
			throw std::logic_error("local ABI context is not a function");
		const std::string id = "__cppgm_abi_local_context_" +
			std::to_string(next_argument_++);
		AbiFactRecord definition;
		definition.set_kind(ABI_FACT_RECORD_DEFINITION);
		definition.definition.id = id;
		definition.definition.set_kind(ABI_DEFINITION_CONTEXT);
		const std::string qualified_name = program_.names.Get(
			function.qualified_name != 0 ?
				function.qualified_name : function.name);
		if (qualified_name == "main")
		{
			definition.definition.context.kind = ABI_CONTEXT_RAW;
			definition.definition.context.fragment = "Z4mainE";
			facts_.records.push_back(definition);
			return id;
		}
		definition.definition.context.kind = ABI_CONTEXT_FUNCTION;
		AbiFunctionTarget& target = definition.definition.context.function;
		target.kind = ABI_FUNCTION_TARGET_PATH;
		target.qualified_name = qualified_name;
		const FunctionTemplateAbiRecipe* recipe = 0;
		if (function.function_template_abi_recipe !=
			kNoFunctionTemplateAbiRecipe)
		{
			if (function.function_template_abi_recipe >=
				program_.function_template_abi_recipes.size())
				throw std::logic_error(
					"local ABI context template recipe is invalid");
			recipe = &program_.function_template_abi_recipes[
				function.function_template_abi_recipe];
		}
		if (function.template_argument_count != 0)
		{
			if (recipe == 0)
				throw std::logic_error(
					"function template specialization has no canonical recipe");
			const std::size_t first = function.template_argument_begin;
			const std::size_t count = function.template_argument_count;
			if (first > program_.template_arguments.size() ||
				count > program_.template_arguments.size() - first)
				throw std::logic_error(
					"local ABI context template arguments are invalid");
			const std::size_t fixed = recipe &&
				recipe->template_parameter_pack ?
				recipe->template_parameter_count - 1 : count;
			if (fixed > count)
				throw std::logic_error(
					"local ABI context template pack is invalid");
			for (std::size_t i = 0; i < fixed; ++i)
			{
				AbiFunctionPathOperand argument;
				argument.kind = ABI_FUNCTION_PATH_TEMPLATE_ARGUMENT;
				argument.argument_ref = AddTemplateArgument(
					first + i, &function, recipe);
				target.path_operands.push_back(argument);
			}
			if (recipe && recipe->template_parameter_pack)
			{
				AbiFunctionPathOperand pack;
				pack.kind = ABI_FUNCTION_PATH_TEMPLATE_ARGUMENT;
				pack.argument_ref = AddTemplateArgumentPack(
					first + fixed, count - fixed, &function, recipe);
				target.path_operands.push_back(pack);
			}
			if (recipe->result_type != kNoFunctionTemplateAbiType)
				target.result_type = MakeFunctionTemplateAbiType(
					recipe->result_type, *recipe);
			else
			{
				TypeId result = type.child;
				const TypeRecord& recipe_type =
					program_.types.Get(recipe->function_type);
				if (UsesFunctionTemplateParameter(
					recipe_type.child, function, *recipe))
					result = recipe_type.child;
				target.result_type = result == type.child ? MakeType(result) :
					MakeType(result, &function, recipe);
			}
			target.has_result_type = true;
		}
		const TypeId* parameters = program_.types.Parameters(function.type);
		const TypeRecord* recipe_type = recipe ?
			&program_.types.Get(recipe->function_type) : 0;
		const TypeId* recipe_parameters = recipe ?
			program_.types.Parameters(recipe->function_type) : 0;
		for (std::size_t i = 0; i < type.parameter_count; ++i)
		{
			TypeId parameter = parameters[i];
			const FunctionTemplateAbiRecipe* parameter_recipe = 0;
			bool pack_expansion = false;
			if (recipe_type)
			{
				const std::size_t fixed = recipe->function_parameter_pack ?
					recipe_type->parameter_count - 1 :
					recipe_type->parameter_count;
				const std::size_t source = i < fixed ? i : fixed;
				if (source >= recipe_type->parameter_count)
					throw std::logic_error(
						"local ABI context parameter recipe is invalid");
				pack_expansion = recipe->function_parameter_pack && i >= fixed;
				if (pack_expansion || UsesFunctionTemplateParameter(
					recipe_parameters[source], function, *recipe))
				{
					parameter = recipe_parameters[source];
					parameter_recipe = recipe;
				}
			}
			AbiType encoded = MakeFunctionTemplateType(
				parameter, function, parameter_recipe);
			if (pack_expansion)
			{
				AbiTypeModifier expansion;
				expansion.kind = ABI_TYPE_PACK_EXPANSION;
				encoded.modifiers.insert(encoded.modifiers.begin(), expansion);
			}
			target.signature_parameter_types.push_back(encoded);
		}
		target.variadic = type.variadic;
		facts_.records.push_back(definition);
		return id;
	}

	bool FunctionTemplateParameter(pa11::TypeId type,
		const pa11::BindingRecord* function,
		const pa11::FunctionTemplateAbiRecipe* recipe,
		std::size_t* index) const
	{
		using namespace pa11;
		if (recipe)
		{
			const TypeRecord& shape = program_.types.Get(type);
			if (shape.kind != TYPE_NAMED) return false;
			const EntityRecord& entity = program_.entities[shape.entity];
			const std::size_t parameter = entity.template_parameter_ordinal;
			if (parameter >= recipe->template_parameter_count ||
				recipe->parameter_shape_begin >
					program_.function_template_parameter_shapes.size() ||
				parameter >= program_.function_template_parameter_shapes.size() -
					recipe->parameter_shape_begin ||
				program_.function_template_parameter_shapes[
					recipe->parameter_shape_begin + parameter] != type)
				return false;
			*index = parameter;
			return true;
		}
		if (!function || function->template_argument_count == 0) return false;
		const std::size_t first = function->template_argument_begin;
		if (first > program_.template_arguments.size() ||
			function->template_argument_count >
				program_.template_arguments.size() - first)
			throw std::logic_error("function template ABI argument range is invalid");
		for (std::size_t i = 0; i < function->template_argument_count; ++i)
		{
			const std::size_t argument = first + i;
			if (argument < program_.canonical_template_arguments.size() &&
				program_.canonical_template_arguments[argument].kind !=
					TEMPLATE_ARGUMENT_TYPE)
				continue;
			if (program_.template_arguments[argument] == type)
			{
				*index = i;
				return true;
			}
		}
		return false;
	}

	abi_mangle::AbiType MakeType(pa11::TypeId type)
	{
		return MakeType(type, 0, 0);
	}

	abi_mangle::AbiType MakeFunctionTemplateType(pa11::TypeId type,
		const pa11::BindingRecord& function,
		const pa11::FunctionTemplateAbiRecipe* recipe = 0)
	{
		// Only the retained pattern recipe can prove that a type occurrence is
		// dependent.  Inferring dependence by comparing an instantiated type
		// with the specialization's template arguments is ambiguous: for
		// `template<class T> int f(T)`, f<int>'s non-dependent result happens to
		// equal T's argument but must be encoded as `i`, not `T_`.
		return recipe ? MakeType(type, &function, recipe) : MakeType(type);
	}

	abi_mangle::AbiType MakeFunctionTemplateAbiType(
		pa11::FunctionTemplateAbiTypeId type,
		const pa11::FunctionTemplateAbiRecipe& recipe)
	{
		using namespace abi_mangle;
		using namespace pa11;
		if (type == kNoFunctionTemplateAbiType ||
			type >= program_.function_template_abi_types.size())
			throw std::logic_error("function template ABI result recipe is invalid");
		const FunctionTemplateAbiType& source =
			program_.function_template_abi_types[type];
		AbiType result;
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_PARAMETER)
		{
			if (source.parameter >= recipe.template_parameter_count)
				throw std::logic_error(
					"function template ABI result parameter is invalid");
			result.kind = ABI_TYPE_TEMPLATE_PARAMETER;
			result.index = source.parameter;
			result.substitutable = true;
			return result;
		}
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_MEMBER)
		{
			if (source.child == kNoFunctionTemplateAbiType || source.name == 0)
				throw std::logic_error(
					"function template ABI result member is invalid");
			result.kind = ABI_TYPE_MEMBER;
			result.name = program_.names.Get(source.name);
			result.types.push_back(
				MakeFunctionTemplateAbiType(source.child, recipe));
			return result;
		}
		if (source.child == kNoFunctionTemplateAbiType)
			throw std::logic_error("function template ABI result modifier is invalid");
		result = MakeFunctionTemplateAbiType(source.child, recipe);
		AbiTypeModifier modifier;
		if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_QUALIFIED)
		{
			modifier.kind = ABI_TYPE_CV;
			modifier.is_const = (source.cv & CV_CONST) != 0;
			modifier.is_volatile = (source.cv & CV_VOLATILE) != 0;
		}
		else if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_POINTER)
			modifier.kind = ABI_TYPE_POINTER;
		else if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_LVALUE_REFERENCE)
			modifier.kind = ABI_TYPE_LVALUE_REFERENCE;
		else if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_RVALUE_REFERENCE)
			modifier.kind = ABI_TYPE_RVALUE_REFERENCE;
		else if (source.kind == FUNCTION_TEMPLATE_ABI_TYPE_ARRAY)
		{
			modifier.kind = ABI_TYPE_ARRAY;
			if (source.parameter != kNoTemplateParameter)
			{
				modifier.array_bound.kind = ABI_ARRAY_BOUND_EXPRESSION;
				modifier.array_bound.value =
					AddTemplateParameterExpression(source.parameter);
			}
			else if (source.bound != 0)
				modifier.array_bound.value = std::to_string(source.bound);
		}
		else throw std::logic_error(
			"function template ABI result node kind is invalid");
		result.modifiers.insert(result.modifiers.begin(), modifier);
		return result;
	}

	bool UsesFunctionTemplateParameter(pa11::TypeId type,
		const pa11::BindingRecord& function,
		const pa11::FunctionTemplateAbiRecipe& recipe) const
	{
		using namespace pa11;
		std::vector<TypeId> pending(1, type);
		while (!pending.empty())
		{
			const TypeId current = pending.back();
			pending.pop_back();
			std::size_t parameter = 0;
			if (FunctionTemplateParameter(
				current, &function, &recipe, &parameter)) return true;
			const TypeRecord& record = program_.types.Get(current);
			if (record.kind == TYPE_ARRAY &&
				record.dependent_bound_parameter != kNoTemplateParameter)
				return true;
			if (record.kind == TYPE_QUALIFIED || record.kind == TYPE_POINTER ||
				record.kind == TYPE_LVALUE_REFERENCE ||
				record.kind == TYPE_RVALUE_REFERENCE || record.kind == TYPE_ARRAY)
			{
				pending.push_back(record.child);
				continue;
			}
			if (record.kind == TYPE_FUNCTION)
			{
				pending.push_back(record.child);
				const TypeId* parameters = program_.types.Parameters(current);
				for (std::size_t i = 0; i < record.parameter_count; ++i)
					pending.push_back(parameters[i]);
				continue;
			}
			if (record.kind == TYPE_MEMBER_POINTER)
			{
				pending.push_back(record.child);
				pending.push_back(static_cast<TypeId>(record.bound));
				continue;
			}
			if (record.kind != TYPE_NAMED) continue;
			const EntityRecord& entity = program_.entities[record.entity];
			if (entity.enclosing_class != kNoEntity)
				pending.push_back(
					program_.entities[entity.enclosing_class].type);
			if (entity.template_argument_count == 0) continue;
			const std::size_t first = entity.template_argument_begin;
			if (first > program_.template_arguments.size() ||
				entity.template_argument_count >
					program_.template_arguments.size() - first)
				throw std::logic_error(
					"function template ABI source argument range is invalid");
			for (std::size_t i = 0; i < entity.template_argument_count; ++i)
			{
				const std::size_t argument = first + i;
				if (argument >= program_.canonical_template_arguments.size() ||
					program_.canonical_template_arguments[argument].kind ==
						TEMPLATE_ARGUMENT_TYPE)
					pending.push_back(program_.template_arguments[argument]);
			}
		}
		return false;
	}

	abi_mangle::AbiType MakeType(pa11::TypeId type,
		const pa11::BindingRecord* function,
		const pa11::FunctionTemplateAbiRecipe* recipe)
	{
		using namespace abi_mangle;
		using namespace pa11;
		std::vector<AbiTypeModifier> modifiers;
		const TypeRecord* record = &program_.types.Get(type);
		std::size_t template_parameter = 0;
		while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_POINTER ||
			record->kind == TYPE_LVALUE_REFERENCE ||
			record->kind == TYPE_RVALUE_REFERENCE || record->kind == TYPE_ARRAY)
		{
			if (FunctionTemplateParameter(
				type, function, recipe, &template_parameter))
			{
				AbiType result;
				result.kind = ABI_TYPE_TEMPLATE_PARAMETER;
				result.index = template_parameter;
				result.substitutable = true;
				result.modifiers.swap(modifiers);
				return result;
			}
			AbiTypeModifier modifier;
			if (record->kind == TYPE_QUALIFIED)
			{
				modifier.kind = ABI_TYPE_CV;
				modifier.is_const = (record->cv & CV_CONST) != 0;
				modifier.is_volatile = (record->cv & CV_VOLATILE) != 0;
			}
			else if (record->kind == TYPE_ARRAY)
			{
				modifier.kind = ABI_TYPE_ARRAY;
				if (record->dependent_bound_parameter !=
					kNoTemplateParameter)
				{
					modifier.array_bound.kind =
						ABI_ARRAY_BOUND_EXPRESSION;
					modifier.array_bound.value =
						AddTemplateParameterExpression(
							record->dependent_bound_parameter);
				}
				else if (record->bound != 0)
					modifier.array_bound.value = std::to_string(record->bound);
			}
			else modifier.kind = record->kind == TYPE_POINTER ? ABI_TYPE_POINTER :
				record->kind == TYPE_LVALUE_REFERENCE ? ABI_TYPE_LVALUE_REFERENCE :
				ABI_TYPE_RVALUE_REFERENCE;
			modifiers.push_back(modifier);
			type = record->child;
			record = &program_.types.Get(type);
		}
		AbiType result;
		result.modifiers.swap(modifiers);
		if (FunctionTemplateParameter(
			type, function, recipe, &template_parameter))
		{
			result.kind = ABI_TYPE_TEMPLATE_PARAMETER;
			result.index = template_parameter;
			result.substitutable = true;
			return result;
		}
		if (record->kind == TYPE_FUNCTION)
		{
			result.kind = ABI_TYPE_FUNCTION;
			result.types.push_back(MakeType(record->child, function, recipe));
			const TypeId* parameters = program_.types.Parameters(type);
			for (std::size_t i = 0; i < record->parameter_count; ++i)
				result.types.push_back(
					MakeType(parameters[i], function, recipe));
			result.variadic = record->variadic;
			return result;
		}
		if (record->kind == TYPE_MEMBER_POINTER)
		{
			result.kind = ABI_TYPE_MEMBER_POINTER;
			result.types.push_back(MakeType(
				static_cast<TypeId>(record->bound), function, recipe));
			result.types.push_back(MakeType(record->child, function, recipe));
			return result;
		}
		if (record->kind == TYPE_NAMED)
		{
			const EntityRecord& entity = program_.entities[record->entity];
			if (entity.lambda_closure)
			{
				if (entity.local_context != kNoBinding &&
					pa18_lowering_detail::PreferLocalObjectBinding(
						program_, record->entity))
				{
					result.kind = ABI_TYPE_LOCAL_TYPE;
					result.context_ref = AddLocalContext(entity.local_context);
					result.name = "$_" +
						std::to_string(entity.lambda_ordinal);
					result.discriminator = "0";
				}
				else if (entity.local_context != kNoBinding)
				{
					result.kind = ABI_TYPE_LAMBDA_CLOSURE;
					result.context_ref = AddLocalContext(entity.local_context);
					result.discriminator =
						LambdaDiscriminator(entity.lambda_ordinal);
					if (entity.lambda_call_operator == kNoBinding ||
						entity.lambda_call_operator >= program_.bindings.size())
						throw std::logic_error(
							"lambda ABI type has no call operator fact");
					const BindingRecord& call =
						program_.bindings[entity.lambda_call_operator];
					const TypeRecord& call_type =
						program_.types.Get(call.type);
					const TypeId* parameters =
						program_.types.Parameters(call.type);
					for (std::size_t i = 0;
						i < call_type.parameter_count; ++i)
						result.types.push_back(MakeType(parameters[i]));
				}
				else
				{
					std::vector<NameId> path;
					program_.BuildEmissionPath(
						entity.owner, entity.identity_name, &path);
					if (path.empty())
						throw std::logic_error(
							"namespace lambda ABI type has no identity path");
					result.kind = ABI_TYPE_NAMESPACE_LAMBDA;
					result.name = program_.names.Get(path.back());
					for (std::size_t i = 0; i + 1 < path.size(); ++i)
						result.namespace_qualifiers.push_back(
							program_.names.Get(path[i]));
				}
			}
			else if (entity.local_context != kNoBinding)
			{
				result.kind = ABI_TYPE_LOCAL_TYPE;
				result.context_ref = AddLocalContext(entity.local_context);
				result.name = program_.names.Get(entity.identity_name);
				result.discriminator = "0";
			}
			else if (entity.enclosing_class != kNoEntity)
			{
				if (entity.enclosing_class >= program_.entities.size())
					throw std::logic_error(
						"nested ABI type has no enclosing class");
				result.kind = ABI_TYPE_MEMBER;
				result.name = program_.names.Get(entity.identity_name);
				result.types.push_back(MakeType(
					program_.entities[entity.enclosing_class].type,
					function, recipe));
			}
			else if (entity.template_argument_count == 0)
			{
				result.kind = ABI_TYPE_NAMED;
				result.name = program_.names.Get(entity.name);
			}
			else
			{
				result.kind = ABI_TYPE_TEMPLATE_SPECIALIZATION;
				result.substitution = "__cppgm_abi_class_" +
					std::to_string(record->entity);
				std::vector<NameId> path;
				program_.BuildEmissionPath(entity.owner, entity.identity_name, &path);
				for (std::size_t i = 0; i < path.size(); ++i)
				{
					if (i != 0) result.name += "::";
					result.name += program_.names.Get(path[i]);
				}
				const std::size_t first = entity.template_argument_begin;
				if (first > program_.template_arguments.size() ||
					entity.template_argument_count >
						program_.template_arguments.size() - first)
					throw std::logic_error(
						"class template ABI argument range is invalid");
				const std::size_t pack = entity.template_argument_pack_begin;
				const std::size_t fixed = pack == kNoTemplateParameter ?
					entity.template_argument_count : pack;
				if (fixed > entity.template_argument_count)
					throw std::logic_error("class template ABI pack offset is invalid");
				for (std::size_t i = 0; i < fixed; ++i)
					result.argument_refs.push_back(AddTemplateArgument(
						first + i, function, recipe));
				if (pack != kNoTemplateParameter)
					result.argument_refs.push_back(AddTemplateArgumentPack(
						first + fixed, entity.template_argument_count - fixed,
						function, recipe));
			}
			return result;
		}
		if (record->kind != TYPE_FUNDAMENTAL)
			throw std::runtime_error("unsupported ABI type in PA15");
		result.kind = ABI_TYPE_BUILTIN;
		switch (record->fundamental)
		{
		case FUND_VOID: result.name = "void"; break;
		case FUND_BOOL: result.name = "bool"; break;
		case FUND_CHAR: result.name = "char"; break;
		case FUND_SIGNED_CHAR: result.name = "schar"; break;
		case FUND_UNSIGNED_CHAR: result.name = "uchar"; break;
		case FUND_SHORT_INT: result.name = "short"; break;
		case FUND_UNSIGNED_SHORT_INT: result.name = "ushort"; break;
		case FUND_INT: result.name = "int"; break;
		case FUND_UNSIGNED_INT: result.name = "uint"; break;
		case FUND_LONG_INT: result.name = "long"; break;
		case FUND_UNSIGNED_LONG_INT: result.name = "ulong"; break;
		case FUND_LONG_LONG_INT: result.name = "longlong"; break;
		case FUND_UNSIGNED_LONG_LONG_INT: result.name = "ulonglong"; break;
		case FUND_INT128: result.name = "int128"; break;
		case FUND_UINT128: result.name = "uint128"; break;
		case FUND_FLOAT: result.name = "float"; break;
		case FUND_DOUBLE: result.name = "double"; break;
		case FUND_LONG_DOUBLE: result.name = "longdouble"; break;
		case FUND_WCHAR_T: result.name = "wchar"; break;
		case FUND_CHAR16_T: result.name = "char16"; break;
		case FUND_CHAR32_T: result.name = "char32"; break;
		case FUND_NULLPTR_T: result.name = "nullptr"; break;
		}
		return result;
	}
};

bool AppendClassTemplateOwner(const pa11::Program& program,
	const pa11::BindingRecord& binding, AbiFactBuilder* builder,
	abi_mangle::AbiFactCase* facts, bool retain_complete_substitution)
{
	using namespace abi_mangle;
	using namespace pa11;
	if (binding.member_owner == kNoEntity) return false;
	const EntityRecord& entity = program.entities[binding.member_owner];
	if (entity.template_argument_count == 0) return false;
	const std::size_t first = entity.template_argument_begin;
	if (first > program.template_arguments.size() ||
		entity.template_argument_count > program.template_arguments.size() - first)
		throw std::logic_error("class template ABI owner arguments are invalid");
	std::vector<NameId> path;
	program.BuildEmissionPath(entity.owner, entity.identity_name, &path);
	std::string prefix;
	for (std::size_t i = 0; i < path.size(); ++i)
	{
		AbiFactRecord component;
		component.set_kind(ABI_FACT_RECORD_FUNCTION);
		component.function.name = program.names.Get(path[i]);
		if (!prefix.empty()) prefix += "::";
		prefix += component.function.name;
		component.function.substitution = prefix;
		if (i + 1 == path.size())
		{
			component.function.kind = ABI_FUNCTION_RECORD_NAME_TEMPLATE;
			component.function.complete_substitution =
				retain_complete_substitution ? "__cppgm_abi_class_" +
					std::to_string(binding.member_owner) : "-";
			component.function.standard_substitution = "-";
			const std::size_t pack = entity.template_argument_pack_begin;
			const std::size_t fixed = pack == kNoTemplateParameter ?
				entity.template_argument_count : pack;
			if (fixed > entity.template_argument_count)
				throw std::logic_error("class template ABI pack offset is invalid");
			for (std::size_t argument = 0; argument < fixed; ++argument)
				component.function.argument_refs.push_back(
					builder->AddTemplateArgument(first + argument));
			if (pack != kNoTemplateParameter)
				component.function.argument_refs.push_back(
					builder->AddTemplateArgumentPack(first + fixed,
						entity.template_argument_count - fixed));
		}
		else component.function.kind = ABI_FUNCTION_RECORD_NAME_SOURCE;
		facts->records.push_back(component);
	}
	return true;
}

std::string OperatorTerminal(OperatorKind kind, bool member,
	std::size_t parameter_count)
{
	switch (kind)
	{
	case OPERATOR_PLUS: return "plus";
	case OPERATOR_MINUS: return "minus";
	case OPERATOR_STAR:
		return (member ? parameter_count == 0 : parameter_count == 1) ?
			"deref" : "multiply";
	case OPERATOR_AMPERSAND:
		return (member ? parameter_count == 0 : parameter_count == 1) ?
			"address-of" : "bit-and";
	case OPERATOR_DIVIDE: return "divide";
	case OPERATOR_REMAINDER: return "remainder";
	case OPERATOR_BIT_OR: return "bit-or";
	case OPERATOR_BIT_XOR: return "bit-xor";
	case OPERATOR_ASSIGN: return "assign";
	case OPERATOR_PLUS_ASSIGN: return "plus-assign";
	case OPERATOR_MINUS_ASSIGN: return "minus-assign";
	case OPERATOR_MULTIPLY_ASSIGN: return "multiply-assign";
	case OPERATOR_DIVIDE_ASSIGN: return "divide-assign";
	case OPERATOR_REMAINDER_ASSIGN: return "remainder-assign";
	case OPERATOR_AND_ASSIGN: return "and-assign";
	case OPERATOR_OR_ASSIGN: return "or-assign";
	case OPERATOR_XOR_ASSIGN: return "xor-assign";
	case OPERATOR_LEFT_SHIFT: return "left-shift";
	case OPERATOR_RIGHT_SHIFT: return "right-shift";
	case OPERATOR_LEFT_SHIFT_ASSIGN: return "left-shift-assign";
	case OPERATOR_RIGHT_SHIFT_ASSIGN: return "right-shift-assign";
	case OPERATOR_EQUAL: return "equal";
	case OPERATOR_NOT_EQUAL: return "not-equal";
	case OPERATOR_LESS: return "less";
	case OPERATOR_GREATER: return "greater";
	case OPERATOR_LESS_EQUAL: return "less-equal";
	case OPERATOR_GREATER_EQUAL: return "greater-equal";
	case OPERATOR_LOGICAL_NOT: return "logical-not";
	case OPERATOR_LOGICAL_AND: return "logical-and";
	case OPERATOR_LOGICAL_OR: return "logical-or";
	case OPERATOR_INCREMENT: return "increment";
	case OPERATOR_DECREMENT: return "decrement";
	case OPERATOR_COMMA: return "comma";
	case OPERATOR_MEMBER_POINTER: return "member-pointer";
	case OPERATOR_ARROW: return "arrow";
	case OPERATOR_CALL: return "call";
	case OPERATOR_INDEX: return "index";
	case OPERATOR_NEW: return "new";
	case OPERATOR_NEW_ARRAY: return "new-array";
	case OPERATOR_DELETE: return "delete";
	case OPERATOR_DELETE_ARRAY: return "delete-array";
	case OPERATOR_NONE:
	case OPERATOR_LITERAL: return std::string();
	}
	throw std::logic_error("invalid typed operator terminal");
}

}

std::string MangleType(const pa11::Program& program, pa11::TypeId type)
{
	using namespace abi_mangle;
	AbiFactFile file;
	file.cases.push_back(AbiFactCase());
	AbiFactBuilder facts(program, file.cases[0]);
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_TYPE;
	target.target.type = facts.MakeType(type);
	file.cases[0].records.push_back(target);
	std::string result = mangle_fact_file(file);
	if (!result.empty() && result[result.size() - 1] == '\n')
		result.resize(result.size() - 1);
	return result;
}

bool IsFunctionEmissionDemanded(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node, bool host_object_emission)
{
	using namespace pa11;
	if (node.binding == kNoBinding) return false;
	if (node.declaration_only) return true;
	const BindingId binding = program.bindings[node.binding].canonical;
	if (host_object_emission &&
		IsTrivialLifecycleBinding(program, binding)) return false;
	return !program.bindings[binding].inline_function ||
		program.bindings[binding].emission_demanded;
}

void ApplyBuiltinSymbolMetadata(pa15_lowir_detail::Symbol* symbol,
	pa11::BuiltinFunctionKind kind)
{
	using namespace pa11;
	using pa15_lowir_detail::Symbol;
	switch (kind)
	{
	case BUILTIN_FUNCTION_STRLEN: symbol->effects = Symbol::EFFECTS_READONLY; break;
	case BUILTIN_FUNCTION_UNREACHABLE:
		symbol->effects = Symbol::EFFECTS_READNONE; symbol->noreturn = true; break;
	case BUILTIN_FUNCTION_MEMCPY:
	case BUILTIN_FUNCTION_MEMMOVE: symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_NANL:
	case BUILTIN_FUNCTION_ISNAN: symbol->effects = Symbol::EFFECTS_READNONE; break;
	case BUILTIN_FUNCTION_OPERATOR_NEW:
	case BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY:
		symbol->runtime_role = Symbol::RUNTIME_ROLE_ALLOCATE_MEMORY;
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_OPERATOR_DELETE:
	case BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY:
		symbol->runtime_role = Symbol::RUNTIME_ROLE_FREE_MEMORY;
		symbol->effects = Symbol::EFFECTS_READWRITE; break;
	case BUILTIN_FUNCTION_NONE: break;
	}
}

void ApplyNativeRuntimeSymbolMetadata(pa15_lowir_detail::Symbol* symbol)
{
	using pa15_lowir_detail::Symbol;
	if (!symbol->c_linkage) return;
	if (symbol->object_name == "malloc")
		symbol->runtime_role = Symbol::RUNTIME_ROLE_ALLOCATE_MEMORY;
	else if (symbol->object_name == "free")
		symbol->runtime_role = Symbol::RUNTIME_ROLE_FREE_MEMORY;
}

void ApplyBuiltinParameterMetadata(pa15_lowir_detail::Parameter* parameter,
	pa11::BuiltinFunctionKind kind, std::size_t index)
{
	using namespace pa11;
	using pa15_lowir_detail::Parameter;
	if (kind == BUILTIN_FUNCTION_STRLEN && index == 0)
	{
		parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		parameter->access = Parameter::ACCESS_READ;
	}
	else if (kind == BUILTIN_FUNCTION_MEMCPY && index < 2)
	{
		parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		parameter->access = index == 0 ? Parameter::ACCESS_WRITE :
			Parameter::ACCESS_READ;
		parameter->alias = Parameter::ALIAS_NOALIAS;
	}
	else if (kind == BUILTIN_FUNCTION_MEMMOVE && index < 2)
	{
		parameter->capture = Parameter::CAPTURE_NOCAPTURE;
		parameter->access = index == 0 ? Parameter::ACCESS_READWRITE :
			Parameter::ACCESS_READ;
	}
}

void AppendFunctionTemplateArgumentsAndResult(const pa11::Program& program,
	const pa11::BindingRecord& binding,
	const pa11::TypeRecord& function_type,
	const pa11::FunctionTemplateAbiRecipe* recipe,
	AbiFactBuilder* facts, abi_mangle::AbiFactCase* output)
{
	using namespace abi_mangle;
	using namespace pa11;
	if (binding.template_argument_count == 0) return;
	const std::size_t first = binding.template_argument_begin;
	const std::size_t count = binding.template_argument_count;
	if (first > program.template_arguments.size() ||
		count > program.template_arguments.size() - first)
		throw std::logic_error(
			"function template argument range is invalid during mangling");
	const std::size_t fixed = recipe && recipe->template_parameter_pack ?
		recipe->template_parameter_count - 1 : count;
	if (fixed > count)
		throw std::logic_error("function template ABI pack range is invalid");
	for (std::size_t i = 0; i < fixed; ++i)
	{
		AbiFactRecord argument;
		argument.set_kind(ABI_FACT_RECORD_FUNCTION);
		argument.function.kind =
			ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
		argument.function.argument_refs.push_back(
			facts->AddTemplateArgument(first + i));
		output->records.push_back(argument);
	}
	if (recipe && recipe->template_parameter_pack)
	{
		AbiFactRecord argument;
		argument.set_kind(ABI_FACT_RECORD_FUNCTION);
		argument.function.kind =
			ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
		argument.function.argument_refs.push_back(
			facts->AddTemplateArgumentPack(first + fixed, count - fixed));
		output->records.push_back(argument);
	}
	AbiFactRecord result;
	result.set_kind(ABI_FACT_RECORD_FUNCTION);
	result.function.kind = ABI_FUNCTION_RECORD_RESULT;
	if (recipe && recipe->result_type != kNoFunctionTemplateAbiType)
	{
		result.function.type = facts->MakeFunctionTemplateAbiType(
			recipe->result_type, *recipe);
	}
	else
	{
		TypeId result_type = function_type.child;
		if (recipe)
		{
			const TypeId source = program.types.Get(recipe->function_type).child;
			if (facts->UsesFunctionTemplateParameter(source, binding, *recipe))
				result_type = source;
		}
		result.function.type = facts->MakeFunctionTemplateType(result_type,
			binding, result_type == function_type.child ? 0 : recipe);
	}
	output->records.push_back(result);
}

std::string MangleLambdaCallOperator(const pa11::Program& program,
	const pa11::BindingRecord& binding,
	const pa11::EntityRecord& lambda)
{
	using namespace abi_mangle;
	using namespace pa11;
	if (binding.operator_kind != OPERATOR_CALL)
		throw std::logic_error("invalid lambda call-operator ABI identity");
	AbiFactFile file;
	file.cases.push_back(AbiFactCase());
	AbiFactBuilder facts(program, file.cases[0]);
	AbiFactRecord lambda_target;
	lambda_target.set_kind(ABI_FACT_RECORD_TARGET);
	lambda_target.target.kind = ABI_TARGET_FACT_FUNCTION;
	AbiFunctionTarget& function = lambda_target.target.function;
	if (lambda.local_context != kNoBinding &&
		pa18_lowering_detail::PreferLocalObjectBinding(
			program, binding.member_owner))
	{
		function.kind = ABI_FUNCTION_TARGET_LOCAL;
		function.context_ref = facts.AddLocalContext(lambda.local_context);
		function.qualified_name = "$_" +
			std::to_string(lambda.lambda_ordinal);
	}
	else if (lambda.local_context != kNoBinding)
	{
		function.kind = ABI_FUNCTION_TARGET_LAMBDA;
		function.context_ref = facts.AddLocalContext(lambda.local_context);
		function.discriminator = LambdaDiscriminator(lambda.lambda_ordinal);
	}
	else
	{
		std::vector<NameId> path;
		program.BuildEmissionPath(lambda.owner, lambda.identity_name, &path);
		if (path.empty())
			throw std::logic_error(
				"namespace lambda call operator has no identity path");
		function.kind = ABI_FUNCTION_TARGET_NAMESPACE_LAMBDA;
		function.source_name = program.names.Get(path.back());
		for (std::size_t i = 0; i + 1 < path.size(); ++i)
			function.namespace_qualifiers.push_back(program.names.Get(path[i]));
	}
	function.terminal = "operator-call";
	const TypeRecord& lambda_type = program.types.Get(binding.type);
	AbiFactRecord qualifier;
	qualifier.set_kind(ABI_FACT_RECORD_FUNCTION);
	qualifier.function.kind = ABI_FUNCTION_RECORD_QUALIFIER;
	if ((lambda_type.cv & CV_CONST) != 0)
		qualifier.function.qualifiers.push_back(
			ABI_FUNCTION_QUALIFIER_CONST);
	if ((lambda_type.cv & CV_VOLATILE) != 0)
		qualifier.function.qualifiers.push_back(
			ABI_FUNCTION_QUALIFIER_VOLATILE);
	if (!qualifier.function.qualifiers.empty())
		file.cases[0].records.push_back(qualifier);
	const TypeId* lambda_parameters = program.types.Parameters(binding.type);
	for (std::size_t i = 0; i < lambda_type.parameter_count; ++i)
	{
		if (function.kind == ABI_FUNCTION_TARGET_LAMBDA)
			function.signature_parameter_types.push_back(
				facts.MakeType(lambda_parameters[i]));
		AbiFactRecord parameter;
		parameter.set_kind(ABI_FACT_RECORD_FUNCTION);
		parameter.function.kind = ABI_FUNCTION_RECORD_PARAMETER;
		parameter.function.type = facts.MakeType(lambda_parameters[i]);
		file.cases[0].records.push_back(parameter);
	}
	file.cases[0].records.push_back(lambda_target);
	std::string result = mangle_fact_file(file);
	if (!result.empty() && result[result.size() - 1] == '\n')
		result.resize(result.size() - 1);
	return result;
}

std::string MangleFunction(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node,
	bool force_lifecycle_base_entry)
{
	using namespace abi_mangle;
	using namespace pa11;
	const BindingRecord& binding = program.bindings[node.binding];
	std::string qualified = program.names.Get(
		binding.qualified_name != 0 ? binding.qualified_name : node.text);
	if (binding.conversion_function)
	{
		const std::size_t terminal = qualified.find("::operator");
		if (terminal != std::string::npos)
			qualified.erase(terminal + std::string("::operator").size());
	}
	if (qualified == "main") return std::string();
	if (binding.builtin_function != BUILTIN_FUNCTION_NONE)
	{
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_NEW)
			return "cppgm_builtin_operator_new";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_DELETE)
			return "cppgm_builtin_operator_delete";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_NEW_ARRAY)
			return "cppgm_builtin_operator_new_array";
		if (binding.builtin_function == BUILTIN_FUNCTION_OPERATOR_DELETE_ARRAY)
			return "cppgm_builtin_operator_delete_array";
		return "cppgm_builtin_" + program.names.Get(binding.name).substr(10);
	}
	if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
		binding.storage_class != STORAGE_CLASS_STATIC)
		return program.names.Get(binding.name);
	const EntityRecord* lambda = binding.member_owner == kNoEntity ? 0 :
		&program.entities[binding.member_owner];
	if (lambda && lambda->lambda_closure)
		return MangleLambdaCallOperator(program, binding, *lambda);
	AbiFactFile file;
	file.cases.push_back(AbiFactCase());
	AbiFactBuilder facts(program, file.cases[0]);
	const FunctionTemplateAbiRecipe* recipe = 0;
	if (binding.function_template_abi_recipe != kNoFunctionTemplateAbiRecipe)
	{
		if (binding.function_template_abi_recipe >=
			program.function_template_abi_recipes.size())
			throw std::logic_error("invalid function template ABI recipe");
		recipe = &program.function_template_abi_recipes[
			binding.function_template_abi_recipe];
		if (!program.types.IsFunction(recipe->function_type))
			throw std::logic_error("function template ABI recipe is not callable");
	}
	const bool structured_class_owner = binding.member_owner != kNoEntity &&
		program.entities[binding.member_owner].template_argument_count != 0;
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_FUNCTION;
	target.target.internal_linkage =
		binding.storage_class == STORAGE_CLASS_STATIC &&
		!binding.unnamed_namespace_linkage;
	target.target.function.kind = structured_class_owner ?
		ABI_FUNCTION_TARGET_ENCODING : ABI_FUNCTION_TARGET_PATH;
	if (!structured_class_owner)
		target.target.function.qualified_name = qualified;
	file.cases[0].records.push_back(target);
	if (structured_class_owner &&
		!AppendClassTemplateOwner(program, binding, &facts, &file.cases[0], true))
		throw std::logic_error("class template ABI owner was lost");
	const TypeRecord& function_type = program.types.Get(node.type);
	const TypeId* parameters = program.types.Parameters(node.type);
	AppendFunctionTemplateArgumentsAndResult(program, binding, function_type,
		recipe, &facts, &file.cases[0]);
	const bool member = binding.member_owner != kNoEntity &&
		!binding.static_member_function;
	if (member)
	{
		const TypeRecord& declared_type = program.types.Get(binding.type);
		AbiFactRecord qualifier;
		qualifier.set_kind(ABI_FACT_RECORD_FUNCTION);
		qualifier.function.kind = ABI_FUNCTION_RECORD_QUALIFIER;
		if ((declared_type.cv & CV_CONST) != 0)
			qualifier.function.qualifiers.push_back(
				ABI_FUNCTION_QUALIFIER_CONST);
		if ((declared_type.cv & CV_VOLATILE) != 0)
			qualifier.function.qualifiers.push_back(
				ABI_FUNCTION_QUALIFIER_VOLATILE);
		if (declared_type.ref_qualifier == FUNCTION_REF_LVALUE)
			qualifier.function.qualifiers.push_back(
				ABI_FUNCTION_QUALIFIER_LVALUE_REFERENCE);
		else if (declared_type.ref_qualifier == FUNCTION_REF_RVALUE)
			qualifier.function.qualifiers.push_back(
				ABI_FUNCTION_QUALIFIER_RVALUE_REFERENCE);
		if (!qualifier.function.qualifiers.empty())
			file.cases[0].records.push_back(qualifier);
	}
	const std::string operator_terminal =
		OperatorTerminal(binding.operator_kind, member,
			program.types.Get(binding.type).parameter_count);
	if (structured_class_owner && binding.operator_kind == OPERATOR_NONE &&
		!binding.conversion_function && !binding.constructor && !binding.destructor)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_NAME_SOURCE;
		terminal.function.name = program.names.Get(binding.name);
		file.cases[0].records.push_back(terminal);
	}
	if (binding.operator_kind == OPERATOR_LITERAL ||
		!operator_terminal.empty())
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_OPERATOR_TERMINAL;
		if (binding.operator_kind == OPERATOR_LITERAL)
		{
			terminal.function.terminal = "literal";
			terminal.function.literal_suffix =
				program.names.Get(binding.operator_literal_suffix);
		}
		else terminal.function.terminal = operator_terminal;
		file.cases[0].records.push_back(terminal);
	}
	else if (binding.conversion_function)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_CONVERSION_TERMINAL;
		terminal.function.type = facts.MakeType(binding.conversion_target);
		file.cases[0].records.push_back(terminal);
	}
	else if (binding.constructor)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_TERMINAL;
		terminal.function.terminal =
			binding.constructor_base_entry || force_lifecycle_base_entry ?
			"constructor-base" : "constructor-complete";
		file.cases[0].records.push_back(terminal);
	}
	else if (binding.destructor)
	{
		AbiFactRecord terminal;
		terminal.set_kind(ABI_FACT_RECORD_FUNCTION);
		terminal.function.kind = ABI_FUNCTION_RECORD_TERMINAL;
		terminal.function.terminal = binding.destructor_base_entry ||
			force_lifecycle_base_entry ?
			"destructor-base" : "destructor-complete";
		file.cases[0].records.push_back(terminal);
	}
	const std::size_t first_parameter = member ? 1 : 0;
	const TypeRecord* recipe_function = recipe ?
		&program.types.Get(recipe->function_type) : 0;
	const TypeId* recipe_parameters = recipe_function &&
		recipe_function->parameter_count != 0 ?
		program.types.Parameters(recipe->function_type) : 0;
	for (std::size_t i = first_parameter;
		i < function_type.parameter_count; ++i)
	{
		AbiFactRecord parameter;
		parameter.set_kind(ABI_FACT_RECORD_FUNCTION);
		parameter.function.kind = ABI_FUNCTION_RECORD_PARAMETER;
		TypeId parameter_type = parameters[i];
		const FunctionTemplateAbiRecipe* parameter_recipe = 0;
		bool pack_expansion = false;
		if (recipe_function)
		{
			const std::size_t explicit_parameter = i - first_parameter;
			const std::size_t fixed = recipe->function_parameter_pack ?
				recipe_function->parameter_count - 1 :
				recipe_function->parameter_count;
			if ((!recipe->function_parameter_pack &&
					explicit_parameter >= fixed) ||
				(recipe->function_parameter_pack && fixed ==
					recipe_function->parameter_count))
				throw std::logic_error(
					"function template ABI parameter shape diverged");
			const std::size_t source_parameter = explicit_parameter < fixed ?
				explicit_parameter : fixed;
			if (source_parameter >= recipe_function->parameter_count)
				throw std::logic_error(
					"function template ABI parameter source is invalid");
			const TypeId source = recipe_parameters[source_parameter];
			pack_expansion = recipe->function_parameter_pack &&
				explicit_parameter >= fixed;
			if (pack_expansion ||
				facts.UsesFunctionTemplateParameter(source, binding, *recipe))
			{
				parameter_type = source;
				parameter_recipe = recipe;
			}
		}
		parameter.function.type = facts.MakeFunctionTemplateType(
			parameter_type, binding, parameter_recipe);
		if (pack_expansion)
		{
			AbiTypeModifier expansion;
			expansion.kind = ABI_TYPE_PACK_EXPANSION;
			parameter.function.type.modifiers.insert(
				parameter.function.type.modifiers.begin(), expansion);
		}
		file.cases[0].records.push_back(parameter);
	}
	if (function_type.variadic)
	{
		AbiFactRecord variadic;
		variadic.set_kind(ABI_FACT_RECORD_FUNCTION);
		variadic.function.kind = ABI_FUNCTION_RECORD_VARIADIC;
		file.cases[0].records.push_back(variadic);
	}
	std::string result = mangle_fact_file(file);
	if (!result.empty() && result[result.size() - 1] == '\n')
		result.resize(result.size() - 1);
	return result;
}

std::string MangleVariable(const pa11::Program& program,
	const pa12_semantic_detail::DumpNode& node)
{
	using namespace abi_mangle;
	using namespace pa11;
	const BindingRecord& binding = program.bindings[node.binding];
	if (binding.language_linkage == LANGUAGE_LINKAGE_C &&
		binding.storage_class != STORAGE_CLASS_STATIC)
		return program.names.Get(binding.name);
	AbiFactFile file;
	file.cases.push_back(AbiFactCase());
	AbiFactBuilder facts(program, file.cases[0]);
	const bool structured_class_owner = binding.member_owner != kNoEntity &&
		program.entities[binding.member_owner].template_argument_count != 0;
	const bool member_variable_template = structured_class_owner &&
		binding.variable_template_specialization;
	AbiFactRecord target;
	target.set_kind(ABI_FACT_RECORD_TARGET);
	target.target.kind = ABI_TARGET_FACT_VARIABLE;
	target.target.function.kind = structured_class_owner ?
		ABI_FUNCTION_TARGET_ENCODING : ABI_FUNCTION_TARGET_PATH;
	target.target.internal_linkage =
		binding.storage_class == STORAGE_CLASS_STATIC &&
		binding.member_owner == kNoEntity &&
		!binding.unnamed_namespace_linkage;
	if (!structured_class_owner)
		target.target.qualified_name = program.names.Get(
			binding.qualified_name != 0 ? binding.qualified_name : node.text);
	file.cases[0].records.push_back(target);
	if (structured_class_owner)
	{
		if (!AppendClassTemplateOwner(
			program, binding, &facts, &file.cases[0], false))
			throw std::logic_error("class template ABI variable owner was lost");
		AbiFactRecord member;
		member.set_kind(ABI_FACT_RECORD_FUNCTION);
		member.function.kind = ABI_FUNCTION_RECORD_NAME_SOURCE;
		member.function.name = program.names.Get(binding.name);
		file.cases[0].records.push_back(member);
	}
	if (member_variable_template && binding.template_argument_count != 0)
	{
		const std::size_t first = binding.template_argument_begin;
		const std::size_t count = binding.template_argument_count;
		if (first > program.template_arguments.size() ||
			count > program.template_arguments.size() - first)
			throw std::logic_error(
				"variable template argument range is invalid during mangling");
		for (std::size_t i = 0; i < count; ++i)
		{
			const std::string argument_id = facts.AddTemplateArgument(first + i);
			AbiFactRecord argument;
			argument.set_kind(ABI_FACT_RECORD_FUNCTION);
			argument.function.kind =
				ABI_FUNCTION_RECORD_FUNCTION_TEMPLATE_ARGUMENT;
			argument.function.argument_refs.push_back(argument_id);
			file.cases[0].records.push_back(argument);
		}
	}
	std::string result = mangle_fact_file(file);
	if (!result.empty() && result[result.size() - 1] == '\n')
		result.resize(result.size() - 1);
	return result;
}

}
}
