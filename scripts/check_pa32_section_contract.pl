#!/usr/bin/perl

use strict;
use warnings;

use File::Temp qw(tempdir);
use FindBin;
use lib $FindBin::Bin;

use CppgmBatchWorker qw(collect_tests run_command_capture);

sub read_file
{
	my ($path) = @_;
	open(my $fh, '<', $path) or die "Unable to read $path: $!\n";
	local $/;
	my $data = <$fh>;
	close($fh) or die "Unable to close $path: $!\n";
	return defined($data) ? $data : '';
}

if (scalar(@ARGV) != 2)
{
	die "Usage: check_pa32_section_contract.pl " .
		"<cppgm++> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root, qr/section-[^.]+\.cpp$/);
die "No PA32 section controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa32-section-contract-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $lowir = "$directory/test.lowir";
	my $status = run_command_capture(
		cmd => [$app, '--emit-lowir', '-g0', '-O0', '-o', $lowir, $test],
		stdout => "$directory/compile.stdout",
		stderr => "$directory/compile.stderr",
		timeout => 30,
	);

	if ($test =~ /(?:unsafe|conflicting)/)
	{
		die "$test: unsupported section attribute was accepted\n"
			if $status == 0;
		next;
	}

	die "$test: token-safe section source did not compile\n" .
		read_file("$directory/compile.stderr") if $status != 0;
	my $text = read_file($lowir);
	my ($metadata) = $text =~
		/^global \@section_alias\b[^\n]*\[([^\]]+)\]\s*=\s*addr \@section_alias$/m;
	die "$test: emitted LowIR lost the section_alias global relationship\n"
		if !defined($metadata);
	die "$test: emitted LowIR lost section=cppgmsec\n"
		if $metadata !~ /(?:^|,\s*)section=cppgmsec(?:,|$)/;

	my $object = "$directory/test.o";
	$status = run_command_capture(
		cmd => [$app, '-c', '-g0', '-O0', '-o', $object, $test],
		stdout => "$directory/object.stdout",
		stderr => "$directory/object.stderr",
		timeout => 30,
	);
	die "$test: direct object compile failed\n" .
		read_file("$directory/object.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => ['readelf', '-SW', $object],
		stdout => "$directory/sections.txt",
		stderr => "$directory/readelf.stderr",
		timeout => 30,
	);
	die "$test: readelf section inspection failed\n" if $status != 0;
	die "$test: section_alias object has no cppgmsec section\n"
		if read_file("$directory/sections.txt") !~
			/^\s*\[\s*\d+\]\s+cppgmsec\s+PROGBITS\b/m;
}

print "PA32 global-section properties: PASS (" . scalar(@tests) .
	"/" . scalar(@tests) . ")\n";
