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

sub require_pair_calls
{
	my ($test, $lowir, $function, $expected, $level) = @_;
	my $body = function_body($test, $lowir, $function);
	my $actual = call_count($body, 'preferred_body');
	die "$test: $level $function has $actual preferred-body calls, " .
		"expected $expected\n" if $actual != $expected;
	return $body;
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
	die "Usage: check_lowir_o3_loop_priority_inline.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-loop-priority-inline\.t$/);
die "No O3 loop-priority inline controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-o3-loop-priority-inline-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (undef, $o1) = optimize($app, $test, $directory, 'O1', 0);
	my (undef, $o2) = optimize($app, $test, $directory, 'O2', 0);
	my ($o3_path, $o3, $stats) =
		optimize($app, $test, $directory, 'O3', 1);

	for my $control (['O1', $o1], ['O2', $o2])
	{
		my ($level, $lowir) = @{$control};
		for my $caller (qw(loop_first loop_second no_loop_pair two_loop_calls))
		{
			require_pair_calls($test, $lowir, $caller, 2, $level);
		}
	}

	my @loop_counts;
	my @loop_bodies;
	for my $caller (qw(loop_first loop_second))
	{
		my $body = function_body($test, $o3, $caller);
		push(@loop_bodies, $body);
		push(@loop_counts, call_count($body, 'preferred_body'));
	}
	@loop_counts = sort { $a <=> $b } @loop_counts;
	die "$test: O3 must expand exactly one of the two eligible loop sites\n"
		if "@loop_counts" ne '1 2';
	require_pair_calls($test, $o3, 'no_loop_pair', 2, 'O3');
	require_pair_calls($test, $o3, 'two_loop_calls', 2, 'O3');

	my @expanded = grep { call_count($_, 'advance') >= 256 } @loop_bodies;
	die "$test: selected loop caller does not contain the complete body\n"
		if scalar(@expanded) != 1;
	my ($failure_label, $failure_body);
	while($expanded[0] =~
		/^\s*block \^([^:]+):\n(.*?)(?=^\s*block \^|\z)/msg) {
		my ($label, $body) = ($1, $2);
		next if $body !~ /^\s+call void \@fail\(\)$/m;
		die "$test: expanded caller contains more than one cloned failure arm\n"
			if defined($failure_label);
		($failure_label, $failure_body) = ($label, $body);
	}
	die "$test: selected loop caller lost the cloned noreturn arm\n"
		if !defined($failure_label);
	die "$test: late inlining retained a normal successor after noreturn\n"
		if $failure_body =~ /^\s+(?:jump|branch|switch)\b/m;
	die "$test: removed noreturn successor remains in a merge phi\n"
		if $expanded[0] =~
			/^\s+%\w+ = phi [^\n]*\[\^\Q$failure_label\E:/m;
	my $retained = function_body($test, $o3, 'preferred_body');
	die "$test: shared callable body was not retained for non-loop calls\n"
		if call_count($retained, 'advance') < 256;

	my @records = grep { /^pa37_opt_stats(?:\s|$)/ } split(/\n/, $stats);
	die "$test: expected one pa37_opt_stats record, found " .
		scalar(@records) . "\n" if scalar(@records) != 1;
	die "$test: expected at least one loop-priority pair consideration\n"
		if stat_value($test, $records[0],
			'o3_loop_inline_pairs_considered') < 1;
	die "$test: expected exactly one selected loop-priority candidate\n"
		if stat_value($test, $records[0],
			'o3_loop_inline_candidates') != 1;
	die "$test: expected exactly one loop-priority inline\n"
		if stat_value($test, $records[0], 'o3_loop_inline_calls') != 1;
	my $cloned = stat_value($test, $records[0],
		'o3_loop_inline_cloned_instructions');
	die "$test: cloned instruction count $cloned is outside the 256--512 bound\n"
		if $cloned < 256 || $cloned > 512;
	die "$test: loop-priority analysis did not report bounded scratch\n"
		if stat_value($test, $records[0],
			'o3_loop_inline_peak_analysis_bytes') < 1;
	stat_value($test, $records[0], 'o3_loop_inline_ns');

	compile_and_run($driver, $test, $directory, $o3_path);
}

print "O3 loop-priority inlining: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
