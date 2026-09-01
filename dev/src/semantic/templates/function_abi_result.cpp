#include "semantic/analysis/analyzer.h"
#include "support/exceptions.h"

#include <limits>
#include <vector>

namespace cppgm
{
namespace semantic
{
namespace
{

FunctionTemplateResultIdentityAtomKind ResultIdentityKind(std::uint64_t atom)
{
	return static_cast<FunctionTemplateResultIdentityAtomKind>(atom >> 56);
}

std::uint64_t ResultIdentityValue(std::uint64_t atom)
{
	return atom & 0x00ffffffffffffffULL;
}

FunctionTemplateAbiTypeId AppendAbiType(Program* program,
	const FunctionTemplateAbiType& type)
{
	if (program->function_template_abi_types.size() >=
		kNoFunctionTemplateAbiType)
		ThrowSemanticResourceLimit("too many function template ABI type nodes");
	const FunctionTemplateAbiTypeId result =
		static_cast<FunctionTemplateAbiTypeId>(
			program->function_template_abi_types.size());
	program->function_template_abi_types.push_back(type);
	return result;
}

FunctionTemplateAbiExpressionId AppendAbiExpression(Program* program,
	const FunctionTemplateAbiExpression& expression)
{
	if (program->function_template_abi_expressions.size() >=
		kNoFunctionTemplateAbiExpression)
		ThrowSemanticResourceLimit("too many function template ABI expressions");
	const FunctionTemplateAbiExpressionId result =
		static_cast<FunctionTemplateAbiExpressionId>(
			program->function_template_abi_expressions.size());
	program->function_template_abi_expressions.push_back(expression);
	return result;
}

class AbiPublication
{
public:
	explicit AbiPublication(Program* program)
		: program_(program), type_mark_(program->function_template_abi_types.size()),
		  argument_mark_(program->function_template_abi_arguments.size()),
		  expression_mark_(program->function_template_abi_expressions.size()),
		  committed_(false) {}

	~AbiPublication()
	{
		if (committed_) return;
		program_->function_template_abi_types.erase(
			program_->function_template_abi_types.begin() + type_mark_,
			program_->function_template_abi_types.end());
		program_->function_template_abi_arguments.erase(
			program_->function_template_abi_arguments.begin() + argument_mark_,
			program_->function_template_abi_arguments.end());
		program_->function_template_abi_expressions.erase(
			program_->function_template_abi_expressions.begin() + expression_mark_,
			program_->function_template_abi_expressions.end());
	}

	void Commit() { committed_ = true; }

private:
	AbiPublication(const AbiPublication&);
	AbiPublication& operator=(const AbiPublication&);

	Program* program_;
	std::size_t type_mark_, argument_mark_, expression_mark_;
	bool committed_;
};

NodeId FindDescendant(const SyntaxArena& arena, NodeId root, const char* tag)
{
	if (root == kNoNode) return kNoNode;
	std::vector<NodeId> pending(1, root);
	while (!pending.empty())
	{
		const NodeId node = pending.back();
		pending.pop_back();
		if (arena.IsTag(node, tag)) return node;
		for (std::uint32_t edge = arena.FirstEdge(node); edge != kNoEdge;
			edge = arena.NextEdge(edge))
			pending.push_back(arena.EdgeChild(edge));
	}
	return kNoNode;
}

struct ParsedComponent
{
	NameId name;
	EntityId entity;
	std::vector<FunctionTemplateAbiArgument> arguments;

	ParsedComponent() : name(0), entity(kNoEntity) {}
};

class AbiIdentityReader
{
public:
	AbiIdentityReader(Program* program,
		const std::deque<ClassTemplatePattern>& class_templates,
		const std::vector<std::uint32_t>& class_template_by_entity,
		const std::vector<TemplateParameter>& parameters,
		const std::vector<std::uint64_t>& atoms)
		: program_(program), class_templates_(class_templates),
		  class_template_by_entity_(class_template_by_entity),
		  parameters_(parameters), atoms_(atoms),
		  position_(0) {}

	FunctionTemplateAbiTypeId ParseType()
	{
		FunctionTemplateAbiTypeId root = ParsePrimaryType();
		if (root == kNoFunctionTemplateAbiType) return root;
		while (IsNode("abstract-declarator"))
			if (!ParseAbstractDeclarator(&root))
				return kNoFunctionTemplateAbiType;
		return root;
	}

	FunctionTemplateAbiExpressionId ParseExpression()
	{
		if (position_ >= atoms_.size())
			return kNoFunctionTemplateAbiExpression;
		const FunctionTemplateResultIdentityAtomKind kind =
			ResultIdentityKind(atoms_[position_]);
		if (kind == FUNCTION_TEMPLATE_RESULT_PARAMETER)
		{
			const std::uint64_t parameter = ResultIdentityValue(atoms_[position_++]);
			if (parameter >= kNoTemplateParameter)
				return kNoFunctionTemplateAbiExpression;
			return AppendAbiExpression(program_, FunctionTemplateAbiExpression(
				FUNCTION_TEMPLATE_ABI_EXPRESSION_TEMPLATE_PARAMETER,
				kNoFunctionTemplateAbiExpression,
				kNoFunctionTemplateAbiExpression, kNoFunctionTemplateAbiType,
				0, static_cast<std::uint32_t>(parameter)));
		}
		if (kind == FUNCTION_TEMPLATE_RESULT_QUALIFIED_BEGIN)
		{
			FunctionTemplateAbiExpressionId expression =
				kNoFunctionTemplateAbiExpression;
			(void)ParseQualifiedType(true, &expression);
			return expression;
		}
		if (!IsNode("parenthesized-expression") &&
			!IsNode("id-expression") && !IsNode("unary-expression") &&
			!IsNode("call-expression"))
			return kNoFunctionTemplateAbiExpression;
		NameId payload = 0;
		const NameId tag = static_cast<NameId>(
			ResultIdentityValue(atoms_[position_]));
		if (!BeginNode(program_->names.Get(tag).c_str(), &payload))
			return kNoFunctionTemplateAbiExpression;
		const std::string& node = program_->names.Get(tag);
		FunctionTemplateAbiExpressionId expression =
			kNoFunctionTemplateAbiExpression;
		if (node == "id-expression")
			expression = position_ < atoms_.size() &&
				ResultIdentityKind(atoms_[position_]) ==
					FUNCTION_TEMPLATE_RESULT_QUALIFIED_BEGIN ?
				ParseTemplateIdExpression() : ParseExpression();
		else if (node == "parenthesized-expression")
			expression = ParseExpression();
		else if (node == "call-expression")
		{
			const FunctionTemplateAbiExpressionId callee = ParseExpression();
			if (callee != kNoFunctionTemplateAbiExpression &&
				ParseEmptyNode("argument-list"))
				expression = AppendAbiExpression(program_,
					FunctionTemplateAbiExpression(
						FUNCTION_TEMPLATE_ABI_EXPRESSION_CALL, callee));
		}
		else if (program_->names.Get(payload) == "*")
		{
			const FunctionTemplateAbiExpressionId operand = ParseExpression();
			if (operand != kNoFunctionTemplateAbiExpression)
				expression = AppendAbiExpression(program_,
					FunctionTemplateAbiExpression(
						FUNCTION_TEMPLATE_ABI_EXPRESSION_UNARY, operand,
						kNoFunctionTemplateAbiExpression,
						kNoFunctionTemplateAbiType, 0,
						kNoTemplateParameter, OPERATOR_STAR));
		}
		if (expression == kNoFunctionTemplateAbiExpression || !EndNode())
			return kNoFunctionTemplateAbiExpression;
		return expression;
	}

	bool Complete() const { return position_ == atoms_.size(); }

private:
	bool IsNode(const char* tag) const
	{
		return position_ < atoms_.size() &&
			ResultIdentityKind(atoms_[position_]) ==
				FUNCTION_TEMPLATE_RESULT_NODE_BEGIN &&
			program_->names.Get(static_cast<NameId>(
				ResultIdentityValue(atoms_[position_]))) == tag;
	}

	bool BeginNode(const char* tag, NameId* payload)
	{
		if (!IsNode(tag) || ++position_ >= atoms_.size() ||
			ResultIdentityKind(atoms_[position_]) !=
				FUNCTION_TEMPLATE_RESULT_NODE_PAYLOAD)
			return false;
		*payload = static_cast<NameId>(ResultIdentityValue(atoms_[position_++]));
		return true;
	}

	bool EndNode()
	{
		if (position_ >= atoms_.size() ||
			ResultIdentityKind(atoms_[position_]) !=
				FUNCTION_TEMPLATE_RESULT_NODE_END) return false;
		++position_;
		return true;
	}

	bool ParseEmptyNode(const char* tag)
	{
		NameId payload = 0;
		return BeginNode(tag, &payload) && EndNode();
	}

	FunctionTemplateAbiExpressionId ParseTemplateIdExpression()
	{
		if (position_ >= atoms_.size() ||
			ResultIdentityKind(atoms_[position_++]) !=
				FUNCTION_TEMPLATE_RESULT_QUALIFIED_BEGIN)
			return kNoFunctionTemplateAbiExpression;
		ParsedComponent component;
		if (!ParseComponent(&component) || component.arguments.empty() ||
			position_ >= atoms_.size() ||
			ResultIdentityKind(atoms_[position_++]) !=
				FUNCTION_TEMPLATE_RESULT_QUALIFIED_END ||
			component.arguments.size() >
				std::numeric_limits<std::uint32_t>::max() ||
			program_->function_template_abi_arguments.size() >
				std::numeric_limits<std::uint32_t>::max() -
					component.arguments.size())
			return kNoFunctionTemplateAbiExpression;
		const std::uint32_t begin = static_cast<std::uint32_t>(
			program_->function_template_abi_arguments.size());
		program_->function_template_abi_arguments.insert(
			program_->function_template_abi_arguments.end(),
			component.arguments.begin(), component.arguments.end());
		return AppendAbiExpression(program_, FunctionTemplateAbiExpression(
			FUNCTION_TEMPLATE_ABI_EXPRESSION_TEMPLATE_ID,
			kNoFunctionTemplateAbiExpression,
			kNoFunctionTemplateAbiExpression, kNoFunctionTemplateAbiType,
			component.name, kNoTemplateParameter, OPERATOR_NONE, false, begin,
			static_cast<std::uint32_t>(component.arguments.size())));
	}

	FunctionTemplateAbiTypeId ParsePrimaryType()
	{
		if (position_ >= atoms_.size()) return kNoFunctionTemplateAbiType;
		if (IsNode("builtin-transform-type"))
		{
			NameId name = 0;
			if (!BeginNode("builtin-transform-type", &name))
				return kNoFunctionTemplateAbiType;
			const FunctionTemplateAbiTypeId operand = ParseType();
			if (name == 0 || operand == kNoFunctionTemplateAbiType || !EndNode())
				return kNoFunctionTemplateAbiType;
			return AppendAbiType(program_, FunctionTemplateAbiType(
				FUNCTION_TEMPLATE_ABI_TYPE_BUILTIN_TRANSFORM, operand, name));
		}
		const FunctionTemplateResultIdentityAtomKind kind =
			ResultIdentityKind(atoms_[position_]);
		if (kind == FUNCTION_TEMPLATE_RESULT_PARAMETER)
		{
			const std::uint64_t parameter = ResultIdentityValue(atoms_[position_++]);
			if (parameter >= kNoTemplateParameter)
				return kNoFunctionTemplateAbiType;
			return AppendAbiType(program_, FunctionTemplateAbiType(
				FUNCTION_TEMPLATE_ABI_TYPE_PARAMETER, kNoFunctionTemplateAbiType,
				0, 0, static_cast<std::uint32_t>(parameter)));
		}
		if (kind == FUNCTION_TEMPLATE_RESULT_TYPE)
		{
			const std::uint64_t type = ResultIdentityValue(atoms_[position_++]);
			if (type >= program_->types.Size()) return kNoFunctionTemplateAbiType;
			return AppendAbiType(program_, FunctionTemplateAbiType(
				FUNCTION_TEMPLATE_ABI_TYPE_CONCRETE,
				kNoFunctionTemplateAbiType, 0, 0, kNoTemplateParameter, 0,
				static_cast<TypeId>(type)));
		}
		if (kind == FUNCTION_TEMPLATE_RESULT_QUALIFIED_BEGIN)
			return ParseQualifiedType(false, 0);
		if (!IsNode("decltype-specifier"))
			return kNoFunctionTemplateAbiType;
		NameId payload = 0;
		if (!BeginNode("decltype-specifier", &payload))
			return kNoFunctionTemplateAbiType;
		const FunctionTemplateAbiExpressionId expression = ParseExpression();
		if (expression == kNoFunctionTemplateAbiExpression || !EndNode())
			return kNoFunctionTemplateAbiType;
		return AppendAbiType(program_, FunctionTemplateAbiType(
			FUNCTION_TEMPLATE_ABI_TYPE_DECLTYPE, kNoFunctionTemplateAbiType,
			0, 0, kNoTemplateParameter, 0, kNoType, kNoEntity, 0, 0,
			expression));
	}

	bool ParseAbstractDeclarator(FunctionTemplateAbiTypeId* root)
	{
		NameId payload = 0;
		if (!BeginNode("abstract-declarator", &payload)) return false;
		while (position_ < atoms_.size() &&
			ResultIdentityKind(atoms_[position_]) !=
				FUNCTION_TEMPLATE_RESULT_NODE_END)
		{
			if (!BeginNode("ptr-operator", &payload)) return false;
			FunctionTemplateAbiTypeKind kind = FUNCTION_TEMPLATE_ABI_TYPE_POINTER;
			const std::string& op = program_->names.Get(payload);
			if (op == "&") kind = FUNCTION_TEMPLATE_ABI_TYPE_LVALUE_REFERENCE;
			else if (op == "&&") kind = FUNCTION_TEMPLATE_ABI_TYPE_RVALUE_REFERENCE;
			else if (op != "*") return false;
			if (!EndNode()) return false;
			*root = AppendAbiType(program_, FunctionTemplateAbiType(kind, *root));
		}
		return EndNode();
	}

	bool IsTypeParameterName(NameId name) const
	{
		for (std::size_t parameter = 0; parameter < parameters_.size(); ++parameter)
			if (parameters_[parameter].kind == TEMPLATE_ARGUMENT_TYPE &&
				parameters_[parameter].name == name) return true;
		return false;
	}

	TemplateArgumentKind ArgumentKind(EntityId entity, std::size_t ordinal) const
	{
		if (entity >= class_template_by_entity_.size())
			return TEMPLATE_ARGUMENT_TYPE;
		const std::uint32_t index = class_template_by_entity_[entity];
		if (index == kNoDumpEdge || index >= class_templates_.size() ||
			class_templates_[index].parameters.empty())
			return TEMPLATE_ARGUMENT_TYPE;
		const std::vector<TemplateParameter>& parameters =
			class_templates_[index].parameters;
		const std::size_t parameter = ordinal < parameters.size() ? ordinal :
			parameters.size() - 1;
		return parameters[parameter].kind;
	}

	EntityId EntityFromMarker(FunctionTemplateResultIdentityAtomKind kind,
		std::uint64_t value) const
	{
		if (kind == FUNCTION_TEMPLATE_RESULT_ENTITY)
			return value < program_->entities.size() ?
				static_cast<EntityId>(value) : kNoEntity;
		if (kind != FUNCTION_TEMPLATE_RESULT_DECLARATION ||
			value >= program_->bindings.size()) return kNoEntity;
		const BindingRecord& binding = program_->bindings[
			program_->bindings[static_cast<BindingId>(value)].canonical];
		if (binding.type == kNoType) return kNoEntity;
		const TypeRecord& type = program_->types.Get(
			program_->types.RemoveTopCv(binding.type));
		return type.kind == TYPE_NAMED ? type.entity : kNoEntity;
	}

	bool ParseComponent(ParsedComponent* component)
	{
		if (position_ >= atoms_.size() ||
			ResultIdentityKind(atoms_[position_]) !=
				FUNCTION_TEMPLATE_RESULT_COMPONENT)
			return false;
		component->name = static_cast<NameId>(
			ResultIdentityValue(atoms_[position_++]));
		if (position_ < atoms_.size())
		{
			const FunctionTemplateResultIdentityAtomKind marker =
				ResultIdentityKind(atoms_[position_]);
			if (marker == FUNCTION_TEMPLATE_RESULT_DECLARATION ||
				marker == FUNCTION_TEMPLATE_RESULT_ENTITY)
			{
				component->entity = EntityFromMarker(
					marker, ResultIdentityValue(atoms_[position_++]));
			}
		}
		if (position_ >= atoms_.size() ||
			ResultIdentityKind(atoms_[position_]) !=
				FUNCTION_TEMPLATE_RESULT_ARGUMENTS_BEGIN) return true;
		++position_;
		for (std::size_t argument = 0; position_ < atoms_.size() &&
			ResultIdentityKind(atoms_[position_]) !=
				FUNCTION_TEMPLATE_RESULT_ARGUMENTS_END; ++argument)
		{
			if (ResultIdentityKind(atoms_[position_]) !=
				FUNCTION_TEMPLATE_RESULT_ARGUMENT_BEGIN) return false;
			++position_;
			bool pack_expansion = position_ < atoms_.size() &&
				ResultIdentityKind(atoms_[position_]) ==
					FUNCTION_TEMPLATE_RESULT_PACK_EXPANSION;
			if (pack_expansion) ++position_;
			if (ArgumentKind(component->entity, argument) ==
				TEMPLATE_ARGUMENT_INTEGRAL)
			{
				const FunctionTemplateAbiExpressionId expression = ParseExpression();
				if (expression == kNoFunctionTemplateAbiExpression) return false;
				component->arguments.push_back(FunctionTemplateAbiArgument(
					FUNCTION_TEMPLATE_ABI_ARGUMENT_EXPRESSION,
					kNoFunctionTemplateAbiType, expression,
					pack_expansion));
			}
			else
			{
				const FunctionTemplateAbiTypeId type = ParseType();
				if (type == kNoFunctionTemplateAbiType) return false;
				component->arguments.push_back(FunctionTemplateAbiArgument(
					FUNCTION_TEMPLATE_ABI_ARGUMENT_TYPE, type,
					kNoFunctionTemplateAbiExpression, pack_expansion));
			}
			if (position_ < atoms_.size() && ResultIdentityKind(atoms_[position_]) ==
				FUNCTION_TEMPLATE_RESULT_PACK_EXPANSION)
			{
				component->arguments.back().pack_expansion = true;
				++position_;
			}
			if (position_ >= atoms_.size() ||
				ResultIdentityKind(atoms_[position_]) !=
					FUNCTION_TEMPLATE_RESULT_ARGUMENT_END) return false;
			++position_;
		}
		if (position_ >= atoms_.size() ||
			ResultIdentityKind(atoms_[position_]) !=
				FUNCTION_TEMPLATE_RESULT_ARGUMENTS_END) return false;
		++position_;
		return true;
	}

	FunctionTemplateAbiTypeId ComponentType(
		const ParsedComponent& component, FunctionTemplateAbiTypeId owner)
	{
		if (component.arguments.empty())
		{
			if (owner != kNoFunctionTemplateAbiType)
				return AppendAbiType(program_, FunctionTemplateAbiType(
					FUNCTION_TEMPLATE_ABI_TYPE_MEMBER, owner, component.name));
			for (std::size_t parameter = 0;
				parameter < parameters_.size(); ++parameter)
				if (parameters_[parameter].kind == TEMPLATE_ARGUMENT_TYPE &&
					parameters_[parameter].name == component.name)
					return AppendAbiType(program_, FunctionTemplateAbiType(
						FUNCTION_TEMPLATE_ABI_TYPE_PARAMETER,
						kNoFunctionTemplateAbiType, 0, 0,
						static_cast<std::uint32_t>(parameter)));
			if (component.entity == kNoEntity ||
				component.entity >= program_->entities.size())
				return kNoFunctionTemplateAbiType;
			return AppendAbiType(program_, FunctionTemplateAbiType(
				FUNCTION_TEMPLATE_ABI_TYPE_CONCRETE, kNoFunctionTemplateAbiType,
				0, 0, kNoTemplateParameter, 0,
				program_->entities[component.entity].type));
		}
		if (component.entity == kNoEntity ||
			component.arguments.size() >
				std::numeric_limits<std::uint32_t>::max() ||
			program_->function_template_abi_arguments.size() >
				std::numeric_limits<std::uint32_t>::max() -
				component.arguments.size())
			return kNoFunctionTemplateAbiType;
		const std::uint32_t begin = static_cast<std::uint32_t>(
			program_->function_template_abi_arguments.size());
		program_->function_template_abi_arguments.insert(
			program_->function_template_abi_arguments.end(),
			component.arguments.begin(), component.arguments.end());
		if (owner == kNoFunctionTemplateAbiType &&
			component.entity < class_template_by_entity_.size())
		{
			const std::uint32_t index =
				class_template_by_entity_[component.entity];
			if (index != kNoDumpEdge && index < class_templates_.size() &&
				class_templates_[index].template_parameter_proxy)
				return AppendAbiType(program_, FunctionTemplateAbiType(
					FUNCTION_TEMPLATE_ABI_TYPE_TEMPLATE_PARAMETER_SPECIALIZATION,
					kNoFunctionTemplateAbiType, component.name, 0,
					class_templates_[index].template_parameter_ordinal, 0,
					kNoType, component.entity, begin,
					static_cast<std::uint32_t>(component.arguments.size())));
		}
		return AppendAbiType(program_, FunctionTemplateAbiType(
			FUNCTION_TEMPLATE_ABI_TYPE_TEMPLATE_SPECIALIZATION, owner,
			component.name, 0, kNoTemplateParameter, 0, kNoType,
			component.entity, begin,
			static_cast<std::uint32_t>(component.arguments.size())));
	}

	FunctionTemplateAbiTypeId ParseQualifiedType(bool value_terminal,
		FunctionTemplateAbiExpressionId* expression)
	{
		if (ResultIdentityKind(atoms_[position_++]) !=
			FUNCTION_TEMPLATE_RESULT_QUALIFIED_BEGIN)
			return kNoFunctionTemplateAbiType;
		FunctionTemplateAbiTypeId root = kNoFunctionTemplateAbiType;
		while (position_ < atoms_.size() &&
			ResultIdentityKind(atoms_[position_]) !=
				FUNCTION_TEMPLATE_RESULT_QUALIFIED_END)
		{
			ParsedComponent component;
			if (!ParseComponent(&component)) return kNoFunctionTemplateAbiType;
			const bool terminal = position_ < atoms_.size() &&
				ResultIdentityKind(atoms_[position_]) ==
					FUNCTION_TEMPLATE_RESULT_QUALIFIED_END;
			if (value_terminal && terminal)
			{
				if (root == kNoFunctionTemplateAbiType ||
					!component.arguments.empty())
					return kNoFunctionTemplateAbiType;
				*expression = AppendAbiExpression(program_,
					FunctionTemplateAbiExpression(
						FUNCTION_TEMPLATE_ABI_EXPRESSION_TYPE_MEMBER,
						kNoFunctionTemplateAbiExpression,
						kNoFunctionTemplateAbiExpression, root,
						component.name));
			}
			else
			{
				// Namespace qualifiers have no type entity.  Their full path is
				// carried by the first following type entity and reconstructed
				// from that entity's canonical owner during ABI lowering.
				if (root == kNoFunctionTemplateAbiType &&
					component.entity == kNoEntity &&
					component.arguments.empty() && !terminal &&
					!IsTypeParameterName(component.name))
					continue;
				root = ComponentType(component, root);
				if (root == kNoFunctionTemplateAbiType)
					return kNoFunctionTemplateAbiType;
			}
		}
		if (position_ >= atoms_.size() ||
			ResultIdentityKind(atoms_[position_++]) !=
				FUNCTION_TEMPLATE_RESULT_QUALIFIED_END)
			return kNoFunctionTemplateAbiType;
		return root;
	}

	Program* program_;
	const std::deque<ClassTemplatePattern>& class_templates_;
	const std::vector<std::uint32_t>& class_template_by_entity_;
	const std::vector<TemplateParameter>& parameters_;
	const std::vector<std::uint64_t>& atoms_;
	std::size_t position_;
};

FunctionTemplateAbiTypeId ApplyTypeModifiers(Program* program,
	TypeId shape, FunctionTemplateAbiTypeId root)
{
	if (root == kNoFunctionTemplateAbiType || shape == kNoType) return root;
	struct Modifier
	{
		FunctionTemplateAbiTypeKind kind;
		std::uint64_t bound;
		std::uint32_t parameter;
		std::uint8_t cv;
	};
	std::vector<Modifier> modifiers;
	TypeId source = shape;
	for (;;)
	{
		const TypeRecord& record = program->types.Get(source);
		Modifier modifier = { FUNCTION_TEMPLATE_ABI_TYPE_POINTER, 0,
			kNoTemplateParameter, 0 };
		if (record.kind == TYPE_QUALIFIED)
		{
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_QUALIFIED;
			modifier.cv = record.cv;
		}
		else if (record.kind == TYPE_POINTER)
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_POINTER;
		else if (record.kind == TYPE_LVALUE_REFERENCE)
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_LVALUE_REFERENCE;
		else if (record.kind == TYPE_RVALUE_REFERENCE)
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_RVALUE_REFERENCE;
		else if (record.kind == TYPE_ARRAY)
		{
			modifier.kind = FUNCTION_TEMPLATE_ABI_TYPE_ARRAY;
			modifier.bound = record.bound;
			modifier.parameter = record.dependent_bound_parameter;
		}
		else break;
		modifiers.push_back(modifier);
		source = record.child;
	}
	for (std::vector<Modifier>::const_reverse_iterator modifier =
		modifiers.rbegin(); modifier != modifiers.rend(); ++modifier)
		root = AppendAbiType(program, FunctionTemplateAbiType(modifier->kind,
			root, 0, modifier->bound, modifier->parameter, modifier->cv));
	return root;
}

FunctionTemplateAbiExpressionId PublishSyntaxExpression(Program* program,
	const SyntaxArena& arena, NodeId syntax,
	const std::vector<ParameterInfo>& parameters)
{
	if (syntax == kNoNode) return kNoFunctionTemplateAbiExpression;
	if (arena.IsTag(syntax, ::cppgm::syntax::STAG_PARENTHESIZED_EXPRESSION))
	{
		const std::uint32_t edge = arena.FirstEdge(syntax);
		return edge == kNoEdge ? kNoFunctionTemplateAbiExpression :
			PublishSyntaxExpression(
				program, arena, arena.EdgeChild(edge), parameters);
	}
	if (arena.IsTag(syntax, ::cppgm::syntax::STAG_ID_EXPRESSION))
	{
		const NameId name = arena.SemanticPayloadId(syntax);
		for (std::size_t i = 0; i < parameters.size(); ++i)
			if (parameters[i].name == name)
				return AppendAbiExpression(program,
					FunctionTemplateAbiExpression(
						FUNCTION_TEMPLATE_ABI_EXPRESSION_FUNCTION_PARAMETER,
						kNoFunctionTemplateAbiExpression,
						kNoFunctionTemplateAbiExpression,
						kNoFunctionTemplateAbiType, 0,
						static_cast<std::uint32_t>(i)));
		return kNoFunctionTemplateAbiExpression;
	}
	const std::uint32_t first = arena.FirstEdge(syntax);
	if (first == kNoEdge) return kNoFunctionTemplateAbiExpression;
	if (arena.IsTag(syntax, ::cppgm::syntax::STAG_MEMBER_EXPRESSION))
	{
		const std::uint32_t second = arena.NextEdge(first);
		if (second == kNoEdge) return kNoFunctionTemplateAbiExpression;
		const FunctionTemplateAbiExpressionId object = PublishSyntaxExpression(
			program, arena, arena.EdgeChild(first), parameters);
		if (object == kNoFunctionTemplateAbiExpression)
			return kNoFunctionTemplateAbiExpression;
		return AppendAbiExpression(program, FunctionTemplateAbiExpression(
			FUNCTION_TEMPLATE_ABI_EXPRESSION_OBJECT_MEMBER, object,
			kNoFunctionTemplateAbiExpression, kNoFunctionTemplateAbiType,
			arena.SemanticPayloadId(arena.EdgeChild(second)),
			kNoTemplateParameter, OPERATOR_NONE,
			ClassifyOperationSpelling(
				arena.SemanticPayload(syntax)) == OP_ARROW));
	}
	if (arena.IsTag(syntax, ::cppgm::syntax::STAG_CALL_EXPRESSION))
	{
		const FunctionTemplateAbiExpressionId callee = PublishSyntaxExpression(
			program, arena, arena.EdgeChild(first), parameters);
		if (callee == kNoFunctionTemplateAbiExpression)
			return kNoFunctionTemplateAbiExpression;
		const std::uint32_t argument_edge = arena.NextEdge(first);
		if (argument_edge != kNoEdge &&
			arena.FirstEdge(arena.EdgeChild(argument_edge)) != kNoEdge)
			return kNoFunctionTemplateAbiExpression;
		return AppendAbiExpression(program, FunctionTemplateAbiExpression(
			FUNCTION_TEMPLATE_ABI_EXPRESSION_CALL, callee));
	}
	if (arena.IsTag(syntax, ::cppgm::syntax::STAG_BINARY_EXPRESSION) &&
		ClassifyOperationSpelling(arena.SemanticPayload(syntax)) == OP_MINUS)
	{
		const std::uint32_t second = arena.NextEdge(first);
		if (second == kNoEdge) return kNoFunctionTemplateAbiExpression;
		const FunctionTemplateAbiExpressionId left = PublishSyntaxExpression(
			program, arena, arena.EdgeChild(first), parameters);
		const FunctionTemplateAbiExpressionId right = PublishSyntaxExpression(
			program, arena, arena.EdgeChild(second), parameters);
		if (left == kNoFunctionTemplateAbiExpression ||
			right == kNoFunctionTemplateAbiExpression)
			return kNoFunctionTemplateAbiExpression;
		return AppendAbiExpression(program, FunctionTemplateAbiExpression(
			FUNCTION_TEMPLATE_ABI_EXPRESSION_BINARY, left, right,
			kNoFunctionTemplateAbiType, 0, kNoTemplateParameter,
			OPERATOR_MINUS));
	}
	return kNoFunctionTemplateAbiExpression;
}

bool HasRetainedParameterRoot(const Program& program,
	const FunctionTemplatePattern& pattern,
	const std::vector<std::uint64_t>& atoms)
{
	if (!atoms.empty() && ResultIdentityKind(atoms[0]) ==
		FUNCTION_TEMPLATE_RESULT_PARAMETER) return true;
	if (atoms.size() < 3 || ResultIdentityKind(atoms[0]) !=
		FUNCTION_TEMPLATE_RESULT_QUALIFIED_BEGIN ||
		ResultIdentityKind(atoms[1]) != FUNCTION_TEMPLATE_RESULT_COMPONENT)
		return false;
	std::size_t terminal = atoms.size();
	bool dependent = false;
	for (std::size_t atom = 1; atom < atoms.size(); ++atom)
	{
		const FunctionTemplateResultIdentityAtomKind kind =
			ResultIdentityKind(atoms[atom]);
		if (kind == FUNCTION_TEMPLATE_RESULT_COMPONENT) terminal = atom;
		else if (kind == FUNCTION_TEMPLATE_RESULT_PARAMETER) dependent = true;
	}
	if (dependent && terminal < atoms.size())
	{
		bool terminal_resolved = false;
		for (std::size_t atom = terminal + 1; atom < atoms.size(); ++atom)
		{
			const FunctionTemplateResultIdentityAtomKind kind =
				ResultIdentityKind(atoms[atom]);
			if (kind == FUNCTION_TEMPLATE_RESULT_DECLARATION ||
				kind == FUNCTION_TEMPLATE_RESULT_ENTITY ||
				kind == FUNCTION_TEMPLATE_RESULT_ARGUMENTS_BEGIN)
				terminal_resolved = true;
			if (kind == FUNCTION_TEMPLATE_RESULT_QUALIFIED_END) break;
		}
		// A terminal dependent member has no canonical semantic type of its
		// own.  Retain its owner/member source DAG; an ordinary class-template
		// specialization remains on the established semantic TypeId path so
		// standard substitutions and cross-parameter slots stay shared.
		if (!terminal_resolved) return true;
	}
	const NameId source_root = static_cast<NameId>(
		ResultIdentityValue(atoms[1]));
	for (std::size_t parameter = 0;
		parameter < pattern.parameters.size(); ++parameter)
		if (pattern.parameters[parameter].kind == TEMPLATE_ARGUMENT_TYPE &&
			pattern.parameters[parameter].name == source_root) return true;
	EntityId entity = kNoEntity;
	const FunctionTemplateResultIdentityAtomKind marker =
		ResultIdentityKind(atoms[2]);
	const std::uint64_t value = ResultIdentityValue(atoms[2]);
	if (marker == FUNCTION_TEMPLATE_RESULT_ENTITY)
		entity = value < program.entities.size() ?
			static_cast<EntityId>(value) : kNoEntity;
	else if (marker == FUNCTION_TEMPLATE_RESULT_DECLARATION &&
		value < program.bindings.size())
	{
		const BindingRecord& binding = program.bindings[
			program.bindings[static_cast<BindingId>(value)].canonical];
		if (binding.type != kNoType)
		{
			const TypeRecord& type = program.types.Get(
				program.types.RemoveTopCv(binding.type));
			if (type.kind == TYPE_NAMED) entity = type.entity;
		}
	}
	if (entity == kNoEntity || entity >= program.entities.size()) return false;
	return program.entities[entity].identity_name != source_root;
}

}

void Analyzer::PublishFunctionTemplateResultAbiType(
	FunctionTemplatePattern* pattern, const DeclaratorInfo& declarator)
{
	if (!pattern) return;
	pattern->abi_result_type = kNoFunctionTemplateAbiType;
	pattern->abi_template_parameter_types.assign(
		pattern->parameters.size(), kNoFunctionTemplateAbiType);
	pattern->abi_function_parameter_types.assign(
		declarator.parameters.size(), kNoFunctionTemplateAbiType);
	for (std::size_t p = 0; p < pattern->parameters.size(); ++p)
	{
		if (pattern->parameters[p].kind != TEMPLATE_ARGUMENT_INTEGRAL)
			continue;
		const NodeId root = FindDescendant(
			*arena_, pattern->parameters[p].specifiers, "structured-type-name");
		if (root == kNoNode) continue;
		FunctionTemplatePattern probe;
		probe.parameters = pattern->parameters;
		probe.lexical_scope = pattern->lexical_scope;
		probe.result_root_structure = root;
		probe.function_parameter_names.reserve(declarator.parameters.size());
		for (std::size_t parameter = 0;
			parameter < declarator.parameters.size(); ++parameter)
			probe.function_parameter_names.push_back(
				declarator.parameters[parameter].name);
		InternExpandedFunctionTemplateResult(&probe);
		if (probe.expanded_result_identity ==
			kNoFunctionTemplateResultIdentity) continue;
		std::vector<std::uint64_t> atoms;
		function_template_result_identities_.CopyAtoms(
			probe.expanded_result_identity, &atoms);
		AbiPublication publication(program_);
		AbiIdentityReader reader(program_, class_templates_,
			class_template_pattern_by_entity_, pattern->parameters, atoms);
		const FunctionTemplateAbiTypeId type = reader.ParseType();
		if (type != kNoFunctionTemplateAbiType && reader.Complete())
		{
			pattern->abi_template_parameter_types[p] = type;
			publication.Commit();
		}
	}
	for (std::size_t p = 0; p < declarator.parameters.size(); ++p)
	{
		const NodeId root = FindDescendant(*arena_,
			declarator.parameters[p].type_syntax,
			"structured-type-name");
		if (root == kNoNode) continue;
		const NamePath path = StructuredNamePath(root);
		if (path.Empty()) continue;
		const LookupResult marker = LookupPath(
			pattern->lexical_scope, path, LOOKUP_TYPE);
		if (!declarator.parameters[p].nondeduced &&
			FindAliasTemplateIndex(marker, path.Last()) >=
				alias_templates_.size()) continue;
		FunctionTemplatePattern probe;
		probe.parameters = pattern->parameters;
		probe.lexical_scope = pattern->lexical_scope;
		probe.result_root_structure = root;
		probe.function_parameter_names.reserve(declarator.parameters.size());
		for (std::size_t parameter = 0;
			parameter < declarator.parameters.size(); ++parameter)
			probe.function_parameter_names.push_back(
				declarator.parameters[parameter].name);
		InternExpandedFunctionTemplateResult(&probe);
		if (probe.expanded_result_identity ==
			kNoFunctionTemplateResultIdentity) continue;
		if (!declarator.parameters[p].nondeduced &&
			!probe.expanded_result_has_alias) continue;
		std::vector<std::uint64_t> atoms;
		function_template_result_identities_.CopyAtoms(
			probe.expanded_result_identity, &atoms);
		AbiPublication publication(program_);
		AbiIdentityReader reader(program_, class_templates_,
			class_template_pattern_by_entity_, pattern->parameters, atoms);
		const FunctionTemplateAbiTypeId type = reader.ParseType();
		if (type != kNoFunctionTemplateAbiType && reader.Complete())
		{
			pattern->abi_function_parameter_types[p] = ApplyTypeModifiers(
				program_, declarator.parameters[p].function_type, type);
			publication.Commit();
		}
	}

	if (pattern->expanded_result_identity !=
		kNoFunctionTemplateResultIdentity)
	{
		std::vector<std::uint64_t> atoms;
		function_template_result_identities_.CopyAtoms(
			pattern->expanded_result_identity, &atoms);
		if (pattern->expanded_result_has_alias ||
			HasRetainedParameterRoot(*program_, *pattern, atoms))
		{
			AbiPublication publication(program_);
			AbiIdentityReader reader(program_, class_templates_,
				class_template_pattern_by_entity_, pattern->parameters, atoms);
			const FunctionTemplateAbiTypeId root = reader.ParseType();
			if (root != kNoFunctionTemplateAbiType && reader.Complete())
			{
				pattern->abi_result_type = ApplyTypeModifiers(program_,
					program_->types.Get(pattern->shape_type).child, root);
				publication.Commit();
			}
		}
	}
	if (pattern->abi_result_type != kNoFunctionTemplateAbiType) return;
	const NodeId decltype_specifier = FindDescendant(
		*arena_, pattern->trailing_return_syntax, "decltype-specifier");
	if (decltype_specifier == kNoNode) return;
	const std::uint32_t edge = arena_->FirstEdge(decltype_specifier);
	if (edge == kNoEdge) return;
	AbiPublication publication(program_);
	const FunctionTemplateAbiExpressionId expression = PublishSyntaxExpression(
		program_, *arena_, arena_->EdgeChild(edge), declarator.parameters);
	if (expression == kNoFunctionTemplateAbiExpression) return;
	const FunctionTemplateAbiTypeId root = AppendAbiType(program_,
		FunctionTemplateAbiType(FUNCTION_TEMPLATE_ABI_TYPE_DECLTYPE,
			kNoFunctionTemplateAbiType, 0, 0, kNoTemplateParameter, 0,
			kNoType, kNoEntity, 0, 0, expression));
	pattern->abi_result_type = ApplyTypeModifiers(program_,
		program_->types.Get(pattern->shape_type).child, root);
	publication.Commit();
}

}
}
