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

sub run_optimizer
{
	my ($app, $test, $directory, $level) = @_;
	my $output = "$directory/$level.lowir";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '--stats', '-o', $output, $test],
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
	die "$test: optimized LowIR has no $name definition\n";
}

sub compile_and_run
{
	my ($driver, $test, $directory, $lowir) = @_;
	my $program = "$directory/program";
	my $status = run_command_capture(
		cmd => [$driver, '-O1', '-o', $program, $lowir],
		stdout => "$directory/native.stdout",
		stderr => "$directory/native.stderr",
		timeout => 60,
	);
	die "$test: optimized LowIR did not compile\n" .
		read_file("$directory/native.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/program.stdout",
		stderr => "$directory/program.stderr",
		timeout => 30,
	);
	die "$test: optimized behavior failed with status $status\n"
		if $status != 0;
}

if (scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_historical_contracts.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/historical-lowir-contracts.*\.t$/);
die "No historical LowIR contract tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-historical-contracts-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my ($o0_path, $o0, $o0_stats) = run_optimizer(
		$app, $test, $directory, 'O0');
	my ($o1_path, $o1, $o1_stats) = run_optimizer(
		$app, $test, $directory, 'O1');
	my ($o2_path, $o2, $o2_stats) = run_optimizer(
		$app, $test, $directory, 'O2');
	for my $field (qw(value_index_reuses value_index_invalidations))
	{
		die "$test: O2 optimizer stats lack a positive $field\n"
			if $o2_stats !~ /(?:^|\s)\Q$field\E=([1-9]\d*)(?:\s|$)/;
	}

	my $o0_readonly = function_body($test, $o0, 'fold_readonly');
	my $o1_readonly = function_body($test, $o1, 'fold_readonly');
	die "$test: O0 did not preserve the readonly load baseline\n"
		if $o0_readonly !~ /^\s+%\w+ = load i64 /m;
	die "$test: O1 did not fold the initialized readonly scalar\n"
		if $o1_readonly =~ /^\s+%\w+ = load i64 /m ||
		   $o1_readonly !~ /^\s+return i64 7$/m;

	my $o2_strength = function_body($test, $o2, 'strength_reduce');
	die "$test: O2 did not strength-reduce the exact loop scaling\n"
		if $o2_strength !~
		   /^\s+%\w+ = binary shl i64 %\w+, 3$/m;
	die "$test: O2 retained the induction multiply\n"
		if $o2_strength =~ /^\s+%\w+ = binary mul i64 %\w+, 8$/m;

	my $o0_scaled = function_body($test, $o0, 'factor_scaled_index');
	my $o1_scaled = function_body($test, $o1, 'factor_scaled_index');
	die "$test: O0 lost the byte-scaled index baseline\n"
		if $o0_scaled !~ /^\s+%scaled = binary mul i64 %index, 24$/m ||
		   $o0_scaled !~ /^\s+%element = index i8 .* %base, %scaled$/m;
	die "$test: O1 did not factor the multiplier into an address scale\n"
		if $o1_scaled !~ /^\s+%scaled = binary mul i64 %index, 3$/m ||
		   $o1_scaled !~
		     /^\s+%element = index obj<8x1> .* %base, %scaled$/m;
	my $multi = function_body($test, $o1, 'retain_multi_use_scale');
	die "$test: multi-use index multiplier was unsafely factored\n"
		if $multi !~ /^\s+%scaled = binary mul i64 %index, 24$/m ||
		   $multi !~ /^\s+%element = index i8 .* %base, %scaled$/m;
	my $unfactorable = function_body(
		$test, $o1, 'retain_unfactorable_scale');
	die "$test: unfactorable index multiplier changed representation\n"
		if $unfactorable !~
		   /^\s+%scaled = binary mul i64 %index, 3$/m ||
		   $unfactorable !~
		     /^\s+%element = index i8 .* %base, %scaled$/m;

	my $o0_promote = function_body(
		$test, $o0, 'promote_complete_object');
	my $o1_promote = function_body(
		$test, $o1, 'promote_complete_object');
	die "$test: O0 did not preserve the small-object baseline\n"
		if $o0_promote !~ /^\s+slot \$value : obj<8x8>$/m;
	die "$test: O1 retained an eligible complete scalar object\n"
		if $o1_promote =~ /^\s+slot \$value : obj<8x8>$/m;
	die "$test: O1 did not preserve the promoted object's value\n"
		if $o1_promote !~ /^\s+return i64 42$/m;

	my $guard = function_body($test, $o1, 'retain_object_value_copy');
	die "$test: object-valued copy operand unsafely lost its slot\n"
		if $guard !~ /^\s+slot \$value : obj<8x8>$/m;
	die "$test: object-valued copy operand was rewritten as an address\n"
		if $guard !~ /^\s+copyobj 8x8 %source, /m ||
		   $guard =~ /^\s+%\w+ = load i64 %source$/m;

	my $o0_scalar = function_body($test, $o0, 'promote_scalar_slot');
	my $o1_scalar = function_body($test, $o1, 'promote_scalar_slot');
	die "$test: O0 lost the scalar-slot promotion baseline\n"
		if $o0_scalar !~ /^\s+slot \$scalar : i64$/m ||
		   scalar(() = $o0_scalar =~ /^\s+store i64 /mg) != 2;
	die "$test: O1 retained the eligible scalar slot or its dead store\n"
		if $o1_scalar =~ /^\s+slot \$scalar : i64$/m ||
		   $o1_scalar =~ /^\s+(?:store|%\w+ = load) i64 /m;
	die "$test: promoted scalar value was not forwarded\n"
		if $o1_scalar !~ /^\s+return i64 %value$/m;

	my $escape = function_body($test, $o1, 'retain_escaped_scalar_slot');
	die "$test: escaped scalar slot was incorrectly promoted\n"
		if $escape !~ /^\s+slot \$scalar : i64$/m ||
		   $escape !~ /^\s+%\w+ = addr \$scalar$/m ||
		   $escape !~ /^\s+%\w+ = call i64 \@observe_scalar_slot\(/m;

	compile_and_run($driver, $test, $directory, $o1_path);
	compile_and_run($driver, $test, $directory, $o2_path);
}

print "historical LowIR contracts: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
