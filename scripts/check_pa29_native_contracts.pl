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
	my ($test, $path) = @_;
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
	my $status = run_command_capture(
		cmd => [$app, '--stats', '--dump-machine-ir', $mir_path,
			'-o', $program, $test],
		stdout => "$directory/compile.stdout",
		stderr => "$directory/compile.stderr",
		timeout => 30,
	);
	die "$test: native compile failed\n" .
		read_file("$directory/compile.stderr", 0) if $status != 0;

	my $expected_status = 0;
	$expected_status = 56 if $test =~ /power-of-two-multiply/;
	$expected_status = 168 if $test =~ /structured-factor-multiply/;
	$expected_status = 66 if $test =~ /frame-copy-operands/;
	$expected_status = 10 if $test =~ /small-copy-boundary/;
	$expected_status = 37 if $test =~ /aligned-medium-copy-direct/;
	$expected_status = 39 if $test =~ /weakly-aligned-medium-copy-compact/;
	$expected_status = 41 if $test =~ /aligned-large-copy-direct/;
	$expected_status = 43 if $test =~ /oversized-aligned-copy-compact/;
	my $run_status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/program.stdout",
		stderr => "$directory/program.stderr",
		timeout => 30,
	);
	die "$test: generated program returned $run_status, expected " .
		"$expected_status\n" if $run_status != $expected_status;

	my $mir = read_file($mir_path, 0);
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
	die "$test: no native property predicate selected\n";
}

print "PA29 native contract properties: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
