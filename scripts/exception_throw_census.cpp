#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <typeinfo>

namespace
{

struct ThrowEntry
{
	const std::type_info* type;
	const char* object;
	const char* symbol;
	std::uintptr_t relative_pc;
	std::uint64_t count;
};

const std::size_t kMaxThrowEntries = 4096;
ThrowEntry entries[kMaxThrowEntries];
std::size_t entry_count;
bool flush_registered;
thread_local bool inside_interposer;

void FlushCensus()
{
	const char* path = std::getenv("CPPGM_EXCEPTION_CENSUS_FILE");
	const char* tag = std::getenv("CPPGM_EXCEPTION_CENSUS_TAG");
	if (!path || !*path || entry_count == 0) return;
	const int output = open(path, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (output < 0) return;
	if (flock(output, LOCK_EX) != 0)
	{
		close(output);
		return;
	}
	for (std::size_t i = 0; i < entry_count; ++i)
	{
		const ThrowEntry& entry = entries[i];
		dprintf(output,
			"pid=%ld tag=%s type=%s object=%s symbol=%s pc=0x%llx count=%llu\n",
			static_cast<long>(getpid()),
			tag && *tag ? tag : "<none>",
			entry.type ? entry.type->name() : "<null>",
			entry.object ? entry.object : "<unknown>",
			entry.symbol ? entry.symbol : "<unknown>",
			static_cast<unsigned long long>(entry.relative_pc),
			static_cast<unsigned long long>(entry.count));
	}
	flock(output, LOCK_UN);
	close(output);
}

void RecordThrow(const std::type_info* type, void* return_address)
{
	Dl_info info;
	const bool resolved = dladdr(return_address, &info) != 0;
	const std::uintptr_t address =
		reinterpret_cast<std::uintptr_t>(return_address);
	const std::uintptr_t base = resolved ?
		reinterpret_cast<std::uintptr_t>(info.dli_fbase) : 0;
	const std::uintptr_t relative = address - base;
	const char* object = resolved ? info.dli_fname : 0;
	const char* symbol = resolved ? info.dli_sname : 0;
	for (std::size_t i = 0; i < entry_count; ++i)
	{
		if (entries[i].type == type && entries[i].relative_pc == relative &&
			entries[i].object == object)
		{
			++entries[i].count;
			return;
		}
	}
	if (entry_count == kMaxThrowEntries) return;
	ThrowEntry& entry = entries[entry_count++];
	entry.type = type;
	entry.object = object;
	entry.symbol = symbol;
	entry.relative_pc = relative;
	entry.count = 1;
}

typedef void (*ThrowFunction)(void*, std::type_info*, void (*)(void*));

ThrowFunction RealThrow()
{
	static ThrowFunction function = reinterpret_cast<ThrowFunction>(
		dlsym(RTLD_NEXT, "__cxa_throw"));
	return function;
}

} // namespace

extern "C" __attribute__((noreturn)) void __cxa_throw(
	void* object, std::type_info* type, void (*destroy)(void*))
{
	ThrowFunction real_throw = RealThrow();
	if (!inside_interposer)
	{
		inside_interposer = true;
		if (!flush_registered)
		{
			flush_registered = true;
			std::atexit(FlushCensus);
		}
		RecordThrow(type, __builtin_return_address(0));
		inside_interposer = false;
	}
	if (!real_throw) _exit(127);
	real_throw(object, type, destroy);
	__builtin_unreachable();
}
