#!/usr/bin/perl

use strict;
use warnings;

use File::Temp qw(tempdir);
use FindBin;
use lib $FindBin::Bin;

use CppgmBatchWorker qw(collect_tests run_command_capture);

sub read_file
{
	my ($path, $binary) = @_;
	open(my $fh, '<', $path) or die "Unable to read $path: $!\n";
	binmode($fh) if $binary;
	local $/;
	my $data = <$fh>;
	close($fh) or die "Unable to close $path: $!\n";
	return defined($data) ? $data : '';
}

sub function_body
{
	my ($test, $mir, $name) = @_;
	return $1 if $mir =~
		/(^function \@\Q$name\E\b.*?)(?=^function \@|\z)/ms;
	die "$test: machine IR has no $name function\n";
}

sub u16
{
	my ($data, $offset) = @_;
	return unpack('v', substr($data, $offset, 2));
}

sub u32
{
	my ($data, $offset) = @_;
	return unpack('V', substr($data, $offset, 4));
}

sub i32
{
	my ($data, $offset) = @_;
	return unpack('l<', substr($data, $offset, 4));
}

sub u64
{
	my ($data, $offset) = @_;
	return unpack('Q<', substr($data, $offset, 8));
}

sub address_to_offset
{
	my ($test, $data, $address) = @_;
	my $program_headers = u64($data, 32);
	my $entry_size = u16($data, 54);
	my $entry_count = u16($data, 56);
	for(my $index = 0; $index != $entry_count; ++$index) {
		my $header = $program_headers + $index * $entry_size;
		next if u32($data, $header) != 1; # PT_LOAD
		my $file_offset = u64($data, $header + 8);
		my $virtual_address = u64($data, $header + 16);
		my $file_size = u64($data, $header + 32);
		next if $address < $virtual_address ||
			$address >= $virtual_address + $file_size;
		return $file_offset + ($address - $virtual_address);
	}
	die "$test: executable address is outside every load segment\n";
}

sub main_code
{
	my ($test, $path, $bounded_prefix) = @_;
	my $data = read_file($path, 1);
	die "$test: generated program is not little-endian ELF64\n"
		if substr($data, 0, 6) ne "\x7fELF\x02\x01";
	my $entry_address = u64($data, 24);
	my $entry_offset = address_to_offset($test, $data, $entry_address);
	my $call_offset;
	for(my $offset = $entry_offset;
		$offset + 5 <= length($data) && $offset < $entry_offset + 32;
		++$offset) {
		if(ord(substr($data, $offset, 1)) == 0xe8) {
			$call_offset = $offset;
			last;
		}
	}
	die "$test: startup has no direct call to the entry function\n"
		if !defined($call_offset);
	my $call_address = $entry_address + ($call_offset - $entry_offset);
	my $main_address = $call_address + 5 + i32($data, $call_offset + 1);
	my $main_offset = address_to_offset($test, $data, $main_address);
	my $limit = $main_offset + 256;
	$limit = length($data) if $limit > length($data);
	return substr($data, $main_offset, $limit - $main_offset)
		if $bounded_prefix;
	my $end = $main_offset;
	while($end < $limit && ord(substr($data, $end, 1)) != 0xc3) {
		++$end;
	}
	die "$test: entry function has no bounded near return\n" if $end == $limit;
	return substr($data, $main_offset, $end - $main_offset + 1);
}

sub has_register_shift_left
{
	my ($code) = @_;
	for(my $offset = 0; $offset + 1 < length($code); ++$offset) {
		my $opcode = ord(substr($code, $offset, 1));
		next if $opcode != 0xd1 && $opcode != 0xc1;
		my $modrm = ord(substr($code, $offset + 1, 1));
		return 1 if ($modrm >> 6) == 3 && (($modrm >> 3) & 7) == 4;
	}
	return 0;
}

sub has_indexed_lea
{
	my ($code) = @_;
	for(my $offset = 0; $offset + 1 < length($code); ++$offset) {
		next if ord(substr($code, $offset, 1)) != 0x8d;
		my $modrm = ord(substr($code, $offset + 1, 1));
		return 1 if ($modrm >> 6) != 3;
	}
	return 0;
}

sub has_integer_multiply
{
	my ($code) = @_;
	for(my $offset = 0; $offset < length($code); ++$offset) {
		my $opcode = ord(substr($code, $offset, 1));
		return 1 if $opcode == 0x69 || $opcode == 0x6b;
		return 1 if $opcode == 0x0f && $offset + 1 < length($code) &&
			ord(substr($code, $offset + 1, 1)) == 0xaf;
	}
	return 0;
}

sub count_register_immediate_compares
{
	my ($code) = @_;
	my $count = 0;
	for(my $offset = 0; $offset + 2 < length($code); ++$offset) {
		my $opcode = $offset;
		my $prefix = ord(substr($code, $opcode, 1));
		++$opcode if $prefix >= 0x40 && $prefix <= 0x4f;
		next if $opcode + 2 >= length($code);
		my $opcode_byte = ord(substr($code, $opcode, 1));
		next if $opcode_byte != 0x81 && $opcode_byte != 0x83;
		my $end = $opcode + ($opcode_byte == 0x81 ? 5 : 2);
		next if $end >= length($code);
		my $modrm = ord(substr($code, $opcode + 1, 1));
		next if ($modrm >> 6) != 3 || (($modrm >> 3) & 7) != 7;
		++$count;
		$offset = $end;
	}
	return $count;
}

sub has_register_and_width
{
	my ($code, $wide, $immediate) = @_;
	for(my $offset = 0; $offset + 2 < length($code); ++$offset) {
		my $opcode = $offset;
		my $first = ord(substr($code, $opcode, 1));
		my $rex_w = 0;
		if($first >= 0x40 && $first <= 0x4f) {
			$rex_w = ($first & 8) != 0;
			++$opcode;
		}
		next if $rex_w != $wide || $opcode + 2 >= length($code);
		my $opcode_byte = ord(substr($code, $opcode, 1));
		my $modrm = ord(substr($code, $opcode + 1, 1));
		next if ($modrm >> 6) != 3;
		if(defined($immediate)) {
			next if $opcode_byte != 0x83 || (($modrm >> 3) & 7) != 4;
			return 1 if ord(substr($code, $opcode + 2, 1)) == $immediate;
		} else {
			return 1 if $opcode_byte == 0x21;
			return 1 if ($opcode_byte == 0x81 || $opcode_byte == 0x83) &&
				(($modrm >> 3) & 7) == 4;
		}
	}
	return 0;
}

sub has_vector_copy_pair
{
	my ($code) = @_;
	my ($load, $store) = (0, 0);
	for(my $offset = 0; $offset + 2 < length($code); ++$offset) {
		next if ord(substr($code, $offset, 1)) != 0xf3;
		my $opcode = $offset + 1;
		my $prefix = ord(substr($code, $opcode, 1));
		++$opcode if $prefix >= 0x40 && $prefix <= 0x4f;
		next if $opcode + 1 >= length($code) ||
			ord(substr($code, $opcode, 1)) != 0x0f;
		my $second = ord(substr($code, $opcode + 1, 1));
		$load = 1 if $second == 0x6f;
		$store = 1 if $second == 0x7f;
	}
	return $load && $store;
}

sub has_vector_zero_store
{
	my ($code) = @_;
	my ($clear, $store) = (0, 0);
	for(my $offset = 0; $offset + 2 < length($code); ++$offset) {
		my $prefix = ord(substr($code, $offset, 1));
		next if $prefix != 0x66 && $prefix != 0xf3;
		my $opcode = $offset + 1;
		my $rex = ord(substr($code, $opcode, 1));
		++$opcode if $rex >= 0x40 && $rex <= 0x4f;
		next if $opcode + 2 >= length($code) ||
			ord(substr($code, $opcode, 1)) != 0x0f;
		my $second = ord(substr($code, $opcode + 1, 1));
		my $modrm = ord(substr($code, $opcode + 2, 1));
		if($prefix == 0x66 && $second == 0xef && ($modrm >> 6) == 3) {
			my $left = ($modrm >> 3) & 7;
			my $right = $modrm & 7;
			$clear = 1 if $left == $right;
		}
		$store = 1 if $prefix == 0xf3 && $second == 0x7f &&
			($modrm >> 6) != 3;
	}
	return $clear && $store;
}

sub has_vector_byte_zero_probe
{
	my ($code) = @_;
	my $compares_bytes = index($code, "\x66\x0f\x74") >= 0;
	my $extracts_mask = index($code, "\x66\x0f\xd7") >= 0;
	return $compares_bytes && $extracts_mask;
}

if(scalar(@ARGV) != 2)
{
	die "Usage: check_pa29_native_contracts.pl " .
		"<lowir2native> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root, qr/\.t$/);
die "No PA29 native contract tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa29-native-contract-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my $mir_path = "$directory/test.mir";
	my $program = "$directory/test.program";
	my @compile_command = ($app, '--stats');
	push @compile_command, '-O1' if $test =~ /strlen-prefix-/;
	push @compile_command, '--dump-machine-ir', $mir_path;
	push @compile_command, '-o', $program
		if $test !~ /strlen-prefix-(?:declaration|incompatible)/;
	push @compile_command, $test;
	my $status = run_command_capture(
		cmd => \@compile_command,
		stdout => "$directory/compile.stdout",
		stderr => "$directory/compile.stderr",
		timeout => 30,
	);
	die "$test: native compile failed\n" .
		read_file("$directory/compile.stderr", 0) if $status != 0;
	my $mir = read_file($mir_path, 0);
	if($test =~ /strlen-prefix-incompatible/) {
		my $control = function_body(
			$test, $mir, 'incompatible_signature_control');
		die "$test: incompatible marked call received a strlen prefix fact\n"
			if $control =~ /\bstrlen_prefix=16\b/;
		next;
	}

	if($test =~ /strlen-prefix-declaration/) {
		my $probe = function_body($test, $mir, 'declared_builtin_probe');
		die "$test: declared builtin call lost its serialized prefix fact\n"
			if $probe !~
			/^\s+call\s+\@measure_bytes\s+\[[^\]]*\bstrlen_prefix=16\b[^\]]*\]$/m;
		my $control = function_body($test, $mir, 'ordinary_call_control');
		die "$test: ordinary call incorrectly received a strlen prefix fact\n"
			if $control =~ /\bstrlen_prefix=16\b/;

		my $o0_mir_path = "$directory/test-o0.mir";
		my $o0_status = run_command_capture(
			cmd => [$app, '-O0', '--dump-machine-ir', $o0_mir_path,
				$test],
			stdout => "$directory/compile-o0.stdout",
			stderr => "$directory/compile-o0.stderr",
			timeout => 30,
		);
		die "$test: O0 control compile failed\n" .
			read_file("$directory/compile-o0.stderr", 0) if $o0_status != 0;
		die "$test: O0 declaration unexpectedly selected the O1 prefix fact\n"
			if read_file($o0_mir_path, 0) =~ /\bstrlen_prefix=16\b/;
		next;
	}

	my $expected_status = 0;
	$expected_status = 56 if $test =~ /power-of-two-multiply/;
	$expected_status = 168 if $test =~ /structured-factor-multiply/;
	$expected_status = 66 if $test =~ /frame-copy-operands/;
	$expected_status = 10 if $test =~ /small-copy-boundary/;
	$expected_status = 37 if $test =~ /aligned-medium-copy-direct/;
	$expected_status = 39 if $test =~ /weakly-aligned-medium-copy-compact/;
	$expected_status = 41 if $test =~ /aligned-large-copy-direct/;
	$expected_status = 43 if $test =~ /oversized-aligned-copy-compact/;
	$expected_status = 73 if $test =~ /large-switch-immediate-cases/;
	$expected_status = 25 if $test =~ /strlen-prefix-call/;
	my $run_status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/program.stdout",
		stderr => "$directory/program.stderr",
		timeout => 30,
	);
	die "$test: generated program returned $run_status, expected " .
		"$expected_status\n" if $run_status != $expected_status;

	if($test =~ /power-of-two-multiply/) {
		my $code = main_code($test, $program);
		die "$test: power-of-two multiply did not select a left shift\n"
			if !has_register_shift_left($code);
		die "$test: power-of-two multiply retained an integer multiply\n"
			if has_integer_multiply($code);
		next;
	}
	if($test =~ /structured-factor-multiply/) {
		my $code = main_code($test, $program);
		die "$test: structured-factor multiply did not select indexed add\n"
			if !has_indexed_lea($code);
		die "$test: structured-factor multiply did not select its shift\n"
			if !has_register_shift_left($code);
		die "$test: structured-factor multiply retained an integer multiply\n"
			if has_integer_multiply($code);
		next;
	}
	if($test =~ /zero-extending-and-mask/) {
		my $code = main_code($test, $program);
		die "$test: upper-clearing i64 mask did not select a 32-bit AND\n"
			if !has_register_and_width($code, 0, undef);
		die "$test: upper-preserving i64 mask incorrectly selected a 32-bit AND\n"
			if !has_register_and_width($code, 1, undef);
		next;
	}
	if($test =~ /large-switch-immediate-cases/) {
		my $body = function_body($test, $mir, 'main');
		my @immediate = $body =~
			/^\s+cmp\.i64\s+[a-z0-9]+,\s*-?\d+\s*$/mg;
		die "$test: large switch did not retain its literal case comparisons\n"
			if scalar(@immediate) < 16;
		die "$test: dynamic switch case lost its register comparison fallback\n"
			if $body !~ /^\s+cmp\.i64\s+[a-z0-9]+,\s*[a-z0-9]+\s*$/m;
		my $code = main_code($test, $program, 1);
		die "$test: literal cases were not encoded as immediate comparisons\n"
			if count_register_immediate_compares($code) < 16;
		next;
	}
	if($test =~ /strlen-prefix-call/) {
		my $body = function_body($test, $mir, 'main');
		my @prefix_calls = $body =~
			/^\s+call\s+\@[^\s]+\s+\[[^\]]*\bstrlen_prefix=16\b[^\]]*\]$/mg;
		die "$test: O1 direct builtin calls lost their serialized prefix fact\n"
			if scalar(@prefix_calls) != 2;
		my $code = main_code($test, $program, 1);
		die "$test: strlen prefix fact selected no vector byte-zero probe\n"
			if !has_vector_byte_zero_probe($code);

		my $o0_mir_path = "$directory/test-o0.mir";
		my $o0_status = run_command_capture(
			cmd => [$app, '-O0', '--dump-machine-ir', $o0_mir_path,
				$test],
			stdout => "$directory/compile-o0.stdout",
			stderr => "$directory/compile-o0.stderr",
			timeout => 30,
		);
		die "$test: O0 control compile failed\n" .
			read_file("$directory/compile-o0.stderr", 0) if $o0_status != 0;
		my $o0_body = function_body(
			$test, read_file($o0_mir_path, 0), 'main');
		die "$test: O0 call unexpectedly selected the O1 prefix operation\n"
			if $o0_body =~ /\bstrlen_prefix=16\b/;
		next;
	}
	if($test =~ /frame-copy-operands/) {
		my $body = function_body($test, $mir, 'main');
		die "$test: small copy did not retain two direct frame operands\n"
			if $body !~
			/^\s+copy_bytes 24x8, \[rbp[^\]]*\], \[rbp[^\]]*\]$/m;
		next;
	}
	if($test =~ /small-copy-boundary/) {
		my $body = function_body($test, $mir, 'main');
		die "$test: boundary copy did not retain the documented 32-byte operation\n"
			if $body !~ /^\s+copy_bytes 32x8, /m;
		my $code = main_code($test, $program);
		die "$test: bounded small copy used string-operation setup\n"
			if index($code, "\xf3\xa4") >= 0;
		die "$test: bounded small copy used no reserved-scratch vector chunk\n"
			if !has_vector_copy_pair($code);
		next;
	}
	if($test =~ /aligned-medium-copy-direct/) {
		my $body = function_body($test, $mir, 'main');
		die "$test: aligned medium copy lost its documented operation\n"
			if $body !~ /^\s+copy_bytes 48x8, /m;
		my $code = main_code($test, $program);
		die "$test: aligned medium copy used string-operation setup\n"
			if index($code, "\xf3\xa4") >= 0;
		die "$test: aligned medium copy used no reserved-scratch vector chunk\n"
			if !has_vector_copy_pair($code);
		next;
	}
	if($test =~ /weakly-aligned-medium-copy-compact/) {
		my $body = function_body($test, $mir, 'main');
		die "$test: weakly aligned medium copy lost its documented operation\n"
			if $body !~ /^\s+copy_bytes 48x1, /m;
		my $code = main_code($test, $program);
		die "$test: weakly aligned medium copy lost compact string encoding\n"
			if index($code, "\xf3\xa4") < 0;
		next;
	}
	if($test =~ /aligned-large-copy-direct/) {
		my $body = function_body($test, $mir, 'main');
		die "$test: aligned large copy lost its documented operation\n"
			if $body !~ /^\s+copy_bytes 56x8, /m;
		my $code = main_code($test, $program);
		die "$test: aligned large copy used string-operation setup\n"
			if index($code, "\xf3\xa4") >= 0;
		die "$test: aligned large copy used no reserved-scratch vector chunk\n"
			if !has_vector_copy_pair($code);
		next;
	}
	if($test =~ /oversized-aligned-copy-compact/) {
		my $body = function_body($test, $mir, 'main');
		die "$test: oversized aligned copy lost its documented operation\n"
			if $body !~ /^\s+copy_bytes 72x8, /m;
		my $code = main_code($test, $program);
		die "$test: oversized aligned copy lost compact string encoding\n"
			if index($code, "\xf3\xa4") < 0;
		next;
	}
	if($test =~ /copyobj-indexed-parameter-order/) {
		my $body = function_body($test, $mir, 'copy_indexed_parameter');
		die "$test: indexed-parameter reducer lost its bulk copy\n"
			if $body !~ /^\s+copy_bytes 4x4, /m;

		my $o1_mir_path = "$directory/test-o1.mir";
		my $o1_program = "$directory/test-o1.program";
		my $o1_status = run_command_capture(
			cmd => [$app, '-O1', '--dump-machine-ir', $o1_mir_path,
				'-o', $o1_program, $test],
			stdout => "$directory/compile-o1.stdout",
			stderr => "$directory/compile-o1.stderr",
			timeout => 30,
		);
		die "$test: O1 control compile failed\n" .
			read_file("$directory/compile-o1.stderr", 0) if $o1_status != 0;
		my $o1_run = run_command_capture(
			cmd => [$o1_program],
			stdout => "$directory/program-o1.stdout",
			stderr => "$directory/program-o1.stderr",
			timeout => 30,
		);
		die "$test: O1 generated program returned $o1_run, expected 0\n"
			if $o1_run != 0;
		my $o1_body = function_body(
			$test, read_file($o1_mir_path, 0), 'copy_indexed_parameter');
		die "$test: O1 indexed-parameter reducer lost its bulk copy\n"
			if $o1_body !~ /^\s+copy_bytes 4x4, /m;
		next;
	}
	if($test =~ /unused-result-builtin-memcpy/) {
		my $eligible = function_body($test, $mir, 'eligible_copy');
		die "$test: eligible unused builtin call lost its dynamic copy\n"
			if $eligible !~ /^\s+copy_bytes_dynamic\s*$/m;
		die "$test: eligible unused builtin call retained a direct call\n"
			if $eligible =~ /^\s+call\s+\@memory_copy\b/m;
		my $used = function_body($test, $mir, 'used_result_copy');
		die "$test: used builtin result did not retain its call\n"
			if $used !~ /^\s+call\s+\@memory_copy\b/m ||
			   $used =~ /^\s+copy_bytes_dynamic\s*$/m;
		my $ordinary = function_body($test, $mir, 'ordinary_unused_copy');
		die "$test: unmarked unused-result function did not retain its call\n"
			if $ordinary !~ /^\s+call\s+\@ordinary_copy\b/m ||
			   $ordinary =~ /^\s+copy_bytes_dynamic\s*$/m;

		my $o1_mir_path = "$directory/test-o1.mir";
		my $o1_program = "$directory/test-o1.program";
		my $o1_status = run_command_capture(
			cmd => [$app, '-O1', '--dump-machine-ir', $o1_mir_path,
				'-o', $o1_program, $test],
			stdout => "$directory/compile-o1.stdout",
			stderr => "$directory/compile-o1.stderr",
			timeout => 30,
		);
		die "$test: O1 control compile failed\n" .
			read_file("$directory/compile-o1.stderr", 0) if $o1_status != 0;
		my $o1_run = run_command_capture(
			cmd => [$o1_program],
			stdout => "$directory/program-o1.stdout",
			stderr => "$directory/program-o1.stderr",
			timeout => 30,
		);
		die "$test: O1 generated program returned $o1_run, expected 0\n"
			if $o1_run != 0;
		my $o1_mir = read_file($o1_mir_path, 0);
		my $o1_eligible = function_body(
			$test, $o1_mir, 'eligible_copy');
		die "$test: O1 eligible call lost its dynamic copy\n"
			if $o1_eligible !~ /^\s+copy_bytes_dynamic\s*$/m;
		my $o1_used = function_body($test, $o1_mir, 'used_result_copy');
		die "$test: O1 used builtin result did not retain its call\n"
			if $o1_used !~ /^\s+call\s+\@memory_copy\b/m;
		my $o1_ordinary = function_body(
			$test, $o1_mir, 'ordinary_unused_copy');
		die "$test: O1 unmarked function did not retain its call\n"
			if $o1_ordinary !~ /^\s+call\s+\@ordinary_copy\b/m;
		next;
	}
	if($test =~ /cost-directed-small-zeroinit/) {
		my $body = function_body($test, $mir, 'main');
		die "$test: fixed zero lost its documented 16-byte operation\n"
			if $body !~ /^\s+zero_bytes 16x8, /m;
		my $code = main_code($test, $program);
		die "$test: 16-byte zero retained string-operation setup\n"
			if index($code, "\xf3\xaa") >= 0;
		die "$test: 16-byte zero used no cleared vector store\n"
			if !has_vector_zero_store($code);
		next;
	}
	if($test =~ /scratch-carried-frame-reloads/) {
		my $stats = read_file("$directory/compile.stderr", 0);
		my ($count) = $stats =~ /\bscratch_carried_reloads=(\d+)\b/;
		die "$test: --stats omitted scratch_carried_reloads\n"
			if !defined($count);
		die "$test: pressure reducer exercised no bounded carried reload\n"
			if $count == 0;
		next;
	}
	if($test =~ /deferred-address-parameter-carrier-reuse/) {
		my $probe = function_body($test, $mir, 'probe');
		my ($this_register) = $probe =~
			/^\s+param %this -> ([a-z0-9]+) : ptr$/m;
		my $this_home = $1 if $probe =~
			/^\s+param-slot %this -> (\[rbp[^\]]*\]) : ptr$/m;
		die "$test: reducer lost its parameter carrier or fallback home\n"
			if !defined($this_register) || !defined($this_home);
		my ($then) = $probe =~
			/(^\s+block \^then\n.*?)(?=^\s+block \^)/ms;
		die "$test: initializer arm is missing\n" if !defined($then);
		die "$test: live parameter-derived address was unnecessarily reloaded\n"
			if $then !~ /^\s+load\.ptr [a-z0-9]+, \[\Q$this_register\E\+104\]$/m ||
			   $then =~ /^\s+load\.ptr \Q$this_register\E, \Q$this_home\E$/m;
		my ($tail) = $probe =~
			/(^\s+block \^tail\n.*?)(?=^\s+block \^|\z)/ms;
		die "$test: post-call arm is missing\n" if !defined($tail);
		die "$test: clobbering call did not restore the parameter from its home\n"
			if $tail !~ /^\s+load\.ptr [a-z0-9]+, \Q$this_home\E$/m;
		next;
	}
	if($test =~ /direct-global-storage/) {
		my $direct = function_body($test, $mir, 'direct_storage_use');
		die "$test: local-global load did not retain the symbol operand\n"
			if $direct !~ /^\s+load\.i64 [a-z0-9]+, \@value$/m;
		die "$test: local-global store did not retain the symbol operand\n"
			if $direct !~ /^\s+store\.i64 \@value, /m;
		my $observed = function_body($test, $mir, 'observe_address');
		my ($carrier) = $observed =~
			/^\s+(?:mov|lea) ([a-z0-9]+), \@value$/m;
		die "$test: observed global address was not materialized\n"
			if !defined($carrier) ||
			   $observed !~ /^\s+ret \Q$carrier\E$/m;
		next;
	}
	if($test =~ /o0-layout-policy/) {
		for my $name ('choose', 'main') {
			my $body = function_body($test, $mir, $name);
			die "$test: O0 $name did not retain a frame pointer\n"
				if $body !~ /^\s+frame_pointer keep$/m;
			die "$test: O0 $name did not retain shared epilogues\n"
				if $body !~ /^\s+epilogues shared$/m;
		}
		next;
	}
	if($test =~ /unreachable-terminator/) {
		my $body = function_body($test, $mir, 'guarded');
		die "$test: guarded function lost its impossible control-flow block\n"
			if $body !~ /^\s+block \^impossible\s*$/m;
		die "$test: unreachable continuation became a synthetic native call\n"
			if $body =~ /^\s+call\s+\@[^\n]*unreachable/m;
		next;
	}
	if($test =~ /by-address-call-materializes-storage/) {
		my $body = function_body($test, $mir, 'main');
		my ($home) = $body =~
			/^\s+temp %v -> (\[[^\]\n]+\]) : i64$/m;
		die "$test: scalar call result received no addressable home\n"
			if !defined($home);
		die "$test: scalar call result was not preserved in its home\n"
			if $body !~ /^\s+store\.i64 \Q$home\E, [a-z0-9]+$/m;
		my ($address) = $body =~
			/^\s+lea ([a-z0-9]+), \Q$home\E$/m;
		die "$test: by-address argument did not materialize the home's address\n"
			if !defined($address);
		die "$test: materialized address was not consumed by the call boundary\n"
			if $body !~
			/^\s+call \@copy_ref \[args=\([^\n)]*\b\Q$address\E\b[^\n)]*\)\]$/m;
		next;
	}
	die "$test: no native property predicate selected\n";
}

print "PA29 native contract properties: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
