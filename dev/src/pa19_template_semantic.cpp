#include "pa12_semantic_detail.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace cppgm
{
namespace pa12_semantic_detail
{
namespace
{

std::size_t NoTemplatePattern()
{
	return std::numeric_limits<std::size_t>::max();
}

bool ClassTemplateArgumentsAreComplete(const Program& program,
	const std::vector<TypeId>& arguments)
{
	for (std::size_t i = 0; i < arguments.size(); ++i)
	{
		const TypeId argument = program.types.RemoveTopCv(arguments[i]);
		const TypeRecord& record = program.types.Get(argument);
		if (record.kind == TYPE_NAMED &&
			!program.entities[record.entity].complete)
			return false;
	}
	return true;
}

std::string TemplateArgumentName(const std::string& source)
{
	std::string spelling = source;
	const char* prefixes[] = {"struct ", "class ", "union "};
	for (std::size_t prefix = 0; prefix < 3; ++prefix)
	{
		const std::size_t length =
			std::char_traits<char>::length(prefixes[prefix]);
		if (spelling.compare(0, length, prefixes[prefix]) == 0)
		{
			spelling.erase(0, length);
			break;
		}
	}
	std::string result;
	result.reserve(spelling.size());
	bool underscore = false;
	for (std::size_t i = 0; i < spelling.size(); ++i)
	{
		const unsigned char character =
			static_cast<unsigned char>(spelling[i]);
		if (std::isalnum(character))
		{
			result += static_cast<char>(character);
			underscore = false;
		}
		else if (!underscore)
		{
			result += '_';
			underscore = true;
		}
	}
	while (!result.empty() && result[result.size() - 1] == '_')
		result.erase(result.size() - 1);
	return result.empty() ? "type" : result;
}

std::string TrimTypeSpelling(const std::string& source)
{
	const std::size_t first = source.find_first_not_of(" \t\r\n");
	if (first == std::string::npos) return std::string();
	const std::size_t last = source.find_last_not_of(" \t\r\n");
	return source.substr(first, last - first + 1);
}

bool RemoveTrailingTypeWord(std::string* spelling, const char* word)
{
	const std::size_t length = std::char_traits<char>::length(word);
	if (spelling->size() < length ||
		spelling->compare(spelling->size() - length, length, word) != 0)
		return false;
	const std::size_t first = spelling->size() - length;
	if (first != 0)
	{
		const unsigned char previous =
			static_cast<unsigned char>((*spelling)[first - 1]);
		if (std::isalnum(previous) || previous == '_') return false;
	}
	spelling->erase(first);
	*spelling = TrimTypeSpelling(*spelling);
	return true;
}

bool RemoveLeadingTypeWord(std::string* spelling, const char* word)
{
	const std::size_t length = std::char_traits<char>::length(word);
	if (spelling->size() < length || spelling->compare(0, length, word) != 0)
		return false;
	if (spelling->size() != length)
	{
		const unsigned char next =
			static_cast<unsigned char>((*spelling)[length]);
		if (std::isalnum(next) || next == '_') return false;
	}
	spelling->erase(0, length);
	*spelling = TrimTypeSpelling(*spelling);
	return true;
}

bool ParseRawTemplateComponent(const std::string& spelling,
	std::string* base, std::vector<std::string>* arguments)
{
	const std::size_t open = spelling.find('<');
	if (open == std::string::npos || spelling.empty() ||
		spelling[spelling.size() - 1] != '>') return false;
	*base = spelling.substr(0, open);
	arguments->clear();
	std::size_t first = open + 1;
	std::size_t angle_depth = 0, paren_depth = 0, bracket_depth = 0;
	for (std::size_t i = first; i + 1 < spelling.size(); ++i)
	{
		if (spelling[i] == '<') ++angle_depth;
		else if (spelling[i] == '>')
		{
			if (angle_depth == 0) return false;
			--angle_depth;
		}
		else if (spelling[i] == '(') ++paren_depth;
		else if (spelling[i] == ')' && paren_depth != 0) --paren_depth;
		else if (spelling[i] == '[') ++bracket_depth;
		else if (spelling[i] == ']' && bracket_depth != 0) --bracket_depth;
		else if (spelling[i] == ',' && angle_depth == 0 &&
			paren_depth == 0 && bracket_depth == 0)
		{
			arguments->push_back(TrimTypeSpelling(
				spelling.substr(first, i - first)));
			first = i + 1;
		}
	}
	if (first + 1 < spelling.size())
		arguments->push_back(TrimTypeSpelling(spelling.substr(
			first, spelling.size() - first - 1)));
	return angle_depth == 0 && paren_depth == 0 && bracket_depth == 0;
}

}

bool SemanticAnalyzer::IsDeclaration(NodeId node) const
{
	return arena_->IsTag(node, "simple-declaration") ||
		arena_->IsTag(node, "function-definition") ||
		arena_->IsTag(node, "alias-declaration") ||
		arena_->IsTag(node, "using-declaration") ||
		arena_->IsTag(node, "using-directive") ||
		arena_->IsTag(node, "namespace-definition") ||
		arena_->IsTag(node, "namespace-alias-definition") ||
		arena_->IsTag(node, "template-declaration") ||
		arena_->IsTag(node, "explicit-instantiation-declaration") ||
		arena_->IsTag(node, "explicit-instantiation-definition") ||
		arena_->IsTag(node, "special-member-declaration") ||
		arena_->IsTag(node, "special-member-definition") ||
		arena_->IsTag(node, "class-specifier") ||
		arena_->IsTag(node, "class-forward-declaration") ||
		arena_->IsTag(node, "enum-specifier") ||
		arena_->IsTag(node, "empty-declaration") ||
		arena_->IsTag(node, "layout-pack-push") ||
		arena_->IsTag(node, "layout-pack-pop") ||
		arena_->IsTag(node, "linkage-specification");
}

void SemanticAnalyzer::RegisterClassMemberFunction(EntityId entity,
	BindingId function)
{
	if (entity == kNoEntity || function == kNoBinding) return;
	if (entity_member_functions_.size() <= entity)
		entity_member_functions_.resize(static_cast<std::size_t>(entity) + 1);
	function = program_->bindings[function].canonical;
	std::vector<BindingId>& functions = entity_member_functions_[entity];
	if (std::find(functions.begin(), functions.end(), function) ==
		functions.end())
		functions.push_back(function);
}

LookupResult SemanticAnalyzer::LookupPath(ScopeId scope,
	const NamePath& path, LookupKind kind)
{
	if (path.Size() <= 1) return program_->Lookup(scope, path, kind);
	ScopeId carrier = path.global ? program_->GlobalScope() : kNoScope;
	std::size_t component = 0;
	if (!path.global)
	{
		const TypeId specialization = ResolveClassTemplateSpecialization(
			scope, program_->names.Get(path[0]));
		if (specialization != kNoType)
		{
			EnsureClassDefinition(specialization);
			carrier = program_->ScopeForType(specialization);
		}
		else
		{
			const LookupResult first = program_->LookupName(
				scope, path[0], LOOKUP_SCOPE_CARRIER);
			if (first.type != kNoType) EnsureClassDefinition(first.type);
			carrier = first.name_space != kNoScope ? first.name_space :
				first.type != kNoType ? program_->ScopeForType(first.type) :
				kNoScope;
		}
		component = 1;
	}
	for (; carrier != kNoScope && component + 1 < path.Size(); ++component)
	{
		const TypeId specialization = ResolveClassTemplateSpecialization(
			carrier, scope, program_->names.Get(path[component]));
		if (specialization != kNoType)
		{
			EnsureClassDefinition(specialization);
			carrier = program_->ScopeForType(specialization);
			continue;
		}
		NamePath one;
		one.Push(path[component]);
		const LookupResult next = program_->LookupQualified(
			carrier, one, LOOKUP_SCOPE_CARRIER);
		if (next.type != kNoType) EnsureClassDefinition(next.type);
		carrier = next.name_space != kNoScope ? next.name_space :
			next.type != kNoType ? program_->ScopeForType(next.type) :
			kNoScope;
	}
	if (carrier == kNoScope) return LookupResult();
	if (kind == LOOKUP_TYPE || kind == LOOKUP_SCOPE_CARRIER)
	{
		const TypeId specialization = ResolveClassTemplateSpecialization(
			carrier, scope, program_->names.Get(path.Last()));
		if (specialization != kNoType)
		{
			LookupResult result;
			result.type = specialization;
			return result;
		}
	}
	NamePath terminal;
	terminal.Push(path.Last());
	return program_->LookupQualified(carrier, terminal, kind);
}

LookupResult SemanticAnalyzer::LookupSpelling(ScopeId scope,
	const std::string& spelling, LookupKind kind)
{
	if (spelling.find("::") == std::string::npos)
		return program_->LookupName(scope, program_->names.Intern(spelling), kind);
	return LookupPath(scope, ParseNamePath(spelling), kind);
}

ScopeId SemanticAnalyzer::ResolveScopeSpelling(ScopeId scope,
	const std::string& spelling)
{
	const LookupResult result =
		LookupSpelling(scope, spelling, LOOKUP_SCOPE_CARRIER);
	if (result.type != kNoType) EnsureClassDefinition(result.type);
	return result.name_space != kNoScope ? result.name_space :
		result.type != kNoType ? program_->ScopeForType(result.type) : kNoScope;
}

ScopeId SemanticAnalyzer::ResolveOwner(ScopeId scope, const NamePath& name)
{
	if (!name.global && name.Size() <= 1) return scope;
	NamePath owner = name;
	if (!owner.Empty()) owner.Pop();
	if (owner.Empty()) return owner.global ? program_->GlobalScope() : scope;
	const ScopeId lookup_scope = owner.global ?
		program_->GlobalScope() : scope;
	if (owner.Size() == 1)
	{
		const TypeId specialization = ResolveClassTemplateSpecialization(
			lookup_scope, program_->names.Get(owner[0]));
		if (specialization != kNoType)
			return program_->ScopeForType(specialization);
	}
	const LookupResult result =
		LookupPath(lookup_scope, owner, LOOKUP_SCOPE_CARRIER);
	return result.name_space != kNoScope ? result.name_space :
		result.type != kNoType ? program_->ScopeForType(result.type) : kNoScope;
}

TypeId SemanticAnalyzer::ResolveTemplateTypeArgument(ScopeId scope,
	const std::string& source)
{
	std::string spelling = TrimTypeSpelling(source);
	if (spelling.empty()) return kNoType;
	while (RemoveLeadingTypeWord(&spelling, "typename")) {}
	NamedFlavor elaborated_flavor = NAMED_NONE;
	if (RemoveLeadingTypeWord(&spelling, "class"))
		elaborated_flavor = NAMED_CLASS;
	else if (RemoveLeadingTypeWord(&spelling, "struct"))
		elaborated_flavor = NAMED_STRUCT;
	std::uint8_t outer_cv = CV_NONE;
	bool removed = true;
	while (removed)
	{
		removed = false;
		if (RemoveTrailingTypeWord(&spelling, "const"))
		{
			outer_cv |= CV_CONST;
			removed = true;
		}
		if (RemoveTrailingTypeWord(&spelling, "volatile"))
		{
			outer_cv |= CV_VOLATILE;
			removed = true;
		}
	}
	TypeId result = kNoType;
	if (spelling.compare(0, 9, "decltype(") == 0)
	{
		std::size_t close = std::string::npos;
		std::size_t depth = 1;
		for (std::size_t i = 9; i < spelling.size(); ++i)
		{
			if (spelling[i] == '(') ++depth;
			else if (spelling[i] == ')' && --depth == 0)
			{
				close = i;
				break;
			}
		}
		if (close != std::string::npos && close + 2 < spelling.size() &&
			spelling.compare(close + 1, 2, "::") == 0)
		{
			const std::string operand = TrimTypeSpelling(
				spelling.substr(9, close - 9));
			const LookupResult declaration =
				LookupSpelling(scope, operand, LOOKUP_ORDINARY);
			if (declaration.ordinary != kNoBinding)
			{
				const TypeId carrier_type = EffectiveType(
					program_->bindings[declaration.ordinary].type);
				EnsureClassDefinition(carrier_type);
				const ScopeId carrier = program_->ScopeForType(carrier_type);
				if (carrier != kNoScope)
				{
					const NamePath terminal = ParseNamePath(
						spelling.substr(close + 3));
					result = program_->LookupQualified(
						carrier, terminal, LOOKUP_TYPE).type;
				}
			}
		}
	}
	else if (!spelling.empty() && spelling[spelling.size() - 1] == ')')
	{
		std::size_t open = std::string::npos;
		std::size_t angle_depth = 0;
		for (std::size_t i = 0; i < spelling.size(); ++i)
		{
			if (spelling[i] == '<') ++angle_depth;
			else if (spelling[i] == '>' && angle_depth != 0) --angle_depth;
			else if (spelling[i] == '(' && angle_depth == 0)
			{
				open = i;
				break;
			}
		}
		if (open == std::string::npos || open == 0) return kNoType;
		const TypeId returned = ResolveTemplateTypeArgument(
			scope, spelling.substr(0, open));
		if (returned == kNoType) return kNoType;
		std::vector<TypeId> parameters;
		std::size_t first = open + 1;
		angle_depth = 0;
		std::size_t paren_depth = 0;
		for (std::size_t i = first; i < spelling.size() - 1; ++i)
		{
			if (spelling[i] == '<') ++angle_depth;
			else if (spelling[i] == '>' && angle_depth != 0) --angle_depth;
			else if (spelling[i] == '(') ++paren_depth;
			else if (spelling[i] == ')' && paren_depth != 0) --paren_depth;
			else if (spelling[i] == ',' && angle_depth == 0 && paren_depth == 0)
			{
				const TypeId parameter = ResolveTemplateTypeArgument(
					scope, spelling.substr(first, i - first));
				if (parameter == kNoType) return kNoType;
				parameters.push_back(parameter);
				first = i + 1;
			}
		}
		if (first < spelling.size() - 1)
		{
			const TypeId parameter = ResolveTemplateTypeArgument(scope,
				spelling.substr(first, spelling.size() - first - 1));
			if (parameter == kNoType) return kNoType;
			parameters.push_back(parameter);
		}
		result = program_->types.Function(returned, parameters, false);
	}
	else if (!spelling.empty() && spelling[spelling.size() - 1] == ']')
	{
		const std::size_t open = spelling.rfind('[');
		if (open == std::string::npos) return kNoType;
		std::uint64_t bound = 0;
		if (open + 1 == spelling.size() - 1) return kNoType;
		for (std::size_t i = open + 1; i + 1 < spelling.size(); ++i)
		{
			if (spelling[i] < '0' || spelling[i] > '9') return kNoType;
			const std::uint64_t digit =
				static_cast<std::uint64_t>(spelling[i] - '0');
			if (bound > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
				throw std::runtime_error("array template argument is too large");
			bound = bound * 10 + digit;
		}
		const TypeId element = ResolveTemplateTypeArgument(
			scope, spelling.substr(0, open));
		if (element != kNoType) result = program_->types.Array(element, bound);
	}
	else if (spelling.size() >= 2 &&
		spelling.compare(spelling.size() - 2, 2, "&&") == 0)
	{
		spelling.erase(spelling.size() - 2);
		const TypeId referred = ResolveTemplateTypeArgument(scope, spelling);
		if (referred != kNoType)
			result = program_->types.Reference(TYPE_RVALUE_REFERENCE, referred);
	}
	else if (!spelling.empty() && spelling[spelling.size() - 1] == '&')
	{
		spelling.erase(spelling.size() - 1);
		const TypeId referred = ResolveTemplateTypeArgument(scope, spelling);
		if (referred != kNoType)
			result = program_->types.Reference(TYPE_LVALUE_REFERENCE, referred);
	}
	else if (!spelling.empty() && spelling[spelling.size() - 1] == '*')
	{
		spelling.erase(spelling.size() - 1);
		const TypeId pointed = ResolveTemplateTypeArgument(scope, spelling);
		if (pointed != kNoType) result = program_->types.Pointer(pointed);
	}
	else
	{
		std::uint8_t base_cv = CV_NONE;
		removed = true;
		while (removed)
		{
			removed = false;
			if (RemoveLeadingTypeWord(&spelling, "const"))
			{
				base_cv |= CV_CONST;
				removed = true;
			}
			if (RemoveLeadingTypeWord(&spelling, "volatile"))
			{
				base_cv |= CV_VOLATILE;
				removed = true;
			}
		}
		result = ResolveClassTemplateSpecialization(scope, spelling);
		if (result == kNoType)
		{
			std::string compact;
			for (std::size_t i = 0; i < spelling.size(); ++i)
				if (!std::isspace(static_cast<unsigned char>(spelling[i])))
					compact += spelling[i];
			FundamentalKind fundamental = FUND_INT;
			bool known = true;
			if (compact == "bool") fundamental = FUND_BOOL;
			else if (compact == "char") fundamental = FUND_CHAR;
			else if (compact == "signedchar") fundamental = FUND_SIGNED_CHAR;
			else if (compact == "unsignedchar") fundamental = FUND_UNSIGNED_CHAR;
			else if (compact == "short" || compact == "shortint" ||
				compact == "signedshort" || compact == "signedshortint")
				fundamental = FUND_SHORT_INT;
			else if (compact == "unsignedshort" ||
				compact == "unsignedshortint")
				fundamental = FUND_UNSIGNED_SHORT_INT;
			else if (compact == "int" || compact == "signed" ||
				compact == "signedint") fundamental = FUND_INT;
			else if (compact == "unsigned" || compact == "unsignedint")
				fundamental = FUND_UNSIGNED_INT;
			else if (compact == "long" || compact == "longint" ||
				compact == "signedlong" || compact == "signedlongint")
				fundamental = FUND_LONG_INT;
			else if (compact == "unsignedlong" ||
				compact == "unsignedlongint") fundamental = FUND_UNSIGNED_LONG_INT;
			else if (compact == "longlong" || compact == "longlongint" ||
				compact == "signedlonglong" ||
				compact == "signedlonglongint") fundamental = FUND_LONG_LONG_INT;
			else if (compact == "unsignedlonglong" ||
				compact == "unsignedlonglongint")
				fundamental = FUND_UNSIGNED_LONG_LONG_INT;
			else if (compact == "float") fundamental = FUND_FLOAT;
			else if (compact == "double") fundamental = FUND_DOUBLE;
			else if (compact == "longdouble") fundamental = FUND_LONG_DOUBLE;
			else if (compact == "void") fundamental = FUND_VOID;
			else if (compact == "wchar_t") fundamental = FUND_WCHAR_T;
			else if (compact == "char16_t") fundamental = FUND_CHAR16_T;
			else if (compact == "char32_t") fundamental = FUND_CHAR32_T;
			else known = false;
			if (known) result = program_->types.Fundamental(fundamental);
			else
			{
				const LookupResult found =
					LookupSpelling(scope, spelling, LOOKUP_TYPE);
				result = found.type;
				if (result == kNoType && elaborated_flavor != NAMED_NONE &&
					spelling.find("::") == std::string::npos &&
					spelling.find('<') == std::string::npos)
				{
					ScopeId owner = scope;
					while (program_->KindOfScope(owner) ==
						SCOPE_TEMPLATE_PARAMETERS)
						owner = program_->ParentScope(owner);
					const NameId name = program_->names.Intern(spelling);
					const EntityId entity = program_->NewEntity(name,
						elaborated_flavor, false, kNoType, owner, name);
					result = program_->entities[entity].type;
					program_->SetTypeName(owner, name, result);
					program_->AddBinding(owner, BIND_TYPE, name, result,
						false, 0, elaborated_flavor);
				}
				if (result != kNoType)
				{
					const TypeRecord& named = program_->types.Get(
						program_->types.RemoveTopCv(result));
					if (named.kind == TYPE_NAMED &&
						program_->entities[named.entity].flavor ==
							NAMED_TEMPLATE_PARAMETER)
						result = kNoType;
				}
			}
		}
		if (result != kNoType && base_cv != CV_NONE &&
			!program_->types.IsFunction(result))
			result = program_->types.Qualify(result, base_cv);
	}
	if (result == kNoType && outer_cv == CV_NONE)
	{
		static const char* const compact_qualifiers[] = {
			"const", "volatile"
		};
		static const std::uint8_t compact_cv[] = {
			CV_CONST, CV_VOLATILE
		};
		for (std::size_t i = 0; i < sizeof(compact_qualifiers) /
			sizeof(compact_qualifiers[0]); ++i)
		{
			const std::size_t length = std::char_traits<char>::length(
				compact_qualifiers[i]);
			if (spelling.size() <= length || spelling.compare(
				spelling.size() - length, length, compact_qualifiers[i]) != 0)
				continue;
			const TypeId base = ResolveTemplateTypeArgument(scope,
				spelling.substr(0, spelling.size() - length));
			if (base == kNoType || program_->types.IsFunction(base)) continue;
			result = program_->types.Qualify(base, compact_cv[i]);
			break;
		}
	}
	if (result != kNoType && outer_cv != CV_NONE &&
		!program_->types.IsFunction(result))
		result = program_->types.Qualify(result, outer_cv);
	return result;
}

bool SemanticAnalyzer::ParseExplicitTemplateArguments(ScopeId scope,
	const std::string& spelling, std::string* base,
	std::vector<TypeId>* arguments)
{
	const std::size_t open = spelling.find('<');
	if (open == std::string::npos || spelling.empty() ||
		spelling[spelling.size() - 1] != '>') return false;
	*base = spelling.substr(0, open);
	arguments->clear();
	std::size_t first = open + 1;
	std::size_t depth = 0;
	std::size_t paren_depth = 0;
	std::size_t bracket_depth = 0;
	for (std::size_t i = first; i < spelling.size() - 1; ++i)
	{
		if (spelling[i] == '<') ++depth;
		else if (spelling[i] == '>')
		{
			if (depth == 0) return false;
			--depth;
		}
		else if (spelling[i] == '(') ++paren_depth;
		else if (spelling[i] == ')' && paren_depth != 0) --paren_depth;
		else if (spelling[i] == '[') ++bracket_depth;
		else if (spelling[i] == ']' && bracket_depth != 0) --bracket_depth;
		else if (spelling[i] == ',' && depth == 0 && paren_depth == 0 &&
			bracket_depth == 0)
		{
			const TypeId type = ResolveTemplateTypeArgument(scope,
				spelling.substr(first, i - first));
			if (type == kNoType)
				throw std::runtime_error("unknown explicit template type argument");
			arguments->push_back(type);
			first = i + 1;
		}
	}
	if (first < spelling.size() - 1)
	{
		const TypeId type = ResolveTemplateTypeArgument(scope,
			spelling.substr(first, spelling.size() - first - 1));
		if (type == kNoType)
			throw std::runtime_error("unknown explicit template type argument");
		arguments->push_back(type);
	}
	return true;
}

bool SemanticAnalyzer::AnalyzeClassTemplateMember(NodeId declaration,
	ScopeId scope, const std::vector<NameId>& parameters)
{
	NodeId declarator = FindChild(declaration, "declarator");
	if (arena_->IsTag(declaration, "simple-declaration"))
	{
		const NodeId list = FindChild(declaration, "init-declarator-list");
		const NodeId item = list == kNoNode ? kNoNode :
			FirstSemanticChild(list);
		declarator = item == kNoNode ? kNoNode :
			FindChild(item, "declarator");
	}
	NamePath path;
	if (arena_->IsTag(declaration, "class-specifier") ||
		arena_->IsTag(declaration, "class-forward-declaration"))
		path = ParseNamePath(arena_->Payload(declaration));
	else if (declarator != kNoNode) path = DeclaratorNamePath(declarator);
	else return false;
	if (path.Size() <= 1) return false;

	std::size_t owner_component = path.Size();
	std::string primary_component;
	std::vector<std::string> owner_arguments;
	for (std::size_t i = 0; i + 1 < path.Size(); ++i)
		if (ParseRawTemplateComponent(program_->names.Get(path[i]),
			&primary_component, &owner_arguments))
		{
			owner_component = i;
			break;
		}
	if (owner_component == path.Size()) return false;

	std::string primary = path.global ? "::" : std::string();
	for (std::size_t i = 0; i <= owner_component; ++i)
	{
		if (i != 0) primary += "::";
		primary += i == owner_component ? primary_component :
			program_->names.Get(path[i]);
	}
	const std::size_t pattern_index = FindClassTemplate(scope, primary);
	if (pattern_index == NoTemplatePattern())
		throw std::runtime_error("class template member owner was not found");
	const ClassTemplatePattern& owner_pattern =
		class_templates_[pattern_index];
	if (!owner_pattern.defined ||
		owner_arguments.size() != owner_pattern.type_parameters.size())
		throw std::runtime_error("invalid class template member owner shape");

	ClassTemplateMemberPattern member;
	member.lexical_scope = scope;
	member.declaration = declaration;
	member.type_parameters = parameters;
	member.owner_parameter_indices.assign(owner_arguments.size(), kNoDumpEdge);
	member.owner_fixed_arguments.assign(owner_arguments.size(), kNoType);
	for (std::size_t component = owner_component + 1;
		component + 1 < path.Size(); ++component)
		member.nested_owner_path.push_back(path[component]);
	std::vector<std::uint8_t> used(parameters.size(), 0);
	for (std::size_t argument = 0; argument < owner_arguments.size(); ++argument)
	{
		std::size_t parameter = parameters.size();
		for (std::size_t candidate = 0; candidate < parameters.size(); ++candidate)
			if (parameters[candidate] != 0 && owner_arguments[argument] ==
				program_->names.Get(parameters[candidate]))
			{
				parameter = candidate;
				break;
			}
		if (parameter != parameters.size())
		{
			if (used[parameter])
				throw std::runtime_error(
					"repeated class template member owner parameter");
			used[parameter] = 1;
			member.owner_parameter_indices[argument] =
				static_cast<std::uint32_t>(parameter);
			continue;
		}
		member.owner_fixed_arguments[argument] =
			ResolveTemplateTypeArgument(scope, owner_arguments[argument]);
		if (member.owner_fixed_arguments[argument] == kNoType)
			throw std::runtime_error(
				"unsupported class template member owner argument");
	}
	for (std::size_t i = 0; i < used.size(); ++i)
		if (!used[i])
			throw std::runtime_error(
				"class template member parameter is not bound by its owner");

	class_templates_[pattern_index].member_definitions.push_back(member);
	const std::vector<BindingId> specializations =
		class_templates_[pattern_index].specialization_bindings;
	const std::size_t parameter_count = owner_pattern.type_parameters.size();
	for (std::size_t i = 0; i < specializations.size(); ++i)
	{
		const EntityId entity = EntityOf(
			program_->bindings[specializations[i]].type);
		if (entity == kNoEntity)
			throw std::logic_error("class specialization has no entity");
		const EntityRecord& record = program_->entities[entity];
		const std::size_t first = record.template_argument_begin;
		if (record.template_argument_count != parameter_count ||
			first > program_->template_arguments.size() || parameter_count >
				program_->template_arguments.size() - first)
			throw std::logic_error("class specialization arguments are invalid");
		const std::vector<TypeId> arguments(program_->template_arguments.begin() +
			first, program_->template_arguments.begin() + first + parameter_count);
		ApplyClassTemplateMemberDefinitions(
			pattern_index, specializations[i], arguments);
	}
	return true;
}

void SemanticAnalyzer::AnalyzeClassTemplate(NodeId declaration, ScopeId scope,
	const std::vector<NameId>& parameters,
	const std::vector<NodeId>& defaults)
{
	const NamePath path = ParseNamePath(arena_->Payload(declaration));
	const NameId name = path.Last();
	const ScopeId owner = ResolveOwner(scope, path);
	if (name == 0 || owner == kNoScope)
		throw std::runtime_error("invalid class template owner or name");
	const LookupResult prior_name =
		program_->LookupDirect(owner, name, LOOKUP_TYPE);
	std::size_t prior_index = NoTemplatePattern();
	if (prior_name.type != kNoType)
	{
		const TypeRecord& prior_type = program_->types.Get(
			program_->types.RemoveTopCv(prior_name.type));
		if (prior_type.kind != TYPE_NAMED ||
			prior_type.entity >= class_template_pattern_by_entity_.size() ||
			class_template_pattern_by_entity_[prior_type.entity] == kNoDumpEdge)
			throw std::runtime_error(
				"class template conflicts with an existing type");
		prior_index = class_template_pattern_by_entity_[prior_type.entity];
	}
	const bool definition =
		(arena_->Flags(declaration) & SYNTAX_FLAG_DEFINITION) != 0;
	if (prior_index != NoTemplatePattern())
	{
		ClassTemplatePattern& prior = class_templates_[prior_index];
		if (prior.type_parameters.size() != parameters.size())
			throw std::runtime_error(
				"class template parameter count mismatch");
		for (std::size_t i = 0; i < defaults.size(); ++i)
			if (prior.default_arguments[i] == kNoNode &&
				defaults[i] != kNoNode)
				prior.default_arguments[i] = defaults[i];
		if (!definition) return;
		if (prior.defined)
			throw std::runtime_error("duplicate class template definition");
		prior.lexical_scope = scope;
		prior.declaration = declaration;
		prior.type_parameters = parameters;
		prior.defined = true;
		UpgradeClassTemplateSpecializations(prior_index);
		return;
	}

	ClassTemplatePattern pattern;
	pattern.owner = owner;
	pattern.lexical_scope = scope;
	pattern.name = name;
	pattern.declaration = declaration;
	pattern.type_parameters = parameters;
	pattern.default_arguments = defaults;
	pattern.defined = definition;
	const NameId marker_name = EmissionName(owner, name);
	pattern.marker_entity = program_->NewEntity(marker_name,
		NAMED_TEMPLATE_PARAMETER, false, kNoType, owner, name);
	const TypeId marker_type =
		program_->entities[pattern.marker_entity].type;
	program_->SetTypeName(owner, name, marker_type);
	program_->AddBinding(owner, BIND_TYPE, name, marker_type,
		false, 0, NAMED_TEMPLATE_PARAMETER);
	const std::size_t index = class_templates_.size();
	if (index > std::numeric_limits<std::uint32_t>::max())
		throw std::runtime_error("too many class templates");
	class_templates_.push_back(pattern);
	if (class_template_pattern_by_entity_.size() <= pattern.marker_entity)
		class_template_pattern_by_entity_.resize(
			static_cast<std::size_t>(pattern.marker_entity) + 1, kNoDumpEdge);
	class_template_pattern_by_entity_[pattern.marker_entity] =
		static_cast<std::uint32_t>(index);
}

std::size_t SemanticAnalyzer::FindClassTemplate(ScopeId scope,
	const std::string& spelling)
{
	const LookupResult found = LookupSpelling(scope, spelling, LOOKUP_TYPE);
	if (found.type == kNoType) return NoTemplatePattern();
	const TypeRecord& type = program_->types.Get(
		program_->types.RemoveTopCv(found.type));
	if (type.kind != TYPE_NAMED ||
		type.entity >= class_template_pattern_by_entity_.size() ||
		class_template_pattern_by_entity_[type.entity] == kNoDumpEdge)
		return NoTemplatePattern();
	const std::size_t index =
		class_template_pattern_by_entity_[type.entity];
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template entity index");
	const ClassTemplatePattern& pattern = class_templates_[index];
	if (type.entity == pattern.marker_entity) return index;
	// A specialization's injected primary name denotes the current template,
	// but an arbitrary alias to that specialization is not itself a template.
	const NameId requested = ParseNamePath(spelling).Last();
	if (requested != pattern.name || found.type_declaration == kNoBinding)
		return NoTemplatePattern();
	const BindingRecord& declaration =
		program_->bindings[found.type_declaration];
	return declaration.owner == program_->entities[type.entity].member_scope &&
		declaration.member_owner == type.entity &&
		declaration.name == pattern.name ? index : NoTemplatePattern();
}

ScopeId SemanticAnalyzer::BindClassTemplateArguments(
	const ClassTemplatePattern& pattern,
	const std::vector<TypeId>& arguments)
{
	const ScopeId template_scope = NewScope(pattern.lexical_scope,
		SCOPE_TEMPLATE_PARAMETERS, 0, ScopePrefixId(pattern.lexical_scope));
	for (std::size_t i = 0; i < arguments.size(); ++i)
		if (pattern.type_parameters[i] != 0)
			program_->AddBinding(template_scope, BIND_TYPE_ALIAS,
				pattern.type_parameters[i], arguments[i]);
	return template_scope;
}

void SemanticAnalyzer::ApplyClassTemplateMemberDefinitions(
	std::size_t index, BindingId specialization,
	const std::vector<TypeId>& arguments)
{
	if (index >= class_templates_.size() || specialization == kNoBinding)
		throw std::logic_error("invalid class template member application");
	const EntityId entity = EntityOf(program_->bindings[specialization].type);
	if (entity == kNoEntity || !program_->entities[entity].complete) return;
	if (class_template_member_definition_counts_.size() <= specialization)
		class_template_member_definition_counts_.resize(
			static_cast<std::size_t>(specialization) + 1, 0);
	while (class_template_member_definition_counts_[specialization] <
		class_templates_[index].member_definitions.size())
	{
		const std::size_t definition_index =
			class_template_member_definition_counts_[specialization]++;
		const ClassTemplateMemberPattern definition =
			class_templates_[index].member_definitions[definition_index];
		if (definition.owner_parameter_indices.size() != arguments.size() ||
			definition.owner_fixed_arguments.size() != arguments.size())
			throw std::logic_error("class template member argument shape changed");
		std::vector<TypeId> bindings(definition.type_parameters.size(), kNoType);
		for (std::size_t owner = 0; owner < arguments.size(); ++owner)
		{
			const std::uint32_t parameter =
				definition.owner_parameter_indices[owner];
			if (parameter == kNoDumpEdge)
			{
				if (definition.owner_fixed_arguments[owner] != arguments[owner])
					throw std::runtime_error(
						"class template member owner does not match specialization");
				continue;
			}
			if (parameter >= bindings.size())
				throw std::logic_error(
					"class template member parameter index is invalid");
			if (bindings[parameter] != kNoType &&
				bindings[parameter] != arguments[owner])
				throw std::runtime_error(
					"class template member owner deduction conflict");
			bindings[parameter] = arguments[owner];
		}
		const ScopeId member_scope = program_->entities[entity].member_scope;
		if (member_scope == kNoScope)
			throw std::logic_error(
				"class template member definition has no class scope");
		for (std::size_t parameter = 0; parameter < bindings.size(); ++parameter)
			if (bindings[parameter] == kNoType ||
				definition.type_parameters[parameter] == 0)
				throw std::runtime_error(
					"unbound class template member parameter");
		const ClassTemplateMemberPattern* definition_pointer = &definition;
		const std::vector<TypeId>* binding_pointer = &bindings;
		const auto make_definition_scope = [this, definition_pointer,
			binding_pointer](ScopeId parent) {
			const ScopeId result = NewScope(parent, SCOPE_TEMPLATE_PARAMETERS,
				0, ScopePrefixId(parent));
			for (std::size_t parameter = 0;
				parameter < binding_pointer->size(); ++parameter)
				program_->AddBinding(result, BIND_TYPE_ALIAS,
					definition_pointer->type_parameters[parameter],
					(*binding_pointer)[parameter]);
			return result;
		};
		ScopeId actual_owner = member_scope;
		for (std::size_t part = 0;
			part < definition.nested_owner_path.size(); ++part)
		{
			const std::string spelling =
				program_->names.Get(definition.nested_owner_path[part]);
			const TypeId specialization =
				ResolveClassTemplateSpecialization(actual_owner, spelling);
			if (specialization != kNoType)
			{
				actual_owner = program_->ScopeForType(specialization);
				continue;
			}
			const LookupResult nested = program_->LookupDirect(actual_owner,
				definition.nested_owner_path[part], LOOKUP_SCOPE_CARRIER);
			actual_owner = nested.name_space != kNoScope ? nested.name_space :
				program_->ScopeForType(nested.type);
			if (actual_owner == kNoScope) break;
		}
		if (actual_owner == kNoScope)
			throw std::runtime_error(
				"class template member definition owner was not found while resolving " +
				program_->names.Get(definition.nested_owner_path.empty() ? 0 :
					definition.nested_owner_path.back()));
		ScopeId definition_scope = make_definition_scope(actual_owner);
		const NodeId node = definition.declaration;

		if (arena_->IsTag(node, "function-definition"))
			AnalyzeFunction(node, definition_scope, root_, true);
		else if (arena_->IsTag(node, "special-member-definition") ||
			arena_->IsTag(node, "special-member-declaration"))
			AnalyzeOutOfClassSpecialMember(node, definition_scope,
				definition_scope, true);
		else if (arena_->IsTag(node, "simple-declaration"))
			AnalyzeSimple(node, definition_scope, root_, false, true);
		else if (arena_->IsTag(node, "class-specifier") ||
			arena_->IsTag(node, "class-forward-declaration"))
		{
			const NamePath nested_name =
				ParseNamePath(arena_->Payload(node));
			const std::string terminal =
				program_->names.Get(nested_name.Last());
			(void)AnalyzeClass(node, definition_scope, std::string(), false,
				terminal, actual_owner, 0, true);
		}
		else throw std::runtime_error(
			"unsupported class template member definition");
	}
}

void SemanticAnalyzer::CompleteClassTemplateSpecialization(std::size_t index,
	BindingId binding, const std::vector<TypeId>& arguments)
{
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template completion pattern");
	if (!class_templates_[index].defined) return;
	if (class_template_specialization_states_.size() <= binding)
		class_template_specialization_states_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	if (class_template_specialization_states_[binding] != 0) return;

	// Completing a body may register nested templates, so retain a stable
	// pattern snapshot across replay while identity remains index-owned.
	const ClassTemplatePattern pattern = class_templates_[index];
	if (arguments.size() != pattern.type_parameters.size())
		throw std::logic_error("class template completion argument mismatch");
	class_template_specialization_states_[binding] = 1;
	std::string specialization_name = program_->names.Get(pattern.name);
	for (std::size_t i = 0; i < arguments.size(); ++i)
		specialization_name += "_" +
			TemplateArgumentName(program_->RenderType(arguments[i]));
	specialization_name += "_";
	const ScopeId template_scope =
		BindClassTemplateArguments(pattern, arguments);
	(void)AnalyzeClass(pattern.declaration, template_scope,
		std::string(), false, specialization_name, pattern.owner,
		pattern.name, true);
	class_template_specialization_states_[binding] = 2;
	ApplyClassTemplateMemberDefinitions(index, binding, arguments);
}

void SemanticAnalyzer::EnsureClassDefinition(TypeId type)
{
	if (type == kNoType) return;
	const TypeRecord* record = &program_->types.Get(type);
	while (record->kind == TYPE_QUALIFIED || record->kind == TYPE_ARRAY)
	{
		type = record->child;
		record = &program_->types.Get(type);
	}
	if (record->kind != TYPE_NAMED) return;
	const EntityId entity = record->entity;
	if (program_->entities[entity].complete) return;

	if (entity < class_template_pattern_by_entity_.size() &&
		class_template_pattern_by_entity_[entity] != kNoDumpEdge)
	{
		const std::size_t index = class_template_pattern_by_entity_[entity];
		if (index >= class_templates_.size())
			throw std::logic_error("invalid class specialization owner index");
		const ClassTemplatePattern pattern = class_templates_[index];
		if (entity == pattern.marker_entity) return;
		const EntityRecord& specialization = program_->entities[entity];
		if (specialization.template_argument_begin == kNoBinding)
			throw std::logic_error("class specialization has no arguments");
		const std::size_t first = specialization.template_argument_begin;
		if (specialization.template_argument_count !=
			pattern.type_parameters.size() ||
			first > program_->template_arguments.size() ||
			pattern.type_parameters.size() >
				program_->template_arguments.size() - first)
			throw std::logic_error("class specialization arguments are truncated");
		const std::vector<TypeId> arguments(
			program_->template_arguments.begin() + first,
			program_->template_arguments.begin() + first +
				pattern.type_parameters.size());
		CompleteClassTemplateSpecialization(index,
			program_->entities[entity].declaration, arguments);
		return;
	}

	if (entity < deferred_class_definition_by_entity_.size() &&
		deferred_class_definition_by_entity_[entity] != kNoNode)
	{
		const NodeId definition = deferred_class_definition_by_entity_[entity];
		const ScopeId scope = deferred_class_scope_by_entity_[entity];
		deferred_class_definition_by_entity_[entity] = kNoNode;
		(void)AnalyzeClass(definition, scope, std::string(), false);
	}
}

bool SemanticAnalyzer::ClassTemplateSpecializationArgumentsComplete(
	EntityId entity) const
{
	if (entity >= class_template_pattern_by_entity_.size() ||
		class_template_pattern_by_entity_[entity] == kNoDumpEdge ||
		program_->entities[entity].template_argument_begin == kNoBinding)
		return true;
	const std::size_t index = class_template_pattern_by_entity_[entity];
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class specialization owner index");
	const EntityRecord& specialization = program_->entities[entity];
	const std::size_t first = specialization.template_argument_begin;
	const std::size_t count = class_templates_[index].type_parameters.size();
	if (specialization.template_argument_count != count ||
		first > program_->template_arguments.size() ||
		count > program_->template_arguments.size() - first)
		throw std::logic_error("class specialization arguments are truncated");
	for (std::size_t i = 0; i < count; ++i)
	{
		const TypeId argument = program_->types.RemoveTopCv(
			program_->template_arguments[first + i]);
		const TypeRecord& record = program_->types.Get(argument);
		if (record.kind == TYPE_NAMED &&
			!program_->entities[record.entity].complete)
			return false;
	}
	return true;
}

bool SemanticAnalyzer::IsClassTemplateSpecializationEntity(
	EntityId entity) const
{
	return entity != kNoEntity &&
		program_->entities[entity].template_argument_begin != kNoBinding;
}

bool SemanticAnalyzer::IsClassTemplateSpecializationContext(
	EntityId entity) const
{
	for (std::size_t depth = 0;
		entity != kNoEntity && depth < program_->entities.size(); ++depth)
	{
		if (IsClassTemplateSpecializationEntity(entity)) return true;
		entity = program_->entities[entity].enclosing_class;
	}
	return false;
}

BindingId SemanticAnalyzer::InstantiateClassTemplate(std::size_t index,
	const std::vector<TypeId>& supplied_arguments)
{
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template pattern");
	ClassTemplatePattern& pattern = class_templates_[index];
	if (supplied_arguments.size() > pattern.type_parameters.size())
		return kNoBinding;
	std::vector<TypeId> arguments = supplied_arguments;
	ScopeId argument_scope = kNoScope;
	if (arguments.size() < pattern.type_parameters.size())
		argument_scope = BindClassTemplateArguments(pattern, arguments);
	for (std::size_t i = arguments.size();
		i < pattern.type_parameters.size(); ++i)
	{
		if (pattern.default_arguments[i] == kNoNode) return kNoBinding;
		NodeId type_id = FindChild(pattern.default_arguments[i], "type-id");
		if (type_id == kNoNode)
			type_id = FirstSemanticChild(pattern.default_arguments[i]);
		if (type_id == kNoNode)
			throw std::runtime_error("empty default template argument");
		const TypeId argument = BuildTypeId(type_id, argument_scope);
		arguments.push_back(argument);
		if (pattern.type_parameters[i] != 0)
			program_->AddBinding(argument_scope, BIND_TYPE_ALIAS,
				pattern.type_parameters[i], argument);
	}

	++template_specialization_requests_;
	const TemplateSpecializationKey key(index, arguments);
	const BindingId old = class_template_instantiations_.Find(key);
	if (old != kNoBinding)
	{
		++template_specialization_cache_hits_;
		if (pattern.defined &&
			(old >= class_template_specialization_states_.size() ||
			 class_template_specialization_states_[old] == 0) &&
			ClassTemplateArgumentsAreComplete(*program_, arguments))
			CompleteClassTemplateSpecialization(index, old, arguments);
		if (old < class_template_specialization_states_.size() &&
			class_template_specialization_states_[old] == 2)
			ApplyClassTemplateMemberDefinitions(index, old, arguments);
		return old;
	}

	std::string specialization_name = program_->names.Get(pattern.name);
	for (std::size_t i = 0; i < arguments.size(); ++i)
		specialization_name += "_" +
			TemplateArgumentName(program_->RenderType(arguments[i]));
	specialization_name += "_";
	const ScopeId template_scope =
		BindClassTemplateArguments(pattern, arguments);
	const TypeId shell = AnalyzeClass(pattern.declaration, template_scope,
		std::string(), false, specialization_name, pattern.owner,
		pattern.name, false);
	const EntityId entity = EntityOf(shell);
	if (entity == kNoEntity || program_->entities[entity].declaration == kNoBinding)
		throw std::logic_error("class template shell has no declaration");
	if (class_template_pattern_by_entity_.size() <= entity)
		class_template_pattern_by_entity_.resize(
			static_cast<std::size_t>(entity) + 1, kNoDumpEdge);
	class_template_pattern_by_entity_[entity] =
		static_cast<std::uint32_t>(index);
	EntityRecord& specialization = program_->entities[entity];
	if (specialization.template_argument_begin == kNoBinding)
	{
		if (program_->template_arguments.size() >
			std::numeric_limits<std::uint32_t>::max() - arguments.size())
			throw std::runtime_error("too many class template entity arguments");
		specialization.template_argument_begin = static_cast<std::uint32_t>(
			program_->template_arguments.size());
		specialization.template_argument_count =
			static_cast<std::uint32_t>(arguments.size());
		program_->template_arguments.insert(program_->template_arguments.end(),
			arguments.begin(), arguments.end());
	}
	const BindingId binding = program_->entities[entity].declaration;
	class_template_instantiations_.Insert(key, binding);
	pattern.specialization_bindings.push_back(binding);
	if (class_template_specialization_states_.size() <= binding)
		class_template_specialization_states_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	if (pattern.defined &&
		ClassTemplateArgumentsAreComplete(*program_, arguments))
		CompleteClassTemplateSpecialization(index, binding, arguments);
	return binding;
}

void SemanticAnalyzer::UpgradeClassTemplateSpecializations(std::size_t index)
{
	if (index >= class_templates_.size())
		throw std::logic_error("invalid class template upgrade");
	// Completing one shell can discover nested class templates and grow the
	// pattern table, so keep a stable snapshot across the completion calls.
	const ClassTemplatePattern pattern = class_templates_[index];
	const std::size_t parameter_count = pattern.type_parameters.size();
	for (std::size_t i = 0; i < pattern.specialization_bindings.size(); ++i)
	{
		const BindingId binding = pattern.specialization_bindings[i];
		if (binding < class_template_specialization_states_.size() &&
			class_template_specialization_states_[binding] != 0)
			continue;
		std::vector<TypeId> arguments;
		const EntityId entity = EntityOf(program_->bindings[binding].type);
		if (entity == kNoEntity)
			throw std::logic_error("class specialization has no entity");
		const EntityRecord& record = program_->entities[entity];
		const std::size_t first = record.template_argument_begin;
		if (record.template_argument_count != parameter_count ||
			first > program_->template_arguments.size() || parameter_count >
				program_->template_arguments.size() - first)
			throw std::logic_error("class specialization arguments are invalid");
		arguments.assign(program_->template_arguments.begin() + first,
			program_->template_arguments.begin() + first + parameter_count);
		if (ClassTemplateArgumentsAreComplete(*program_, arguments))
			CompleteClassTemplateSpecialization(index, binding, arguments);
	}
}

TypeId SemanticAnalyzer::ResolveClassTemplateSpecialization(ScopeId scope,
	const std::string& spelling)
{
	return ResolveClassTemplateSpecialization(scope, scope, spelling);
}

TypeId SemanticAnalyzer::ResolveClassTemplateSpecialization(
	ScopeId template_scope, ScopeId argument_scope,
	const std::string& spelling)
{
	if (spelling.find('<') == std::string::npos) return kNoType;
	std::string base;
	std::vector<TypeId> arguments;
	if (!ParseExplicitTemplateArguments(argument_scope, spelling,
		&base, &arguments))
		return kNoType;
	const std::size_t pattern = FindClassTemplate(template_scope, base);
	if (pattern == NoTemplatePattern()) return kNoType;
	const BindingId binding = InstantiateClassTemplate(pattern, arguments);
	return binding == kNoBinding ? kNoType : program_->bindings[binding].type;
}

void SemanticAnalyzer::AnalyzeExplicitInstantiation(NodeId node,
	ScopeId scope, bool definition)
{
	const NodeId target = FirstSemanticChild(node);
	if (target == kNoNode ||
		(!arena_->IsTag(target, "class-forward-declaration") &&
		 !arena_->IsTag(target, "class-specifier")))
		throw std::runtime_error(
			"PA19 explicit instantiation requires a class template-id");
	std::string base;
	std::vector<TypeId> arguments;
	if (!ParseExplicitTemplateArguments(scope, arena_->Payload(target),
		&base, &arguments))
		throw std::runtime_error(
			"explicit class instantiation requires a simple-template-id");
	const std::size_t pattern_index = FindClassTemplate(scope, base);
	if (pattern_index == NoTemplatePattern())
		throw std::runtime_error("explicit class instantiation target was not found");
	const ClassTemplatePattern& pattern = class_templates_[pattern_index];
	const ScopeKind scope_kind = program_->KindOfScope(scope);
	if (scope_kind != SCOPE_NAMESPACE)
		throw std::runtime_error(
			"explicit class instantiation must appear at namespace scope");
	const NamePath target_path = ParseNamePath(base);
	if (!target_path.global && target_path.Size() == 1)
	{
		ScopeId permitted = pattern.owner;
		while (permitted != scope && program_->IsInlineNamespace(permitted))
			permitted = program_->ParentScope(permitted);
		if (permitted != scope)
			throw std::runtime_error(
				"unqualified explicit instantiation is in the wrong namespace");
	}
	else
	{
		bool enclosing = false;
		for (ScopeId owner = pattern.owner; owner != kNoScope;
			owner = program_->ParentScope(owner))
			if (owner == scope)
			{
				enclosing = true;
				break;
			}
		if (!enclosing)
			throw std::runtime_error(
				"qualified explicit instantiation is outside its namespace");
	}
	const NodeId target_key = FindChild(target, "class-key");
	const NodeId pattern_key = FindChild(pattern.declaration, "class-key");
	if (target_key == kNoNode || pattern_key == kNoNode ||
		(PayloadSource(target_key) == "union") !=
			(PayloadSource(pattern_key) == "union"))
		throw std::runtime_error("explicit instantiation class-key mismatch");
	const BindingId instantiated = InstantiateClassTemplate(pattern_index, arguments);
	const TypeId type = instantiated == kNoBinding ? kNoType :
		program_->bindings[instantiated].type;
	if (type == kNoType)
		throw std::runtime_error("invalid explicit class template arguments");
	EnsureClassDefinition(type);
	const EntityId entity = EntityOf(type);
	if (entity == kNoEntity || !program_->entities[entity].complete)
		throw std::runtime_error(
			"explicit class instantiation target is incomplete");
	const BindingId specialization =
		program_->entities[entity].declaration;
	if (class_template_explicit_instantiation_states_.size() <= specialization)
		class_template_explicit_instantiation_states_.resize(
			static_cast<std::size_t>(specialization) + 1, 0);
	std::uint8_t& state =
		class_template_explicit_instantiation_states_[specialization];
	if (!definition)
	{
		if ((state & 2) != 0)
			throw std::runtime_error(
				"explicit instantiation declaration follows its definition");
		state |= 1;
		return;
	}
	if ((state & 2) != 0)
		throw std::runtime_error(
			"duplicate explicit class instantiation definition");
	state |= 2;
	const auto demand_member = [this](BindingId binding) {
		if (binding == kNoBinding) return;
		binding = program_->bindings[binding].canonical;
		const FunctionInfo& function = GetFunction(binding);
		if (!function.defined || function.implicit_constructor ||
			function.implicit_destructor || function.implicit_special_member ||
			function.deleted_constructor || function.deleted_destructor ||
			function.deleted_special_member)
			return;
		program_->bindings[binding].weak_odr = true;
		program_->bindings[binding].object_output_root = true;
		DemandFunction(binding);
	};
	if (entity < entity_member_functions_.size())
		for (std::size_t i = 0; i < entity_member_functions_[entity].size(); ++i)
			demand_member(entity_member_functions_[entity][i]);
	if (entity < entity_conversion_functions_.size())
		for (std::size_t i = 0;
			i < entity_conversion_functions_[entity].size(); ++i)
			demand_member(entity_conversion_functions_[entity][i]);
	if (entity < entity_constructors_.size())
		for (std::size_t i = 0; i < entity_constructors_[entity].size(); ++i)
			demand_member(entity_constructors_[entity][i]);
	if (entity < entity_destructor_by_entity_.size())
		demand_member(entity_destructor_by_entity_[entity]);
}

}
}
