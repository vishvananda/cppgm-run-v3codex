#include "pa15_lowering_support.h"

namespace cppgm
{
namespace pa15_lowering_support
{

namespace
{

int HexDigit(char value)
{
	return value >= '0' && value <= '9' ? value - '0' :
		value >= 'a' && value <= 'f' ? value - 'a' + 10 :
		value >= 'A' && value <= 'F' ? value - 'A' + 10 : -1;
}

}

std::string StripOperationPrefix(const std::string& operation)
{
	const std::size_t colon = operation.rfind(':');
	return colon == std::string::npos ? operation : operation.substr(colon + 1);
}

std::string SanitizeSymbol(const std::string& name)
{
	std::string result;
	result.reserve(name.size() + 8);
	for (std::size_t i = 0; i < name.size(); ++i)
	{
		if (i + 1 < name.size() && name[i] == ':' && name[i + 1] == ':')
		{
			result += "__";
			++i;
		}
		else
		{
			const unsigned char c = static_cast<unsigned char>(name[i]);
			result += (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_' ? static_cast<char>(c) : '_';
		}
	}
	if (result.empty()) result = "anonymous";
	return result;
}

std::int64_t CanonicalIntegerImmediate(std::int64_t value,
	std::uint8_t width, bool is_signed)
{
	if (width >= 64) return value;
	const std::uint64_t mask = (std::uint64_t(1) << width) - 1;
	std::uint64_t narrowed = static_cast<std::uint64_t>(value) & mask;
	if (is_signed &&
		(narrowed & (std::uint64_t(1) << (width - 1))) != 0)
		narrowed |= ~mask;
	return static_cast<std::int64_t>(narrowed);
}

std::vector<unsigned char> DecodeStringLiteral(const std::string& spelling)
{
	std::vector<unsigned char> bytes;
	const std::size_t first = spelling.find('"');
	const std::size_t last = spelling.rfind('"');
	if (first == std::string::npos || last <= first)
		throw std::runtime_error("invalid PA15 string literal spelling");
	for (std::size_t i = first + 1; i < last; ++i)
	{
		unsigned value = static_cast<unsigned char>(spelling[i]);
		if (spelling[i] == '\\' && ++i < last)
		{
			const char escape = spelling[i];
			if (escape == 'x')
			{
				value = 0;
				int digit = -1;
				while (i + 1 < last && (digit = HexDigit(spelling[i + 1])) >= 0)
				{
					value = value * 16 + static_cast<unsigned>(digit);
					++i;
				}
			}
			else if (escape >= '0' && escape <= '7')
			{
				value = static_cast<unsigned>(escape - '0');
				for (int count = 1; count < 3 && i + 1 < last &&
					spelling[i + 1] >= '0' && spelling[i + 1] <= '7'; ++count)
					value = value * 8 +
						static_cast<unsigned>(spelling[++i] - '0');
			}
			else value = escape == 'n' ? '\n' : escape == 'r' ? '\r' :
				escape == 't' ? '\t' : escape == 'v' ? '\v' :
				escape == 'b' ? '\b' : escape == 'f' ? '\f' :
				escape == 'a' ? '\a' : static_cast<unsigned char>(escape);
		}
		bytes.push_back(static_cast<unsigned char>(value));
	}
	bytes.push_back(0);
	return bytes;
}

CountingStreamBuffer::CountingStreamBuffer(std::streambuf* destination)
	: destination_(destination), bytes_(0)
{
}

std::size_t CountingStreamBuffer::Bytes() const
{
	return bytes_;
}

CountingStreamBuffer::int_type CountingStreamBuffer::overflow(int_type character)
{
	if (traits_type::eq_int_type(character, traits_type::eof()))
		return traits_type::not_eof(character);
	const int_type written = destination_->sputc(
		traits_type::to_char_type(character));
	if (!traits_type::eq_int_type(written, traits_type::eof())) ++bytes_;
	return written;
}

std::streamsize CountingStreamBuffer::xsputn(const char* data,
	std::streamsize size)
{
	const std::streamsize written = destination_->sputn(data, size);
	if (written > 0) bytes_ += static_cast<std::size_t>(written);
	return written;
}

int CountingStreamBuffer::sync()
{
	return destination_->pubsync();
}

}
}
