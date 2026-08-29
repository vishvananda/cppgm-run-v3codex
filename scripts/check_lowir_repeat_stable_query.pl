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

sub optimize
{
	my ($app, $test, $directory, $level, $stats) = @_;
	my $output = "$directory/$level.lowir";
	my @command = ($app, "-$level");
	push(@command, '--stats') if $stats;
	push(@command, '-o', $output, $test);
	my $status = run_command_capture(
		cmd => \@command,
		stdout => "$directory/$level.stdout",
		stderr => "$directory/$level.stderr",
		timeout => 30,
	);
	die "$test: lowiropt -$level failed\n" .
		read_file("$directory/$level.stderr") if $status != 0;
	return ($output, read_file($output),
		read_file("$directory/$level.stderr"));
}

sub function_body
{
	my ($test, $lowir, $name) = @_;
	return $1 if $lowir =~
		/(function \@\Q$name\E\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: optimized output has no $name definition\n";
}

sub call_count
{
	my ($body, $callee) = @_;
	return scalar(() = $body =~ /call i64 \@\Q$callee\E\(/g);
}

sub require_calls
{
	my ($test, $lowir, $function, $callee, $expected, $description) = @_;
	my $body = function_body($test, $lowir, $function);
	my $actual = call_count($body, $callee);
	die "$test: $description (expected $expected calls, found $actual)\n"
		if $actual != $expected;
}

sub compile_and_run
{
	my ($driver, $test, $directory, $lowir) = @_;
	my $program = "$directory/behavior";
	my $status = run_command_capture(
		cmd => [$driver, '-O0', '-o', $program, $lowir],
		stdout => "$directory/compile.stdout",
		stderr => "$directory/compile.stderr",
		timeout => 60,
	);
	die "$test: optimized LowIR did not compile\n" .
		read_file("$directory/compile.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/run.stdout",
		stderr => "$directory/run.stderr",
		timeout => 30,
	);
	die "$test: optimized behavior failed with status $status\n"
		if $status != 0;
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_repeat_stable_query.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-repeat-stable-query\.t$/);
die "No O3 repeat-stable-query controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-repeat-stable-query-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (undef, $o1) = optimize($app, $test, $directory, 'O1', 0);
	my (undef, $o2) = optimize($app, $test, $directory, 'O2', 0);
	my ($o3_path, $o3, $stats) =
		optimize($app, $test, $directory, 'O3', 1);

	for my $control (['O1', $o1], ['O2', $o2])
	{
		my ($level, $lowir) = @{$control};
		require_calls($test, $lowir, 'reuse_identical_query',
			'stable_query', 2,
			"$level unexpectedly reused the query");
		require_calls($test, $lowir,
			'reuse_after_normal_cold_return',
			'stable_query_with_cold_noreturn', 2,
			"$level unexpectedly reused the cold-arm query");
	}

	require_calls($test, $o3, 'reuse_identical_query', 'stable_query', 1,
		'O3 did not reuse an identical normally returned query');
	require_calls($test, $o3, 'reuse_after_normal_cold_return',
		'stable_query_with_cold_noreturn', 1,
		'O3 did not ignore the synthetic return after a noreturn call');
	require_calls($test, $o3, 'keep_across_store', 'stable_query', 2,
		'O3 reused a query across a store');
	require_calls($test, $o3, 'keep_across_call', 'stable_query', 2,
		'O3 reused a query across an ordinary call');
	require_calls($test, $o3, 'keep_different_arguments', 'stable_query', 2,
		'O3 merged calls with different arguments');
	require_calls($test, $o3, 'keep_across_volatile', 'stable_query', 2,
		'O3 reused a query across a volatile access');
	require_calls($test, $o3, 'keep_volatile_query', 'volatile_query', 2,
		'O3 treated a volatile fast path as repeat-stable');

	my @records = grep { /^pa37_opt_stats(?:\s|$)/ } split(/\n/, $stats);
	die "$test: expected one pa37_opt_stats record, found " .
		scalar(@records) . "\n" if scalar(@records) != 1;
	my %minimum = (
		repeat_stable_function_visits => 1,
		repeat_stable_functions => 2,
		repeat_stable_call_sites => 12,
		repeat_stable_signatures => 5,
		repeat_stable_reuses => 2,
		repeat_stable_peak_analysis_bytes => 1,
	);
	for my $field (sort keys %minimum)
	{
		die "$test: optimizer stats record lacks $field\n"
			if $records[0] !~ /(?:^|\s)\Q$field\E=(\d+)(?:\s|$)/;
		die "$test: $field was $1, expected at least $minimum{$field}\n"
			if $1 < $minimum{$field};
	}
	die "$test: repeat-stable fixture exhausted its dataflow budget\n"
		if $records[0] !~ /(?:^|\s)repeat_stable_budget_skips=0(?:\s|$)/;
	die "$test: optimizer stats record lacks repeat_stable_ns\n"
		if $records[0] !~ /(?:^|\s)repeat_stable_ns=\d+(?:\s|$)/;

	compile_and_run($driver, $test, $directory, $o3_path);
}

print "O3 repeat-stable queries: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
