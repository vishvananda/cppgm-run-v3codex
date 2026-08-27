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

if (scalar(@ARGV) != 4)
{
	die "Usage: check_pa13_index_projection_contract.pl " .
		"<lowiropt> <lowir2cy86> <cy86> <test-or-directory>\n";
}

my ($lowiropt, $lowir2cy86, $cy86, $root) = @ARGV;
my @retained = collect_tests($root, qr/retained-index-projections\.lowir$/);
my @removed = collect_tests($root,
	qr/removed-(?:base-subobject|reference-field)-projection\.lowir$/);
die "No PA13 index-projection controls found under $root\n"
	if !@retained && !@removed;

for my $test (@retained)
{
	my $directory = tempdir('pa13-index-projection-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $roundtrip = "$directory/roundtrip.lowir";
	my $cy86_source = "$directory/test.cy86";
	my $program = "$directory/test.program";
	my $status = run_command_capture(
		cmd => [$lowiropt, '-O0', '-o', $roundtrip, $test],
		stdout => "$directory/lowiropt.stdout",
		stderr => "$directory/lowiropt.stderr",
		timeout => 30,
	);
	die "$test: O0 LowIR roundtrip failed\n" .
		read_file("$directory/lowiropt.stderr") if $status != 0;
	my $text = read_file($roundtrip);
	my ($base) = $text =~ /^\s+(%\w+) = addr \@values$/m;
	die "$test: retained projection control lost its common base\n"
		if !defined($base);
	die "$test: field projection did not preserve its byte offset\n"
		if $text !~
			/^\s+%\w+ = index i8 \[projection=field\] \Q$base\E, 8$/m;
	die "$test: array projection did not preserve its element index\n"
		if $text !~
			/^\s+%\w+ = index i64 \[projection=array_element\] \Q$base\E, 1$/m;
	die "$test: removed source-origin projection remains in LowIR\n"
		if $text =~ /\bprojection=(?:base_subobject|reference_field)\b/;

	$status = run_command_capture(
		cmd => [$lowir2cy86, '-o', $cy86_source, $roundtrip],
		stdout => "$directory/lowir2cy86.stdout",
		stderr => "$directory/lowir2cy86.stderr",
		timeout => 30,
	);
	die "$test: LowIR-to-CY86 translation failed\n" .
		read_file("$directory/lowir2cy86.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$cy86, '-o', $program, $cy86_source],
		stdout => "$directory/cy86.stdout",
		stderr => "$directory/cy86.stderr",
		timeout => 30,
	);
	die "$test: generated CY86 did not compile\n" .
		read_file("$directory/cy86.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/program.stdout",
		stderr => "$directory/program.stderr",
		timeout => 10,
	);
	die "$test: retained projection behavior returned $status, expected 0\n"
		if $status != 0;
}

for my $test (@removed)
{
	my $directory = tempdir('pa13-removed-projection-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $status = run_command_capture(
		cmd => [$lowiropt, '-O0', '-o', "$directory/output.lowir", $test],
		stdout => "$directory/lowiropt.stdout",
		stderr => "$directory/lowiropt.stderr",
		timeout => 30,
	);
	die "$test: removed source-origin projection was accepted\n"
		if $status == 0;
}

my $count = scalar(@retained) + scalar(@removed);
print "PA13 index-projection contract: PASS ($count/$count)\n";
