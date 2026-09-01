#!/usr/bin/perl

use strict;
use warnings;

use File::Copy qw(copy);
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

sub function_body
{
	my ($test, $mir, $symbol) = @_;
	return $1 if $mir =~ /(^function \@\Q$symbol\E\b.*?)(?=^function \@|\z)/ms;
	die "$test: machine IR has no $symbol function\n";
}

sub run_program
{
	my ($test, $program, $label, $directory) = @_;
	my $status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/$label.run.stdout",
		stderr => "$directory/$label.run.stderr",
		timeout => 30,
	);
	die "$test: $label behavior failed with status $status\n"
		if $status != 0;
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_native_sibling_tail.pl " .
		"<lowir2native> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-sibling-tail-transfer\.t$/);
die "No O3 sibling-tail controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('native-sibling-tail-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my %mir;
	for my $level (qw(O0 O1 O2 O3)) {
		my $mir_path = "$directory/$level.mir";
		my $program = "$directory/$level.program";
		my $status = run_command_capture(
			cmd => [$app, "-$level", '--dump-machine-ir', $mir_path,
				'-o', $program, $test],
			stdout => "$directory/$level.compile.stdout",
			stderr => "$directory/$level.compile.stderr",
			timeout => 30,
		);
		die "$test: native -$level compile failed\n" .
			read_file("$directory/$level.compile.stderr") if $status != 0;
		$mir{$level} = read_file($mir_path);
		run_program($test, $program, $level, $directory);
	}

	for my $level (qw(O0 O1 O2)) {
		my $body = function_body($test, $mir{$level}, 'tail_transfer');
		die "$test: $level unexpectedly selected a sibling transfer\n"
			if $body =~ /^\s+sibling_call\s+\@record\b/m;
		die "$test: $level lost the ordinary direct call/return path\n"
			if $body !~ /^\s+call\s+\@record\b/m ||
		   $body !~ /^\s+ret\b/m;
	}

	my $tail = function_body($test, $mir{O3}, 'tail_transfer');
	my @sibling = $tail =~ /^\s+sibling_call\s+\@([^\s\[]+)/mg;
	die "$test: O3 did not select exactly one direct sibling transfer\n"
		if scalar(@sibling) != 1 || $sibling[0] ne 'record';
	my @blocks = $tail =~
		/(^\s+block \^[^\n]+\n.*?)(?=^\s+block \^|\z)/msg;
	my @slow_blocks = grep { /^\s+sibling_call\s+\@record\b/m } @blocks;
	my $slow_block = $slow_blocks[0];
	die "$test: O3 sibling transfer has no containing block\n"
		if scalar(@slow_blocks) != 1;
	die "$test: O3 retained an unreachable return after the sibling transfer\n"
		if $slow_block =~ /^\s+ret\b/m;
	die "$test: sibling transfer lost the two exact register arguments\n"
		if $slow_block !~ /\[args=[^\]]*rdi[^\]]*\]/ ||
		   $slow_block !~ /\[args=[^\]]*rsi[^\]]*\]/;

	for my $guard (qw(changed_argument local_storage)) {
		my $body = function_body($test, $mir{O3}, $guard);
		die "$test: O3 used sibling transfer without exact parameter reuse in $guard\n"
			if $body =~ /^\s+sibling_call\b/m;
		die "$test: O3 lost the guarded ordinary call in $guard\n"
			if $body !~ /^\s+call\s+\@record\b/m;
	}

	my $driver_input = "$directory/driver.lowir";
	copy($test, $driver_input) or
		die "$test: unable to prepare cppgm++ replay: $!\n";
	my $driver_program = "$directory/driver.program";
	my $status = run_command_capture(
		cmd => [$driver, '-O3', '-o', $driver_program, $driver_input],
		stdout => "$directory/driver.compile.stdout",
		stderr => "$directory/driver.compile.stderr",
		timeout => 60,
	);
	die "$test: O3 cppgm++ replay failed\n" .
		read_file("$directory/driver.compile.stderr") if $status != 0;
	run_program($test, $driver_program, 'driver', $directory);
}

print "O3 sibling-tail transfer: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
