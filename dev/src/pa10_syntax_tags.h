#ifndef CPPGM_PA10_SYNTAX_TAGS_H
#define CPPGM_PA10_SYNTAX_TAGS_H

#include "pa10_syntax_tag_catalog.h"

#include <cstdint>

namespace cppgm
{
namespace pa10_syntax_detail
{

enum SyntaxTagCode : std::uint16_t
{
	STAG_NONE = 0,
#define CPPGM_DEFINE_SYNTAX_TAG(code, spelling) code,
	CPPGM_FOR_EACH_SYNTAX_TAG(CPPGM_DEFINE_SYNTAX_TAG)
#undef CPPGM_DEFINE_SYNTAX_TAG
};

SyntaxTagCode ClassifySyntaxTag(const char* spelling);

}
}

#endif
