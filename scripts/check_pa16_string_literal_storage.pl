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
	die "$test: generated LowIR has no $name reducer\n";
}

sub addressed_global
{
	my ($test, $body, $name) = @_;
	return $1 if $body =~ /^\s+%\w+ = addr \@([A-Za-z0-9_]+)$/m;
	die "$test: $name does not take the address of backing storage\n";
}

sub structured_global_metadata
{
	my ($test, $lowir, $symbol) = @_;
	return defined($1) ? $1 : '' if $lowir =~
		/^global \@\Q$symbol\E\s*(\[[^\]]*\])?\s*=\s*\{/m;
	die "$test: addressed $symbol is not structured global data\n";
}

if(scalar(@ARGV) != 2)
{
	die "Usage: check_pa16_string_literal_storage.pl " .
		"<cppgm++> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root, qr/string-literal-readonly\.cpp$/);
die "No PA16 string-literal storage properties found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa16-string-storage-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
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
	my $literal = addressed_global($test,
		function_body($test, $lowir, 'literal_pointer'), 'literal_pointer');
	my $writable = addressed_global($test,
		function_body($test, $lowir, 'writable_pointer'), 'writable_pointer');
	my $literal_metadata = structured_global_metadata($test, $lowir, $literal);
	my $writable_metadata = structured_global_metadata($test, $lowir, $writable);
	die "$test: literal backing array is not storage=readonly\n"
		if $literal_metadata !~ /\bstorage=readonly\b/;
	die "$test: writable character array became storage=readonly\n"
		if $writable_metadata =~ /\bstorage=readonly\b/;
}

print "PA16 string-literal storage properties: PASS (" .
	scalar(@tests) . "/" . scalar(@tests) . ")\n";
