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

sub call_count
{
	my ($lowir, $name) = @_;
	return scalar(() = $lowir =~ /call i64 \@\Q$name\E\(/g);
}

sub tagged_call_target
{
	my ($test, $main, $tag) = @_;
	return $1 if $main =~
		/^\s+%\Q$tag\E = call i64 \@([^\s(]+)\(/m;
	die "$test: optimized main has no call producing $tag\n";
}

sub parameter_count
{
	my ($body) = @_;
	return 0 if $body =~ /^function \@[^\s(]+\(\)/;
	return 1 + scalar(() = $body =~ /^function \@[^\s(]+\([^\n)]*,/);
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
	die "Usage: check_lowir_readonly_string_specialization.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/readonly-string-specialization\.t$/);
die "No readonly-string specialization controls found under $root\n"
	if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-readonly-string-specialization-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (undef, $o0) = optimize($app, $test, $directory, 'O0');
	my (undef, $o1) = optimize($app, $test, $directory, 'O1');
	my (undef, $o2) = optimize($app, $test, $directory, 'O2');
	my ($o3_path, $o3) = optimize($app, $test, $directory, 'O3');
	for my $unchanged (['O0', $o0], ['O1', $o1], ['O2', $o2])
	{
		die "$test: $unchanged->[0] changed readonly-string call groups\n"
			if call_count($unchanged->[1], 'classify') != 7;
	}

	my $main = function_body($test, $o3, 'main');
	my @tags = qw(letter_result digit_result high_result);
	my @clones = map { tagged_call_target($test, $main, $_) } @tags;
	my %distinct = map { $_ => 1 } @clones;
	die "$test: O3 did not create one clone per profitable string group\n"
		if scalar(keys(%distinct)) != scalar(@tags) || $distinct{classify};
	die "$test: O3 did not retain replaceable, writable, unterminated, " .
		"and dynamic calls on the original body\n"
		if call_count($o3, 'classify') != 4;

	my $original = function_body($test, $o3, 'classify');
	for my $clone_name (@clones)
	{
		my $clone = function_body($test, $o3, $clone_name);
		die "$test: specialized clone did not remove the string parameter\n"
			if parameter_count($clone) > 1 ||
				$clone =~ /^function \@[^\s(]+\([^\n)]*:\s*ptr\b/;
		die "$test: specialized clone retained a serialized byte load\n"
			if $clone =~ /\bload\s+(?:i8|u8)\b/;
		die "$test: specialized clone retained a decided switch\n"
			if $clone =~ /^\s+switch\b/m;
		die "$test: specialized clone retained a stale merge phi\n"
			if $clone =~ /^\s+%\S+\s+=\s+phi\b/m;
		die "$test: specialized clone did not discard enough work\n"
			if instruction_count($clone) >= instruction_count($original);
	}

	die "$test: volatile byte access was specialized as immutable data\n"
		if call_count($o3, 'volatile_probe') != 2;
	die "$test: variable byte offset was specialized as a fixed byte\n"
		if call_count($o3, 'indexed_probe') != 2;
	compile_and_run($driver, $test, $directory, $o3_path);
}

print "Readonly-string specialization: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
