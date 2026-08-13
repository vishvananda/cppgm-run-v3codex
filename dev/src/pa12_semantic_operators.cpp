#include "pa12_semantic_detail.h"

#include <climits>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{

static LogicalOperation ClassifyBuiltinLogicalOperation(
	const std::string& operation)
{
	return operation == "&&" ? LOGICAL_OPERATION_AND :
		operation == "||" ? LOGICAL_OPERATION_OR : LOGICAL_OPERATION_NONE;
}

bool SemanticAnalyzer::IsMeasurableObjectType(
	TypeId type, bool alignment_query)
{
	std::size_t multiplier = 1;
	while (true)
	{
		type = program_->types.RemoveTopCv(EffectiveType(type));
		const TypeRecord record = program_->types.Get(type);
		if (record.kind == TYPE_ARRAY)
		{
			if (!alignment_query &&
				(record.dependent_bound_parameter != kNoTemplateParameter ||
				 record.bound == 0 ||
				 record.bound > std::numeric_limits<std::size_t>::max() ||
				 multiplier > std::numeric_limits<std::size_t>::max() /
					static_cast<std::size_t>(record.bound)))
				return false;
			if (!alignment_query)
				multiplier *= static_cast<std::size_t>(record.bound);
			type = record.child;
			continue;
		}
		if (record.kind == TYPE_FUNDAMENTAL)
			return record.fundamental != FUND_VOID;
		if (record.kind == TYPE_POINTER ||
			record.kind == TYPE_LVALUE_REFERENCE ||
			record.kind == TYPE_RVALUE_REFERENCE ||
			record.kind == TYPE_MEMBER_POINTER)
			return true;
		if (record.kind != TYPE_NAMED) return false;
		EnsureClassDefinition(type);
		const EntityRecord& entity = program_->entities[record.entity];
		if (!entity.complete) return false;
		if (entity.flavor == NAMED_ENUM || entity.flavor == NAMED_ENUM_CLASS)
		{
			type = entity.underlying;
			continue;
		}
		const std::uint64_t extent = alignment_query ?
			entity.object_alignment : entity.object_size;
		if (!entity.layout_complete || extent == 0) return false;
		return alignment_query ||
			multiplier <= std::numeric_limits<std::size_t>::max() /
				static_cast<std::size_t>(extent);
	}
}

bool SemanticAnalyzer::IsPointerToCompleteObject(TypeId type)
{
	type = program_->types.RemoveTopCv(EffectiveType(type));
	const TypeRecord pointer = program_->types.Get(type);
	if (pointer.kind != TYPE_POINTER) return false;
	type = pointer.child;
	while (true)
	{
		type = program_->types.RemoveTopCv(type);
		const TypeRecord record = program_->types.Get(type);
		if (record.kind != TYPE_ARRAY)
		{
			if (record.kind == TYPE_FUNDAMENTAL)
				return record.fundamental != FUND_VOID;
			if (record.kind == TYPE_POINTER ||
				record.kind == TYPE_MEMBER_POINTER) return true;
			if (record.kind != TYPE_NAMED) return false;
			EnsureClassDefinition(type);
			return program_->entities[record.entity].complete;
		}
		if (record.bound == 0 ||
			record.dependent_bound_parameter != kNoTemplateParameter)
			return false;
		type = record.child;
	}
}

ExpressionInfo SemanticAnalyzer::AnalyzeSizeof(NodeId node, ScopeId scope)
{
	const NodeId operand = FirstSemanticChild(node);
	if (operand == kNoNode) throw std::runtime_error("empty sizeof");
	TypeId measured = kNoType;
	if (arena_->IsTag(operand, "type-id"))
	{
		const NodeId specifiers = FindChild(operand, "type-specifier-seq");
		const NodeId name = specifiers == kNoNode ? kNoNode :
			FirstSemanticChild(specifiers);
		const NodeId declarator = FindChild(operand, "abstract-declarator");
		const NodeId clause = declarator == kNoNode ? kNoNode :
			FindChild(declarator, "parameter-clause");
		NamePath base;
		std::vector<TypeId> explicit_arguments;
		const bool ambiguous_function_call = name != kNoNode &&
			arena_->IsTag(name, "type-name") && clause != kNoNode &&
			FirstSemanticChild(clause) == kNoNode &&
			ParseExplicitTemplateArguments(
				name, scope, &base, &explicit_arguments);
		if (ambiguous_function_call)
		{
			const std::vector<std::size_t> patterns =
				FindFunctionTemplates(scope, base);
			std::vector<BindingId> candidates;
			const std::vector<ExpressionInfo> no_arguments;
			DeduceFunctionTemplatePatterns(patterns, no_arguments,
				&candidates, &explicit_arguments);
			if (candidates.size() == 1)
				measured = program_->types.Get(
					GetFunction(candidates[0]).type).child;
			else if (!candidates.empty())
				throw std::runtime_error(
					"ambiguous function template in sizeof expression");
		}
		if (measured == kNoType && name != kNoNode &&
			arena_->IsTag(name, "type-name"))
		{
			const NodeId structure = FindChild(name, "structured-type-name");
			const LookupResult value = structure != kNoNode ?
				LookupStructuredName(name, scope, LOOKUP_ORDINARY) :
				LookupSpelling(scope, PayloadSource(name), LOOKUP_ORDINARY);
			if (value.ordinary != kNoBinding)
			{
				measured = EffectiveType(
					program_->bindings[value.ordinary].type);
				for (std::uint32_t edge = declarator == kNoNode ? kNoEdge :
					arena_->FirstEdge(declarator); edge != kNoEdge;
					edge = arena_->NextEdge(edge))
					if (arena_->IsTag(arena_->EdgeChild(edge), "array-suffix"))
					{
						const TypeRecord array = program_->types.Get(
							program_->types.RemoveTopCv(measured));
						if (array.kind != TYPE_ARRAY)
							throw std::runtime_error(
								"sizeof subscript recovery requires an array");
						measured = array.child;
					}
			}
		}
		if (measured == kNoType) measured = BuildTypeId(operand, scope);
	}
	else if (arena_->IsTag(operand, "id-expression"))
	{
		const std::string spelling = arena_->Payload(operand);
		const NodeId structure = FindChild(operand, "structured-type-name");
		const LookupResult ordinary = structure != kNoNode ?
			LookupStructuredName(operand, scope, LOOKUP_ORDINARY) :
			LookupSpelling(scope, spelling, LOOKUP_ORDINARY);
		if (ordinary.ordinary == kNoBinding)
		{
			const LookupResult type = structure != kNoNode ?
				LookupStructuredName(operand, scope, LOOKUP_TYPE) :
				LookupSpelling(scope, spelling, LOOKUP_TYPE);
			if (type.type != kNoType) measured = type.type;
		}
		else measured = EffectiveType(
			program_->bindings[ordinary.ordinary].type);
	}
	if (measured == kNoType)
	{
		++unevaluated_depth_;
		try
		{
			measured = EffectiveType(AnalyzeExpression(operand, scope).type);
		}
		catch (...)
		{
			--unevaluated_depth_;
			throw;
		}
		--unevaluated_depth_;
	}
	const bool alignment_query = arena_->IsTag(node, "type-trait-expression");
	if (CandidateSubstitutionFailed() || measured == kNoType)
		return ExpressionInfo();
	measured = EffectiveType(measured);
	if (!IsMeasurableObjectType(measured, alignment_query) &&
		FunctionTemplateTypeIsDependent(measured) &&
		!CandidateSubstitutionActive())
	{
		ExpressionInfo result;
		result.type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
		result.node = MakeDump(DUMP_SIZEOF_EXPRESSION,
			result.type, VALUE_PRVALUE);
		result.constant = true;
		result.value = 0;
		dump_.nodes[result.node].template_parameter_constant = true;
		dump_.nodes[result.node].template_layout_constant = true;
		RecordExpressionFacts(result);
		++expression_count_;
		return result;
	}
	if (!IsMeasurableObjectType(measured, alignment_query))
		return CandidateExpressionFailure(
			alignment_query ? "invalid alignof operand type" :
			"invalid sizeof operand type");
	const std::size_t value = alignment_query ? program_->AlignOf(measured) :
		program_->SizeOf(measured);
	ExpressionInfo result;
	result.type = program_->types.Fundamental(FUND_UNSIGNED_LONG_INT);
	result.node = MakeDump(DUMP_SIZEOF_EXPRESSION, result.type, VALUE_PRVALUE);
	dump_.nodes[result.node].template_layout_constant =
		IsClassTemplateSpecializationContext(EntityOf(measured));
	result.constant = true;
	result.value = static_cast<std::int64_t>(value);
	RecordExpressionFacts(result);
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeUnary(NodeId node, ScopeId scope, TypeId target) {
	const bool postfix = arena_->IsTag(node, "postfix-expression"); const std::string operation = PayloadSource(node);
	const NodeId operand_syntax = FirstSemanticChild(node);
	const TypeId address_context_target = UnaryAddressContextTarget(operation, target, operand_syntax, scope);
	const TypeId operand_target =
		UnaryAddressOperandTarget(operation, address_context_target);
	ExpressionInfo operand = AnalyzeExpression(operand_syntax, scope, operand_target);
	if (CandidateSubstitutionFailed()) return operand;
	if (operand.type == kNoType)
	{
		if (operation == "&" && target == kNoType) return operand;
		throw std::runtime_error("unresolved unary operand");
	}
	std::vector<NodeId> overloaded_syntax(1, operand_syntax);
	std::vector<ExpressionInfo> overloaded_operands(1, operand);
	if (postfix && (operation == "++" || operation == "--"))
	{
		ExpressionInfo dummy = MakeLiteral(
			program_->types.Fundamental(FUND_INT), program_->names.Intern("0"));
		dummy.constant = true;
		dummy.value = 0;
		dummy.integer_literal_zero = true;
		overloaded_syntax.push_back(kNoNode);
		overloaded_operands.push_back(dummy);
	}
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator(operation, scope, overloaded_syntax,
		overloaded_operands, false, target, &overloaded)) return overloaded;
	const std::uint32_t operand_object = ExpressionObject(operand),
		operand_complete_object = ExpressionCompleteObject(operand);
	(void)ApplyBuiltinUnaryConversion(operation, &operand);
	const TypeId address_target = MemberPointerAddressTarget(
		operand, operand_syntax, address_context_target);
	const bool member_pointer_address =
		address_target != kNoType && IsMemberPointer(address_target);
	if (operation == "&" && operand.binding != kNoBinding &&
		!member_pointer_address)
		EnsureStaticMemberStorage(operand.binding, true);
	if (operation == "&" && operand.binding != kNoBinding &&
		program_->bindings[operand.binding].bit_field)
		throw std::runtime_error("address-of bit-field unsupported");
	TypeId result_type = EffectiveType(operand.type);
	ValueCategory category = VALUE_PRVALUE;
	bool constant = operand.constant;
	ConstexprScalarValue scalar;
	std::uint32_t address = ExpressionAddress(operand);
	if (address == kNoConstexprAddress &&
		(operation == "*" || operation == "+") &&
		IsPointer(Decay(operand.type)))
		address = LvalueAddress(&operand);
	std::uint32_t lvalue_address = kNoConstexprAddress;
	BindingId selected_member = kNoBinding;
	if (constant && address == kNoConstexprAddress)
		scalar = ExpressionScalar(operand);
	if (operation == "&")
	{
		if (operand.category != VALUE_LVALUE)
			throw std::runtime_error("address-of requires lvalue");
		if (member_pointer_address)
		{
			if (!FormMemberPointerAddress(operand, address_target, &result_type,
				&constant, &scalar, &selected_member))
				return CandidateExpressionFailure(
					"invalid member pointer address conversion");
		}
		else
		{
			result_type = program_->types.Pointer(result_type);
			address = LvalueAddress(&operand);
			constant = address != kNoConstexprAddress;
		}
	}
	else if (operation == "*")
	{
		TypeId decayed = Decay(result_type);
		const TypeRecord pointer = program_->types.Get(decayed);
		if (pointer.kind != TYPE_POINTER)
			throw std::runtime_error("dereference requires pointer");
		result_type = pointer.child;
		category = VALUE_LVALUE;
		lvalue_address = address;
		constant = false;
	}
	else if (operation == "++" || operation == "--")
	{
		if (!IsModifiableLvalue(operand) ||
			(!IsArithmetic(result_type) && !IsPointer(result_type)) ||
			(IsPointer(result_type) &&
			 !IsPointerToCompleteObject(result_type)))
		{
			if (CandidateSubstitutionActive())
				return CandidateSubstitutionFailure();
			throw std::runtime_error("invalid increment operand");
		}
		category = postfix ? VALUE_PRVALUE : VALUE_LVALUE;
		constant = false;
		if (constexpr_evaluation_depth_ != 0 &&
			constant_evaluation_suppressed_depth_ == 0 &&
			operand.constexpr_local < constexpr_locals_.size() &&
			operand.constant)
		{
			ConstexprLocalValue& local =
				constexpr_locals_[operand.constexpr_local];
			if (IsPointer(result_type) &&
				local.address != kNoConstexprAddress)
			{
				const TypeRecord pointer = program_->types.Get(
					program_->types.RemoveTopCv(result_type));
				const std::int64_t step = static_cast<std::int64_t>(
					program_->SizeOf(pointer.child));
				const std::uint32_t previous = local.address;
				const std::uint32_t updated = OffsetConstexprAddress(previous,
					operation == "++" ? step : -step, false);
				if (updated != kNoConstexprAddress)
				{
					local.address = updated;
					address = postfix ? previous : updated;
					constant = true;
				}
			}
			else if (IsIntegral(result_type, true) || IsFloating(result_type))
			{
				const ConstexprScalarValue previous = local.value;
				const ConstexprScalarValue one = IsFloating(result_type) ?
					ConstexprScalarValue(1.0L) :
					ConstexprScalarValue(static_cast<std::int64_t>(1));
				const ConstexprScalarValue updated = ApplyConstantScalarBinary(
					operation == "++" ? "+" : "-", previous, one, result_type);
				local.value = updated;
				constant = true;
				scalar = postfix ? previous : updated;
			}
		}
	}
	else if (operation == "!")
	{
		const TypeRecord operand_shape = program_->types.Get(
			program_->types.RemoveTopCv(EffectiveType(result_type)));
		if (!IsArithmetic(result_type) && !IsPointer(Decay(result_type)) &&
			!IsNullptr(result_type) &&
			operand_shape.kind != TYPE_MEMBER_POINTER)
			throw std::runtime_error("invalid logical-not operand");
		result_type = program_->types.Fundamental(FUND_BOOL);
		if (constant) scalar = ConstexprScalarValue(
			static_cast<std::int64_t>(!ExpressionTruth(operand)));
		address = kNoConstexprAddress;
	}
	else if (operation == "+" || operation == "-" || operation == "~")
	{
		if (operation == "+" && IsPointer(Decay(result_type)))
		{
			result_type = Decay(result_type);
			constant = address != kNoConstexprAddress;
		}
		else if ((operation == "~" && !IsIntegral(result_type)) ||
			(operation != "~" && !IsArithmetic(result_type)))
			throw std::runtime_error("invalid unary arithmetic operand");
		else if (IsIntegral(result_type) &&
			(IntegralRank(result_type) < 3 ||
			 program_->types.Get(program_->types.RemoveTopCv(result_type)).kind ==
				TYPE_NAMED))
			result_type = program_->types.Fundamental(FUND_INT);
		if (constant && IsIntegral(result_type, true) && IntegralWidth(result_type) > 64) constant = false;
		if (constant)
		{
			if (IsFloating(result_type))
			{
				if (operation == "-") scalar.floating = -scalar.floating;
				scalar = NormalizeScalarConstant(result_type, scalar);
			}
			else if (operation == "-")
			{
				const std::size_t width = IntegralWidth(result_type);
				const std::int64_t minimum = width == 64 ? INT64_MIN :
					- static_cast<std::int64_t>(
						std::uint64_t(1) << (width - 1));
				if (!IsUnsignedIntegral(result_type) && scalar.integral == minimum)
					throw std::runtime_error(
						"signed constant unary negation overflow");
				scalar.integral = static_cast<std::int64_t>(
					- static_cast<std::uint64_t>(scalar.integral));
			}
			else if (operation == "~") scalar.integral = ~scalar.integral;
			if (IsIntegral(result_type, true))
				scalar = NormalizeScalarConstant(result_type, scalar);
		}
	}
	else throw std::runtime_error("unsupported unary operator");
	const std::uint32_t expression = MakeDump(postfix ?
		DUMP_POSTFIX_EXPRESSION : DUMP_UNARY_EXPRESSION,
		result_type, category, program_->names.Intern(arena_->Payload(node)));
	if (member_pointer_address)
		RecordMemberPointerAddressFacts(expression, selected_member);
	dump_.Add(expression, operand.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = result_type;
	result.category = category;
	if (member_pointer_address) result.binding = selected_member;
	if (constant && address != kNoConstexprAddress)
		SetExpressionAddress(&result, address);
	else if (constant) SetExpressionScalar(&result, scalar);
	if (lvalue_address != kNoConstexprAddress)
		SetExpressionLvalueAddress(&result, lvalue_address);
	if (operation == "&" && operand_object != kNoConstexprObject)
		SetExpressionSubobject(
			&result, operand_object, operand_complete_object);
	if (operation == "*" && operand_object != kNoConstexprObject)
		SetExpressionSubobject(
			&result, operand_object, operand_complete_object);
	if (operation == "*" && lvalue_address != kNoConstexprAddress)
	{
		const ConstexprAddressValue* pointed =
			ConstexprAddressAt(lvalue_address);
		if (pointed && pointed->kind == CONSTEXPR_ADDRESS_BINDING &&
			pointed->identity < program_->bindings.size() &&
			program_->bindings[static_cast<BindingId>(pointed->identity)].constant &&
			program_->types.Get(program_->types.RemoveTopCv(EffectiveType(
				program_->bindings[static_cast<BindingId>(
					pointed->identity)].type))).kind == TYPE_ARRAY &&
			pointed->offset >= 0)
		{
			const std::uint32_t object = BindingObject(
				static_cast<BindingId>(pointed->identity));
			const std::int64_t step = static_cast<std::int64_t>(
				program_->SizeOf(result_type));
			if (object != kNoConstexprObject && step > 0 &&
				pointed->offset < pointed->upper_bound &&
				pointed->offset % step == 0)
			{
				const ConstexprObjectElement* element =
					ConstexprObjectElementAt(object,
						static_cast<std::size_t>(pointed->offset / step));
				if (element) SetExpressionObjectElement(&result, *element);
			}
		}
	}
	++expression_count_;
	return result;
}

ExpressionInfo SemanticAnalyzer::AnalyzeBinary(NodeId node, ScopeId scope)
{
	const std::uint32_t first_edge = arena_->FirstEdge(node);
	if (first_edge == kNoEdge) throw std::runtime_error("empty binary expression");
	const std::uint32_t second_edge = arena_->NextEdge(first_edge);
	if (second_edge == kNoEdge) throw std::runtime_error("unary binary expression");
	const NodeId left_syntax = arena_->EdgeChild(first_edge);
	const NodeId right_syntax = arena_->EdgeChild(second_edge);
	ExpressionInfo left = AnalyzeExpression(left_syntax, scope);
	if (CandidateSubstitutionFailed()) return left;
	const std::string operation = PayloadSource(node);
	const bool short_circuit = left.constant &&
		((operation == "&&" && !ExpressionTruth(left)) ||
		 (operation == "||" && ExpressionTruth(left)));
	if (short_circuit) ++constant_evaluation_suppressed_depth_;
	ExpressionInfo right;
	try
	{
			right = AnalyzeExpression(right_syntax, scope);
	}
	catch (...)
	{
		if (short_circuit) --constant_evaluation_suppressed_depth_;
		throw;
	}
	if (short_circuit) --constant_evaluation_suppressed_depth_;
	if (CandidateSubstitutionFailed()) return right;
	return BuildBinaryExpression(operation, arena_->Payload(node),
		left_syntax, right_syntax, left, right, scope);
}

bool SemanticAnalyzer::PrepareBuiltinComparison(const std::string& operation,
	ExpressionInfo* left, ExpressionInfo* right, TypeId* operand_type)
{
	const bool equality = operation == "==" || operation == "!=";
	const TypeId left_unqualified = program_->types.RemoveTopCv(
		EffectiveType(left->type));
	const TypeId right_unqualified = program_->types.RemoveTopCv(
		EffectiveType(right->type));
	const EntityId comparison_enum = left_unqualified == right_unqualified ?
		EntityOf(left_unqualified) : kNoEntity;
	if (comparison_enum != kNoEntity &&
		(program_->entities[comparison_enum].flavor == NAMED_ENUM ||
		 program_->entities[comparison_enum].flavor == NAMED_ENUM_CLASS))
		*operand_type = left_unqualified;
	else if (IsArithmetic(left->type) && IsArithmetic(right->type))
		*operand_type = CommonArithmeticType(left->type, right->type);
	else if (IsNullptr(left->type) && IsNullptr(right->type) && equality)
		*operand_type = left_unqualified;
	else if (equality &&
		program_->types.Get(left_unqualified).kind == TYPE_MEMBER_POINTER &&
		left_unqualified == right_unqualified)
		*operand_type = left_unqualified;
	else if (equality &&
		program_->types.Get(left_unqualified).kind == TYPE_MEMBER_POINTER &&
		(IsNullptr(right->type) || right->integer_literal_zero))
	{
		*operand_type = left_unqualified;
		*right = ApplyTarget(*right, left_unqualified);
	}
	else if (equality &&
		program_->types.Get(right_unqualified).kind == TYPE_MEMBER_POINTER &&
		(IsNullptr(left->type) || left->integer_literal_zero))
	{
		*operand_type = right_unqualified;
		*left = ApplyTarget(*left, right_unqualified);
	}
	else if (IsPointer(Decay(left->type)) && IsPointer(Decay(right->type)))
	{
		const TypeId left_pointer = Decay(left->type);
		const TypeId right_pointer = Decay(right->type);
		const ConversionRank right_to_left = Conversion(*right, left_pointer);
		const ConversionRank left_to_right = Conversion(*left, right_pointer);
		if (right_to_left != CONVERSION_INVALID &&
			(left_to_right == CONVERSION_INVALID ||
			 right_to_left <= left_to_right))
		{
			*operand_type = left_pointer;
			*right = ApplyTarget(*right, left_pointer);
		}
		else if (left_to_right != CONVERSION_INVALID)
		{
			*operand_type = right_pointer;
			*left = ApplyTarget(*left, right_pointer);
		}
		else
		{
			const TypeRecord& left_shape = program_->types.Get(left_pointer);
			const TypeRecord& right_shape = program_->types.Get(right_pointer);
			TypeId left_object = program_->types.RemoveTopCv(left_shape.child);
			TypeId right_object = program_->types.RemoveTopCv(right_shape.child);
			const EntityId left_entity = EntityOf(left_object);
			const EntityId right_entity = EntityOf(right_object);
			TypeId composite_object = kNoType;
			if (left_object == right_object)
				composite_object = left_object;
			else if (left_entity != kNoEntity && right_entity != kNoEntity &&
				BaseConversionAllowed(left_entity, right_entity))
				composite_object = right_object;
			else if (left_entity != kNoEntity && right_entity != kNoEntity &&
				BaseConversionAllowed(right_entity, left_entity))
				composite_object = left_object;
			if (composite_object == kNoType)
			{
				(void)CandidateExpressionFailure(
					"invalid pointer comparison operands");
				return false;
			}
			const TypeRecord& left_cv = program_->types.Get(left_shape.child);
			const TypeRecord& right_cv = program_->types.Get(right_shape.child);
			const std::uint8_t cv =
				(left_cv.kind == TYPE_QUALIFIED ? left_cv.cv : CV_NONE) |
				(right_cv.kind == TYPE_QUALIFIED ? right_cv.cv : CV_NONE);
			if (cv != CV_NONE)
				composite_object = program_->types.Qualify(composite_object, cv);
			*operand_type = program_->types.Pointer(composite_object);
			*left = ApplyTarget(*left, *operand_type);
			*right = ApplyTarget(*right, *operand_type);
		}
	}
	else if (IsPointer(Decay(left->type)) &&
		((right->integer_literal_zero && equality) || IsNullptr(right->type)))
		SetExpressionAddress(right, NullConstexprAddress());
	else if (IsPointer(Decay(right->type)) &&
		((left->integer_literal_zero && equality) || IsNullptr(left->type)))
		SetExpressionAddress(left, NullConstexprAddress());
	else
	{
		(void)CandidateExpressionFailure("invalid comparison operands");
		return false;
	}
	return true;
}

TypeId SemanticAnalyzer::PrepareBuiltinArithmetic(
	const std::string& operation, const ExpressionInfo& left,
	const ExpressionInfo& right)
{
	const bool integral_only = operation == "%" || operation == "<<" ||
		operation == ">>" || operation == "&" || operation == "|" ||
		operation == "^";
	if ((integral_only &&
		(!IsIntegral(left.type) || !IsIntegral(right.type))) ||
		(!integral_only &&
		 (!IsArithmetic(left.type) || !IsArithmetic(right.type))))
	{
		(void)CandidateExpressionFailure("invalid binary arithmetic operands");
		return kNoType;
	}
	return operation == "<<" || operation == ">>" ?
		IntegralPromotionType(left.type) :
		CommonArithmeticType(left.type, right.type);
}

ExpressionInfo SemanticAnalyzer::BuildBinaryExpression(
	const std::string& operation, const std::string& display_operation,
	NodeId left_syntax, NodeId right_syntax, ExpressionInfo left,
	ExpressionInfo right, ScopeId scope)
{
	ExpressionInfo typeid_comparison; if (TryAnalyzeTypeidComparison(operation, display_operation, left_syntax, right_syntax, left, right, scope, &typeid_comparison)) return typeid_comparison;
	std::vector<NodeId> overloaded_syntax;
	overloaded_syntax.push_back(left_syntax);
	overloaded_syntax.push_back(right_syntax);
	std::vector<ExpressionInfo> overloaded_operands;
	overloaded_operands.push_back(left);
	overloaded_operands.push_back(right);
	std::vector<ConversionRank> builtin_ranks;
	const bool builtin_viable = ApplyBuiltinBinaryConversions(operation,
		&left, &right, &builtin_ranks, false);
	const bool builtin_competes = builtin_viable && operation != ",";
	ExpressionInfo overloaded;
	if (TryAnalyzeOverloadedOperator(operation, scope, overloaded_syntax,
		overloaded_operands, false, kNoType, &overloaded,
		builtin_competes ? &builtin_ranks : 0)) return overloaded;
	ExpressionInfo member_pointer;
	if (TryAnalyzeMemberPointerApplication(operation, display_operation,
		left, right, &member_pointer)) return member_pointer;
	(void)ApplyBuiltinBinaryConversions(operation, &left, &right);
	TypeId result_type = kNoType;
	TypeId operand_type = kNoType;
	ValueCategory result_category = VALUE_PRVALUE;
	if (operation == "&&" || operation == "||")
	{
		if (!IsBuiltinLogicalOperand(left) || !IsBuiltinLogicalOperand(right))
			throw std::runtime_error("invalid logical operands");
		result_type = program_->types.Fundamental(FUND_BOOL);
	}
	else if (operation == "==" || operation == "!=" || operation == "<" ||
		operation == ">" || operation == "<=" || operation == ">=")
	{
		if (!PrepareBuiltinComparison(
			operation, &left, &right, &operand_type)) return ExpressionInfo();
		result_type = program_->types.Fundamental(FUND_BOOL);
	}
	else if (operation == ",")
	{
		left = MaterializeDiscardedClassResult(left);
		result_type = EffectiveType(right.type);
		result_category = right.category;
	}
	else if (operation == "+" || operation == "-")
	{
		if (IsPointer(Decay(left.type)) && IsIntegral(right.type))
		{
			if (!IsPointerToCompleteObject(Decay(left.type)))
			{
				if (CandidateSubstitutionActive())
					return CandidateSubstitutionFailure();
				throw std::runtime_error(
					"arithmetic on pointer to incomplete or non-object type");
			}
			result_type = Decay(left.type);
		}
		else if (operation == "+" && IsIntegral(left.type) &&
			IsPointer(Decay(right.type)))
		{
			if (!IsPointerToCompleteObject(Decay(right.type)))
			{
				if (CandidateSubstitutionActive())
					return CandidateSubstitutionFailure();
				throw std::runtime_error(
					"arithmetic on pointer to incomplete or non-object type");
			}
			result_type = Decay(right.type);
		}
		else if (operation == "-" && IsPointer(Decay(left.type)) &&
			IsPointer(Decay(right.type)))
		{
			if (!IsPointerToCompleteObject(Decay(left.type)) ||
				!IsPointerToCompleteObject(Decay(right.type)))
			{
				if (CandidateSubstitutionActive())
					return CandidateSubstitutionFailure();
				throw std::runtime_error(
					"subtraction on pointer to incomplete or non-object type");
			}
			result_type = program_->types.Fundamental(FUND_LONG_INT);
		}
		else if (IsArithmetic(left.type) && IsArithmetic(right.type))
			result_type = operand_type = CommonArithmeticType(left.type, right.type);
		else throw std::runtime_error("invalid additive operands");
	}
	else
	{
		result_type = operand_type =
			PrepareBuiltinArithmetic(operation, left, right);
		if (result_type == kNoType) return ExpressionInfo();
	}
	const std::uint32_t expression = MakeDump(DUMP_BINARY_EXPRESSION, result_type,
		result_category, program_->names.Intern(display_operation));
	dump_.nodes[expression].operand_type = operand_type;
	dump_.nodes[expression].logical_operation = ClassifyBuiltinLogicalOperation(operation);
	dump_.Add(expression, left.node);
	dump_.Add(expression, right.node);
	ExpressionInfo result;
	result.node = expression;
	result.type = result_type;
	result.category = result_category;
	const bool short_circuit = left.constant &&
		((operation == "&&" && !ExpressionTruth(left)) ||
		 (operation == "||" && ExpressionTruth(left)));
	result.constant = constant_evaluation_suppressed_depth_ == 0 && (short_circuit || (left.constant && right.constant));
	if (result.constant && operand_type != kNoType && IsIntegral(operand_type, true) && IntegralWidth(operand_type) > 64) result.constant = false;
	if (result.constant)
	{
		if (operation == ",")
		{
			result = right;
			result.node = expression;
			result.type = result_type;
			result.category = result_category;
			RecordExpressionFacts(result);
			++expression_count_;
			return result;
		}
		std::uint32_t left_address = ExpressionAddress(left);
		std::uint32_t right_address = ExpressionAddress(right);
		if (left_address == kNoConstexprAddress &&
			IsPointer(Decay(left.type)))
			left_address = LvalueAddress(&left);
		if (right_address == kNoConstexprAddress &&
			IsPointer(Decay(right.type)))
			right_address = LvalueAddress(&right);
		if (operation == "&&" || operation == "||")
		{
			SetExpressionScalar(&result, ConstexprScalarValue(
				static_cast<std::int64_t>(short_circuit ?
					ExpressionTruth(left) :
					(operation == "&&" ?
					 ExpressionTruth(left) && ExpressionTruth(right) :
					 ExpressionTruth(left) || ExpressionTruth(right)))));
			++expression_count_;
			return result;
		}
		if ((operation == "==" || operation == "!=" || operation == "<" ||
			operation == ">" || operation == "<=" || operation == ">=") &&
			(left_address != kNoConstexprAddress ||
			 right_address != kNoConstexprAddress))
		{
			const ConstexprAddressValue* a = ConstexprAddressAt(left_address);
			const ConstexprAddressValue* b = ConstexprAddressAt(right_address);
			if (!a || !b)
				result.constant = false;
			else
			{
				const bool same_base = a->kind == b->kind &&
					a->identity == b->identity;
				bool compared = false;
				if (operation == "==")
					compared = same_base && a->offset == b->offset;
				else if (operation == "!=")
					compared = !(same_base && a->offset == b->offset);
				else if (!same_base) result.constant = false;
				else if (operation == "<") compared = a->offset < b->offset;
				else if (operation == ">") compared = a->offset > b->offset;
				else if (operation == "<=") compared = a->offset <= b->offset;
				else compared = a->offset >= b->offset;
				if (result.constant) SetExpressionScalar(&result,
					ConstexprScalarValue(static_cast<std::int64_t>(compared)));
			}
			++expression_count_;
			return result;
		}
		if ((operation == "+" || operation == "-") &&
			(left_address != kNoConstexprAddress ||
			 right_address != kNoConstexprAddress))
		{
			if (left_address != kNoConstexprAddress &&
				right_address != kNoConstexprAddress)
			{
				const ConstexprAddressValue* a =
					ConstexprAddressAt(left_address);
				const ConstexprAddressValue* b =
					ConstexprAddressAt(right_address);
				if (operation != "-" || !a || !b || a->kind != b->kind ||
					a->identity != b->identity)
					result.constant = false;
				else
				{
					const TypeRecord pointer = program_->types.Get(
						program_->types.RemoveTopCv(Decay(left.type)));
					const std::int64_t step = static_cast<std::int64_t>(
						program_->SizeOf(pointer.child));
					if (step == 0 || (a->offset - b->offset) % step != 0)
						result.constant = false;
					else SetExpressionScalar(&result, ConstexprScalarValue(
						(a->offset - b->offset) / step));
				}
			}
			else
			{
				const bool pointer_left = left_address != kNoConstexprAddress;
				const ExpressionInfo& index = pointer_left ? right : left;
				const TypeId pointer_type = Decay(
					(pointer_left ? left : right).type);
				const TypeRecord pointer = program_->types.Get(
					program_->types.RemoveTopCv(pointer_type));
				const std::int64_t count = ExpressionScalar(index).integral;
				const std::int64_t step = static_cast<std::int64_t>(
					program_->SizeOf(pointer.child));
				if (step != 0 && (count >
					std::numeric_limits<std::int64_t>::max() / step ||
					count < std::numeric_limits<std::int64_t>::min() / step))
					result.constant = false;
				else
				{
					std::int64_t delta = count * step;
					if (operation == "-" && pointer_left) delta = -delta;
					const std::uint32_t advanced = OffsetConstexprAddress(
						pointer_left ? left_address : right_address,
						delta, false);
					if (advanced == kNoConstexprAddress)
						result.constant = false;
					else SetExpressionAddress(&result, advanced);
				}
			}
			++expression_count_;
			return result;
		}
		ConstexprScalarValue left_value = ExpressionScalar(left);
		ConstexprScalarValue right_value = ExpressionScalar(right);
		if (operand_type != kNoType &&
			(IsIntegral(operand_type, true) || IsFloating(operand_type)))
		{
			left_value = ConvertScalarConstant(
				left.type, operand_type, left_value);
			right_value = ConvertScalarConstant(
				right.type, operand_type, right_value);
		}
		SetExpressionScalar(&result, short_circuit ?
			ConstexprScalarValue(static_cast<std::int64_t>(
				ScalarTruth(left_value))) :
			ApplyConstantScalarBinary(operation, left_value, right_value,
				operand_type != kNoType ? operand_type : result_type));
	}
	++expression_count_;
	return result;
}

}
}
