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
	die "$test: optimized LowIR has no $name definition\n";
}

sub cell_load_count
{
	my ($body) = @_;
	my @loads = $body =~
		/^\s+%[A-Za-z0-9_]+ = load i64 \@cell$/mg;
	return scalar(@loads);
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
	die "Usage: check_lowir_late_load_reuse.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests(
	$root, qr/(?:late-inline-hint-load-reuse|post-inline-memory-gvn).*\.t$/);
die "No late load-reuse tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('cppgm-late-load-reuse-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	if ($test =~ /post-inline-memory-gvn/) {
		my ($o0_path, $o0) = run_optimizer(
			$app, $test, $directory, 'O0');
		my ($o2_path, $o2) = run_optimizer(
			$app, $test, $directory, 'O2');
		my $baseline = function_body(
			$test, $o0, 'shared_load_heavy');
		die "$test: O0 lost the oversized repeated-load baseline\n"
			if cell_load_count($baseline) != 19;
		my $retained = function_body(
			$test, $o2, 'shared_load_heavy');
		my @shared_calls = $o2 =~
			/^\s+%[A-Za-z0-9_]+ = call i64 \@shared_load_heavy\([01], %[A-Za-z0-9_]+\)$/mg;
		die "$test: O2 duplicated an initially oversized shared callee\n"
			if scalar(@shared_calls) != 2;
		die "$test: post-inline O2 load reuse did not optimize the retained body\n"
			if cell_load_count($retained) != 1;
		my $guard = function_body($test, $o2, 'store_barrier_guard');
		die "$test: post-inline O2 load reuse crossed a writing store\n"
			if cell_load_count($guard) != 2 ||
			   $guard !~ /^\s+store i64 7, \@cell$/m;
		compile_and_run($driver, $test, $directory, $o2_path);
		next;
	}
	my ($o0_path, $o0) = run_optimizer(
		$app, $test, $directory, 'O0');
	my ($o1_path, $o1) = run_optimizer(
		$app, $test, $directory, 'O1');
	my $o0_hot = function_body($test, $o0, 'reuse_hot');
	my $o1_hot = function_body($test, $o1, 'reuse_hot');
	die "$test: O0 did not preserve the two-load baseline\n"
		if cell_load_count($o0_hot) != 2;
	die "$test: O1 did not reuse the dominating hot-definition load\n"
		if cell_load_count($o1_hot) != 1;

	my $o0_bounded = function_body(
		$test, $o0, 'reuse_lifetime_bounded');
	my $bounded = function_body(
		$test, $o1, 'reuse_lifetime_bounded');
	die "$test: O0 did not preserve the ordinary two-load baseline\n"
		if cell_load_count($o0_bounded) != 2;
	die "$test: O1 did not reuse a lifetime-bounded ordinary load\n"
		if cell_load_count($bounded) != 1;

	my $extended = function_body(
		$test, $o1, 'retain_lifetime_extension');
	die "$test: O1 lengthened an ordinary load value's lifetime\n"
		if cell_load_count($extended) != 2;

	my $store = function_body($test, $o1, 'retain_store_barrier');
	die "$test: O1 reused a load across a writing store\n"
		if cell_load_count($store) != 2 ||
		   $store !~ /^\s+store i64 9, \@cell$/m;

	my $writing_call = function_body(
		$test, $o1, 'retain_writing_call_barrier');
	die "$test: O1 reused a load across an ordinary writing call\n"
		if cell_load_count($writing_call) != 2 ||
		   $writing_call !~ /^\s+call void \@mutate\(\)$/m;

	my $readonly = function_body(
		$test, $o1, 'reuse_across_readonly_call');
	die "$test: O1 lost a load fact across a readonly nonthrowing call\n"
		if cell_load_count($readonly) != 1 ||
		   $readonly !~
		   /^\s+%[A-Za-z0-9_]+ = call i64 \@inspect\(\)$/m;

	compile_and_run($driver, $test, $directory, $o1_path);
}

print "late memory/load reuse: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
