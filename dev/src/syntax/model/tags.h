#ifndef CPPGM_SYNTAX_MODEL_TAGS_H
#define CPPGM_SYNTAX_MODEL_TAGS_H

#include "syntax/model/tag_catalog.h"

#include <cstdint>

namespace cppgm
{
namespace syntax
{

enum SyntaxTagCode : std::uint16_t
{
	STAG_NONE = 0,
#define CPPGM_DEFINE_SYNTAX_TAG(code, spelling) code,
	CPPGM_FOR_EACH_SYNTAX_TAG(CPPGM_DEFINE_SYNTAX_TAG)
#undef CPPGM_DEFINE_SYNTAX_TAG
	STAG_COUNT
};

SyntaxTagCode ClassifySyntaxTag(const char* spelling);
const char* SyntaxTagSpelling(SyntaxTagCode code);

}
}

#endif
