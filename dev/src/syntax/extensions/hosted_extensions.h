#pragma once

#include "hosted_extension_registry.h"
#include "syntax/model/arena.h"

#include <cstddef>
#include <string>

namespace cppgm
{
namespace hosted_extension
{

template <class Derived>
class Syntax
{
protected:
	bool SkipHostedAttributeSyntax()
	{
		using namespace syntax;
		Derived& parser = static_cast<Derived&>(*this);
		const std::size_t start = parser.position_;
		if (parser.AtIdentifier() &&
			IsGnuAttributeIntroducer(parser.Spelling(parser.position_)))
		{
			++parser.position_;
			if (parser.SkipBalanced(OP_LPAREN, OP_RPAREN)) return true;
			parser.position_ = start;
			return false;
		}
		if (parser.At(KW_ALIGNAS))
		{
			++parser.position_;
			if (parser.SkipBalanced(OP_LPAREN, OP_RPAREN)) return true;
			parser.position_ = start;
			return false;
		}
		if (parser.At(OP_LSQUARE) && parser.AtOffset(1, OP_LSQUARE))
		{
			parser.position_ += 2;
			while (!parser.AtEof())
			{
				if (parser.At(OP_RSQUARE) && parser.AtOffset(1, OP_RSQUARE))
				{
					parser.position_ += 2;
					return true;
				}
				++parser.position_;
			}
		}
		parser.position_ = start;
		return false;
	}

	bool SkipHostedTypeAnnotations()
	{
		Derived& parser = static_cast<Derived&>(*this);
		bool consumed = false;
		while (parser.AtIdentifier() &&
			IsTypeAnnotation(parser.Spelling(parser.position_)))
		{
			++parser.position_;
			consumed = true;
		}
		return consumed;
	}

	bool AtHostedAttribute() const
	{
		using namespace syntax;
		const Derived& parser = static_cast<const Derived&>(*this);
		return (parser.At(OP_LSQUARE) && parser.AtOffset(1, OP_LSQUARE)) ||
			(parser.AtIdentifier() && IsGnuAttributeIntroducer(
				parser.Spelling(parser.position_)));
	}

	bool MatchHostedExtensionMarker()
	{
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.AtIdentifier() || FindSpecifier(
			parser.Spelling(parser.position_)) != SPECIFIER_EXTENSION) return false;
		++parser.position_;
		return true;
	}

	bool StartsHostedDeclaration(std::size_t position) const
	{
		using namespace syntax;
		const Derived& parser = static_cast<const Derived&>(*this);
		return position < parser.tokens_.size() &&
			parser.tokens_[position].Kind() == kIdentifierToken &&
			FindSpecifier(parser.Spelling(position)) != SPECIFIER_NONE;
	}

	bool StartsHostedType(std::size_t position) const
	{
		using namespace syntax;
		const Derived& parser = static_cast<const Derived&>(*this);
		if (position >= parser.tokens_.size()) return false;
		if (parser.tokens_[position].Kind() ==
			static_cast<std::uint16_t>(OP_LSQUARE) &&
			position + 1 < parser.tokens_.size() &&
			parser.tokens_[position + 1].Kind() ==
				static_cast<std::uint16_t>(OP_LSQUARE)) return true;
		if (parser.tokens_[position].Kind() != kIdentifierToken) return false;
		const std::string& spelling = parser.Spelling(position);
		if (IsGnuAttributeIntroducer(spelling)) return true;
		const SpecifierKind kind = FindSpecifier(spelling);
		return kind != SPECIFIER_NONE &&
			!IsDeclarationOnlySpecifier(kind) && kind != SPECIFIER_EXTENSION;
	}

	bool TryParseHostedDeclSpecifier(
		syntax::NodeId sequence, bool for_type_id,
		bool* consumed, bool* saw_type, bool* saw_user_type,
		bool* saw_int128, std::string* first_type)
	{
		using namespace syntax;
		Derived& parser = static_cast<Derived&>(*this);
		if (!parser.AtIdentifier()) return false;
		const SpecifierKind kind =
			FindSpecifier(parser.Spelling(parser.position_));
		if (kind == SPECIFIER_NONE ||
			(for_type_id && IsDeclarationOnlySpecifier(kind))) return false;
		if (IsTypeSpecifier(kind) && kind != SPECIFIER_COMPLEX &&
			*saw_user_type) return false;
		const std::size_t source = parser.position_++;
		*consumed = true;
		if (kind == SPECIFIER_EXTENSION) return true;
		if (kind == SPECIFIER_COMPLEX)
		{
			parser.arena_.Add(sequence, parser.arena_.Make(
				for_type_id ? "type-specifier" : "decl-specifier", "_Complex"));
			return true;
		}
		if (kind == SPECIFIER_BITINT)
		{
			if (!parser.Match(OP_LPAREN))
				throw parser.Error("expected _BitInt width");
			const NodeId bitint = parser.arena_.Make(
				"bitint-type-specifier", "_BitInt");
			const NodeId width = parser.ParseExpression();
			if (width == kNoNode) throw parser.Error("expected _BitInt width");
			parser.Expect(OP_RPAREN);
			parser.arena_.Add(bitint, width);
			parser.arena_.Add(sequence, bitint);
			if (first_type && first_type->empty())
				*first_type = parser.Spelling(source);
			*saw_type = true;
			*saw_user_type = true;
			return true;
		}
		if (kind == SPECIFIER_ATOMIC)
		{
			if (!parser.Match(OP_LPAREN))
				throw parser.Error("expected _Atomic type operand");
			const NodeId atomic = parser.arena_.Make(
				"atomic-type-specifier", "_Atomic");
			if (!parser.ParseTypeId(atomic))
				throw parser.Error("expected _Atomic type operand");
			parser.Expect(OP_RPAREN);
			parser.arena_.Add(sequence, atomic);
			if (first_type && first_type->empty())
				*first_type = parser.Spelling(source);
			*saw_type = true;
			*saw_user_type = true;
			return true;
		}
		const char* const canonical = CanonicalSpecifier(kind);
		const char* const tag = for_type_id && IsCvSpecifier(kind) ?
			"cv-qualifier" : for_type_id ? "type-specifier" : "decl-specifier";
		parser.arena_.Add(sequence, parser.arena_.Make(tag, canonical));
		if (IsTypeSpecifier(kind))
		{
			if (first_type && first_type->empty())
				*first_type = parser.Spelling(source);
			*saw_type = true;
			if (kind != SPECIFIER_SIGNED)
			{
				*saw_user_type = true;
				*saw_int128 = kind == SPECIFIER_INT128 ||
					kind == SPECIFIER_UINT128;
			}
		}
		return true;
	}
};

}
}
