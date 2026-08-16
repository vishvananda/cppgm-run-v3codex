#include "pa10_syntax_tags.h"

#include <cstring>

namespace cppgm
{
namespace pa10_syntax_detail
{
namespace
{

struct SyntaxTagEntry
{
	const char* spelling;
	SyntaxTagCode code;
};

const SyntaxTagEntry kSyntaxTags[] = {
#define CPPGM_DEFINE_SYNTAX_TAG_ENTRY(code, spelling) {spelling, code},
	CPPGM_FOR_EACH_SYNTAX_TAG(CPPGM_DEFINE_SYNTAX_TAG_ENTRY)
#undef CPPGM_DEFINE_SYNTAX_TAG_ENTRY
};

}

SyntaxTagCode ClassifySyntaxTag(const char* spelling)
{
	for (std::size_t i = 0; i < sizeof(kSyntaxTags) / sizeof(kSyntaxTags[0]); ++i)
		if (std::strcmp(spelling, kSyntaxTags[i].spelling) == 0)
			return kSyntaxTags[i].code;
	return STAG_NONE;
}

}
}
