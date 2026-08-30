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
	my $stderr = "$directory/$level.stderr";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '--stats', '-o', $output, $test],
		stdout => "$directory/$level.stdout",
		stderr => $stderr,
		timeout => 30,
	);
	die "$test: lowiropt -$level failed\n" . read_file($stderr)
		if $status != 0;
	return (read_file($output), read_file($stderr), $output);
}

sub main_body
{
	my ($test, $lowir) = @_;
	return $1 if $lowir =~ /(function \@main\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: optimized output has no main definition\n";
}

sub stat_value
{
	my ($test, $stats, $name) = @_;
	return 0 + $1 if $stats =~ /(?:^|\s)\Q$name\E=(\d+)(?:\s|$)/;
	die "$test: optimizer stats omit $name\n";
}

sub compile_and_run
{
	my ($driver, $test, $directory, $level, $lowir) = @_;
	my $program = "$directory/behavior-$level";
	my $stderr = "$directory/compile-$level.stderr";
	my $status = run_command_capture(
		cmd => [$driver, "-$level", '-o', $program, $lowir],
		stdout => "$directory/compile-$level.stdout",
		stderr => $stderr,
		timeout => 60,
	);
	die "$test: $level optimized LowIR did not compile\n" .
		read_file($stderr) if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/run-$level.stdout",
		stderr => "$directory/run-$level.stderr",
		timeout => 30,
	);
	die "$test: $level optimized behavior failed with status $status\n"
		if $status != 0;
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_inlined_addressed_store.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/inlined-addressed-store.*\.t$/);
die "No inlined addressed-store controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-inlined-addressed-store-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (%outputs, %stats, %paths);
	for my $level ('O0', 'O1', 'O2', 'O3') {
		($outputs{$level}, $stats{$level}, $paths{$level}) =
			optimize($app, $test, $directory, $level);
	}

	for my $level ('O0', 'O1') {
		my $body = main_body($test, $outputs{$level});
		die "$test: $level did not retain the lower-level addressed slot\n"
			if $body !~ /^\s+slot \$value : i32$/m ||
			   $body !~ /^\s+%\w+ = addr \$value$/m;
		die "$test: $level reported addressed-slot recovery\n"
			if stat_value($test, $stats{$level},
				'addressed_scalars_promoted') != 0;
	}
	for my $level ('O2', 'O3') {
		my $body = main_body($test, $outputs{$level});
		die "$test: $level retained the recoverable addressed slot\n"
			if $body =~ /^\s+slot \$value : i32$/m ||
			   $body =~ /^\s+%\w+ = addr \$value$/m;
		die "$test: $level did not report addressed-slot recovery\n"
			if stat_value($test, $stats{$level},
				'addressed_scalars_promoted') == 0;
	}

	for my $level ('O1', 'O2', 'O3') {
		compile_and_run(
			$driver, $test, $directory, $level, $paths{$level});
	}
}

print "inlined addressed stores: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
