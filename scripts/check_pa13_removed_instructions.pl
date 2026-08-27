#!/usr/bin/perl

use strict;
use warnings;

use File::Temp qw(tempdir);
use FindBin;
use lib $FindBin::Bin;

use CppgmBatchWorker qw(collect_tests run_command_capture);

if (scalar(@ARGV) != 2)
{
	die "Usage: check_pa13_removed_instructions.pl " .
		"<lowiropt> <test-or-directory>\n";
}

my ($lowiropt, $root) = @ARGV;
my @tests = collect_tests($root, qr/removed-(?:unary-decay)\.lowir$/);
die "No PA13 removed-instruction controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa13-removed-instruction-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $status = run_command_capture(
		cmd => [$lowiropt, '-O0', '-o', "$directory/output.lowir", $test],
		stdout => "$directory/lowiropt.stdout",
		stderr => "$directory/lowiropt.stderr",
		timeout => 30,
	);
	die "$test: removed LowIR instruction was accepted\n" if $status == 0;
}

print "PA13 removed instructions: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
