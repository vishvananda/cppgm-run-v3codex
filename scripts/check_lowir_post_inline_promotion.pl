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

sub compile_and_run
{
	my ($driver, $test, $directory, $level, $lowir) = @_;
	my $executable = "$directory/$level-behavior";
	my $compile_stderr = "$directory/$level-compile.stderr";
	my $compile_status = run_command_capture(
		cmd => [$driver, "-$level", '-o', $executable, $lowir],
		stdout => "$directory/$level-compile.stdout",
		stderr => $compile_stderr,
		timeout => 60,
	);
	die "$test: $level LowIR did not compile\n" .
		read_file($compile_stderr) if $compile_status != 0;
	my $run_status = run_command_capture(
		cmd => [$executable],
		stdout => "$directory/$level-run.stdout",
		stderr => "$directory/$level-run.stderr",
		timeout => 30,
	);
	die "$test: $level behavior failed with status $run_status\n"
		if $run_status != 0;
}

if (scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_post_inline_promotion.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests(
	$root, qr/post-prune-inline-slot-promotion.*\.t$/);
die "No post-prune inline-promotion tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('cppgm-post-inline-promotion-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my ($o0_path, $o0) = run_optimizer($app, $test, $directory, 'O0');
	my ($o1_path, $o1) = run_optimizer($app, $test, $directory, 'O1');
	my $o0_walk = function_body($test, $o0, 'walk');
	my $o1_walk = function_body($test, $o1, 'walk');

	die "$test: O0 did not preserve the addressed scalar slot\n"
		if $o0_walk !~ /^  slot \$result : ptr$/m ||
			$o0_walk !~ /call void \@advance_slot\(/;
	die "$test: O1 left the post-inline scalar slot allocated\n"
		if $o1_walk =~ /^  slot \$result : ptr$/m ||
			$o1_walk =~ /(?:load ptr|store ptr [^\n,]+,) \$result\b/;
	die "$test: O1 did not inline the now-single-use slot helper\n"
		if $o1_walk =~ /call void \@advance_slot\b/;
	die "$test: O1 did not construct a pointer phi for the loop value\n"
		if $o1_walk !~ /\%[A-Za-z0-9_]+ = phi ptr /;

	compile_and_run($driver, $test, $directory, 'O1', $o1_path);
}

print "post-prune inline slot promotion: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
