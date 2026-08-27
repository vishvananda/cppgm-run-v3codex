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

if (scalar(@ARGV) != 2)
{
	die "Usage: check_pa13_global_section.pl " .
		"<lowiropt> <test-or-directory>\n";
}

my ($lowiropt, $root) = @ARGV;
my @tests = collect_tests($root,
	qr/(?:retained-global-section|global-section-rejects-[^.]+)\.lowir$/);
die "No PA13 global-section controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa13-global-section-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	if ($test =~ /rejects-/)
	{
		my $status = run_command_capture(
			cmd => [$lowiropt, '-O0', '-o', "$directory/rejected.lowir", $test],
			stdout => "$directory/rejected.stdout",
			stderr => "$directory/rejected.stderr",
			timeout => 30,
		);
		die "$test: unsupported section metadata was accepted\n"
			if $status == 0;
		next;
	}

	for my $level (qw(0 1 2 3))
	{
		my $roundtrip = "$directory/o$level.lowir";
		my $status = run_command_capture(
			cmd => [$lowiropt, "-O$level", '-o', $roundtrip, $test],
			stdout => "$directory/o$level.stdout",
			stderr => "$directory/o$level.stderr",
			timeout => 30,
		);
		die "$test: -O$level roundtrip failed\n" .
			read_file("$directory/o$level.stderr") if $status != 0;
		my $text = read_file($roundtrip);
		my ($metadata) = $text =~
			/^global \@payload\b[^\n]*\[([^\]]+)\][^\n]*$/m;
		die "$test: -O$level lost the payload global or its metadata\n"
			if !defined($metadata);
		die "$test: -O$level lost section=.cppgm_data\n"
			if $metadata !~ /(?:^|,\s*)section=\.cppgm_data(?:,|$)/;
	}
}

print "PA13 global-section properties: PASS (" . scalar(@tests) .
	"/" . scalar(@tests) . ")\n";
