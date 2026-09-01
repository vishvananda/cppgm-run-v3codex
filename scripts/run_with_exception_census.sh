#!/bin/sh

set -eu

: "${CPPGM_EXCEPTION_CENSUS_COMPILER:?set the compiler to execute}"
: "${CPPGM_EXCEPTION_CENSUS_LIBRARY:?set the exception census preload library}"
: "${CPPGM_EXCEPTION_CENSUS_FILE:?set the exception census output file}"

if [ -n "${LD_PRELOAD:-}" ]; then
	CPPGM_CENSUS_PRELOAD="${CPPGM_EXCEPTION_CENSUS_LIBRARY}:${LD_PRELOAD}"
else
	CPPGM_CENSUS_PRELOAD="${CPPGM_EXCEPTION_CENSUS_LIBRARY}"
fi
export LD_PRELOAD="${CPPGM_CENSUS_PRELOAD}"

exec "${CPPGM_EXCEPTION_CENSUS_COMPILER}" "$@"
