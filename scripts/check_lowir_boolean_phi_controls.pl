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
	my $stderr = "$directory/$level.stderr";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '-o', $output, $test],
		stdout => "$directory/$level.stdout",
		stderr => $stderr,
		timeout => 30,
	);
	die "$test: lowiropt -$level failed\n" . read_file($stderr)
		if $status != 0;
	return ($output, read_file($output));
}

sub function_body
{
	my ($test, $output, $name) = @_;
	return $1 if $output =~
		/(function \@\Q$name\E\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: optimized output has no $name definition\n";
}

sub direct_phi_branch_count
{
	my ($body) = @_;
	my $count = 0;
	while ($body =~
		/(?:^|\n)  block \^[A-Za-z0-9_]+:\n(.*?)(?=\n  block \^|\n\}|\z)/sg)
	{
		my @instructions = grep { length($_) }
			map { s/^\s+//r } split(/\n/, $1);
		next if scalar(@instructions) < 2;
		next if $instructions[0] !~ /^%([A-Za-z0-9_]+) = phi i64 /;
		my $phi = $1;
		++$count if $instructions[-1] =~ /^branch %\Q$phi\E, /;
	}
	return $count;
}

if (scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_boolean_phi_controls.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/loop-local-boolean-phi-branch.*\.t$/);
die "No loop-local Boolean-phi tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('cppgm-boolean-phi-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my ($o0_path, $o0) = run_optimizer($app, $test, $directory, 'O0');
	my ($o1_path, $o1) = run_optimizer($app, $test, $directory, 'O1');
	my $o0_fold = function_body($test, $o0, 'fold_loop_local_choice');
	my $o1_fold = function_body($test, $o1, 'fold_loop_local_choice');
	die "$test: O0 did not preserve the loop-local Boolean phi branch\n"
		if direct_phi_branch_count($o0_fold) != 1;
	die "$test: O1 did not thread the private loop-local Boolean phi\n"
		if direct_phi_branch_count($o1_fold) != 0;
	for my $retained ('retain_shared_choice',
		'retain_loop_carried_choice')
	{
		my $body = function_body($test, $o1, $retained);
		die "$test: O1 incorrectly threaded $retained\n"
			if direct_phi_branch_count($body) != 1;
	}

	my $executable = "$directory/behavior";
	my $compile_stderr = "$directory/compile.stderr";
	my $compile_status = run_command_capture(
		cmd => [$driver, '-O1', '-o', $executable, $o1_path],
		stdout => "$directory/compile.stdout",
		stderr => $compile_stderr,
		timeout => 60,
	);
	die "$test: optimized LowIR did not compile\n" .
		read_file($compile_stderr) if $compile_status != 0;
	my $run_status = run_command_capture(
		cmd => [$executable],
		stdout => "$directory/run.stdout",
		stderr => "$directory/run.stderr",
		timeout => 30,
	);
	die "$test: optimized LowIR behavior failed with status $run_status\n"
		if $run_status != 0;
}

print "loop-local Boolean-phi controls: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
