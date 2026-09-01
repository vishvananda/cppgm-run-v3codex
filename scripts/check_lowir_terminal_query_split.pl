#!/usr/bin/perl

use strict;
use warnings;

use File::Copy qw(copy);
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
	my ($app, $test, $directory, $level) = @_;
	my $output = "$directory/$level.lowir";
	my $stderr = "$directory/$level.stats";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '--stats', '-o', $output, $test],
		stdout => "$directory/$level.stdout",
		stderr => $stderr,
		timeout => 30,
	);
	die "$test: lowiropt -$level failed\n" . read_file($stderr)
		if $status != 0;
	return (read_file($output), read_file($stderr), $output);
}

sub functions
{
	my ($test, $text) = @_;
	my %result;
	while($text =~ /(function\s+\@([^\s(]+).*?\n\})
	                 (?=\nfunction\s+\@|\s*\z)/sgx) {
		die "$test: duplicate optimized function symbol $2\n"
			if exists($result{$2});
		$result{$2} = $1;
	}
	die "$test: optimized LowIR contained no functions\n" if !%result;
	return \%result;
}

sub stats_value
{
	my ($test, $stats, $name) = @_;
	return $1 if $stats =~ /(?:^| )\Q$name\E=([0-9]+)/;
	die "$test: optimizer statistics omitted $name\n";
}

sub parameter_types
{
	my ($body) = @_;
	my ($parameters) = $body =~ /^function\s+\@[^\s(]+\((.*?)\)\s*->/s;
	return () if !defined($parameters) || $parameters !~ /\S/;
	return $parameters =~ /%[A-Za-z0-9_]+\s*:\s*([^,\)]+(?:\[[^\]]+\])?)/g;
}

sub query_shape
{
	my ($body) = @_;
	return if scalar(() = $body =~ /^\s*block\s+\^/mg) != 3;
	return if $body !~ /\[projection=array_element\]/ ||
	          $body !~ /^\s+%[A-Za-z0-9_]+\s*=\s*binary and i64 /m ||
	          $body !~ /^\s+%([A-Za-z0-9_]+)\s*=\s*call i32\s+\@([^\s(]+)\([^\n]*\)\s*$/m;
	my ($call_value, $effect_target) = ($1, $2);
	my ($stored_at) = $body =~
		/^\s+store i32 %\Q$call_value\E,\s*%([A-Za-z0-9_]+)\s*$/m;
	return if !defined($stored_at);
	my $stores = scalar(() = $body =~
		/^\s+store i32 [^,]+,\s*%\Q$stored_at\E\s*$/mg);
	my ($extent) = $body =~
		/^function\s+\@[^\s(]+\([^)]*\bobject_bytes=([1-9][0-9]*)\b/s;
	return {
		call_value => $call_value,
		effect_target => $effect_target,
		extent => defined($extent) ? $extent : 0,
		stores => $stores,
	};
}

sub direct_call_count
{
	my ($records, $target) = @_;
	my $count = 0;
	for my $body (values(%$records)) {
		$count += scalar(() = $body =~
			/^\s+%[A-Za-z0-9_]+\s*=\s*call i32\s+\@\Q$target\E\(/mg);
	}
	return $count;
}

sub compile_and_run
{
	my ($driver, $test, $directory, $label, $level, $input) = @_;
	my $program = "$directory/$label.program";
	my $status = run_command_capture(
		cmd => [$driver, "-$level", '-o', $program, $input],
		stdout => "$directory/$label.compile.stdout",
		stderr => "$directory/$label.compile.stderr",
		timeout => 60,
	);
	die "$test: $label compile failed\n" .
		read_file("$directory/$label.compile.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/$label.run.stdout",
		stderr => "$directory/$label.run.stderr",
		timeout => 30,
	);
	die "$test: $label behavior failed with status $status\n"
		if $status != 0;
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_terminal_query_split.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-terminal-query-slow-suffix\.t$/);
die "No O3 terminal-query controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-terminal-query-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (%outputs, %stats, %paths, %records);
	for my $level (qw(O0 O1 O2 O3)) {
		($outputs{$level}, $stats{$level}, $paths{$level}) =
			optimize($app, $test, $directory, $level);
		$records{$level} = functions($test, $outputs{$level});
		compile_and_run($driver, $test, $directory, "behavior-$level",
			'O0', $paths{$level});
	}

	my ($eligible, $unbounded, $overwritten, $effect_target);
	for my $symbol (keys(%{$records{O0}})) {
		my $shape = query_shape($records{O0}{$symbol});
		next if !defined($shape);
		$effect_target = $shape->{effect_target}
			if !defined($effect_target);
		die "$test: query reducers use different effect targets\n"
			if $effect_target ne $shape->{effect_target};
		if($shape->{extent} && $shape->{stores} == 1) {
			die "$test: control has multiple eligible query reducers\n"
				if defined($eligible);
			$eligible = $symbol;
		} elsif(!$shape->{extent} && $shape->{stores} == 1) {
			$unbounded = $symbol;
		} elsif($shape->{extent} && $shape->{stores} > 1) {
			$overwritten = $symbol;
		}
	}
	die "$test: control does not expose one bounded, one unbounded, and " .
		"one overwritten query relationship\n"
		if !defined($eligible) || !defined($unbounded) ||
		   !defined($overwritten) || !defined($effect_target);
	my $source_calls = direct_call_count($records{O0}, $eligible);
	die "$test: eligible query needs at least eight retained direct calls\n"
		if $source_calls < 8;

	for my $level (qw(O0 O1 O2)) {
		die "$test: $level unexpectedly split a terminal query\n"
			if stats_value($test, $stats{$level},
				'o3_terminal_query_splits') != 0;
		my $body = $records{$level}{$eligible} //
			die "$test: $level lost the eligible query identity\n";
		die "$test: $level did not retain the original effect/load join\n"
			if $body !~ /^\s+%[A-Za-z0-9_]+\s*=\s*call i32\s+\@\Q$effect_target\E\(/m ||
			   $body !~ /^\s+jump\s+\^/m;
	}

	my $splits = stats_value(
		$test, $stats{O3}, 'o3_terminal_query_splits');
	my $covered_calls = stats_value(
		$test, $stats{O3}, 'o3_terminal_query_call_sites');
	my $extracted = stats_value(
		$test, $stats{O3}, 'o3_terminal_query_extracted_instructions');
	die "$test: O3 did not select exactly one bounded query suffix\n"
		if $splits != 1;
	die "$test: O3 query-suffix statistics lost the repeated-call threshold\n"
		if $covered_calls < 8 || $covered_calls > $source_calls;
	die "$test: O3 query-suffix extraction exceeded its fixture bound\n"
		if $extracted < 2 || $extracted > 64;

	my $wrapper = $records{O3}{$eligible} //
		die "$test: O3 lost the eligible query identity\n";
	my ($result, $helper, $argument) = $wrapper =~
		/^\s+%([A-Za-z0-9_]+)\s*=\s*call i32\s+\@([^\s(]+)\((%[A-Za-z0-9_]+)\)\s*\n\s+return i32\s+%\1\s*$/m;
	die "$test: O3 slow arm is not an exact scalar helper call/return\n"
		if !defined($helper) || $helper eq $effect_target;
	my $helper_body = $records{O3}{$helper} //
		die "$test: O3 slow-arm target has no serialized definition\n";
	die "$test: extracted slow suffix is not private and non-inline\n"
		if $helper_body !~ /^function\s+\@[^\s(]+.*\bbinding=internal\b/s ||
		   $helper_body !~ /^function\s+\@[^\s(]+.*\bno_inline=yes\b/s;
	die "$test: extracted suffix is not one straight-line block\n"
		if scalar(() = $helper_body =~ /^\s*block\s+\^/mg) != 1;
	my ($call_value) = $helper_body =~
		/^\s+%([A-Za-z0-9_]+)\s*=\s*call i32\s+\@\Q$effect_target\E\([^\n]*\)$/m;
	die "$test: extracted suffix lost its effectful value producer\n"
		if !defined($call_value);
	die "$test: extracted suffix does not store and return the same call value\n"
		if $helper_body !~ /^\s+store i32 %\Q$call_value\E,\s*%/m ||
		   $helper_body !~ /^\s+return i32 %\Q$call_value\E\s*$/m;
	my @wrapper_types = parameter_types($wrapper);
	my @helper_types = parameter_types($helper_body);
	die "$test: slow helper did not preserve the wrapper parameter contract\n"
		if join('|', @wrapper_types) ne join('|', @helper_types);

	for my $guard ($unbounded, $overwritten) {
		my $body = $records{O3}{$guard} //
			die "$test: O3 lost a guarded query identity\n";
		die "$test: O3 split an unproved query suffix\n"
			if $body =~ /^\s+%[A-Za-z0-9_]+\s*=\s*call i32\s+\@\Q$helper\E\(/m;
		die "$test: O3 removed the guarded effect/load join\n"
			if $body !~ /^\s+%[A-Za-z0-9_]+\s*=\s*call i32\s+\@\Q$effect_target\E\(/m ||
			   $body !~ /^\s+jump\s+\^/m;
	}

	my $driver_input = "$directory/driver.lowir";
	copy($test, $driver_input) or
		die "$test: unable to prepare cppgm++ replay: $!\n";
	compile_and_run($driver, $test, $directory, 'driver-O3',
		'O3', $driver_input);
}

print "O3 terminal-query slow suffix: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
