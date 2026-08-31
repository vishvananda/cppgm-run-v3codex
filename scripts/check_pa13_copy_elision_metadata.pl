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

if (scalar(@ARGV) != 4)
{
	die "Usage: check_pa13_copy_elision_metadata.pl " .
		"<lowiropt> <lowir2cy86> <cy86> <test-or-directory>\n";
}

my ($lowiropt, $lowir2cy86, $cy86, $root) = @ARGV;
my @tests = collect_tests($root, qr/(?:retained|rejected)-copy-elision-.*\.lowir$/);
die "No PA13 copy-elision metadata controls found under $root\n" if !@tests;

my $accepted = 0;
my $rejected = 0;
for my $test (@tests)
{
	my $directory = tempdir('pa13-copy-elision-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $roundtrip = "$directory/roundtrip.lowir";
	my $status = run_command_capture(
		cmd => [$lowiropt, '-O0', '-o', $roundtrip, $test],
		stdout => "$directory/lowiropt.stdout",
		stderr => "$directory/lowiropt.stderr",
		timeout => 30,
	);
	if ($test =~ /rejected-copy-elision-/)
	{
		die "$test: invalid copy-elision metadata was accepted\n"
			if $status == 0;
		++$rejected;
		next;
	}
	die "$test: valid copy-elision metadata was rejected\n" .
		read_file("$directory/lowiropt.stderr") if $status != 0;
	my $text = read_file($roundtrip);
	my ($destination, $source) = $text =~
		/^\s+call void \@\w+\((%\w+), (%\w+)\) \[elision=copy\]$/m;
	die "$test: O0 roundtrip lost the copy-elision permission\n"
		if !defined($source) || $destination eq $source;
	my $marker = index($text, "[elision=copy]");
	die "$test: O0 changed the permitted call's ordinary lifetime behavior\n"
		if $text !~ /^\s+call void \@\w+\(\Q$source\E\)$/m ||
		   index($text, "($source)", $marker) < $marker;

	my $cy86_source = "$directory/test.cy86";
	my $program = "$directory/test.program";
	$status = run_command_capture(
		cmd => [$lowir2cy86, '-o', $cy86_source, $roundtrip],
		stdout => "$directory/lowir2cy86.stdout",
		stderr => "$directory/lowir2cy86.stderr",
		timeout => 30,
	);
	die "$test: accepted LowIR did not translate to CY86\n" .
		read_file("$directory/lowir2cy86.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$cy86, '-o', $program, $cy86_source],
		stdout => "$directory/cy86.stdout",
		stderr => "$directory/cy86.stderr",
		timeout => 30,
	);
	die "$test: generated CY86 did not compile\n" .
		read_file("$directory/cy86.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/program.stdout",
		stderr => "$directory/program.stderr",
		timeout => 10,
	);
	die "$test: O0 copy-elision permission changed behavior (status $status)\n"
		if $status != 0;
	++$accepted;
}

die "PA13 copy-elision controls lack an accepted case\n" if !$accepted;
die "PA13 copy-elision controls lack rejected cases\n" if !$rejected;
print "PA13 copy-elision metadata: PASS (accepted=$accepted rejected=$rejected)\n";
