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
	die "Usage: check_lowir_optimizer_stats.pl <lowiropt> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root, qr/partial-inline-census.*\.t$/);
die "No optimizer-census tests found under $root\n" if !@tests;
my @required_fields = qw(
	value_index_builds
	value_index_reuses
	value_index_invalidations
	value_index_instruction_visits
	value_index_operand_visits
	value_index_allocations
	value_index_peak_bytes
	partial_inline_census_direct_calls
	partial_inline_census_eligible_calls
	partial_inline_census_eligible_callees
	partial_inline_census_prefix_blocks
	partial_inline_census_prefix_instructions
	partial_inline_census_bailout_edges);

for my $test (@tests)
{
	my $directory = tempdir('cppgm-lowir-stats-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $stdout = "$directory/stats.stdout";
	my $stderr = "$directory/stats.stderr";
	my $output = "$directory/stats.lowir";
	my $status = run_command_capture(
		cmd => [$app, '-O1', '--stats', '-o', $output, $test],
		stdout => $stdout,
		stderr => $stderr,
		timeout => 30,
	);
	die "$test: lowiropt --stats failed\n" . read_file($stderr)
		if $status != 0;
	my @records = grep { /^pa37_opt_stats(?:\s|$)/ }
		split(/\n/, read_file($stderr));
	die "$test: expected one pa37_opt_stats record, found " .
		scalar(@records) . "\n" if scalar(@records) != 1;
	my $record = $records[0];
	my %values;
	for my $field (@required_fields)
	{
		die "$test: optimizer stats record lacks $field\n"
			if $record !~ /(?:^|\s)\Q$field\E=(\d+)(?:\s|$)/;
		$values{$field} = $1;
	}
	for my $field (@required_fields)
	{
		next if $field eq 'partial_inline_census_direct_calls' ||
			$field eq 'value_index_reuses';
		die "$test: optimizer fixture did not contribute to $field\n"
			if $values{$field} == 0;
	}
}

print "optimizer census: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
