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

sub optimize
{
	my ($app, $input, $directory, $label, $level) = @_;
	my $output = "$directory/$label.lowir";
	my $stderr = "$directory/$label.stderr";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '--stats', '-o', $output, $input],
		stdout => "$directory/$label.stdout",
		stderr => $stderr,
		timeout => 30,
	);
	die "$input: lowiropt -$level failed\n" . read_file($stderr)
		if $status != 0;
	return ($output, read_file($output), read_file($stderr));
}

sub function_body
{
	my ($test, $lowir, $name) = @_;
	return $1 if $lowir =~
		/(function \@\Q$name\E\b.*?)(?=\nfunction \@|\z)/s;
	die "$test: optimized output has no $name definition\n";
}

sub block_body
{
	my ($test, $function, $label) = @_;
	return $1 if $function =~
		/^\s*block \^\Q$label\E:\n(.*?)(?=^\s*block \^|^\}|\z)/ms;
	die "$test: generated clone has no $label block\n";
}

sub stat_value
{
	my ($test, $stats, $name) = @_;
	return 0 + $1 if $stats =~ /(?:^|\s)\Q$name\E=(\d+)(?:\s|$)/;
	die "$test: optimizer stats omit $name\n";
}

sub direct_call_count
{
	my ($body, $name) = @_;
	return scalar(() = $body =~ /\bcall u8 \@\Q$name\E\(/g);
}

sub private_table_minimum
{
	my ($test, $lowir) = @_;
	my ($initializer) = $lowir =~
		/global \@safe_ranges\b[^\n]*= \{(.*?)\n\}/s;
	die "$test: safe table role has no structured initializer\n"
		if !defined($initializer);
	my @values = $initializer =~ /^\s*i32 (-?\d+)\s*$/mg;
	die "$test: safe table role has no integer elements\n" if !@values;
	my $minimum = $values[0];
	for my $value (@values) {
		$minimum = $value if $value < $minimum;
	}
	return $minimum;
}

sub check_o3_shape
{
	my ($test, $lowir, $expect_unlike_call) = @_;
	my $main = function_body($test, $lowir, 'main');
	if($expect_unlike_call) {
		die "$test: O3 did not retain the unlike private-table call\n"
			if direct_call_count($main, 'private_table_search') != 1;
	}
	die "$test: O3 specialized an escaped table group\n"
		if direct_call_count($main, 'escaped_table_search') != 5;
	die "$test: O3 used an unaligned table load in its bound proof\n"
		if direct_call_count($main, 'unaligned_table_search') != 5;

	my %single_argument_calls;
	while($main =~ /^\s+(?:%\S+\s*=\s*)?call u8 \@([^\s(]+)\(([^()]*)\)$/mg) {
		my ($symbol, $arguments) = ($1, $2);
		++$single_argument_calls{$symbol} if $arguments !~ /,/;
	}
	my @clones = grep { $single_argument_calls{$_} == 4 }
		keys %single_argument_calls;
	die "$test: O3 did not redirect four table calls to one clone\n"
		if scalar(@clones) != 1;

	my $clone = function_body($test, $lowir, $clones[0]);
	my ($parameters) = $clone =~
		/^function \@\Q$clones[0]\E\(([^()]*)\) -> u8\b/m;
	die "$test: table clone does not have a readable signature\n"
		if !defined($parameters);
	my ($key) = $parameters =~ /^%([^\s:]+)\s*:\s*i32$/;
	die "$test: table clone did not remove the fixed table and count\n"
		if !defined($key);
	die "$test: table clone no longer reads the selected table\n"
		if $clone !~ /^\s*%\S+ = index \S+.*\@safe_ranges(?:,|\b)/m;

	my ($entry_label, $entry) = $clone =~
		/^\s*block \^([^:]+):\n(.*?)(?=^\s*block \^|^\}|\z)/ms;
	die "$test: table clone has no entry block\n" if !defined($entry);
	my $minimum = private_table_minimum($test, $lowir);
	my ($predicate) = $entry =~
		/^\s*%(\S+) = cmp lt i32 %\Q$key\E, \Q$minimum\E\s*$/m;
	die "$test: clone entry lacks the proven table-minimum prefilter\n"
		if !defined($predicate);
	my ($below, $ordinary) = $entry =~
		/^\s*branch %\Q$predicate\E, \^([^,\s]+), \^([^\s]+)\s*$/m;
	die "$test: table-minimum predicate does not control the entry edge\n"
		if !defined($below) || !defined($ordinary) || $below eq $ordinary;
	my $below_body = block_body($test, $clone, $below);
	$below_body =~ s/^\s+|\s+$//g;
	die "$test: below-minimum edge is not a standalone false return\n"
		if $below_body ne 'return u8 0';
}

sub compile_and_run
{
	my ($driver, $test, $directory, $input) = @_;
	my $program = "$directory/behavior";
	my $status = run_command_capture(
		cmd => [$driver, '-O3', '-o', $program, $input],
		stdout => "$directory/compile.stdout",
		stderr => "$directory/compile.stderr",
		timeout => 60,
	);
	die "$test: optimized LowIR did not compile\n" .
		read_file("$directory/compile.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/run.stdout",
		stderr => "$directory/run.stderr",
		timeout => 30,
	);
	die "$test: optimized behavior failed with status $status\n"
		if $status != 0;
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_private_table_prefilter.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-private-table-prefilter\.t$/);
die "No O3 private-table prefilter controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-private-table-prefilter-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (%paths, %outputs, %stats);
	for my $level ('O0', 'O1', 'O2', 'O3') {
		($paths{$level}, $outputs{$level}, $stats{$level}) =
			optimize($app, $test, $directory, $level, $level);
	}
	for my $level ('O0', 'O1', 'O2') {
		my $main = function_body($test, $outputs{$level}, 'main');
		die "$test: $level changed the private table call group\n"
			if direct_call_count($main, 'private_table_search') != 5 ||
			   direct_call_count($main, 'escaped_table_search') != 5 ||
			   direct_call_count($main, 'unaligned_table_search') != 5;
		die "$test: $level reported an O3 table prefilter\n"
			if stat_value($test, $stats{$level},
				'ipa_table_prefilter_clones') != 0 ||
			   stat_value($test, $stats{$level},
				'ipa_table_prefilter_calls') != 0;
	}
	check_o3_shape($test, $outputs{O3}, 1);
	my $clones = stat_value(
		$test, $stats{O3}, 'ipa_table_prefilter_clones');
	my $calls = stat_value(
		$test, $stats{O3}, 'ipa_table_prefilter_calls');
	die "$test: O3 table-prefilter stats are missing or unbounded\n"
		if $clones != 1 || $calls != 4 || $clones > 24;

	my ($replay_path, $replay) = optimize(
		$app, $paths{O3}, $directory, 'O3-replay', 'O3');
	check_o3_shape($test, $replay, 0);
	compile_and_run($driver, $test, $directory, $replay_path);
}

print "O3 private-table prefilter: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
