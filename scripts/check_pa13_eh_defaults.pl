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

if (scalar(@ARGV) != 3)
{
	die "Usage: check_pa13_eh_defaults.pl " .
		"<lowir2cy86> <cy86> <test-or-directory>\n";
}

my ($lowir2cy86, $cy86, $root) = @ARGV;
my @tests = collect_tests($root, qr/default-eh-(?:caught|unhandled)\.lowir$/);
die "No PA13 default-EH controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa13-eh-default-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $cy86_source = "$directory/test.cy86";
	my $program = "$directory/test.program";
	my $status = run_command_capture(
		cmd => [$lowir2cy86, '-o', $cy86_source, $test],
		stdout => "$directory/lowir2cy86.stdout",
		stderr => "$directory/lowir2cy86.stderr",
		timeout => 30,
	);
	die "$test: LowIR-to-CY86 translation failed\n" .
		read_file("$directory/lowir2cy86.stderr") if $status != 0;

	my $text = read_file($cy86_source);
	die "$test: private EH handler-stack/value storage is missing\n"
		if $text !~ /^g____cppgm_eh_top:\s*$/m ||
		   $text !~ /^g____cppgm_eh_value:\s*$/m;
	die "$test: private unhandled-exception fallback is missing\n"
		if $text !~ /^fn____cppgm_eh_unhandled:\s*$/m;

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
	my $expected = $test =~ /unhandled/ ? 23 : 0;
	die "$test: generated program returned $status, expected $expected\n"
		if $status != $expected;
}

print "PA13 private EH defaults: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
