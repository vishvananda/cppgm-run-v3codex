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
	my ($app, $input, $directory, $label, $level) = @_;
	my $output = "$directory/$label.lowir";
	my $stderr = "$directory/$label.stderr";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '--stats', '-o', $output, $input],
		stdout => "$directory/$label.stdout",
		stderr => $stderr,
		timeout => 30,
	);
	die "$input: lowiropt -$level failed\n" . read_file($stderr)
		if $status != 0;
	return ($output, read_file($output), read_file($stderr));
}

sub function_body
{
	my ($test, $output, $name) = @_;
	return $1 if $output =~
		/(function \@\Q$name\E\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: optimized output has no $name definition\n";
}

sub stat_value
{
	my ($test, $stats, $name) = @_;
	return 0 + $1 if $stats =~ /(?:^|\s)\Q$name\E=(\d+)(?:\s|$)/;
	die "$test: optimizer stats omit $name\n";
}

sub check_positive_shapes
{
	my ($test, $output, $label, $expect_recovered) = @_;
	for my $name ('recover_direct', 'recover_pointer_chain',
		'recover_addressed_store', 'recover_complete_zero') {
		my $body = function_body($test, $output, $name);
		my $has_value_slot = $body =~ /^\s+slot \$value : i64$/m;
		my $has_value_address = $body =~ /^\s+%\w+ = addr \$value$/m;
		if($expect_recovered) {
			die "$test: $label retained the recoverable slot in $name\n"
				if $has_value_slot || $has_value_address;
		} else {
			die "$test: $label recovered the O3-only slot in $name\n"
				if !$has_value_slot || !$has_value_address;
		}
	}
	my $copy = function_body($test, $output, 'recover_complete_copy');
	my $has_copy_slots = $copy =~ /^\s+slot \$(?:source|destination) : i64$/m;
	my $has_bulk_copy = $copy =~ /^\s+copyobj 8x8 /m;
	if($expect_recovered) {
		die "$test: $label retained the complete local scalar copy\n"
			if $has_copy_slots || $has_bulk_copy;
	} else {
		die "$test: $label changed the O3-only scalar-copy baseline\n"
			if !$has_copy_slots || !$has_bulk_copy;
	}
}

sub check_guard_shapes
{
	my ($test, $output, $label) = @_;
	for my $name ('keep_escaped_address', 'keep_nonzero_index',
		'keep_variable_index', 'keep_volatile_access', 'keep_partial_type') {
		my $body = function_body($test, $output, $name);
		die "$test: $label removed the guarded slot in $name\n"
			if $body !~ /^\s+slot \$value : i64$/m ||
			   $body !~ /^\s+%\w+ = addr \$value$/m;
	}
	my $escaped = function_body($test, $output, 'keep_escaped_address');
	die "$test: $label removed the escaping call\n"
		if $escaped !~ /^\s+call void \@mutate\(%\w+\)$/m;
	my $nonzero = function_body($test, $output, 'keep_nonzero_index');
	die "$test: $label removed the nonzero address step\n"
		if $nonzero !~ /^\s+%\w+ = index i8 %\w+, 1$/m;
	my $variable = function_body($test, $output, 'keep_variable_index');
	die "$test: $label removed the variable address step\n"
		if $variable !~ /^\s+%\w+ = index i8 %\w+, %offset$/m;
	my $volatile = function_body($test, $output, 'keep_volatile_access');
	die "$test: $label removed or weakened volatile traffic\n"
		if $volatile !~ /^\s+store volatile i64 /m ||
		   $volatile !~ /^\s+%\w+ = load volatile i64 /m;
	my $partial = function_body($test, $output, 'keep_partial_type');
	die "$test: $label removed the partial typed access\n"
		if $partial !~ /^\s+%\w+ = load u32 /m;
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_addressed_scalar_slots.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-addressed-scalar-slot.*\.t$/);
die "No addressed-scalar-slot tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('cppgm-addressed-scalar-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (%paths, %outputs, %stats);
	for my $level ('O0', 'O1', 'O2', 'O3') {
		($paths{$level}, $outputs{$level}, $stats{$level}) =
			run_optimizer($app, $test, $directory, $level, $level);
	}

	for my $level ('O0', 'O1', 'O2') {
		check_positive_shapes($test, $outputs{$level}, $level, 0);
		die "$test: $level reported the O3-only recovery\n"
			if stat_value($test, $stats{$level},
				'addressed_scalars_promoted') != 0;
	}
	check_positive_shapes($test, $outputs{O3}, 'O3', 1);
	check_guard_shapes($test, $outputs{O3}, 'O3');

	my $candidates = stat_value(
		$test, $stats{O3}, 'addressed_scalar_candidates');
	my $promoted = stat_value(
		$test, $stats{O3}, 'addressed_scalars_promoted');
	my $memory = stat_value(
		$test, $stats{O3}, 'addressed_scalar_memory_rewrites');
	my $copies = stat_value(
		$test, $stats{O3}, 'addressed_scalar_copies_rewritten');
	die "$test: addressed-scalar stats are empty or unbounded\n"
		if !$promoted || !$memory || !$copies ||
		   $promoted > $candidates || $candidates > 64 ||
		   $memory > $candidates * 8 || $copies > $candidates * 2;

	my ($replay_path, $replay, $replay_stats) = run_optimizer(
		$app, $paths{O3}, $directory, 'O3-replay', 'O3');
	check_positive_shapes($test, $replay, 'serialized O3 replay', 1);
	check_guard_shapes($test, $replay, 'serialized O3 replay');

	for my $level ('O2', 'O3') {
		my $executable = "$directory/behavior-$level";
		my $compile_stderr = "$directory/compile-$level.stderr";
		my $compile_status = run_command_capture(
			cmd => [$driver, "-$level", '-o', $executable, $paths{$level}],
			stdout => "$directory/compile-$level.stdout",
			stderr => $compile_stderr,
			timeout => 60,
		);
		die "$test: $level optimized LowIR did not compile\n" .
			read_file($compile_stderr) if $compile_status != 0;
		my $run_status = run_command_capture(
			cmd => [$executable],
			stdout => "$directory/run-$level.stdout",
			stderr => "$directory/run-$level.stderr",
			timeout => 30,
		);
		die "$test: $level optimized behavior failed with status " .
			"$run_status\n" if $run_status != 0;
	}
}

print "addressed scalar slots: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
