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
	my $status = run_command_capture(
		cmd => [$app, "-$level", '-o', $output, $test],
		stdout => "$directory/$level.stdout",
		stderr => "$directory/$level.stderr",
		timeout => 30,
	);
	die "$test: lowiropt -$level failed\n" .
		read_file("$directory/$level.stderr") if $status != 0;
	return ($output, read_file($output));
}

sub function_body
{
	my ($test, $lowir, $name) = @_;
	return $1 if $lowir =~
		/(function \@\Q$name\E\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: optimized output has no $name definition\n";
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
	die "Usage: check_lowir_weak_specialization.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests(
	$root, qr/weak-specialization-profitability\.t$/);
die "No weak-specialization profitability controls found under $root\n"
	if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-weak-specialization-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my ($o0_path, $o0) = optimize($app, $test, $directory, 'O0');
	my ($o2_path, $o2) = optimize($app, $test, $directory, 'O2');
	my $baseline_main = function_body($test, $o0, 'main');
	die "$test: O0 lacks both control-target calls\n"
		if scalar(() = $baseline_main =~ /call i64 \@control_target\(/g) != 2;
	die "$test: O0 lacks both data-target calls\n"
		if scalar(() = $baseline_main =~ /call i64 \@data_target\(/g) != 2;

	my $optimized_main = function_body($test, $o2, 'main');
	die "$test: O2 did not specialize the uniform control-flow parameter\n"
		if $optimized_main =~ /call i64 \@control_target\(/;
	my @guard_calls = $optimized_main =~
		/^\s+%\w+ = call i64 \@data_target\(7, [34]\)$/mg;
	die "$test: O2 cloned a weak target for a data-only uniform parameter\n"
		if scalar(@guard_calls) != 2;
	my $guard = function_body($test, $o2, 'data_target');
	die "$test: O2 changed the observable weak data-target ABI\n"
		if $guard !~ /function \@data_target\(%bias : i64, %value : i64\)/;
	compile_and_run($driver, $test, $directory, $o2_path);
}

print "weak specialization profitability: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
