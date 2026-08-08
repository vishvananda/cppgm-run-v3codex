#include "pa12_semantic_detail.h"

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

std::string TemplateArgumentName(const std::string& source)
{
	std::string result;
	result.reserve(source.size());
	bool underscore = false;
	for (std::size_t i = 0; i < source.size(); ++i)
	{
		const unsigned char character =
			static_cast<unsigned char>(source[i]);
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
	if (!spelling.empty() && spelling[spelling.size() - 1] == ')')
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
	if (result != kNoType && outer_cv != CV_NONE &&
		!program_->types.IsFunction(result))
		result = program_->types.Qualify(result, outer_cv);
	return result;
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
	return class_template_pattern_by_entity_[type.entity];
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
			 class_template_specialization_states_[old] == 0))
		{
			if (class_template_specialization_states_.size() <= old)
				class_template_specialization_states_.resize(
					static_cast<std::size_t>(old) + 1, 0);
			class_template_specialization_states_[old] = 1;
			std::string specialization_name =
				program_->names.Get(pattern.name);
			for (std::size_t i = 0; i < arguments.size(); ++i)
				specialization_name += "_" +
					TemplateArgumentName(program_->RenderType(arguments[i]));
			specialization_name += "_";
			const ScopeId template_scope =
				BindClassTemplateArguments(pattern, arguments);
			(void)AnalyzeClass(pattern.declaration, template_scope,
				std::string(), false, specialization_name, pattern.owner,
				pattern.name, true);
			class_template_specialization_states_[old] = 2;
		}
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
	const BindingId binding = program_->entities[entity].declaration;
	class_template_instantiations_.Insert(key, binding);
	pattern.specialization_bindings.push_back(binding);
	pattern.specialization_arguments.insert(
		pattern.specialization_arguments.end(), arguments.begin(), arguments.end());
	if (class_template_specialization_states_.size() <= binding)
		class_template_specialization_states_.resize(
			static_cast<std::size_t>(binding) + 1, 0);
	if (pattern.defined)
	{
		class_template_specialization_states_[binding] = 1;
		(void)AnalyzeClass(pattern.declaration, template_scope,
			std::string(), false, specialization_name, pattern.owner,
			pattern.name, true);
		class_template_specialization_states_[binding] = 2;
	}
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
		arguments.reserve(parameter_count);
		for (std::size_t p = 0; p < parameter_count; ++p)
			arguments.push_back(pattern.specialization_arguments[
				i * parameter_count + p]);
		const ScopeId template_scope =
			BindClassTemplateArguments(pattern, arguments);
		std::string specialization_name = program_->names.Get(pattern.name);
		for (std::size_t p = 0; p < arguments.size(); ++p)
			specialization_name += "_" +
				TemplateArgumentName(program_->RenderType(arguments[p]));
		specialization_name += "_";
		class_template_specialization_states_[binding] = 1;
		(void)AnalyzeClass(pattern.declaration, template_scope,
			std::string(), false, specialization_name, pattern.owner,
			pattern.name, true);
		class_template_specialization_states_[binding] = 2;
	}
}

TypeId SemanticAnalyzer::ResolveClassTemplateSpecialization(ScopeId scope,
	const std::string& spelling)
{
	if (spelling.find('<') == std::string::npos) return kNoType;
	std::string base;
	std::vector<TypeId> arguments;
	if (!ParseExplicitTemplateArguments(scope, spelling, &base, &arguments))
		return kNoType;
	const std::size_t pattern = FindClassTemplate(scope, base);
	if (pattern == NoTemplatePattern()) return kNoType;
	const BindingId binding = InstantiateClassTemplate(pattern, arguments);
	return binding == kNoBinding ? kNoType : program_->bindings[binding].type;
}

}
}
