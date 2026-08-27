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
	die "Usage: check_pa13_unreachable_terminator.pl " .
		"<lowiropt> <lowir2cy86> <cy86> <test-or-directory>\n";
}

my ($lowiropt, $lowir2cy86, $cy86, $root) = @ARGV;
my @positive = collect_tests($root, qr/unreachable-terminator\.lowir$/);
my @negative = collect_tests($root,
	qr/(?:unreachable-followed-by-instruction|unreachable-with-operand|removed-unreachable-role)\.lowir$/);
die "No PA13 unreachable-terminator control found under $root\n"
	if !@positive;
die "PA13 unreachable negative controls are incomplete under $root\n"
	if scalar(@negative) != 3;

for my $test (@positive)
{
	my $directory = tempdir('pa13-unreachable-XXXXXX', TMPDIR => 1,
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
	die "$test: LowIR O0 roundtrip failed\n" .
		read_file("$directory/lowiropt.stderr") if $status != 0;
	my $text = read_file($roundtrip);
	die "$test: O0 roundtrip lost the unreachable terminator\n"
		if $text !~ /^\s+unreachable\s*$/m;
	die "$test: O0 roundtrip retained a synthetic unreachable symbol\n"
		if $text =~ /\brole=unreachable\b|\bcall\s+void\s+\@[^\n]*unreachable/;

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
	die "$test: guarded normal path returned $status, expected 0\n"
		if $status != 0;
}

for my $test (@negative)
{
	my $directory = tempdir('pa13-unreachable-bad-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $status = run_command_capture(
		cmd => [$lowiropt, '-O0', '-o', "$directory/output.lowir", $test],
		stdout => "$directory/lowiropt.stdout",
		stderr => "$directory/lowiropt.stderr",
		timeout => 30,
	);
	die "$test: invalid removed/terminator syntax was accepted\n"
		if $status == 0;
}

print "PA13 unreachable terminator properties: PASS (" .
	(scalar(@positive) + scalar(@negative)) . "/" .
	(scalar(@positive) + scalar(@negative)) . ")\n";
