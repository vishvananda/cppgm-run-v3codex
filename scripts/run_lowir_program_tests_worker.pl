#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(getcwd);
use File::Basename qw(basename);
use FindBin;
use lib $FindBin::Bin;

use CppgmBatchWorker qw(
	clear_progress_state
	close_worker
	collect_tests
	detect_jobs
	ensure_test_app_available
	get_timeout_from_env
	note_progress_state
	open_worker
	print_test_run_summary
	run_command_capture
	submit_cli_request
	write_file
	write_numeric_status
);

# PA13 behavior tests translate a LowIR program to CY86, assemble it, and run
# the result.  The translator is the assignment tool under test; the assembler
# is the PA9 tool, which supplies the execution the LowIR text alone cannot.
sub process_one_test
{
	my ($assembler, $suffix, $test, $worker_out, $worker_in) = @_;
	note_progress_state('build', $test);
	my $test_base = $test;
	$test_base =~ s/\.t$//;

	unlink(glob("$test_base.$suffix"));
	unlink(glob("$test_base.$suffix.program"));
	unlink(glob("$test_base.$suffix.program.exit_status"));
	unlink(glob("$test_base.$suffix.program.stdout"));
	unlink(glob("$test_base.$suffix.program.stderr"));
	unlink(glob("$test_base.$suffix.impl.stdout"));
	unlink(glob("$test_base.$suffix.impl.stderr"));
	unlink(glob("$test_base.$suffix.impl.exit_status"));

	my $impl_stdout = "$test_base.$suffix.impl.stdout";
	my $impl_stderr = "$test_base.$suffix.impl.stderr";
	write_file($impl_stdout, '');
	write_file($impl_stderr, '');

	my $cy86_source = "$test_base.$suffix";
	my $build_timeout = get_timeout_from_env("CPPGM_BUILD_TEST_TIMEOUT_SEC", 30);
	my $impl_status = submit_cli_request($worker_in,
	                                     $worker_out,
	                                     $impl_stdout,
	                                     $impl_stderr,
	                                     { CPPGM_BATCH_TIMEOUT_SEC => $build_timeout },
	                                     '-o', $cy86_source, $test);

	if ($impl_status == 0)
	{
		note_progress_state('assemble', $test);
		$impl_status = run_command_capture(
			cmd => [$assembler, '-o', "$test_base.$suffix.program", $cy86_source],
			stdout => $impl_stdout,
			stderr => $impl_stderr,
			timeout => $build_timeout,
		);
	}
	write_numeric_status("$test_base.$suffix.impl.exit_status", $impl_status);

	if ($impl_status == 0)
	{
		note_progress_state('run', $test);
		my $program_status = run_command_capture(
			cmd => ["$test_base.$suffix.program"],
			stdout => "$test_base.$suffix.program.stdout",
			stderr => "$test_base.$suffix.program.stderr",
			stdin => (-f "$test_base.stdin" ? "$test_base.stdin" : undef),
			timeout => get_timeout_from_env("CPPGM_PROGRAM_TEST_TIMEOUT_SEC", 10),
		);
		write_numeric_status("$test_base.$suffix.program.exit_status", $program_status);
	}
	else
	{
		unlink(glob("$test_base.$suffix.program"));
		unlink(glob("$test_base.$suffix.program.exit_status"));
		unlink(glob("$test_base.$suffix.program.stdout"));
		unlink(glob("$test_base.$suffix.program.stderr"));
	}
}

sub run_program_tests
{
	my ($app, $assembler, $suffix, $tests, $verbose) = @_;
	my ($worker_pid, $worker_out, $worker_in) = open_worker($app);
	for my $test (@{$tests})
	{
		print "Running $test...\n" if $verbose;
		process_one_test($assembler, $suffix, $test, $worker_out, $worker_in);
	}
	close_worker($worker_pid, $worker_out, $worker_in);
}

if (scalar(@ARGV) != 4)
{
	die "Usage: run_lowir_program_tests_worker.pl <app> <assembler> <suffix> <testlocation>";
}

my ($app, $assembler, $suffix, $tests_root) = @ARGV;
ensure_test_app_available($app, $suffix, $tests_root);
my @tests = collect_tests($tests_root, qr/\.t$/);
my $verbose = $ENV{VERBOSE} || $ENV{CPGM_TEST_VERBOSE};
my $keep_going = $ENV{KEEP_GOING};
my $assignment = basename(getcwd());
if (!$verbose && !$keep_going)
{
	print_test_run_summary($assignment, $tests_root, \@tests);
}
my $ntests = scalar(@tests);
my $jobs = detect_jobs();
$jobs = $ntests if $jobs > $ntests;
if ($jobs <= 1)
{
	clear_progress_state();
	run_program_tests($app, $assembler, $suffix, \@tests, $verbose);
	clear_progress_state();
	exit 0;
}

clear_progress_state();
my @shards;
for (my $i = 0; $i < $jobs; ++$i)
{
	$shards[$i] = [];
}
for (my $i = 0; $i < @tests; ++$i)
{
	push @{$shards[$i % $jobs]}, $tests[$i];
}

my @pids;
for my $shard (@shards)
{
	next if scalar(@{$shard}) == 0;
	my $pid = fork();
	die "fork failed: $!" if !defined($pid);
	if ($pid == 0)
	{
		run_program_tests($app, $assembler, $suffix, $shard, $verbose);
		exit 0;
	}
	push @pids, $pid;
}

my $failed = 0;
for my $pid (@pids)
{
	waitpid($pid, 0);
	$failed = 1 if $? != 0;
}

clear_progress_state();
exit($failed ? 1 : 0);
