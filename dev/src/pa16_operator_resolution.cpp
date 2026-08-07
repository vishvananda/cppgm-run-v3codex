#include "pa12_semantic_detail.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

void SemanticAnalyzer::BeginAssociatedLookup()
{
	++associated_generation_;
	if (associated_generation_ == 0)
	{
		std::fill(associated_entity_marks_.begin(),
			associated_entity_marks_.end(), 0);
		std::fill(associated_scope_marks_.begin(),
			associated_scope_marks_.end(), 0);
		std::fill(associated_type_marks_.begin(),
			associated_type_marks_.end(), 0);
		associated_generation_ = 1;
	}
	associated_entities_.clear();
	associated_scopes_.clear();
	associated_type_scratch_.clear();
	if (associated_entity_marks_.size() < program_->entities.size())
		associated_entity_marks_.resize(program_->entities.size(), 0);
	if (associated_scope_marks_.size() < scope_parents_.size())
		associated_scope_marks_.resize(scope_parents_.size(), 0);
	if (associated_type_marks_.size() < program_->types.Size())
		associated_type_marks_.resize(program_->types.Size(), 0);
}

void SemanticAnalyzer::AddAssociatedEntity(EntityId entity)
{
	if (entity == kNoEntity || entity >= program_->entities.size()) return;
	if (associated_entity_marks_.size() <= entity)
		associated_entity_marks_.resize(static_cast<std::size_t>(entity) + 1, 0);
	if (associated_entity_marks_[entity] == associated_generation_) return;
	associated_entity_marks_[entity] = associated_generation_;
	associated_entities_.push_back(entity);
}

void SemanticAnalyzer::AddAssociatedScope(ScopeId scope)
{
	if (scope == kNoScope) return;
	if (associated_scope_marks_.size() <= scope)
		associated_scope_marks_.resize(static_cast<std::size_t>(scope) + 1, 0);
	if (associated_scope_marks_[scope] == associated_generation_) return;
	associated_scope_marks_[scope] = associated_generation_;
	associated_scopes_.push_back(scope);
}

void SemanticAnalyzer::AddAssociatedType(TypeId type)
{
	if (type == kNoType || type >= program_->types.Size()) return;
	associated_type_scratch_.push_back(type);
	while (!associated_type_scratch_.empty())
	{
		type = associated_type_scratch_.back();
		associated_type_scratch_.pop_back();
		if (type == kNoType || type >= program_->types.Size()) continue;
		if (associated_type_marks_.size() <= type)
			associated_type_marks_.resize(static_cast<std::size_t>(type) + 1, 0);
		if (associated_type_marks_[type] == associated_generation_) continue;
		associated_type_marks_[type] = associated_generation_;
		const TypeRecord shape = program_->types.Get(type);
		switch (shape.kind)
		{
		case TYPE_QUALIFIED:
		case TYPE_POINTER:
		case TYPE_LVALUE_REFERENCE:
		case TYPE_RVALUE_REFERENCE:
		case TYPE_ARRAY:
			associated_type_scratch_.push_back(shape.child);
			break;
		case TYPE_FUNCTION:
		{
			associated_type_scratch_.push_back(shape.child);
			const TypeId* parameters = program_->types.Parameters(type);
			for (std::size_t i = 0; i < shape.parameter_count; ++i)
				associated_type_scratch_.push_back(parameters[i]);
			break;
		}
		case TYPE_NAMED:
			AddAssociatedEntity(shape.entity);
			break;
		default:
			break;
		}
	}
}

void SemanticAnalyzer::BeginCandidateCollection()
{
	++candidate_generation_;
	if (candidate_generation_ == 0)
	{
		std::fill(candidate_marks_.begin(), candidate_marks_.end(), 0);
		candidate_generation_ = 1;
	}
	if (candidate_marks_.size() < program_->bindings.size())
		candidate_marks_.resize(program_->bindings.size(), 0);
}

void SemanticAnalyzer::AddCandidate(BindingId binding,
	std::vector<BindingId>* candidates)
{
	if (binding == kNoBinding || binding >= program_->bindings.size()) return;
	binding = program_->bindings[binding].canonical;
	if (candidate_marks_.size() <= binding)
		candidate_marks_.resize(static_cast<std::size_t>(binding) + 1, 0);
	if (candidate_marks_[binding] == candidate_generation_) return;
	candidate_marks_[binding] = candidate_generation_;
	candidates->push_back(binding);
}

void SemanticAnalyzer::AppendDirectFunctionCandidates(ScopeId owner,
	NameId name, std::vector<BindingId>* candidates)
{
	const std::uint64_t key = (static_cast<std::uint64_t>(owner) << 32) | name;
	const CompactIndexSequence* functions = ordinary_function_sets_.Find(key);
	if (!functions) return;
	for (std::size_t i = 0; i < functions->Size(); ++i)
	{
		++associated_declaration_visits_;
		AddCandidate(static_cast<BindingId>((*functions)[i]), candidates);
	}
}

void SemanticAnalyzer::AppendHiddenFriendCandidates(EntityId owner,
	NameId name, std::vector<BindingId>* candidates)
{
	const std::uint64_t key = (static_cast<std::uint64_t>(owner) << 32) | name;
	const CompactIndexSequence* functions = hidden_friend_sets_.Find(key);
	if (!functions) return;
	for (std::size_t i = 0; i < functions->Size(); ++i)
	{
		++associated_declaration_visits_;
		AddCandidate(static_cast<BindingId>((*functions)[i]), candidates);
	}
}

void SemanticAnalyzer::AppendArgumentDependentCandidates(NameId name,
	const std::vector<ExpressionInfo>& arguments,
	std::vector<BindingId>* candidates)
{
	BeginAssociatedLookup();
	for (std::size_t i = 0; i < arguments.size(); ++i)
		AddAssociatedType(arguments[i].type);
	for (std::size_t i = 0; i < associated_entities_.size(); ++i)
	{
		const EntityId entity = associated_entities_[i];
		const EntityRecord& record = program_->entities[entity];
		if (record.direct_base != kNoEntity)
			AddAssociatedEntity(record.direct_base);
		ScopeId owner = record.owner;
		while (owner != kNoScope &&
			program_->KindOfScope(owner) != SCOPE_NAMESPACE)
		{
			const EntityId enclosing = program_->EntityForScope(owner);
			if (enclosing != kNoEntity) AddAssociatedEntity(enclosing);
			owner = program_->ParentScope(owner);
		}
		AddAssociatedScope(owner);
	}
	for (std::size_t i = 0; i < associated_scopes_.size(); ++i)
	{
		++associated_scope_visits_;
		AppendDirectFunctionCandidates(associated_scopes_[i], name, candidates);
	}
	for (std::size_t i = 0; i < associated_entities_.size(); ++i)
		AppendHiddenFriendCandidates(associated_entities_[i], name, candidates);
}

ExpressionInfo SemanticAnalyzer::MakeImplicitObjectPointer(
	const ExpressionInfo& object)
{
	ExpressionInfo result = object;
	const TypeId object_type = EffectiveType(object.type);
	result.type = program_->types.Pointer(object_type);
	result.category = VALUE_PRVALUE;
	result.binding = kNoBinding;
	result.constant = false;
	result.node = MakeDump(DUMP_UNARY_EXPRESSION, result.type, VALUE_PRVALUE,
		program_->names.Intern("OP_AMP:&"));
	dump_.Add(result.node, object.node);
	++expression_count_;
	return result;
}

BindingId SemanticAnalyzer::SelectOperatorOverload(ScopeId scope,
	const std::vector<NodeId>& operand_syntax,
	const std::vector<ExpressionInfo>& operands,
	const std::vector<BindingId>& candidates,
	const ExpressionInfo& object, bool* selected_member)
{
	const std::size_t arity = operands.size();
	if (arity != 0 && candidates.size() >
		std::numeric_limits<std::size_t>::max() / arity)
		throw std::runtime_error("operator conversion table is too large");
	std::vector<ConversionRank> ranks(candidates.size() * arity,
		CONVERSION_ELLIPSIS);
	std::vector<std::size_t> base_distances(candidates.size() * arity,
		std::numeric_limits<std::size_t>::max());
	std::vector<bool> viable(candidates.size(), true);
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		++overload_candidates_;
		const FunctionInfo& function = GetFunction(candidates[c]);
		const TypeRecord function_type = program_->types.Get(function.type);
		const bool member = function.member_owner != kNoType;
		const std::size_t argument_begin = member ? 1 : 0;
		if (member)
		{
			TypeId object_type = function.member_owner;
			if ((function_type.cv & CV_CONST) != 0)
				object_type = program_->types.Qualify(object_type, CV_CONST);
			if ((function_type.cv & CV_VOLATILE) != 0)
				object_type = program_->types.Qualify(object_type, CV_VOLATILE);
			const TypeId target = program_->types.Pointer(object_type);
			const ConversionRank rank = Conversion(object, target);
			ranks[c * arity] = rank;
			if (rank == CONVERSION_DERIVED_TO_BASE)
				base_distances[c * arity] =
					BaseConversionDistance(object.type, target);
			if (rank == CONVERSION_INVALID)
			{
				viable[c] = false;
				continue;
			}
		}
		const std::size_t explicit_arity = arity - argument_begin;
		std::size_t required = function_type.parameter_count;
		while (required != 0 && required <= function.parameters.size() &&
			function.parameters[required - 1].default_argument != kNoNode)
			--required;
		if (explicit_arity < required ||
			(!function_type.variadic &&
			 explicit_arity > function_type.parameter_count))
		{
			viable[c] = false;
			continue;
		}
		const TypeId* parameters = program_->types.Parameters(function.type);
		for (std::size_t a = argument_begin; a < arity; ++a)
		{
			const std::size_t parameter = a - argument_begin;
			ConversionRank rank = CONVERSION_ELLIPSIS;
			if (parameter < function_type.parameter_count)
			{
				if (operands[a].type != kNoType)
					rank = Conversion(operands[a], parameters[parameter]);
				else if (a < operand_syntax.size() &&
					operand_syntax[a] != kNoNode)
				{
					const std::vector<BindingId> functions = FunctionCandidates(
						scope, arena_->Payload(operand_syntax[a]));
					TypeId desired =
						program_->types.RemoveTopCv(parameters[parameter]);
					if (program_->types.Get(desired).kind == TYPE_POINTER)
						desired = program_->types.Get(desired).child;
					std::size_t matches = 0;
					for (std::size_t f = 0; f < functions.size(); ++f)
						if (GetFunction(functions[f]).type == desired) ++matches;
					rank = matches == 1 ?
						CONVERSION_EXACT : CONVERSION_INVALID;
				}
				else rank = CONVERSION_INVALID;
			}
			ranks[c * arity + a] = rank;
			if (rank == CONVERSION_DERIVED_TO_BASE)
				base_distances[c * arity + a] = BaseConversionDistance(
					operands[a].type, parameters[parameter]);
			if (rank == CONVERSION_INVALID) viable[c] = false;
		}
	}

	const auto better = [this, &ranks, &base_distances, &candidates, arity](
		std::size_t left, std::size_t right) -> bool
	{
		++overload_order_comparisons_;
		bool no_worse = true;
		bool strictly_better = false;
		for (std::size_t a = 0; a < arity; ++a)
		{
			const ConversionRank lrank = ranks[left * arity + a];
			const ConversionRank rrank = ranks[right * arity + a];
			const std::size_t ldistance = base_distances[left * arity + a];
			const std::size_t rdistance = base_distances[right * arity + a];
			if (lrank > rrank ||
				(lrank == rrank && lrank == CONVERSION_DERIVED_TO_BASE &&
				 ldistance > rdistance)) no_worse = false;
			if (lrank < rrank ||
				(lrank == rrank && lrank == CONVERSION_DERIVED_TO_BASE &&
				 ldistance < rdistance)) strictly_better = true;
		}
		if (!no_worse) return false;
		if (strictly_better) return true;
		const FunctionInfo& lfunction = GetFunction(candidates[left]);
		const FunctionInfo& rfunction = GetFunction(candidates[right]);
		return !lfunction.template_specialization &&
			rfunction.template_specialization;
	};

	std::size_t viable_count = 0;
	std::size_t champion = candidates.size();
	for (std::size_t c = 0; c < candidates.size(); ++c)
	{
		if (!viable[c]) continue;
		++viable_count;
		if (champion == candidates.size() || better(c, champion)) champion = c;
	}
	if (viable_count == 0) return kNoBinding;
	for (std::size_t other = 0; other < candidates.size(); ++other)
		if (other != champion && viable[other] && !better(champion, other))
			throw std::runtime_error("ambiguous overloaded operator");
	*selected_member = GetFunction(candidates[champion]).member_owner != kNoType;
	return candidates[champion];
}

bool SemanticAnalyzer::TryAnalyzeOverloadedOperator(
	const std::string& operation, ScopeId scope,
	const std::vector<NodeId>& operand_syntax,
	const std::vector<ExpressionInfo>& operands, bool member_only,
	TypeId target, ExpressionInfo* result)
{
	bool overloadable_operand = false;
	for (std::size_t i = 0; i < operands.size(); ++i)
	{
		const EntityId entity = EntityOf(operands[i].type);
		if (entity == kNoEntity) continue;
		const NamedFlavor flavor = program_->entities[entity].flavor;
		if (flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
			flavor == NAMED_UNION || flavor == NAMED_ENUM ||
			flavor == NAMED_ENUM_CLASS) overloadable_operand = true;
	}
	if (!overloadable_operand || operands.empty()) return false;
	const NameId name = program_->names.Intern("operator" + operation);
	BeginCandidateCollection();
	std::vector<BindingId> candidates;
	EntityId naming_class = kNoEntity;
	const EntityId left_entity = EntityOf(operands[0].type);
	if (left_entity != kNoEntity &&
		program_->entities[left_entity].member_scope != kNoScope)
	{
		const LookupResult member = program_->LookupMember(
			left_entity, name, LOOKUP_ORDINARY);
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
	}
	if (!member_only)
	{
		const std::vector<BindingId> ordinary =
			FunctionCandidates(scope, program_->names.Get(name));
		for (std::size_t i = 0; i < ordinary.size(); ++i)
			if (GetFunction(ordinary[i]).member_owner == kNoType)
				AddCandidate(ordinary[i], &candidates);
		AppendArgumentDependentCandidates(name, operands, &candidates);
	}
	if (candidates.empty()) return false;
	const ExpressionInfo object = MakeImplicitObjectPointer(operands[0]);
	bool selected_member = false;
	const BindingId selected = SelectOperatorOverload(scope, operand_syntax,
		operands, candidates, object, &selected_member);
	if (selected == kNoBinding) return false;
	std::vector<NodeId> arguments_syntax;
	std::vector<ExpressionInfo> arguments;
	const std::size_t first = selected_member ? 1 : 0;
	for (std::size_t i = first; i < operands.size(); ++i)
	{
		arguments.push_back(operands[i]);
		arguments_syntax.push_back(i < operand_syntax.size() ?
			operand_syntax[i] : kNoNode);
	}
	*result = BuildResolvedCall(selected, scope, arguments_syntax, arguments,
		selected_member ? &object : 0, target,
		selected_member ? naming_class : kNoEntity);
	return true;
}

bool SemanticAnalyzer::TryAnalyzeCallOperator(ScopeId scope,
	const ExpressionInfo& callee,
	const std::vector<NodeId>& argument_syntax,
	const std::vector<ExpressionInfo>* analyzed_arguments, TypeId target,
	ExpressionInfo* result)
{
	std::vector<NodeId> operands_syntax(1, kNoNode);
	std::vector<ExpressionInfo> operands(1, callee);
	for (std::size_t i = 0; i < argument_syntax.size(); ++i)
	{
		operands_syntax.push_back(argument_syntax[i]);
		operands.push_back(analyzed_arguments ? (*analyzed_arguments)[i] :
			AnalyzeExpression(argument_syntax[i], scope));
	}
	return TryAnalyzeOverloadedOperator("()", scope, operands_syntax,
		operands, true, target, result);
}

}
}
