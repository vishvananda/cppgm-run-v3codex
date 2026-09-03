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

sub extents
{
	my ($text) = @_;
	return sort { $a <=> $b } ($text =~ /\bobject_bytes=([0-9]+)\b/g);
}

if(scalar(@ARGV) != 4)
{
	die "Usage: check_pa13_object_extent.pl " .
		"<lowiropt> <lowir2cy86> <cy86> <test-or-directory>\n";
}

my ($optimizer, $adapter, $cy86, $root) = @ARGV;
my @tests = collect_tests($root,
	qr/retained-parameter-object-extent\.lowir$/);
die "No PA13 object-extent controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa13-object-extent-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my $optimized = "$directory/optimized.lowir";
	my $status = run_command_capture(
		cmd => [$optimizer, '-O0', '-o', $optimized, $test],
		stdout => "$directory/optimizer.stdout",
		stderr => "$directory/optimizer.stderr",
		timeout => 30,
	);
	die "$test: valid object extent was rejected\n" .
		read_file("$directory/optimizer.stderr") if $status != 0;
	my @before = extents(read_file($test));
	my @after = extents(read_file($optimized));
	die "$test: O0 LowIR transport changed parameter object extents\n"
		if join(',', @before) ne join(',', @after) || !@after;
	my $text = read_file($optimized);
	die "$test: object extent was detached from pointer metadata\n"
		if $text =~ /\b(?:i1|i8|u8|i16|u16|i32|u32|i64|f32|f64|f80)\s+\[[^\]]*object_bytes=/;
	die "$test: combined by-address/object-extent metadata was not preserved\n"
		if $text !~ /\bptr\s+\[[^\]]*pass=by_address[^\]]*object_bytes=[1-9][0-9]*[^\]]*\]/;

	my $cy86_source = "$directory/program.cy86";
	$status = run_command_capture(
		cmd => [$adapter, '-o', $cy86_source, $optimized],
		stdout => "$directory/adapter.stdout",
		stderr => "$directory/adapter.stderr",
		timeout => 30,
	);
	die "$test: lowir2cy86 rejected transported object metadata\n" .
		read_file("$directory/adapter.stderr") if $status != 0;
	my $program = "$directory/program";
	$status = run_command_capture(
		cmd => [$cy86, '-o', $program, $cy86_source],
		stdout => "$directory/cy86.stdout",
		stderr => "$directory/cy86.stderr",
		timeout => 30,
	);
	die "$test: transported LowIR did not lower to an executable\n" .
		read_file("$directory/cy86.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/program.stdout",
		stderr => "$directory/program.stderr",
		timeout => 30,
	);
	die "$test: object-extent control behavior failed with status $status\n"
		if $status != 0;
}

print "PA13 parameter object extents: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
