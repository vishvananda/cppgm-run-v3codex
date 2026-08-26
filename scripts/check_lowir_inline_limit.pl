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
	my ($app, $test, $directory, $label, @options) = @_;
	my $stdout = "$directory/$label.stdout";
	my $stderr = "$directory/$label.stderr";
	my $output = "$directory/$label.lowir";
	my $status = run_command_capture(
		cmd => [$app, '-O1', @options, '-o', $output, $test],
		stdout => $stdout,
		stderr => $stderr,
		timeout => 30,
	);
	die "$test: lowiropt $label failed\n" . read_file($stderr)
		if $status != 0;
	return read_file($output);
}

sub caller_body
{
	my ($test, $output) = @_;
	return $1 if $output =~ /(function \@caller\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: optimized output has no caller definition\n";
}

if (scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_inline_limit.pl <lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/inline-limit-once-cap.*\.t$/);
my @driver_tests = collect_tests($root, qr/driver-inline-limit.*\.t$/);
die "No inline-limit tests found under $root\n"
	if !@tests && !@driver_tests;

for my $test (@tests)
{
	my $directory = tempdir('cppgm-inline-limit-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $normal = run_optimizer($app, $test, $directory, 'normal');
	my $limited = run_optimizer($app, $test, $directory, 'limited',
		'--inline-limit', 'once-cap=1');
	my $normal_caller = caller_body($test, $normal);
	my $limited_caller = caller_body($test, $limited);
	die "$test: default O1 did not inline the eligible single-use helper\n"
		if $normal_caller =~ /\bcall\s+i64\s+\@helper\b/;
	die "$test: once-cap=1 did not retain the over-limit helper call\n"
		if $limited_caller !~ /\bcall\s+i64\s+\@helper\b/;

	for my $invalid_spec ('once-cap=0', 'once-cap=-1', 'unknown=1')
	{
		my $invalid_stdout = "$directory/invalid.stdout";
		my $invalid_stderr = "$directory/invalid.stderr";
		my $invalid = run_command_capture(
			cmd => [$app, '-O1', '--inline-limit', $invalid_spec,
				'-o', "$directory/invalid.lowir", $test],
			stdout => $invalid_stdout,
			stderr => $invalid_stderr,
			timeout => 30,
		);
		die "$test: invalid inline limit '$invalid_spec' was accepted\n"
			if $invalid == 0;
	}
}

for my $test (@driver_tests)
{
	my $directory = tempdir('cppgm-driver-inline-limit-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my @limits = (
		'--inline-limit', 'caller-budget=1',
		'--inline-limit', 'once-cap=1',
		'--inline-limit', 'once-caller-budget=1',
		'--inline-limit', 'hint-late-cap=1');
	my $status = run_command_capture(
		cmd => [$driver, '--emit-lowir', '-g0', '-O1', @limits,
			'-o', "$directory/driver.lowir", $test],
		stdout => "$directory/driver.stdout",
		stderr => "$directory/driver.stderr",
		timeout => 60,
	);
	die "$test: source driver rejected documented repeatable limits\n" .
		read_file("$directory/driver.stderr") if $status != 0;
	for my $invalid_spec ('caller-budget=0', 'once-cap=-1', 'unknown=1')
	{
		my $invalid = run_command_capture(
			cmd => [$driver, '--emit-lowir', '-g0', '-O1',
				'--inline-limit', $invalid_spec,
				'-o', "$directory/invalid.lowir", $test],
			stdout => "$directory/invalid.stdout",
			stderr => "$directory/invalid.stderr",
			timeout => 60,
		);
		die "$test: source driver accepted invalid limit '$invalid_spec'\n"
			if $invalid == 0;
	}
}

print "inline-limit controls: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . " LowIR, " . scalar(@driver_tests) . "/" .
	scalar(@driver_tests) . " driver)\n";
