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

sub scalar_call_targets
{
	my ($body) = @_;
	return $body =~ /^\s+%\w+ = call i64 \@(\w+)\(/mg;
}

sub require_call_count
{
	my ($test, $lowir, $name, $expected, $description) = @_;
	my $body = function_body($test, $lowir, $name);
	my @targets = scalar_call_targets($body);
	die "$test: $description (expected $expected calls, found " .
		scalar(@targets) . ")\n" if scalar(@targets) != $expected;
	return @targets;
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
	die "Usage: check_lowir_stable_prefix_query.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-stable-prefix-query\.t$/);
die "No O3 stable-prefix-query controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-stable-prefix-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my (undef, $o0) = optimize($app, $test, $directory, 'O0', 0);
	my (undef, $o1) = optimize($app, $test, $directory, 'O1', 0);
	my (undef, $o2) = optimize($app, $test, $directory, 'O2', 0);
	my ($o3_path, $o3, $stats) =
		optimize($app, $test, $directory, 'O3', 1);

	for my $control (['O0', $o0], ['O1', $o1], ['O2', $o2]) {
		my ($level, $lowir) = @$control;
		my $query = function_body($test, $lowir, 'prefix_query');
		die "$test: $level lost query=stable_prefix\n"
			if $query !~ /^function [^\n]*\bquery=stable_prefix\b/m;
		for my $ordinal (0 .. 7) {
			require_call_count($test, $lowir, "clone_case$ordinal", 3,
				"$level consumed the O3-only prefix promise");
		}
	}

	my $query = function_body($test, $o3, 'prefix_query');
	die "$test: O3 generic query lost its valid boundary metadata\n"
		if $query !~ /^function [^\n]*\bquery=stable_prefix\b/m;
	for my $ordinal (0 .. 7) {
		my @targets = require_call_count($test, $o3, "clone_case$ordinal", 2,
			'O3 did not preserve the lower result across a higher query');
		my %targets = map { $_ => 1 } @targets;
		die "$test: O3 did not compose the query family across specialization\n"
			if scalar(keys %targets) < 2;
	}
	require_call_count($test, $o3, 'equal_index', 1,
		'O3 did not reuse an equal-index query');
	require_call_count($test, $o3, 'descending_index', 3,
		'O3 reused a higher result across a lower query');
	require_call_count($test, $o3, 'different_receiver', 3,
		'O3 merged different query receivers');
	require_call_count($test, $o3, 'unknown_index', 3,
		'O3 trusted an unknown prefix index');
	require_call_count($test, $o3, 'negative_index', 3,
		'O3 trusted a negative prefix index');
	require_call_count($test, $o3, 'store_barrier', 2,
		'O3 reused a query across a store');
	require_call_count($test, $o3, 'call_barrier', 2,
		'O3 reused a query across an ordinary call');

	my @records = grep { /^pa37_opt_stats(?:\s|$)/ } split(/\n/, $stats);
	die "$test: expected one pa37_opt_stats record, found " .
		scalar(@records) . "\n" if scalar(@records) != 1;
	die "$test: optimizer stats lack stable-prefix reuses\n"
		if $records[0] !~ /(?:^|\s)repeat_stable_reuses=(\d+)(?:\s|$)/ ||
		   $1 < 9;
	die "$test: stable-prefix fixture exhausted its dataflow budget\n"
		if $records[0] !~ /(?:^|\s)repeat_stable_budget_skips=0(?:\s|$)/;

	compile_and_run($driver, $test, $directory, $o3_path);
}

print "O3 stable-prefix queries: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
