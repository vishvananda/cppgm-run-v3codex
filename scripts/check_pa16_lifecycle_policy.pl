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

sub function_body
{
	my ($test, $lowir, $name) = @_;
	return $1 if $lowir =~
		/(function \@\Q$name\E\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: generated LowIR has no $name definition\n";
}

if (scalar(@ARGV) != 2)
{
	die "Usage: check_pa16_lifecycle_policy.pl " .
		"<cppgm++> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root, qr/lifecycle-inline-policy\.cpp$/);
die "No PA16 lifecycle-policy properties found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa16-lifecycle-policy-XXXXXX', TMPDIR => 1,
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
	die "$test: removed trivial_lifecycle classification escaped into LowIR\n"
		if $lowir =~ /\btrivial_lifecycle\s*=/;
	die "$test: semantically trivial Mandatory constructor was retained\n"
		if $lowir =~ /\bobject=_ZN9MandatoryC[12]Ev\b/;

	my ($header, $guarded) = $lowir =~
		/^(function \@([A-Za-z0-9_]+)\([^\n]*\)[^\n]*\bobject=_ZN7GuardedC1Ev\b[^\n]*\{)$/m;
	die "$test: nontrivial Guarded constructor definition is missing\n"
		if !defined($header) || !defined($guarded);
	die "$test: Guarded constructor lost no_inline policy\n"
		if $header !~ /\bno_inline=yes\b/;
	die "$test: Guarded constructor incorrectly became mandatory inline\n"
		if $header =~ /\bforce_inline=yes\b/;
	my $main = function_body($test, $lowir, 'main');
	die "$test: no_inline Guarded constructor call was removed\n"
		if $main !~ /^\s+call void \@\Q$guarded\E\(/m;
}

print "PA16 lifecycle policy properties: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
