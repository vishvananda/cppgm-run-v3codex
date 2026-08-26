#!/usr/bin/perl

use strict;
use warnings;

use File::Basename qw(basename);
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

sub expected_symbols
{
	my ($test) = @_;
	(my $path = $test) =~ s/\.t$/.census.expect/;
	open(my $fh, '<', $path) or die "Unable to read $path: $!\n";
	my @symbols;
	while(my $line = <$fh>)
	{
		chomp($line);
		next if $line =~ /^\s*(?:#|$)/;
		die "$path: invalid symbol spelling '$line'\n"
			if $line !~ /^[A-Za-z_][A-Za-z0-9_]*$/;
		push @symbols, $line;
	}
	close($fh) or die "Unable to close $path: $!\n";
	die "$path: expected-symbol list is empty\n" if !@symbols;
	return @symbols;
}

sub expected_loop_symbols
{
	my ($test) = @_;
	(my $path = $test) =~ s/\.t$/.loop.expect/;
	return () if !-f $path;
	open(my $fh, '<', $path) or die "Unable to read $path: $!\n";
	my @symbols;
	while(my $line = <$fh>)
	{
		chomp($line);
		next if $line =~ /^\s*(?:#|$)/;
		die "$path: invalid loop symbol spelling '$line'\n"
			if $line !~ /^[A-Za-z_][A-Za-z0-9_]*$/;
		push @symbols, $line;
	}
	close($fh) or die "Unable to close $path: $!\n";
	die "$path: expected-loop-symbol list is empty\n" if !@symbols;
	return @symbols;
}

if (scalar(@ARGV) != 2)
{
	die "Usage: check_cppgm_function_census.pl <cppgm++> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root, qr/\.t$/);
die "No function-census tests found under $root\n" if !@tests;
my @required_fields = qw(mir mov_loads mov_stores mov_copies planned grants
	releases spills frame_homes grant_busy grant_busy_parameters
	grant_busy_values defined_in_plan defined_frame defined_other_register);

for my $test (@tests)
{
	my $directory = tempdir('cppgm-function-census-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $stdout = "$directory/compile.stdout";
	my $stderr = "$directory/compile.stderr";
	my $object = "$directory/test.o";
	my $status = run_command_capture(
		cmd => [$app, '-O1', '--stats-functions', '-c', '-o', $object, $test],
		stdout => $stdout,
		stderr => $stderr,
		timeout => 60,
	);
	die "$test: --stats-functions compile failed\n" . read_file($stderr)
		if $status != 0;
	my $diagnostics = read_file($stderr);
	my @lines = grep { /^function_census\s+/ }
		split(/\n/, $diagnostics);
	my %records;
	for my $line (@lines)
	{
		die "$test: malformed function census record\n"
			if $line !~ /^function_census symbol=([^\s]+)/;
		my $symbol = $1;
		die "$test: duplicate census record for $symbol\n"
			if exists($records{$symbol});
		for my $field (@required_fields)
		{
			die "$test: census record for $symbol lacks $field\n"
				if $line !~ /(?:^|\s)\Q$field\E=\d+(?:\s|$)/;
		}
		$records{$symbol} = 1;
	}
	for my $symbol (expected_symbols($test))
	{
		die "$test: no census record for $symbol\n"
			if !exists($records{$symbol});
	}
	my @loop_lines = grep { /^loop_census\s+/ } split(/\n/, $diagnostics);
	my %loop_records;
	my @loop_fields = qw(header blocks mir calls eh depth frame_operands
		frame_bindings callee_saved);
	for my $line (@loop_lines)
	{
		die "$test: malformed loop census record\n"
			if $line !~ /^loop_census symbol=([^\s]+)/;
		my $symbol = $1;
		die "$test: loop census record for $symbol lacks header_label\n"
			if $line !~ /(?:^|\s)header_label=\S+(?:\s|$)/;
		for my $field (@loop_fields)
		{
			die "$test: loop census record for $symbol lacks $field\n"
				if $line !~ /(?:^|\s)\Q$field\E=\d+(?:\s|$)/;
		}
		$loop_records{$symbol} = 1;
	}
	for my $symbol (expected_loop_symbols($test))
	{
		die "$test: no loop census record for $symbol\n"
			if !exists($loop_records{$symbol});
	}
}

print "function census: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
