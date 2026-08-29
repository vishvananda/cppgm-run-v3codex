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

sub instruction_count
{
	my ($body) = @_;
	return scalar(() = $body =~ /^    \S/mg);
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
	die "Usage: check_lowir_o3_group_specialization.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-group-specialization\.t$/);
die "No O3 grouped-specialization controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-o3-group-specialization-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (undef, $o1) = optimize($app, $test, $directory, 'O1');
	my (undef, $o2) = optimize($app, $test, $directory, 'O2');
	my ($o3_path, $o3) = optimize($app, $test, $directory, 'O3');
	my $o1_main = function_body($test, $o1, 'main');
	die "$test: O1 changed a mixed constant call population\n"
		if scalar(() = $o1_main =~ /call i64 \@group_target\(/g) != 9;
	my $o2_main = function_body($test, $o2, 'main');
	die "$test: O2 changed a mixed constant call population\n"
		if scalar(() = $o2_main =~ /call i64 \@group_target\(/g) != 9;
	my $o3_main = function_body($test, $o3, 'main');
	die "$test: O3 did not retain the unlike call on the original body\n"
		if scalar(() = $o3_main =~ /call i64 \@group_target\(/g) != 1;

	my %one_argument_calls;
	while($o3_main =~ /^\s+%\w+ = call i64 \@([^\s(]+)\(([^,()]*)\)$/mg)
	{
		++$one_argument_calls{$1} if $1 ne 'edge_target';
	}
	my @clone_symbols = grep { $one_argument_calls{$_} == 8 }
		keys %one_argument_calls;
	die "$test: O3 did not redirect the repeated group to one clone\n"
		if scalar(@clone_symbols) != 1;
	my $clone = function_body($test, $o3, $clone_symbols[0]);
	die "$test: grouped clone did not remove the constant parameter\n"
		if $clone !~ /^function \@\Q$clone_symbols[0]\E\([^,()]+\)/;
	my $original = function_body($test, $o3, 'group_target');
	die "$test: grouped clone did not discard enough specialized work\n"
		if instruction_count($clone) >= instruction_count($original);

	my $o1_edge = function_body($test, $o1, 'edge_target');
	my $o2_edge = function_body($test, $o2, 'edge_target');
	my $o3_edge = function_body($test, $o3, 'edge_target');
	die "$test: O1 control lacks the redundant zero comparison\n"
		if $o1_edge !~ /cmp ne i64 %value, 0/;
	die "$test: O2 control lacks the redundant zero comparison\n"
		if $o2_edge !~ /cmp ne i64 %value, 0/;
	die "$test: O3 did not consume the unsigned-zero edge fact\n"
		if $o3_edge =~ /cmp ne i64 %value, 0/;
	compile_and_run($driver, $test, $directory, $o3_path);
}

print "O3 grouped specialization: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
