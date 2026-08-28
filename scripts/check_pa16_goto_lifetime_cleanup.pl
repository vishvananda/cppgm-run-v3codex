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

sub function_with_object
{
	my ($test, $lowir, $object) = @_;
	my @functions = split(/(?=^function \@)/m, $lowir);
	for my $function (@functions)
	{
		my ($header) = $function =~ /^(function \@[^\n]+)/m;
		next if !defined($header);
		return $function if $header =~ /\bobject=\Q$object\E(?:,|\])/;
	}
	die "$test: generated LowIR has no definition for object $object\n";
}

sub function_symbol_with_object
{
	my ($test, $lowir, $object) = @_;
	return $1 if $lowir =~
		/^function \@([A-Za-z0-9_]+)\([^\n]*\)[^\n]*\bobject=\Q$object\E(?:,|\])/m;
	die "$test: generated LowIR has no symbol for object $object\n";
}

sub function_blocks
{
	my ($function) = @_;
	my @blocks;
	while ($function =~
		/(^  block \^([A-Za-z0-9_]+):\n.*?)(?=^  block \^|\n\}\s*(?:\n|\z))/msg)
	{
		push @blocks, [$2, $1];
	}
	return @blocks;
}

if (scalar(@ARGV) != 2)
{
	die "Usage: check_pa16_goto_lifetime_cleanup.pl " .
		"<cppgm++> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root, qr/goto-lifetime-cleanup\.cpp$/);
die "No PA16 goto-lifetime-cleanup properties found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa16-goto-lifetime-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $output = "$directory/output.lowir";
	my $status = run_command_capture(
		cmd => [$app, '--emit-lowir', '-O0', '-o', $output, $test],
		stdout => "$directory/compiler.stdout",
		stderr => "$directory/compiler.stderr",
		timeout => 60,
	);
	die "$test: source-to-LowIR compile failed\n" .
		read_file("$directory/compiler.stderr") if $status != 0;
	my $lowir = read_file($output);
	my $constructor = function_symbol_with_object(
		$test, $lowir, '_ZN5GuardC1Ei');
	my $destructor = function_symbol_with_object(
		$test, $lowir, '_ZN5GuardD1Ev');

	my $nested = function_with_object(
		$test, $lowir, '_Z19leave_nested_scopesv');
	my $nested_cleanup = 0;
	for my $record (function_blocks($nested))
	{
		my $block = $record->[1];
		if ($block =~
			/^\s+(%[A-Za-z0-9_]+) = addr \$inner\n\s+call void \@\Q$destructor\E\(\1\).*?^\s+(%[A-Za-z0-9_]+) = addr \$outer\n\s+call void \@\Q$destructor\E\(\2\).*?^\s+jump \^/ms)
		{
			$nested_cleanup = 1;
			last;
		}
	}
	die "$test: goto did not destroy inner then outer before leaving scopes\n"
		if !$nested_cleanup;

	my $repeat = function_with_object(
		$test, $lowir, '_Z17repeat_same_scopev');
	my $repeat_target;
	for my $record (function_blocks($repeat))
	{
		my ($label, $block) = @{$record};
		if ($block =~
			/^\s+(%[A-Za-z0-9_]+) = addr \$repeated\n\s+call void \@\Q$constructor\E\(\1, 3\)/m)
		{
			$repeat_target = $label;
			last;
		}
	}
	die "$test: backward-goto target does not reconstruct the object\n"
		if !defined($repeat_target);
	my $repeat_cleanup = 0;
	for my $record (function_blocks($repeat))
	{
		my $block = $record->[1];
		if ($block =~
			/^\s+(%[A-Za-z0-9_]+) = addr \$repeated\n\s+call void \@\Q$destructor\E\(\1\)\n\s+jump \^\Q$repeat_target\E\s*$/m)
		{
			$repeat_cleanup = 1;
			last;
		}
	}
	die "$test: backward goto did not destroy the active object before jumping\n"
		if !$repeat_cleanup;
}

print "PA16 goto lifetime properties: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
