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
	my ($app, $test, $directory, $level) = @_;
	my $output = "$directory/$level.lowir";
	my $stderr = "$directory/$level.stderr";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '--stats', '-o', $output, $test],
		stdout => "$directory/$level.stdout",
		stderr => $stderr,
		timeout => 30,
	);
	die "$test: lowiropt -$level failed\n" . read_file($stderr)
		if $status != 0;
	return (read_file($output), read_file($stderr), $output);
}

sub function_body
{
	my ($test, $lowir, $name) = @_;
	return $1 if $lowir =~
		/(function \@\Q$name\E\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: optimized output has no $name definition\n";
}

sub instruction_count
{
	my ($body, $instruction) = @_;
	return scalar(() = $body =~ /^\s+\Q$instruction\E\b/mg);
}

sub phi_count
{
	my ($body) = @_;
	return scalar(() = $body =~ /^\s+%\w+ = phi\b/mg);
}

sub stats_record
{
	my ($test, $stderr, $level) = @_;
	my @records = grep { /^pa37_opt_stats(?:\s|$)/ } split(/\n/, $stderr);
	die "$test: $level expected one optimizer stats record, found " .
		scalar(@records) . "\n" if scalar(@records) != 1;
	return $records[0];
}

sub stat_value
{
	my ($test, $record, $field) = @_;
	die "$test: optimizer stats record lacks $field\n"
		if $record !~ /(?:^|\s)\Q$field\E=(\d+)(?:\s|$)/;
	return $1;
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
	die "Usage: check_lowir_terminal_phi_returns.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/terminal-phi-return\.t$/);
die "No terminal-phi return controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-terminal-phi-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my ($o0, $o0_stats) = optimize($app, $test, $directory, 'O0');
	my ($o1, $o1_stats) = optimize($app, $test, $directory, 'O1');
	my ($o2, $o2_stats, $o2_path) =
		optimize($app, $test, $directory, 'O2');
	my ($o3, $o3_stats, $o3_path) =
		optimize($app, $test, $directory, 'O3');

	for my $positive (qw(thread_terminal_chain thread_phi_to_return_branch)) {
		for my $control (['O0', $o0], ['O1', $o1]) {
			my ($level, $lowir) = @{$control};
			my $body = function_body($test, $lowir, $positive);
			die "$test: $level changed the O2 terminal phi in $positive\n"
				if phi_count($body) != 1;
		}
		for my $optimized (['O2', $o2], ['O3', $o3]) {
			my ($level, $lowir) = @{$optimized};
			my $body = function_body($test, $lowir, $positive);
			die "$test: $level retained the eligible phi in $positive\n"
				if phi_count($body) != 0;
			die "$test: $level did not distribute $positive onto return paths\n"
				if instruction_count($body, 'return') < 2;
		}
	}

	for my $guard (qw(retain_shared_terminal_phi retain_long_terminal_chain)) {
		for my $optimized (['O2', $o2], ['O3', $o3]) {
			my ($level, $lowir) = @{$optimized};
			my $body = function_body($test, $lowir, $guard);
			die "$test: $level crossed the bounded guard in $guard\n"
				if phi_count($body) != 1;
		}
	}

	for my $control (['O0', $o0_stats], ['O1', $o1_stats]) {
		my ($level, $stderr) = @{$control};
		my $record = stats_record($test, $stderr, $level);
		die "$test: $level ran O2 terminal-phi threading\n"
			if stat_value($test, $record, 'o3_terminal_phi_runs') != 0 ||
			   stat_value($test, $record, 'o3_terminal_phi_merges') != 0;
	}
	for my $optimized (['O2', $o2_stats], ['O3', $o3_stats]) {
		my ($level, $stderr) = @{$optimized};
		my $record = stats_record($test, $stderr, $level);
		die "$test: $level did not report both eligible terminal-phi moves\n"
			if stat_value($test, $record, 'o3_terminal_phi_merges') < 2;
		die "$test: $level did not report the four incoming edges\n"
			if stat_value($test, $record,
				'o3_terminal_phi_incoming_edges') < 4;
		my $cloned = stat_value(
			$test, $record, 'o3_terminal_phi_cloned_instructions');
		die "$test: $level terminal scalar cloning $cloned is outside its fixture bound\n"
			if $cloned < 6 || $cloned > 24;
		die "$test: $level small fixture reached the per-function cap\n"
			if stat_value($test, $record,
				'o3_terminal_phi_round_cap_hits') != 0;
		stat_value($test, $record, 'o3_terminal_phi_ns');
	}

	compile_and_run($driver, $test, $directory, $o2_path);
	compile_and_run($driver, $test, $directory, $o3_path);
}

print "terminal-phi returns: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
