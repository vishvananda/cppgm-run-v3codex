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
	die "Usage: check_pa16_unreachable_terminator.pl " .
		"<cppgm++> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root, qr/unreachable-terminator\.cpp$/);
die "No PA16 unreachable-terminator properties found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa16-unreachable-XXXXXX', TMPDIR => 1,
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
	die "$test: source builtin did not lower to an unreachable terminator\n"
		if $lowir !~ /^\s+unreachable\s*$/m;
	die "$test: source builtin retained a synthetic declaration or call\n"
		if $lowir =~ /\brole=unreachable\b|^declare function \@[^\n]*unreachable|^\s+call\s+void\s+\@[^\n]*unreachable/m;
	my ($guarded) = $lowir =~
		/(function \@[A-Za-z0-9_]+\([^\n]*\)[^\n]*\bobject=_Z13guarded_valueb\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: guarded source function is missing\n" if !defined($guarded);
	die "$test: unreachable did not terminate its source block\n"
		if $guarded !~ /^\s+block \^[A-Za-z0-9_]+:\n\s+unreachable\s*(?:\n\s*)?(?=block \^|\})/m;
}

print "PA16 unreachable lowering properties: PASS (" . scalar(@tests) .
	"/" . scalar(@tests) . ")\n";
