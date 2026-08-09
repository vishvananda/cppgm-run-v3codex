#include "pa12_semantic_detail.h"

#include <cctype>
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
		return top.bound != 0 && IsConstexprLiteralType(top.child);
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
				constructor.special_member != SPECIAL_MEMBER_COPY_CONSTRUCTOR &&
				constructor.special_member != SPECIAL_MEMBER_MOVE_CONSTRUCTOR)
			{
				literal_constructor = true;
				break;
			}
		}
	}
	if (!literal_constructor) return false;
	if (entity.direct_base != kNoEntity &&
		!IsConstexprLiteralType(program_->entities[entity.direct_base].type))
		return false;
	if (top.entity < entity_data_members_.size())
		for (std::size_t i = 0; i < entity_data_members_[top.entity].size(); ++i)
		{
			const BindingRecord& member = program_->bindings[
				entity_data_members_[top.entity][i]];
			if (!IsConstexprLiteralType(member.type)) return false;
		}
	return true;
}

void SemanticAnalyzer::FindLocalStaticSource(NameId name,
	std::uint32_t* line, std::uint32_t* column) const
{
	*line = 0;
	*column = 0;
	if (!source_text_) return;
	const std::string& source = *source_text_;
	const std::string& spelling = program_->names.Get(name);
	if (spelling.empty()) return;
	std::size_t position = 0;
	while ((position = source.find(spelling, position)) != std::string::npos)
	{
		const std::size_t after = position + spelling.size();
		const bool left_identifier = position != 0 &&
			(std::isalnum(static_cast<unsigned char>(source[position - 1])) ||
			 source[position - 1] == '_');
		const bool right_identifier = after < source.size() &&
			(std::isalnum(static_cast<unsigned char>(source[after])) ||
			 source[after] == '_');
		if (!left_identifier && !right_identifier)
		{
			const std::size_t line_begin_value = position == 0 ? 0 :
				source.rfind('\n', position - 1);
			const std::size_t line_begin = line_begin_value == std::string::npos ?
				0 : line_begin_value + 1;
			const std::string prefix = source.substr(line_begin,
				position - line_begin);
			std::size_t static_position = prefix.find("static");
			while (static_position != std::string::npos)
			{
				const std::size_t static_after = static_position + 6;
				const bool static_left = static_position != 0 &&
					(std::isalnum(static_cast<unsigned char>(
						prefix[static_position - 1])) ||
					 prefix[static_position - 1] == '_');
				const bool static_right = static_after < prefix.size() &&
					(std::isalnum(static_cast<unsigned char>(prefix[static_after])) ||
					 prefix[static_after] == '_');
				if (!static_left && !static_right)
				{
					std::size_t source_line = 1;
					for (std::size_t i = 0; i < line_begin; ++i)
						if (source[i] == '\n') ++source_line;
					if (source_line > std::numeric_limits<std::uint32_t>::max() ||
						position - line_begin + 1 >
							std::numeric_limits<std::uint32_t>::max())
						throw std::runtime_error(
							"local static source location is too large");
					*line = static_cast<std::uint32_t>(source_line);
					*column = static_cast<std::uint32_t>(
						position - line_begin + 1);
					return;
				}
				static_position = prefix.find("static", static_position + 6);
			}
		}
		position = after;
	}
}

void SemanticAnalyzer::AddLocalStaticObjectAction(std::uint32_t variable,
	BindingId object, TypeId type, std::uint32_t initializer, NodeId syntax)
{
	if (current_function_context_ == kNoBinding)
		throw std::logic_error("local static object has no function owner");
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
	std::uint32_t line = 0;
	std::uint32_t column = 0;
	FindLocalStaticSource(program_->bindings[object].name, &line, &column);
	const NameId source_file = source_path_ ?
		program_->names.Intern(*source_path_) : 0;
	const std::size_t first = arena_->TokenFirst(syntax);
	const std::size_t last = arena_->TokenLast(syntax);
	if (first > std::numeric_limits<std::uint32_t>::max() ||
		last > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("local static token identity is too large");
	std::vector<NameId> source_string_literals;
	std::vector<NodeId> pending(1, syntax);
	while (!pending.empty())
	{
		const NodeId current = pending.back();
		pending.pop_back();
		if (arena_->IsTag(current, "literal"))
		{
			std::string spelling = arena_->Payload(current);
			if (spelling.compare(0, 11, "TT_LITERAL:") == 0)
				spelling.erase(0, 11);
			if (spelling.find('"') != std::string::npos)
				source_string_literals.push_back(
					program_->names.Intern(spelling));
		}
		std::vector<NodeId> children;
		for (std::uint32_t edge = arena_->FirstEdge(current);
			edge != kNoEdge; edge = arena_->NextEdge(edge))
			children.push_back(arena_->EdgeChild(edge));
		for (std::size_t i = children.size(); i != 0; --i)
			pending.push_back(children[i - 1]);
	}
	local_static_objects_.push_back(LocalStaticObjectAction(object,
		current_function_context_, type, variable, initializer,
		destructor_action, static_cast<std::uint32_t>(first),
		static_cast<std::uint32_t>(last), source_file, line, column,
		source_string_literals));
}

void SemanticAnalyzer::RegisterVariableLifetimeAndStorage(ScopeId scope,
	bool local, bool declaration_only, std::uint32_t variable,
	BindingId object, TypeId type, NodeId syntax)
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
		AddLocalStaticObjectAction(variable, object, type, initializer, syntax);
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

void SemanticAnalyzer::DemandRuntimeInitializerFunctions(
	std::uint32_t initializer)
{
	std::vector<std::uint32_t> pending(1, initializer);
	while (!pending.empty())
	{
		const std::uint32_t current = pending.back();
		pending.pop_back();
		const DumpNode& action = dump_.nodes[current];
		if ((action.kind == DUMP_CALL_EXPRESSION ||
			action.kind == DUMP_CONSTRUCTOR_ACTION) &&
			action.binding != kNoBinding)
			DemandFunction(action.binding);
		else if (action.kind == DUMP_CLASS_VALUE_TRANSFER &&
			action.selected_binding != kNoBinding)
			DemandFunction(action.selected_binding);
		for (std::uint32_t edge = action.first_edge;
			edge != kNoDumpEdge; edge = dump_.edges[edge].next)
			pending.push_back(dump_.edges[edge].child);
	}
}

}
}
