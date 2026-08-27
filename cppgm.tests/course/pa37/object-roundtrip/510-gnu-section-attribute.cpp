extern "C" const void *section_alias;

__attribute__((section("cppgmsec")))
const void *section_alias = &section_alias;

int main()
{
	return section_alias == &section_alias ? 0 : 1;
}
