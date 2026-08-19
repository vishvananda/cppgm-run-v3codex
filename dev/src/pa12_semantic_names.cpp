#include "pa12_semantic_detail.h"

#include <cctype>
#include <stdexcept>
#include <string>

namespace cppgm
{
namespace pa12_semantic_detail
{

NamePath SemanticAnalyzer::ParseNamePath(const std::string& spelling)
{
	if (stats_) ++stats_->name_path_parse_requests;
	NamePath result;
	std::size_t first = 0;
	result.global = spelling.size() >= 2 && spelling[0] == ':' &&
		spelling[1] == ':';
	if (result.global) first = 2;
	std::size_t conversion_terminal = std::string::npos;
	if (spelling.compare(first, 9, "operator ") == 0)
		conversion_terminal = first;
	else
	{
		const std::size_t separator = spelling.find("::operator ", first);
		if (separator != std::string::npos)
			conversion_terminal = separator + 2;
	}
	while (first < spelling.size())
	{
		std::size_t separator = std::string::npos;
		if (first != conversion_terminal)
		{
			std::size_t angle_depth = 0;
			for (std::size_t scan = first; scan + 1 < spelling.size(); ++scan)
			{
				if (spelling[scan] == '<') ++angle_depth;
				else if (spelling[scan] == '>' && angle_depth != 0) --angle_depth;
				else if (spelling[scan] == ':' && spelling[scan + 1] == ':' &&
					angle_depth == 0)
				{
					separator = scan;
					break;
				}
			}
		}
		const std::size_t last = separator == std::string::npos ?
			spelling.size() : separator;
		if (last == first)
			throw std::runtime_error("invalid qualified name");
		if (first == conversion_terminal)
		{
			std::string terminal;
			terminal.reserve(last - first);
			for (std::size_t i = first; i < last; ++i)
				if (!std::isspace(static_cast<unsigned char>(spelling[i])))
					terminal += spelling[i];
			result.Push(program_->names.Intern(terminal));
		}
		else result.Push(
			program_->names.InternRange(spelling, first, last - first));
		if (stats_) ++stats_->name_path_parse_components;
		if (separator == std::string::npos) break;
		first = separator + 2;
	}
	if (stats_ && result.Size() == 1)
		++stats_->name_path_single_component_parses;
	return result;
}

}
}
