#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "DebugPPTokenStream.h"
#include "pp_tokenizer.h"

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	try
	{
		std::ios_base::sync_with_stdio(false);
		std::cin.tie(0);
		const std::string source((std::istreambuf_iterator<char>(std::cin)),
			std::istreambuf_iterator<char>());

		DebugPPTokenStream output;
		cppgm::TokenizePreprocessingFile(source, output);
		return EXIT_SUCCESS;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
