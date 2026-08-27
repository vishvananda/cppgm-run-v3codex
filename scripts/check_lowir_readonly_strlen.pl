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
	die "$test: optimized output has no $name reducer\n";
}

sub compile_and_run
{
	my ($driver, $test, $directory, $tag, $lowir) = @_;
	my $program = "$directory/$tag.program";
	my $status = run_command_capture(
		cmd => [$driver, '-O0', '-o', $program, $lowir],
		stdout => "$directory/$tag.native.stdout",
		stderr => "$directory/$tag.native.stderr",
		timeout => 60,
	);
	die "$test: $tag LowIR did not compile\n" .
		read_file("$directory/$tag.native.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/$tag.program.stdout",
		stderr => "$directory/$tag.program.stderr",
		timeout => 30,
	);
	die "$test: $tag behavior failed with status $status\n"
		if $status != 0;
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_readonly_strlen.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/readonly-byte-strlen\.t$/);
die "No readonly strlen properties found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-readonly-strlen-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my ($o0_path, $o0) = optimize($app, $test, $directory, 'O0');
	my ($o1_path, $o1) = optimize($app, $test, $directory, 'O1');
	for my $case (['folds_complete_word', 3], ['folds_at_first_nul', 1]) {
		my ($name, $length) = @$case;
		my $baseline = function_body($test, $o0, $name);
		my $positive = function_body($test, $o1, $name);
		die "$test: $name lacks the O0 strlen-call baseline\n"
			if $baseline !~ /^\s+%\w+ = call i64 \@\w+\(%\w+\)$/m;
		die "$test: $name retained strlen or lost the first-NUL result\n"
			if $positive =~ /^\s+%\w+ = call i64 /m ||
			   $positive !~ /^\s+return i64 $length$/m;
	}
	for my $name ('keeps_writable_data', 'keeps_unterminated_data',
		'keeps_dynamic_pointer') {
		my $guard = function_body($test, $o1, $name);
		die "$test: $name unsafely folded a nonconstant string length\n"
			if $guard !~ /^\s+%\w+ = call i64 \@\w+\(%\w+\)$/m;
	}

	my $baseline = function_body(
		$test, $o0, 'folds_indexed_readonly_table');
	my $positive = function_body(
		$test, $o1, 'folds_indexed_readonly_table');
	die "$test: indexed table lacks the O0 strlen-call baseline\n"
		if $baseline !~ /^\s+%\w+ = call i64 \@measure_bytes\(%\w+\)$/m;
	die "$test: indexed table lacks its two local initialization stores\n"
		if scalar(() = $baseline =~ /^\s+store ptr /mg) != 2;
	die "$test: eligible indexed readonly table retained strlen\n"
		if $positive =~ /^\s+%\w+ = call i64 \@measure_bytes\(%\w+\)$/m;
	die "$test: eligible table retained its local pointer initialization\n"
		if $positive =~ /^\s+store ptr /m;
	my ($record) = $positive =~
		/^\s+(%\w+) = index obj<16x8> \[projection=array_element\] .*?, %which$/m;
	die "$test: eligible table did not select a readonly record\n"
		if !defined($record) ||
		   $positive !~ /^\s+%\w+ = load ptr \Q$record\E$/m;
	my ($length_address) = $positive =~
		/^\s+(%\w+) = index i64 \[projection=field\] \Q$record\E, 1$/m;
	die "$test: pointer and length loads did not share the selected record\n"
		if !defined($length_address) ||
		   $positive !~ /^\s+%\w+ = load i64 \Q$length_address\E$/m;
	my @readonly_record_tables = grep {
		$_ =~ /\A\s*ptr addr \@\w+\s+i64\s+\d+\s+ptr addr \@\w+\s+i64\s+\d+\s*\z/s
	} ($o1 =~ /global \@\w+ \[storage=readonly, binding=internal\] = \{(.*?)\n\}/sg);
	die "$test: eligible table did not publish two readonly " .
		"pointer-and-length records\n" if !@readonly_record_tables;

	for my $name ('keeps_partial_readonly_table',
		'keeps_writable_string_table', 'keeps_escaped_readonly_table',
		'keeps_unterminated_string_table', 'keeps_mutated_readonly_table',
		'keeps_volatile_readonly_table') {
		my $guard = function_body($test, $o1, $name);
		die "$test: $name unsafely replaced its indexed strlen\n"
			if $guard !~
			   /^\s+%\w+ = call i64 \@measure_bytes\(%\w+\)$/m;
	}

	compile_and_run($driver, $test, $directory, 'O0', $o0_path);
	compile_and_run($driver, $test, $directory, 'O1', $o1_path);
}

print "readonly byte strlen properties: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
