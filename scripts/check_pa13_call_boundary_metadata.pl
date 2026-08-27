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
	die "Usage: check_pa13_call_boundary_metadata.pl " .
		"<lowiropt> <lowir2cy86> <cy86> <test-or-directory>\n";
}

my ($lowiropt, $lowir2cy86, $cy86, $root) = @ARGV;
my @tests = collect_tests($root, qr/retained-call-boundary-metadata\.lowir$/);
die "No PA13 retained call-boundary metadata control found under $root\n"
	if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa13-call-boundary-XXXXXX', TMPDIR => 1,
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
	my ($parameters) = $text =~ /^function \@helper\(([^\n]*)\) -> void/m;
	die "$test: O0 roundtrip lost helper boundary\n" if !defined($parameters);
	for my $mode (qw(indirect_result by_address))
	{
		die "$test: O0 roundtrip lost pass=$mode\n"
			if $parameters !~ /\bpass=\Q$mode\E\b/;
	}
	die "$test: ordinary pointer gained source-origin passing metadata\n"
		if $parameters !~ /%ordinary : ptr(?:,|$)/ ||
		   $text =~ /\bpass=decay\b|\bunary\s+decay\b/;
	die "$test: call boundary does not pass an explicit pointer value\n"
		if $text !~ /^\s+%\w+ = addr \$out$/m ||
		   $text !~ /^\s+call void \@helper\(%\w+, %\w+, %\w+, 5\)$/m;

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
	die "$test: retained call-boundary behavior returned $status\n"
		if $status != 0;
}

print "PA13 call-boundary metadata properties: PASS (" . scalar(@tests) .
	"/" . scalar(@tests) . ")\n";
