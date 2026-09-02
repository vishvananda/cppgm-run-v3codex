#include <cstdlib>
#include <exception>
#include <iostream>
#include <iterator>
#include <string>

#include "preprocess/tokens/DebugPPTokenStream.h"
#include "preprocess/tokens/pp_tokenizer.h"
#include "support/exception_types.h"

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
	catch (const CompilerError& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
	catch (const std::exception& error)
	{
		std::cerr << "ERROR: " << error.what() << std::endl;
		return EXIT_FAILURE;
	}
}
