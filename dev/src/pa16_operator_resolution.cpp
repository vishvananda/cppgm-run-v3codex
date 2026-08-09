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
	const BindingId canonical = program_->bindings[binding].canonical;
	if (candidate_marks_.size() <= canonical)
		candidate_marks_.resize(static_cast<std::size_t>(canonical) + 1, 0);
	if (candidate_marks_[canonical] == candidate_generation_) return;
	candidate_marks_[canonical] = candidate_generation_;
	candidates->push_back(binding);
}

TypeId SemanticAnalyzer::EnumOperatorOperandType(TypeId type) const
{
	const TypeRecord* shape = &program_->types.Get(type);
	if (shape->kind == TYPE_LVALUE_REFERENCE ||
		shape->kind == TYPE_RVALUE_REFERENCE)
	{
		type = shape->child;
		shape = &program_->types.Get(type);
	}
	if (shape->kind == TYPE_QUALIFIED)
	{
		type = shape->child;
		shape = &program_->types.Get(type);
	}
	if (shape->kind != TYPE_NAMED) return kNoType;
	const NamedFlavor flavor = program_->entities[shape->entity].flavor;
	return flavor == NAMED_ENUM || flavor == NAMED_ENUM_CLASS ?
		type : kNoType;
}

bool SemanticAnalyzer::MatchesEnumOnlyOperatorCandidate(BindingId binding,
	const std::vector<ExpressionInfo>& operands) const
{
	const FunctionInfo& function = GetFunction(binding);
	if (function.member_owner != kNoType) return false;
	const TypeRecord& function_type = program_->types.Get(function.type);
	const TypeId* parameters = program_->types.Parameters(function.type);
	const std::size_t count = std::min<std::size_t>(
		function_type.parameter_count, operands.size());
	for (std::size_t i = 0; i < count; ++i)
	{
		const TypeId enum_type = EnumOperatorOperandType(operands[i].type);
		if (enum_type != kNoType &&
			EnumOperatorOperandType(parameters[i]) == enum_type)
			return true;
	}
	return false;
}

void SemanticAnalyzer::IndexEnumOperatorCandidate(BindingId binding)
{
	if (binding == kNoBinding || binding >= program_->bindings.size()) return;
	const BindingRecord& record = program_->bindings[binding];
	if (record.kind != BIND_FUNCTION) return;
	const FunctionInfo& function = GetFunction(binding);
	if (function.member_owner != kNoType) return;
	const BindingRecord& canonical =
		program_->bindings[record.canonical];
	if (canonical.operator_kind == OPERATOR_NONE) return;
	const TypeRecord& function_type = program_->types.Get(function.type);
	const TypeId* parameters = program_->types.Parameters(function.type);
	const std::size_t count = std::min<std::size_t>(
		function_type.parameter_count, 2);
	for (std::size_t i = 0; i < count; ++i)
	{
		const TypeId enum_type = EnumOperatorOperandType(parameters[i]);
		if (enum_type == kNoType) continue;
		const EnumOperatorCandidateKey key(record.owner, record.name,
			enum_type, static_cast<std::uint8_t>(i));
		enum_operator_candidates_.Ensure(key).Push(binding);
	}
}

void SemanticAnalyzer::AppendIndexedEnumOperatorCandidates(ScopeId owner,
	NameId name, const std::vector<ExpressionInfo>& operands,
	std::vector<BindingId>* candidates)
{
	const std::size_t count = std::min<std::size_t>(operands.size(), 2);
	for (std::size_t i = 0; i < count; ++i)
	{
		const TypeId enum_type = EnumOperatorOperandType(operands[i].type);
		if (enum_type == kNoType) continue;
		const EnumOperatorCandidateKey key(owner, name, enum_type,
			static_cast<std::uint8_t>(i));
		const CompactIndexSequence* functions =
			enum_operator_candidates_.Find(key);
		if (!functions) continue;
		for (std::size_t candidate = 0;
			candidate < functions->Size(); ++candidate)
		{
			++associated_declaration_visits_;
			AddCandidate(static_cast<BindingId>((*functions)[candidate]),
				candidates);
		}
	}
}

void SemanticAnalyzer::AppendVisibleEnumOperatorCandidates(ScopeId scope,
	NameId name, const std::vector<ExpressionInfo>& operands,
	std::vector<BindingId>* candidates)
{
	const LookupResult found =
		program_->LookupName(scope, name, LOOKUP_ORDINARY);
	for (std::size_t i = 0; i < found.OrdinaryCount(); ++i)
	{
		const BindingId anchor = found.OrdinaryAt(i);
		if (anchor == kNoBinding || anchor >= program_->bindings.size()) continue;
		const BindingRecord& record = program_->bindings[anchor];
		if (record.kind != BIND_FUNCTION) continue;
		AppendIndexedEnumOperatorCandidates(record.owner, record.name,
			operands, candidates);
	}
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
	NameId name, const std::vector<ExpressionInfo>* enum_only_operands,
	std::vector<BindingId>* candidates)
{
	const std::uint64_t key = (static_cast<std::uint64_t>(owner) << 32) | name;
	const CompactIndexSequence* functions = hidden_friend_sets_.Find(key);
	if (!functions) return;
	for (std::size_t i = 0; i < functions->Size(); ++i)
	{
		++associated_declaration_visits_;
		const BindingId binding = static_cast<BindingId>((*functions)[i]);
		if (!enum_only_operands ||
			MatchesEnumOnlyOperatorCandidate(binding, *enum_only_operands))
			AddCandidate(binding, candidates);
	}
}

void SemanticAnalyzer::AppendArgumentDependentCandidates(NameId name,
	const std::vector<ExpressionInfo>& arguments,
	std::vector<BindingId>* candidates, bool enum_operator_only)
{
	BeginAssociatedLookup();
	for (std::size_t i = 0; i < arguments.size(); ++i)
		AddAssociatedType(arguments[i].type);
	for (std::size_t i = 0; i < associated_entities_.size(); ++i)
	{
		const EntityId entity = associated_entities_[i];
		const EntityRecord& record = program_->entities[entity];
		if (entity < class_template_pattern_by_entity_.size() &&
			class_template_pattern_by_entity_[entity] != kNoDumpEdge &&
			record.template_argument_begin != kNoBinding)
		{
			const std::size_t pattern =
				class_template_pattern_by_entity_[entity];
			if (pattern >= class_templates_.size())
				throw std::logic_error(
					"associated class template pattern is invalid");
			const std::size_t first = record.template_argument_begin;
			const std::size_t count =
				class_templates_[pattern].parameters.size();
			if (record.template_argument_count != count ||
				first > program_->template_arguments.size() ||
				count > program_->template_arguments.size() - first)
				throw std::logic_error(
					"associated class template argument range is invalid");
			for (std::size_t argument = 0; argument < count; ++argument)
				AddAssociatedType(program_->template_arguments[first + argument]);
		}
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
		const std::uint64_t key =
			(static_cast<std::uint64_t>(associated_scopes_[i]) << 32) | name;
		const CompactIndexSequence* template_patterns =
			template_function_sets_.Find(key);
		if (template_patterns)
		{
			const std::vector<std::size_t> patterns = template_patterns->Copy();
			associated_declaration_visits_ += patterns.size();
			std::vector<BindingId> specializations;
			DeduceFunctionTemplatePatterns(patterns, arguments,
				&specializations);
			for (std::size_t specialization = 0;
				specialization < specializations.size(); ++specialization)
				if (!enum_operator_only || MatchesEnumOnlyOperatorCandidate(
					specializations[specialization], arguments))
					AddCandidate(specializations[specialization], candidates);
		}
		if (enum_operator_only)
			AppendIndexedEnumOperatorCandidates(
				associated_scopes_[i], name, arguments, candidates);
		else AppendDirectFunctionCandidates(
			associated_scopes_[i], name, candidates);
	}
	for (std::size_t i = 0; i < associated_entities_.size(); ++i)
		AppendHiddenFriendCandidates(associated_entities_[i], name,
			enum_operator_only ? &arguments : 0, candidates);
}

CallConversionFact SemanticAnalyzer::ConvertingConstructor(
	const ExpressionInfo& source, TypeId target)
{
	CallConversionFact result;
	const TypeRecord top = program_->types.Get(target);
	if (top.kind == TYPE_LVALUE_REFERENCE || top.kind == TYPE_RVALUE_REFERENCE)
	{
		if (top.kind == TYPE_LVALUE_REFERENCE)
		{
			const TypeRecord referred = program_->types.Get(top.child);
			const std::uint8_t cv = referred.kind == TYPE_QUALIFIED ?
				referred.cv : CV_NONE;
			if ((cv & CV_CONST) == 0 || (cv & CV_VOLATILE) != 0)
				return result;
		}
		const TypeId source_type = program_->types.RemoveTopCv(
			EffectiveType(source.type));
		const TypeId referred_type =
			program_->types.RemoveTopCv(top.child);
		if (source_type == referred_type ||
			BaseConversionDistance(source_type, referred_type) !=
				std::numeric_limits<std::size_t>::max())
			return result;
		target = top.child;
	}
	target = program_->types.RemoveTopCv(target);
	const TypeRecord object = program_->types.Get(target);
	if (object.kind != TYPE_NAMED) return result;
	const NamedFlavor flavor = program_->entities[object.entity].flavor;
	if (flavor != NAMED_STRUCT && flavor != NAMED_CLASS &&
		flavor != NAMED_UNION) return result;

	const std::vector<BindingId>& candidates =
		ConstructorCandidates(object.entity);
	BindingId selected = kNoBinding;
	ConversionRank best = CONVERSION_INVALID;
	bool ambiguous = false;
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		++overload_candidates_;
		const FunctionInfo& constructor = GetFunction(candidates[i]);
		const TypeRecord function = program_->types.Get(constructor.type);
		if (!constructor.constructor || constructor.explicit_constructor ||
			function.parameter_count == 0)
			continue;
		std::size_t required = function.parameter_count;
		while (required != 0 && required <= constructor.parameters.size() &&
			constructor.parameters[required - 1].default_argument != kNoNode)
			--required;
		if (required > 1) continue;
		const ConversionRank rank = Conversion(
			source, program_->types.Parameters(constructor.type)[0]);
		if (rank == CONVERSION_INVALID) continue;
		if (selected == kNoBinding || rank < best)
		{
			selected = candidates[i];
			best = rank;
			ambiguous = false;
		}
		else if (rank == best) ambiguous = true;
	}
	if (ambiguous || selected == kNoBinding) return result;
	result.rank = CONVERSION_USER_DEFINED;
	result.constructor = selected;
	result.constructor_argument_rank = best;
	return result;
}

void SemanticAnalyzer::AppendConversionFunctions(EntityId entity,
	std::vector<BindingId>* candidates) const
{
	while (entity != kNoEntity)
	{
		if (entity < entity_conversion_functions_.size())
			candidates->insert(candidates->end(),
				entity_conversion_functions_[entity].begin(),
				entity_conversion_functions_[entity].end());
		entity = program_->entities[entity].direct_base;
	}
}

void SemanticAnalyzer::AppendBuiltinConversionTargets(
	const ExpressionInfo& source, std::vector<TypeId>* targets) const
{
	std::vector<BindingId> functions;
	AppendConversionFunctions(EntityOf(source.type), &functions);
	for (std::size_t i = 0; i < functions.size(); ++i)
	{
		const FunctionInfo& function = GetFunction(functions[i]);
		if (!function.conversion_function || function.explicit_conversion)
			continue;
		const TypeId target = Decay(function.conversion_target);
		if (std::find(targets->begin(), targets->end(), target) == targets->end())
			targets->push_back(target);
	}
}

bool SemanticAnalyzer::BuiltinBinaryParameterTypes(
	const std::string& operation, const ExpressionInfo& left,
	TypeId left_type, const ExpressionInfo& right, TypeId right_type,
	TypeId* left_target, TypeId* right_target)
{
	left_type = Decay(left_type);
	right_type = Decay(right_type);
	*left_target = left_type;
	*right_target = right_type;
	if (operation == "&&" || operation == "||")
	{
		if ((!IsArithmetic(left_type) && !IsPointer(left_type) &&
			 !IsNullptr(left_type)) ||
			(!IsArithmetic(right_type) && !IsPointer(right_type) &&
			 !IsNullptr(right_type))) return false;
		*left_target = *right_target =
			program_->types.Fundamental(FUND_BOOL);
		return true;
	}
	const bool comparison = operation == "==" || operation == "!=" ||
		operation == "<" || operation == ">" || operation == "<=" ||
		operation == ">=";
	if (comparison)
	{
		const bool equality = operation == "==" || operation == "!=";
		if (IsArithmetic(left_type) && IsArithmetic(right_type))
		{
			*left_target = *right_target =
				CommonArithmeticType(left_type, right_type);
			return true;
		}
		if (IsPointer(left_type) && IsPointer(right_type))
		{
			const ConversionRank right_to_left = Conversion(right_type,
				VALUE_PRVALUE, false, left_type);
			const ConversionRank left_to_right = Conversion(left_type,
				VALUE_PRVALUE, false, right_type);
			if (right_to_left != CONVERSION_INVALID &&
				(left_to_right == CONVERSION_INVALID ||
				 right_to_left <= left_to_right))
				*left_target = *right_target = left_type;
			else if (left_to_right != CONVERSION_INVALID)
				*left_target = *right_target = right_type;
			else return false;
			return true;
		}
		if (IsPointer(left_type) && equality &&
			(IsNullptr(right_type) || right.integer_literal_zero))
		{
			*right_target = left_type;
			return true;
		}
		if (IsPointer(right_type) && equality &&
			(IsNullptr(left_type) || left.integer_literal_zero))
		{
			*left_target = right_type;
			return true;
		}
		return false;
	}
	if (operation == "+" || operation == "-" || operation == "[]")
	{
		if (IsPointer(left_type) && IsIntegral(right_type))
		{
			*right_target = IntegralPromotionType(right_type);
			return true;
		}
		if ((operation == "+" || operation == "[]") &&
			IsIntegral(left_type) && IsPointer(right_type))
		{
			*left_target = IntegralPromotionType(left_type);
			return true;
		}
		if (operation == "-" && IsPointer(left_type) &&
			IsPointer(right_type))
		{
			const ConversionRank right_to_left = Conversion(right_type,
				VALUE_PRVALUE, false, left_type);
			const ConversionRank left_to_right = Conversion(left_type,
				VALUE_PRVALUE, false, right_type);
			if (right_to_left != CONVERSION_INVALID)
				*right_target = left_type;
			else if (left_to_right != CONVERSION_INVALID)
				*left_target = right_type;
			else return false;
			return true;
		}
		if (!IsArithmetic(left_type) || !IsArithmetic(right_type)) return false;
		*left_target = *right_target =
			CommonArithmeticType(left_type, right_type);
		return true;
	}
	const bool integral_only = operation == "%" || operation == "<<" ||
		operation == ">>" || operation == "&" || operation == "|" ||
		operation == "^";
	if (integral_only && (!IsIntegral(left_type) || !IsIntegral(right_type)))
		return false;
	if (!integral_only &&
		(!IsArithmetic(left_type) || !IsArithmetic(right_type))) return false;
	if (operation == "<<" || operation == ">>")
	{
		*left_target = IntegralPromotionType(left_type);
		*right_target = IntegralPromotionType(right_type);
	}
	else *left_target = *right_target =
		CommonArithmeticType(left_type, right_type);
	return true;
}

bool SemanticAnalyzer::ApplyBuiltinUnaryConversion(
	const std::string& operation, ExpressionInfo* operand)
{
	if (EntityOf(operand->type) == kNoEntity) return true;
	if (operation == "!")
	{
		const TypeId boolean = program_->types.Fundamental(FUND_BOOL);
		const CallConversionFact conversion =
			ConvertingFunction(*operand, boolean, true);
		if (conversion.rank == CONVERSION_INVALID) return false;
		*operand = ApplyCallArgument(*operand, boolean, &conversion);
		return true;
	}
	std::vector<TypeId> results;
	AppendBuiltinConversionTargets(*operand, &results);
	std::vector<TypeId> targets;
	for (std::size_t i = 0; i < results.size(); ++i)
	{
		TypeId target = results[i];
		const bool viable = operation == "*" ? IsPointer(target) :
			operation == "~" ? IsIntegral(target) :
			(operation == "+" || operation == "-") ?
				(IsArithmetic(target) ||
				 (operation == "+" && IsPointer(target))) : false;
		if (!viable) continue;
		if (IsIntegral(target)) target = IntegralPromotionType(target);
		if (std::find(targets.begin(), targets.end(), target) == targets.end())
			targets.push_back(target);
	}
	if (targets.size() != 1) return false;
	const CallConversionFact conversion =
		ConvertingFunction(*operand, targets[0], false);
	if (conversion.rank == CONVERSION_INVALID) return false;
	*operand = ApplyCallArgument(*operand, targets[0], &conversion);
	return true;
}

bool SemanticAnalyzer::ApplyBuiltinBinaryConversions(
	const std::string& operation, ExpressionInfo* left,
	ExpressionInfo* right, std::vector<ConversionRank>* selected_ranks,
	bool apply)
{
	if (left->type == kNoType || right->type == kNoType) return false;
	const EntityId left_entity = EntityOf(left->type);
	const EntityId right_entity = EntityOf(right->type);
	const bool left_class = left_entity != kNoEntity &&
		(program_->entities[left_entity].flavor == NAMED_STRUCT ||
		 program_->entities[left_entity].flavor == NAMED_CLASS ||
		 program_->entities[left_entity].flavor == NAMED_UNION);
	const bool right_class = right_entity != kNoEntity &&
		(program_->entities[right_entity].flavor == NAMED_STRUCT ||
		 program_->entities[right_entity].flavor == NAMED_CLASS ||
		 program_->entities[right_entity].flavor == NAMED_UNION);
	if (!left_class && !right_class)
	{
		if (operation == ",")
		{
			if (selected_ranks)
			{
				selected_ranks->clear();
				selected_ranks->push_back(CONVERSION_EXACT);
				selected_ranks->push_back(CONVERSION_EXACT);
			}
			return true;
		}
		TypeId left_target = kNoType;
		TypeId right_target = kNoType;
		if (!BuiltinBinaryParameterTypes(operation, *left, Decay(left->type),
			*right, Decay(right->type), &left_target, &right_target))
			return false;
		const ConversionRank left_rank = Conversion(*left, left_target);
		const ConversionRank right_rank = Conversion(*right, right_target);
		if (left_rank == CONVERSION_INVALID ||
			right_rank == CONVERSION_INVALID) return false;
		if (selected_ranks)
		{
			selected_ranks->clear();
			selected_ranks->push_back(left_rank);
			selected_ranks->push_back(right_rank);
		}
		if (apply && EnumOperatorOperandType(left->type) != kNoType)
			*left = ApplyTarget(*left, left_target, left_rank);
		if (apply && EnumOperatorOperandType(right->type) != kNoType)
			*right = ApplyTarget(*right, right_target, right_rank);
		return true;
	}
	if (operation == "&&" || operation == "||")
	{
		const TypeId boolean = program_->types.Fundamental(FUND_BOOL);
		CallConversionFact left_conversion;
		CallConversionFact right_conversion;
		if (left_class)
		{
			left_conversion = ConvertingFunction(*left, boolean, true);
			if (left_conversion.rank == CONVERSION_INVALID) return false;
		}
		else if (Conversion(*left, boolean) == CONVERSION_INVALID) return false;
		if (right_class)
		{
			right_conversion = ConvertingFunction(*right, boolean, true);
			if (right_conversion.rank == CONVERSION_INVALID) return false;
		}
		else if (Conversion(*right, boolean) == CONVERSION_INVALID) return false;
		if (selected_ranks)
		{
			selected_ranks->clear();
			selected_ranks->push_back(left_class ? left_conversion.rank :
				Conversion(*left, boolean));
			selected_ranks->push_back(right_class ? right_conversion.rank :
				Conversion(*right, boolean));
		}
		if (apply && left_class)
			*left = ApplyCallArgument(*left, boolean, &left_conversion);
		if (apply && right_class)
			*right = ApplyCallArgument(*right, boolean, &right_conversion);
		return true;
	}
	std::vector<TypeId> left_results;
	std::vector<TypeId> right_results;
	if (left_class) AppendBuiltinConversionTargets(*left, &left_results);
	else left_results.push_back(Decay(left->type));
	if (right_class) AppendBuiltinConversionTargets(*right, &right_results);
	else right_results.push_back(Decay(right->type));
	std::vector<TypeId> left_targets;
	std::vector<TypeId> right_targets;
	std::vector<CallConversionFact> left_facts;
	std::vector<CallConversionFact> right_facts;
	std::vector<ConversionRank> left_ranks;
	std::vector<ConversionRank> right_ranks;
	for (std::size_t l = 0; l < left_results.size(); ++l)
		for (std::size_t r = 0; r < right_results.size(); ++r)
		{
			TypeId left_target = kNoType;
			TypeId right_target = kNoType;
			if (!BuiltinBinaryParameterTypes(operation, *left, left_results[l],
				*right, right_results[r], &left_target, &right_target)) continue;
			bool duplicate = false;
			for (std::size_t i = 0; i < left_targets.size(); ++i)
				if (left_targets[i] == left_target &&
					right_targets[i] == right_target) duplicate = true;
			if (duplicate) continue;
			const CallConversionFact left_fact = left_class ?
				ConvertingFunction(*left, left_target, false) :
				CallConversionFact();
			const CallConversionFact right_fact = right_class ?
				ConvertingFunction(*right, right_target, false) :
				CallConversionFact();
			const ConversionRank left_rank = left_class ? left_fact.rank :
				Conversion(*left, left_target);
			const ConversionRank right_rank = right_class ? right_fact.rank :
				Conversion(*right, right_target);
			if (left_rank == CONVERSION_INVALID ||
				right_rank == CONVERSION_INVALID) continue;
			left_targets.push_back(left_target);
			right_targets.push_back(right_target);
			left_facts.push_back(left_fact);
			right_facts.push_back(right_fact);
			left_ranks.push_back(left_rank);
			right_ranks.push_back(right_rank);
		}
	if (left_targets.empty()) return false;
	const auto better = [this, left, right, &left_facts, &right_facts,
		&left_ranks, &right_ranks](std::size_t a, std::size_t b) -> bool
	{
		bool no_worse = true;
		bool strict = false;
		const ConversionRank aranks[2] = {left_ranks[a], right_ranks[a]};
		const ConversionRank branks[2] = {left_ranks[b], right_ranks[b]};
		const CallConversionFact afacts[2] = {left_facts[a], right_facts[a]};
		const CallConversionFact bfacts[2] = {left_facts[b], right_facts[b]};
		for (std::size_t i = 0; i < 2; ++i)
		{
			if (aranks[i] > branks[i]) no_worse = false;
			else if (aranks[i] < branks[i]) strict = true;
			else if (aranks[i] == CONVERSION_USER_DEFINED)
			{
				int preference =
					CompareCallConversions(afacts[i], bfacts[i]);
				if (preference == 0 &&
					afacts[i].conversion_function != kNoBinding &&
					bfacts[i].conversion_function != kNoBinding)
				{
					if (afacts[i].conversion_object_rank <
						bfacts[i].conversion_object_rank) preference = 1;
					else if (afacts[i].conversion_object_rank >
						bfacts[i].conversion_object_rank) preference = -1;
					else preference = CompareImplicitObjectBindings(
						i == 0 ? left->category : right->category,
						program_->types.Get(GetFunction(
							afacts[i].conversion_function).type),
						program_->types.Get(GetFunction(
							bfacts[i].conversion_function).type));
				}
				if (preference < 0) no_worse = false;
				else if (preference > 0) strict = true;
			}
		}
		return no_worse && strict;
	};
	std::size_t selected = 0;
	for (std::size_t i = 1; i < left_targets.size(); ++i)
		if (better(i, selected)) selected = i;
	for (std::size_t i = 0; i < left_targets.size(); ++i)
		if (i != selected && !better(selected, i)) return false;
	if (selected_ranks)
	{
		selected_ranks->clear();
		selected_ranks->push_back(left_ranks[selected]);
		selected_ranks->push_back(right_ranks[selected]);
	}
	if (apply && left_class)
		*left = ApplyCallArgument(*left, left_targets[selected],
			&left_facts[selected]);
	if (apply && right_class)
		*right = ApplyCallArgument(*right, right_targets[selected],
			&right_facts[selected]);
	return true;
}

bool SemanticAnalyzer::ApplyBuiltinAssignmentConversion(
	const std::string& operation, const ExpressionInfo& left,
	ExpressionInfo* right)
{
	if (EntityOf(right->type) == kNoEntity) return true;
	if (operation == "=")
	{
		const TypeId target = EffectiveType(left.type);
		const CallConversionFact conversion =
			ConvertingFunction(*right, target, false);
		if (conversion.rank == CONVERSION_INVALID) return false;
		*right = ApplyCallArgument(*right, target, &conversion);
		return true;
	}
	ExpressionInfo left_copy = left;
	return ApplyBuiltinBinaryConversions(
		operation.substr(0, operation.size() - 1), &left_copy, right);
}

CallConversionFact SemanticAnalyzer::ConvertingFunction(
	const ExpressionInfo& source, TypeId target, bool allow_explicit)
{
	CallConversionFact result;
	const EntityId entity = EntityOf(source.type);
	if (entity == kNoEntity) return result;
	std::vector<BindingId> candidates;
	AppendConversionFunctions(entity, &candidates);
	BindingId selected = kNoBinding;
	ConversionRank best_result = CONVERSION_INVALID;
	ConversionRank best_object = CONVERSION_INVALID;
	std::uint32_t best_projections = 0;
	bool ambiguous = false;
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		++overload_candidates_;
		const FunctionInfo& function = GetFunction(candidates[i]);
		if (!function.conversion_function ||
			(function.explicit_conversion && !allow_explicit)) continue;
		const TypeRecord& function_type = program_->types.Get(function.type);
		if (!RefQualifierViable(source, function_type)) continue;
		TypeId object_type = function.member_owner;
		if ((function_type.cv & CV_CONST) != 0)
			object_type = program_->types.Qualify(object_type, CV_CONST);
		if ((function_type.cv & CV_VOLATILE) != 0)
			object_type = program_->types.Qualify(object_type, CV_VOLATILE);
		ExpressionInfo object = source;
		object.type = program_->types.Pointer(EffectiveType(source.type));
		const ConversionRank object_rank = MemberObjectConversion(object,
			program_->types.Pointer(object_type), candidates[i]);
		if (object_rank == CONVERSION_INVALID) continue;
		ExpressionInfo converted;
		converted.type = function.conversion_target;
		const TypeRecord& converted_type =
			program_->types.Get(function.conversion_target);
		converted.category = converted_type.kind == TYPE_LVALUE_REFERENCE ?
			VALUE_LVALUE : converted_type.kind == TYPE_RVALUE_REFERENCE ?
			VALUE_XVALUE : VALUE_PRVALUE;
		const ConversionRank result_rank = Conversion(converted, target);
		if (result_rank == CONVERSION_INVALID) continue;
		if (function.explicit_conversion &&
			program_->types.RemoveTopCv(EffectiveType(function.conversion_target)) !=
			program_->types.RemoveTopCv(EffectiveType(target))) continue;
		std::uint32_t projections = 0;
		if (object_rank == CONVERSION_DERIVED_TO_BASE)
		{
			const std::size_t count = BaseProjectionCount(source.type, object_type);
			if (count == std::numeric_limits<std::size_t>::max() ||
				count > std::numeric_limits<std::uint32_t>::max())
				throw std::logic_error(
					"conversion function has no bounded object path");
			projections = static_cast<std::uint32_t>(count);
		}
		bool better = selected == kNoBinding || result_rank < best_result ||
			(result_rank == best_result && object_rank < best_object);
		if (!better && selected != kNoBinding && result_rank == best_result &&
			object_rank == best_object)
		{
			const int preference = CompareImplicitObjectBindings(source.category,
				function_type, program_->types.Get(GetFunction(selected).type));
			better = preference > 0;
			if (preference == 0) ambiguous = true;
		}
		if (better)
		{
			selected = candidates[i];
			best_result = result_rank;
			best_object = object_rank;
			best_projections = projections;
			ambiguous = false;
		}
	}
	if (selected == kNoBinding || ambiguous) return result;
	result.rank = CONVERSION_USER_DEFINED;
	result.conversion_function = selected;
	result.conversion_result_rank = best_result;
	result.conversion_object_rank = best_object;
	result.conversion_base_projection_count = best_projections;
	return result;
}

int SemanticAnalyzer::CompareCallConversions(
	const CallConversionFact& left, const CallConversionFact& right) const
{
	if (left.rank != CONVERSION_USER_DEFINED ||
		right.rank != CONVERSION_USER_DEFINED) return 0;
	if (left.conversion_function != kNoBinding &&
		left.conversion_function == right.conversion_function)
	{
		if (left.conversion_result_rank < right.conversion_result_rank) return 1;
		if (left.conversion_result_rank > right.conversion_result_rank) return -1;
		if (left.conversion_object_rank < right.conversion_object_rank) return 1;
		if (left.conversion_object_rank > right.conversion_object_rank) return -1;
	}
	if (left.constructor != kNoBinding &&
		left.constructor == right.constructor)
	{
		if (left.constructor_argument_rank < right.constructor_argument_rank)
			return 1;
		if (left.constructor_argument_rank > right.constructor_argument_rank)
			return -1;
	}
	return 0;
}

ExpressionInfo SemanticAnalyzer::ApplyExplicitConversion(
	ExpressionInfo value, TypeId target)
{
	const ConversionRank standard = Conversion(value, target);
	if (standard != CONVERSION_INVALID)
		return ApplyTarget(value, target, standard);
	const CallConversionFact conversion =
		ConvertingFunction(value, target, true);
	if (conversion.rank == CONVERSION_INVALID ||
		conversion.conversion_function == kNoBinding)
		throw std::runtime_error("invalid explicit conversion");
	ObjectConversionFact object_conversion;
	object_conversion.rank = conversion.conversion_object_rank;
	object_conversion.base_projection_count =
		conversion.conversion_base_projection_count;
	const ExpressionInfo object = MakeImplicitObjectPointer(value);
	const std::vector<NodeId> syntax;
	const std::vector<ExpressionInfo> arguments;
	return BuildResolvedCall(conversion.conversion_function, kNoScope,
		syntax, arguments, &object, target, kNoEntity,
		&object_conversion, 0);
}

CallConversionFact SemanticAnalyzer::CallConversion(
	const ExpressionInfo& source, TypeId target,
	CallConversionTable* cache, std::size_t source_ordinal)
{
	CallConversionFact result;
	const ConversionRank standard = Conversion(source, target);
	if (standard != CONVERSION_INVALID)
	{
		result.rank = standard;
		return result;
	}
	if (source_ordinal > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("call conversion source ordinal is too large");
	const std::uint64_t key =
		(static_cast<std::uint64_t>(source_ordinal) << 32) | target;
	if (cache)
	{
		const CallConversionFact* existing = cache->Find(key);
		if (existing)
		{
			++call_conversion_cache_hits_;
			return *existing;
		}
		++call_conversion_cache_misses_;
	}
	const CallConversionFact constructor = ConvertingConstructor(source, target);
	const CallConversionFact conversion =
		ConvertingFunction(source, target, false);
	if (constructor.rank == CONVERSION_INVALID) result = conversion;
	else if (conversion.rank == CONVERSION_INVALID) result = constructor;
	else
	{
		const TypeRecord& target_type = program_->types.Get(target);
		result = (target_type.kind == TYPE_LVALUE_REFERENCE ||
			target_type.kind == TYPE_RVALUE_REFERENCE) &&
			conversion.conversion_result_rank == CONVERSION_EXACT ?
			conversion : constructor;
	}
	if (cache) cache->Insert(key, result);
	return result;
}

ExpressionInfo SemanticAnalyzer::BuildConvertingArgument(
	const ExpressionInfo& source, TypeId target,
	const CallConversionFact& conversion)
{
	const BindingId constructor_binding = conversion.constructor;
	if (constructor_binding == kNoBinding)
		throw std::logic_error("missing selected converting constructor");
	const TypeRecord target_top = program_->types.Get(target);
	TypeId object_type = target;
	if (target_top.kind == TYPE_LVALUE_REFERENCE ||
		target_top.kind == TYPE_RVALUE_REFERENCE)
		object_type = target_top.child;
	object_type = program_->types.RemoveTopCv(object_type);
	const FunctionInfo constructor = GetFunction(constructor_binding);
	if (constructor.deleted_constructor)
		throw std::runtime_error("selected converting constructor is deleted");
	if (!CanAccessMember(constructor_binding))
		throw std::runtime_error("selected converting constructor is inaccessible");
	const TypeRecord function = program_->types.Get(constructor.type);
	if (function.parameter_count == 0)
		throw std::logic_error("converting constructor has no source parameter");
	const TypeId* parameter_data = program_->types.Parameters(constructor.type);
	std::vector<TypeId> parameters(parameter_data,
		parameter_data + function.parameter_count);
	const std::uint32_t action = MakeDump(DUMP_CONSTRUCTOR_ACTION,
		AdaptMemberFunctionType(constructor_binding), VALUE_NONE,
		constructor.display_name, constructor_binding);
	CallConversionFact parameter_conversion;
	parameter_conversion.rank = conversion.constructor_argument_rank;
	ExpressionInfo converted = ApplyCallArgument(
		source, parameters[0], &parameter_conversion);
	dump_.Add(action, converted.node);
	for (std::size_t i = 1; i < function.parameter_count; ++i)
	{
		if (i >= constructor.parameters.size() ||
			constructor.parameters[i].default_argument == kNoNode)
			throw std::logic_error(
				"converting constructor lacks a default argument");
		ExpressionInfo value = AnalyzeExpression(
			constructor.parameters[i].default_argument,
			constructor.parameters[i].default_scope, parameters[i]);
		value = ApplyCallArgument(value, parameters[i]);
		dump_.Add(action, value.node);
	}
	DemandFunction(constructor_binding);
	const std::uint32_t temporary = MakeDump(DUMP_TEMPORARY_OBJECT,
		object_type, VALUE_XVALUE);
	dump_.Add(temporary, action);
	dump_.nodes[temporary].argument_materialization = true;
	ExpressionInfo result;
	result.node = temporary;
	result.type = object_type;
	result.category = VALUE_XVALUE;
	expression_count_ += 2;
	return ApplyTarget(result, target);
}

ExpressionInfo SemanticAnalyzer::ApplyCallArgument(
	ExpressionInfo value, TypeId target, const CallConversionFact* conversion)
{
	const CallConversionFact resolved = conversion ? *conversion :
		CallConversion(value, target, 0, 0);
	bool converted_by_function = false;
	if (resolved.rank == CONVERSION_INVALID)
		throw std::runtime_error("invalid implicit call conversion");
	if (resolved.rank == CONVERSION_USER_DEFINED)
	{
		if (resolved.conversion_function != kNoBinding)
		{
			const std::vector<NodeId> syntax;
			const std::vector<ExpressionInfo> arguments;
			ObjectConversionFact object_conversion;
			object_conversion.rank = resolved.conversion_object_rank;
			object_conversion.base_projection_count =
				resolved.conversion_base_projection_count;
			const ExpressionInfo object = MakeImplicitObjectPointer(value);
			value = BuildResolvedCall(resolved.conversion_function, kNoScope,
				syntax, arguments, &object, target, kNoEntity,
				&object_conversion, 0);
			converted_by_function = true;
		}
		else
		{
			value = BuildConvertingArgument(value, target, resolved);
			const TypeRecord converted_target = program_->types.Get(target);
			if (converted_target.kind != TYPE_LVALUE_REFERENCE &&
				converted_target.kind != TYPE_RVALUE_REFERENCE)
				dump_.nodes[value.node].class_argument_staging = true;
			return value;
		}
	}
	const TypeRecord target_top = program_->types.Get(target);
	const TypeId target_object = program_->types.RemoveTopCv(target);
	const TypeRecord& target_record = program_->types.Get(target_object);
	const bool class_value = target_top.kind != TYPE_LVALUE_REFERENCE &&
		target_top.kind != TYPE_RVALUE_REFERENCE &&
		target_record.kind == TYPE_NAMED &&
		(program_->entities[target_record.entity].flavor == NAMED_STRUCT ||
		 program_->entities[target_record.entity].flavor == NAMED_CLASS ||
		 program_->entities[target_record.entity].flavor == NAMED_UNION) &&
		(program_->types.RemoveTopCv(EffectiveType(value.type)) == target_object ||
		 resolved.rank == CONVERSION_DERIVED_TO_BASE);
	if (!converted_by_function)
		value = ApplyTarget(value, target, resolved.rank);
	if (class_value)
	{
		while (dump_.nodes[value.node].kind == DUMP_TEMPORARY_OBJECT &&
			dump_.nodes[value.node].first_edge != kNoDumpEdge &&
			dump_.edges[dump_.nodes[value.node].first_edge].next == kNoDumpEdge)
		{
			const std::uint32_t edge = dump_.nodes[value.node].first_edge;
			const std::uint32_t child = dump_.edges[edge].child;
			if (dump_.nodes[child].kind == DUMP_CONSTRUCTOR_ACTION ||
				dump_.nodes[child].kind == DUMP_TEMPORARY_OBJECT)
			{
				value.node = child;
				value.category = VALUE_PRVALUE;
			}
			else break;
		}
		while (dump_.nodes[value.node].kind == DUMP_CONSTRUCTOR_ACTION &&
			dump_.nodes[value.node].first_edge != kNoDumpEdge &&
			dump_.edges[dump_.nodes[value.node].first_edge].next == kNoDumpEdge)
		{
			std::uint32_t child =
				dump_.edges[dump_.nodes[value.node].first_edge].child;
			if (dump_.nodes[child].kind == DUMP_TEMPORARY_OBJECT &&
				dump_.nodes[child].first_edge != kNoDumpEdge &&
				dump_.edges[dump_.nodes[child].first_edge].next == kNoDumpEdge)
				child = dump_.edges[dump_.nodes[child].first_edge].child;
			if (dump_.nodes[child].kind != DUMP_CONSTRUCTOR_ACTION) break;
			value.node = child;
			value.category = VALUE_PRVALUE;
		}
		if (value.category == VALUE_PRVALUE &&
			dump_.nodes[value.node].kind == DUMP_CALL_EXPRESSION &&
			!dump_.nodes[value.node].explicit_user_conversion_call)
		{
			const BindingId selected =
				ValidateClassValueConstruction(target, value);
			value = BuildDirectClassValueTransfer(value, target, selected);
		}
		else if (dump_.nodes[value.node].kind == DUMP_BRACED_INIT_LIST &&
			dump_.nodes[value.node].value_initialization &&
			dump_.nodes[value.node].value_constructor != kNoDumpEdge)
			value.node = dump_.nodes[value.node].value_constructor;
		else if (dump_.nodes[value.node].kind != DUMP_CONSTRUCTOR_ACTION)
			value.node = BuildClassValueConstructorAction(target, value);
		if (dump_.nodes[value.node].kind == DUMP_CONSTRUCTOR_ACTION &&
			dump_.nodes[value.node].binding != kNoBinding &&
			!dump_.nodes[value.node].trivial_special_member_action &&
			!dump_.nodes[value.node].elide_empty_constructor)
			DemandFunction(dump_.nodes[value.node].binding);
		value.type = target_object;
		value.category = VALUE_PRVALUE;
		dump_.nodes[value.node].class_argument_staging = true;
		return value;
	}
	if ((target_top.kind == TYPE_LVALUE_REFERENCE ||
		target_top.kind == TYPE_RVALUE_REFERENCE) &&
		dump_.nodes[value.node].kind == DUMP_CALL_EXPRESSION)
	{
		const TypeId referred = program_->types.RemoveTopCv(target_top.child);
		const TypeRecord& object = program_->types.Get(referred);
		if (object.kind == TYPE_NAMED &&
			program_->entities[object.entity].flavor == NAMED_UNION)
		{
			const std::uint32_t temporary = MakeDump(
				DUMP_TEMPORARY_OBJECT, referred, VALUE_XVALUE);
			dump_.Add(temporary, value.node);
			value.node = temporary;
			value.type = referred;
			value.category = VALUE_XVALUE;
			++expression_count_;
		}
	}
	const EntityId materialized_entity = EntityOf(value.type);
	const TypeRecord& materialized_top = program_->types.Get(value.type);
	const bool materialized_class = materialized_entity != kNoEntity &&
		(program_->entities[materialized_entity].flavor == NAMED_STRUCT ||
		 program_->entities[materialized_entity].flavor == NAMED_CLASS ||
		 program_->entities[materialized_entity].flavor == NAMED_UNION);
	if ((target_top.kind == TYPE_LVALUE_REFERENCE ||
		target_top.kind == TYPE_RVALUE_REFERENCE) &&
		value.category != VALUE_LVALUE &&
		materialized_top.kind != TYPE_LVALUE_REFERENCE &&
		materialized_top.kind != TYPE_RVALUE_REFERENCE &&
		materialized_class &&
		dump_.nodes[value.node].kind != DUMP_TEMPORARY_OBJECT &&
		(dump_.nodes[value.node].kind == DUMP_CALL_EXPRESSION ||
		 dump_.nodes[value.node].kind == DUMP_CONDITIONAL_EXPRESSION ||
		 dump_.nodes[value.node].kind == DUMP_CONSTRUCTOR_ACTION ||
		 dump_.nodes[value.node].kind == DUMP_BRACED_INIT_LIST ||
		 dump_.nodes[value.node].kind == DUMP_AGGREGATE_CONSTRUCTION_ACTION ||
		 dump_.nodes[value.node].kind == DUMP_CLASS_VALUE_TRANSFER))
		value = MaterializeTemporary(value);
	if ((target_top.kind == TYPE_LVALUE_REFERENCE ||
		target_top.kind == TYPE_RVALUE_REFERENCE) &&
		dump_.nodes[value.node].kind == DUMP_TEMPORARY_OBJECT)
		dump_.nodes[value.node].argument_materialization = true;
	else
	{
		const TypeId object_type = program_->types.RemoveTopCv(target);
		const TypeRecord object = program_->types.Get(object_type);
		if (object.kind == TYPE_NAMED)
		{
			const NamedFlavor flavor = program_->entities[object.entity].flavor;
			if (flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
				flavor == NAMED_UNION)
				dump_.nodes[value.node].class_argument_staging = true;
		}
	}
	return value;
}

ExpressionInfo SemanticAnalyzer::MakeImplicitObjectPointer(
	const ExpressionInfo& object)
{
	ExpressionInfo result = object;
	const TypeId object_type = EffectiveType(object.type);
	result.type = program_->types.Pointer(object_type);
	// The LowIR address is a pointer prvalue, while overload resolution needs
	// the value category of the source object expression.  Keep that semantic
	// fact on the transient expression record; the dump node remains a prvalue.
	result.category = object.category;
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
	const ExpressionInfo& object, bool* selected_member,
	ObjectConversionFact* object_conversion,
	std::vector<CallConversionFact>* argument_conversions)
{
	const std::size_t arity = operands.size();
	if (arity != 0 && candidates.size() >
		std::numeric_limits<std::size_t>::max() / arity)
		throw std::runtime_error("operator conversion table is too large");
	std::vector<ConversionRank> ranks(candidates.size() * arity,
		CONVERSION_ELLIPSIS);
	std::vector<std::size_t> base_distances(candidates.size() * arity,
		std::numeric_limits<std::size_t>::max());
	std::vector<ConversionRank> actual_object_ranks(candidates.size(),
		CONVERSION_INVALID);
	std::vector<std::size_t> actual_object_distances(candidates.size(),
		std::numeric_limits<std::size_t>::max());
	std::vector<CallConversionFact> conversions(candidates.size() * arity);
	CallConversionTable conversion_cache;
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
			if (!RefQualifierViable(object, function_type))
			{
				viable[c] = false;
				continue;
			}
			TypeId object_type = function.member_owner;
			if ((function_type.cv & CV_CONST) != 0)
				object_type = program_->types.Qualify(object_type, CV_CONST);
			if ((function_type.cv & CV_VOLATILE) != 0)
				object_type = program_->types.Qualify(object_type, CV_VOLATILE);
			const TypeId target = program_->types.Pointer(object_type);
			const ConversionRank actual_rank = MemberObjectConversion(object, target,
				candidates[c]);
			actual_object_ranks[c] = actual_rank;
			if (actual_rank == CONVERSION_DERIVED_TO_BASE)
				actual_object_distances[c] =
					BaseConversionDistance(object.type, target);
			std::size_t selection_distance = actual_object_distances[c];
			const ConversionRank selection_rank =
				MemberCandidateSelectionRank(object, candidates[c], actual_rank,
					&selection_distance);
			ranks[c * arity] = selection_rank;
			if (selection_rank == CONVERSION_DERIVED_TO_BASE)
				base_distances[c * arity] = selection_distance;
			if (actual_rank == CONVERSION_INVALID)
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
				{
					const CallConversionFact conversion = CallConversion(
						operands[a], parameters[parameter],
						&conversion_cache, a);
					conversions[c * arity + a] = conversion;
					rank = conversion.rank;
				}
				else if (a < operand_syntax.size() &&
					operand_syntax[a] != kNoNode)
				{
					TypeId desired =
						program_->types.RemoveTopCv(parameters[parameter]);
					if (program_->types.Get(desired).kind == TYPE_POINTER)
						desired = program_->types.Get(desired).child;
					std::vector<BindingId> functions = FunctionCandidates(
						scope, arena_->Payload(operand_syntax[a]), 0,
						operand_syntax[a]);
					const std::vector<BindingId> template_functions =
						FunctionTemplateTargetCandidates(scope,
							arena_->Payload(operand_syntax[a]), desired,
							operand_syntax[a]);
					for (std::size_t f = 0; f < template_functions.size(); ++f)
						if (std::find(functions.begin(), functions.end(),
							template_functions[f]) == functions.end())
							functions.push_back(template_functions[f]);
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

	const auto better = [this, &ranks, &base_distances, &conversions,
		&candidates, &object, &operands, arity](
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
			if (lrank == CONVERSION_USER_DEFINED &&
				rrank == CONVERSION_USER_DEFINED)
			{
				const int preference = CompareCallConversions(
					conversions[left * arity + a],
					conversions[right * arity + a]);
				if (preference < 0) no_worse = false;
				if (preference > 0) strictly_better = true;
			}
		}
		if (!no_worse) return false;
		if (strictly_better) return true;
		const FunctionInfo& lfunction = GetFunction(candidates[left]);
		const FunctionInfo& rfunction = GetFunction(candidates[right]);
		const TypeRecord& ltype = program_->types.Get(lfunction.type);
		const TypeRecord& rtype = program_->types.Get(rfunction.type);
		const bool lmember = lfunction.member_owner != kNoType;
		const bool rmember = rfunction.member_owner != kNoType;
		const TypeId* lparameters =
			program_->types.Parameters(lfunction.type);
		const TypeId* rparameters =
			program_->types.Parameters(rfunction.type);
		for (std::size_t a = 0; a < arity; ++a)
		{
			if ((lmember && a == 0) || (rmember && a == 0))
				continue;
			const std::size_t lp = a - (lmember ? 1 : 0);
			const std::size_t rp = a - (rmember ? 1 : 0);
			if (lp >= ltype.parameter_count || rp >= rtype.parameter_count)
				continue;
			const int preference = CompareReferenceBindings(
				operands[a], lparameters[lp], rparameters[rp]);
			if (preference != 0) return preference > 0;
		}
		if (lfunction.member_owner != kNoType &&
			rfunction.member_owner != kNoType)
		{
			const int preference = CompareImplicitObjectBindings(
				object.category, program_->types.Get(lfunction.type),
				program_->types.Get(rfunction.type));
			if (preference != 0) return preference > 0;
		}
		const int template_preference = CompareFunctionTemplateConstraints(
			lfunction, rfunction);
		if (template_preference != 0) return template_preference > 0;
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
	if (*selected_member && object_conversion)
	{
		object_conversion->rank = actual_object_ranks[champion];
		if (object_conversion->rank == CONVERSION_DERIVED_TO_BASE)
		{
			const std::size_t projections = actual_object_distances[champion];
			if (projections == std::numeric_limits<std::size_t>::max() ||
				projections > std::numeric_limits<std::uint32_t>::max())
				throw std::logic_error(
					"selected operator object has no bounded base path");
			object_conversion->base_projection_count = projections == 0 ? 0 : 1;
		}
	}
	if (argument_conversions)
	{
		argument_conversions->clear();
		const std::size_t first = *selected_member ? 1 : 0;
		argument_conversions->reserve(arity - first);
		for (std::size_t a = first; a < arity; ++a)
			argument_conversions->push_back(
				conversions[champion * arity + a]);
	}
	return candidates[champion];
}

bool SemanticAnalyzer::TryAnalyzeOverloadedOperator(
	const std::string& operation, ScopeId scope,
	const std::vector<NodeId>& operand_syntax,
	const std::vector<ExpressionInfo>& operands, bool member_only,
	TypeId target, ExpressionInfo* result,
	const std::vector<ConversionRank>* competing_builtin_ranks)
{
	bool overloadable_operand = false;
	bool class_operand = false;
	for (std::size_t i = 0; i < operands.size(); ++i)
	{
		if (operands[i].type == kNoType) continue;
		const EntityId entity = EntityOf(operands[i].type);
		if (entity == kNoEntity) continue;
		const NamedFlavor flavor = program_->entities[entity].flavor;
		if (flavor == NAMED_STRUCT || flavor == NAMED_CLASS ||
			flavor == NAMED_UNION)
		{
			overloadable_operand = true;
			class_operand = true;
		}
		else if (flavor == NAMED_ENUM || flavor == NAMED_ENUM_CLASS)
			overloadable_operand = true;
	}
	if (!overloadable_operand || operands.empty()) return false;
	const bool enum_operator_only = !class_operand;
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
		const LookupResult member_templates = program_->LookupMember(
			left_entity, name, LOOKUP_FUNCTION_TEMPLATE);
		std::vector<std::size_t> patterns;
		for (std::size_t owner = 0;
			owner < member_templates.FunctionTemplateOwnerCount(); ++owner)
		{
			const ScopeId template_owner =
				member_templates.FunctionTemplateOwnerAt(owner);
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
			std::vector<ExpressionInfo> arguments(
				operands.begin() + 1, operands.end());
			std::vector<BindingId> specializations;
			DeduceFunctionTemplatePatterns(
				patterns, arguments, &specializations);
			for (std::size_t i = 0; i < specializations.size(); ++i)
				if (GetFunction(specializations[i]).member_owner != kNoType)
					AddCandidate(specializations[i], &candidates);
			if (naming_class == kNoEntity)
				naming_class = member_templates.naming_class;
		}
	}
	if (!member_only)
	{
		if (enum_operator_only)
			AppendVisibleEnumOperatorCandidates(
				scope, name, operands, &candidates);
		else
		{
			const std::vector<BindingId> ordinary =
				FunctionCandidates(scope, program_->names.Get(name));
			for (std::size_t i = 0; i < ordinary.size(); ++i)
				if (GetFunction(ordinary[i]).member_owner == kNoType)
					AddCandidate(ordinary[i], &candidates);
		}
		AppendArgumentDependentCandidates(name, operands, &candidates,
			enum_operator_only);
	}
	if (candidates.empty()) return false;
	const ExpressionInfo object = MakeImplicitObjectPointer(operands[0]);
	bool selected_member = false;
	ObjectConversionFact object_conversion;
	std::vector<CallConversionFact> argument_conversions;
	const BindingId selected = SelectOperatorOverload(scope, operand_syntax,
		operands, candidates, object, &selected_member, &object_conversion,
		&argument_conversions);
	if (selected == kNoBinding) return false;
	if (competing_builtin_ranks &&
		competing_builtin_ranks->size() == operands.size())
	{
		bool no_worse = true;
		bool strictly_better = false;
		for (std::size_t i = 0; i < operands.size(); ++i)
		{
			const ConversionRank overloaded_rank = selected_member && i == 0 ?
				object_conversion.rank :
				argument_conversions[i - (selected_member ? 1 : 0)].rank;
			const ConversionRank builtin_rank = (*competing_builtin_ranks)[i];
			if (builtin_rank > overloaded_rank) no_worse = false;
			if (builtin_rank < overloaded_rank) strictly_better = true;
		}
		if (no_worse && strictly_better) return false;
	}
	if (GetFunction(selected).deleted_special_member)
		throw std::runtime_error("selected special member is deleted");
	std::vector<NodeId> arguments_syntax;
	std::vector<ExpressionInfo> arguments;
	ExpressionInfo selected_object = object;
	if (selected_member && operands[0].category != VALUE_LVALUE &&
		dump_.nodes[operands[0].node].kind != DUMP_TEMPORARY_OBJECT)
	{
		const EntityId entity = EntityOf(operands[0].type);
		if (entity != kNoEntity &&
			(program_->entities[entity].flavor == NAMED_STRUCT ||
			 program_->entities[entity].flavor == NAMED_CLASS ||
			 program_->entities[entity].flavor == NAMED_UNION))
			selected_object = MakeImplicitObjectPointer(
				MaterializeTemporary(operands[0]));
	}
	const std::size_t first = selected_member ? 1 : 0;
	for (std::size_t i = first; i < operands.size(); ++i)
	{
		arguments.push_back(operands[i]);
		arguments_syntax.push_back(i < operand_syntax.size() ?
			operand_syntax[i] : kNoNode);
	}
	*result = BuildResolvedCall(selected, scope, arguments_syntax, arguments,
		selected_member ? &selected_object : 0, target,
		selected_member ? naming_class : kNoEntity,
		selected_member ? &object_conversion : 0, &argument_conversions);
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

bool SemanticAnalyzer::TryAnalyzeCallSurrogate(ScopeId scope,
	const ExpressionInfo& callee,
	const std::vector<ExpressionInfo>& arguments, TypeId target,
	ExpressionInfo* result)
{
	const EntityId entity = EntityOf(callee.type);
	if (entity == kNoEntity) return false;
	std::vector<BindingId> conversions;
	AppendConversionFunctions(entity, &conversions);
	struct Candidate
	{
		BindingId binding;
		TypeId function_type;
		ConversionRank object_rank;
		std::size_t object_distance;
		std::size_t argument_offset;
		Candidate()
			: binding(kNoBinding), function_type(kNoType),
			  object_rank(CONVERSION_INVALID),
			  object_distance(std::numeric_limits<std::size_t>::max()),
			  argument_offset(0) {}
	};
	std::vector<Candidate> candidates;
	if (!arguments.empty() && conversions.size() >
		std::numeric_limits<std::size_t>::max() / arguments.size())
		throw std::runtime_error("callable surrogate table is too large");
	std::vector<CallConversionFact> argument_facts;
	argument_facts.reserve(conversions.size() * arguments.size());
	CallConversionTable conversion_cache;
	for (std::size_t i = 0; i < conversions.size(); ++i)
	{
		++overload_candidates_;
		const BindingId binding = conversions[i];
		const FunctionInfo& conversion = GetFunction(binding);
		if (!conversion.conversion_function || conversion.explicit_conversion)
			continue;
		const TypeRecord& conversion_type =
			program_->types.Get(conversion.type);
		if (!RefQualifierViable(callee, conversion_type)) continue;
		const TypeId pointer_type = Decay(conversion.conversion_target);
		const TypeRecord& pointer = program_->types.Get(pointer_type);
		if (pointer.kind != TYPE_POINTER) continue;
		const TypeRecord& callable = program_->types.Get(pointer.child);
		if (callable.kind != TYPE_FUNCTION ||
			arguments.size() < callable.parameter_count ||
			(!callable.variadic &&
			 arguments.size() != callable.parameter_count))
			continue;

		TypeId object_type = conversion.member_owner;
		if ((conversion_type.cv & CV_CONST) != 0)
			object_type = program_->types.Qualify(object_type, CV_CONST);
		if ((conversion_type.cv & CV_VOLATILE) != 0)
			object_type = program_->types.Qualify(object_type, CV_VOLATILE);
		ExpressionInfo object = callee;
		object.type = program_->types.Pointer(EffectiveType(callee.type));
		const TypeId object_target = program_->types.Pointer(object_type);
		const ConversionRank object_rank = MemberObjectConversion(
			object, object_target, binding);
		if (object_rank == CONVERSION_INVALID) continue;

		Candidate candidate;
		candidate.binding = binding;
		candidate.function_type = pointer.child;
		candidate.object_rank = object_rank;
		if (object_rank == CONVERSION_DERIVED_TO_BASE)
			candidate.object_distance = BaseConversionDistance(
				object.type, object_target);
		candidate.argument_offset = argument_facts.size();
		const TypeId* parameters =
			program_->types.Parameters(candidate.function_type);
		bool viable = true;
		for (std::size_t a = 0; a < arguments.size(); ++a)
		{
			CallConversionFact fact;
			fact.rank = CONVERSION_ELLIPSIS;
			if (a < callable.parameter_count)
				fact = CallConversion(arguments[a], parameters[a],
					&conversion_cache, a);
			if (fact.rank == CONVERSION_INVALID) viable = false;
			argument_facts.push_back(fact);
		}
		if (viable) candidates.push_back(candidate);
		else argument_facts.resize(candidate.argument_offset);
	}
	if (candidates.empty()) return false;

	const auto better = [this, &candidates, &argument_facts, &arguments, &callee](
		std::size_t left, std::size_t right) -> bool
	{
		++overload_order_comparisons_;
		const Candidate& l = candidates[left];
		const Candidate& r = candidates[right];
		bool no_worse = l.object_rank <= r.object_rank;
		bool strictly_better = l.object_rank < r.object_rank;
		if (l.object_rank == r.object_rank &&
			l.object_rank == CONVERSION_DERIVED_TO_BASE)
		{
			if (l.object_distance > r.object_distance) no_worse = false;
			if (l.object_distance < r.object_distance) strictly_better = true;
		}
		for (std::size_t a = 0; a < arguments.size(); ++a)
		{
			const CallConversionFact& lf =
				argument_facts[l.argument_offset + a];
			const CallConversionFact& rf =
				argument_facts[r.argument_offset + a];
			if (lf.rank > rf.rank) no_worse = false;
			if (lf.rank < rf.rank) strictly_better = true;
			if (lf.rank == CONVERSION_USER_DEFINED &&
				rf.rank == CONVERSION_USER_DEFINED)
			{
				const int preference = CompareCallConversions(lf, rf);
				if (preference < 0) no_worse = false;
				if (preference > 0) strictly_better = true;
			}
		}
		if (!no_worse) return false;
		if (strictly_better) return true;
		const FunctionInfo& lfunction = GetFunction(l.binding);
		const FunctionInfo& rfunction = GetFunction(r.binding);
		const int object_preference = CompareImplicitObjectBindings(
			callee.category, program_->types.Get(lfunction.type),
			program_->types.Get(rfunction.type));
		return object_preference > 0;
	};

	std::size_t champion = 0;
	for (std::size_t i = 1; i < candidates.size(); ++i)
		if (better(i, champion)) champion = i;
	for (std::size_t i = 0; i < candidates.size(); ++i)
		if (i != champion && !better(champion, i))
			throw std::runtime_error("ambiguous callable surrogate");

	const Candidate& selected = candidates[champion];
	ExpressionInfo selected_callee = callee;
	if (selected_callee.category != VALUE_LVALUE &&
		dump_.nodes[selected_callee.node].kind != DUMP_TEMPORARY_OBJECT)
		selected_callee = MaterializeTemporary(selected_callee);
	ExpressionInfo object = MakeImplicitObjectPointer(selected_callee);
	ObjectConversionFact object_conversion;
	object_conversion.rank = selected.object_rank;
	if (selected.object_rank == CONVERSION_DERIVED_TO_BASE)
	{
		if (selected.object_distance ==
				std::numeric_limits<std::size_t>::max() ||
			selected.object_distance >
				std::numeric_limits<std::uint32_t>::max())
			throw std::logic_error(
				"callable surrogate has no bounded object path");
		object_conversion.base_projection_count =
			static_cast<std::uint32_t>(selected.object_distance);
	}
	const std::vector<NodeId> no_syntax;
	const std::vector<ExpressionInfo> no_arguments;
	const ExpressionInfo converted = BuildResolvedCall(selected.binding,
		scope, no_syntax, no_arguments, &object, kNoType,
		program_->bindings[selected.binding].member_owner,
		&object_conversion, 0);

	const TypeRecord& callable = program_->types.Get(selected.function_type);
	const TypeId* parameters =
		program_->types.Parameters(selected.function_type);
	const TypeRecord& returned = program_->types.Get(callable.child);
	const ValueCategory category = returned.kind == TYPE_LVALUE_REFERENCE ?
		VALUE_LVALUE : returned.kind == TYPE_RVALUE_REFERENCE ?
		VALUE_XVALUE : VALUE_PRVALUE;
	const std::uint32_t call = MakeDump(DUMP_CALL_EXPRESSION,
		callable.child, category);
	dump_.Add(call, converted.node);
	for (std::size_t a = 0; a < arguments.size(); ++a)
	{
		ExpressionInfo argument = arguments[a];
		if (a < callable.parameter_count)
			argument = ApplyCallArgument(argument, parameters[a],
				&argument_facts[selected.argument_offset + a]);
		dump_.Add(call, argument.node);
	}
	result->node = call;
	result->type = callable.child;
	result->category = category;
	++expression_count_;
	*result = ApplyTarget(*result, target);
	return true;
}

}
}
