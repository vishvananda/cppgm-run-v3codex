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
	die "Usage: check_pa13_stable_prefix_query.pl " .
		"<lowiropt> <lowir2cy86> <cy86> <test-or-directory>\n";
}

my ($lowiropt, $lowir2cy86, $cy86, $root) = @ARGV;
my @tests = collect_tests($root,
	qr/retained-stable-prefix-query\.lowir$/);
die "No PA13 stable-prefix-query controls found under $root\n" if !@tests;

my $retained = 0;
for my $test (@tests)
{
	my $directory = tempdir('pa13-stable-prefix-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $roundtrip = "$directory/roundtrip.lowir";
	my $cy86_source = "$directory/test.cy86";
	my $status = run_command_capture(
		cmd => [$lowiropt, '-O0', '-o', $roundtrip, $test],
		stdout => "$directory/lowiropt.stdout",
		stderr => "$directory/lowiropt.stderr",
		timeout => 30,
	);

	die "$test: LowIR O0 roundtrip failed\n" .
		read_file("$directory/lowiropt.stderr") if $status != 0;
	my $text = read_file($roundtrip);
	my ($header) = $text =~ /^(function \@prefix_query\([^\n]+\)[^\n]+)$/m;
	die "$test: O0 roundtrip lost the query function boundary\n"
		if !defined($header);
	die "$test: O0 roundtrip lost query=stable_prefix\n"
		if $header !~ /\bquery=stable_prefix\b/;
	die "$test: stable-prefix boundary lost its final integer parameter\n"
		if $header !~ /%\w+\s*:\s*i64\)\s*->\s*i64\b/;

	$status = run_command_capture(
		cmd => [$lowir2cy86, '-o', $cy86_source, $roundtrip],
		stdout => "$directory/lowir2cy86.stdout",
		stderr => "$directory/lowir2cy86.stderr",
		timeout => 30,
	);
	die "$test: LowIR-to-CY86 translation failed\n" .
		read_file("$directory/lowir2cy86.stderr") if $status != 0;
	my $program = "$directory/test.program";
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
	die "$test: stable-prefix behavior returned $status\n" if $status != 0;
	++$retained;
}

die "PA13 stable-prefix controls lack a retained case\n" if !$retained;
print "PA13 stable-prefix query properties: PASS (" . scalar(@tests) .
	"/" . scalar(@tests) . ")\n";
