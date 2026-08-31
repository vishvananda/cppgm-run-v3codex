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
		next if $instructions[0] !~
			/^%([A-Za-z0-9_]+) = phi (?:i64|u8) /;
		my $phi = $1;
		++$count if $instructions[-1] =~ /^branch %\Q$phi\E, /;
	}
	return $count;
}

sub forwarded_boolean_phi_count
{
	my ($body) = @_;
	my %blocks;
	while ($body =~
		/(?:^|\n)  block \^([A-Za-z0-9_]+):\n(.*?)(?=\n  block \^|\n\}|\z)/sg)
	{
		my ($label, $text) = ($1, $2);
		my @instructions = grep { length($_) }
			map { s/^\s+//r } split(/\n/, $text);
		$blocks{$label} = \@instructions;
	}
	my $count = 0;
	for my $instructions (values %blocks)
	{
		next if scalar(@$instructions) != 3;
		next if $instructions->[0] !~
			/^%([A-Za-z0-9_]+) = phi i64 /;
		my $wide = $1;
		next if $instructions->[1] !~
			/^%([A-Za-z0-9_]+) = convert trunc u8 i64 %\Q$wide\E$/;
		my $narrow = $1;
		next if $instructions->[2] !~ /^jump \^([A-Za-z0-9_]+)$/;
		my $continuation_label = $1;
		my $continuation = $blocks{$continuation_label};
		next if !defined($continuation) || !@$continuation;
		++$count if $continuation->[-1] =~
			/^branch %\Q$narrow\E, \^[A-Za-z0-9_]+, \^[A-Za-z0-9_]+$/;
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
	my ($o2_path, $o2) = run_optimizer($app, $test, $directory, 'O2');
	my ($o3_path, $o3) = run_optimizer($app, $test, $directory, 'O3');
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

	for my $level_body (['O0', $o0], ['O1', $o1])
	{
		my ($level, $output) = @$level_body;
		my $body = function_body($test, $output,
			'fold_acyclic_boolean_diamond');
		die "$test: $level did not preserve the O2 acyclic Boolean diamond\n"
			if direct_phi_branch_count($body) != 1;
	}
	for my $level_body (['O2', $o2], ['O3', $o3])
	{
		my ($level, $output) = @$level_body;
		my $fold = function_body($test, $output,
			'fold_acyclic_boolean_diamond');
		die "$test: $level did not bypass the private acyclic Boolean diamond\n"
			if direct_phi_branch_count($fold) != 0;
		my $shared_body = function_body($test, $output,
			'retain_shared_acyclic_diamond');
		die "$test: $level incorrectly bypassed a shared acyclic Boolean value\n"
			if direct_phi_branch_count($shared_body) != 1;
		my $effectful_body = function_body($test, $output,
			'retain_effectful_acyclic_diamond');
		die "$test: $level discarded work from a Boolean-diamond arm\n"
			if $effectful_body !~ /\bcall void \@observe\(/;
	}

	for my $level_body (['O0', $o0], ['O1', $o1], ['O2', $o2])
	{
		my ($level, $output) = @$level_body;
		my $body = function_body($test, $output,
			'fold_forwarded_boolean');
		die "$test: $level did not preserve the forwarded Boolean chain\n"
			if forwarded_boolean_phi_count($body) != 1;
	}
	my $o3_forwarded = function_body($test, $o3,
		'fold_forwarded_boolean');
	die "$test: O3 did not thread the forwarded Boolean decision\n"
		if forwarded_boolean_phi_count($o3_forwarded) != 0;
	die "$test: O3 discarded incoming-edge work while threading the decision\n"
		if $o3_forwarded !~ /\bcall void \@observe\(73\)/;
	die "$test: O3 did not branch on the incoming comparison result\n"
		if $o3_forwarded !~ /branch %positive, \^hit, \^miss/;
	for my $retained ('retain_shared_forwarded_boolean',
		'retain_non_boolean_forwarded', 'retain_successor_phi',
		'retain_cyclic_forwarded_boolean', 'retain_eh_forwarded_boolean')
	{
		my $body = function_body($test, $o3, $retained);
		die "$test: O3 incorrectly threaded $retained\n"
			if forwarded_boolean_phi_count($body) != 1;
	}

	for my $behavior (['O0', $o0_path], ['O1', $o1_path],
		['O2', $o2_path], ['O3', $o3_path])
	{
		my ($level, $lowir_path) = @$behavior;
		my $executable = "$directory/behavior-$level";
		my $compile_stderr = "$directory/compile-$level.stderr";
		my $compile_status = run_command_capture(
			cmd => [$driver, "-$level", '-o', $executable, $lowir_path],
			stdout => "$directory/compile-$level.stdout",
			stderr => $compile_stderr,
			timeout => 60,
		);
		die "$test: $level optimized LowIR did not compile\n" .
			read_file($compile_stderr) if $compile_status != 0;
		my $run_status = run_command_capture(
			cmd => [$executable],
			stdout => "$directory/run-$level.stdout",
			stderr => "$directory/run-$level.stderr",
			timeout => 30,
		);
		die "$test: $level optimized LowIR behavior failed with status " .
			"$run_status\n" if $run_status != 0;
	}
}

print "Boolean-phi controls: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
