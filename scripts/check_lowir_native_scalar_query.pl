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

sub lowir_functions
{
	my ($test, $text) = @_;
	my %result;
	while($text =~ /(function\s+\@([^\s(]+).*?\n\})
	                 (?=\n(?:[ \t]*\n)*function\s+\@|\s*\z)/sgx) {
		die "$test: duplicate LowIR function symbol $2\n"
			if exists($result{$2});
		$result{$2} = $1;
	}
	return \%result;
}

sub mir_functions
{
	my ($test, $text) = @_;
	my %result;
	while($text =~ /(^function\s+\@([^\s]+).*?)(?=^function\s+\@|\z)/msg) {
		die "$test: duplicate MIR function symbol $2\n"
			if exists($result{$2});
		$result{$2} = $1;
	}
	return \%result;
}

sub stats_value
{
	my ($test, $stats, $name) = @_;
	return $1 if $stats =~ /(?:^| )\Q$name\E=([0-9]+)/;
	die "$test: native statistics omitted $name\n";
}

sub run_program
{
	my ($test, $program, $label, $directory) = @_;
	my $status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/$label.run.stdout",
		stderr => "$directory/$label.run.stderr",
		timeout => 30,
	);
	die "$test: $label behavior failed with status $status\n"
		if $status != 0;
}

sub block_with
{
	my ($body, $pattern) = @_;
	my @blocks = $body =~
		/(^\s+block\s+\^[^\n]+\n.*?)(?=^\s+block\s+\^|\z)/msg;
	my @matches = grep { $_ =~ $pattern } @blocks;
	return if @matches != 1;
	return $matches[0];
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_native_scalar_query.pl " .
		"<lowir2native> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-scalar-sibling-query\.t$/);
die "No O3 scalar sibling-query controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $source = lowir_functions($test, read_file($test));
	my ($helper, $producer, $wrapper, $changed);
	for my $symbol (keys(%$source)) {
		my $body = $source->{$symbol};
		next if scalar(() = $body =~ /^\s*block\s+\^/mg) != 1;
		my ($value, $target) = $body =~
			/^\s+%([A-Za-z0-9_]+)\s*=\s*call i32\s+\@([^\s(]+)\([^\n]*\)\s*$/m;
		next if !defined($value) ||
		        $body !~ /^\s+store i32 %\Q$value\E,\s*%/m ||
		        $body !~ /^\s+return i32 %\Q$value\E\s*$/m;
		die "$test: control has multiple terminal call/store/return helpers\n"
			if defined($helper);
		($helper, $producer) = ($symbol, $target);
	}
	die "$test: control has no structurally identifiable slow helper\n"
		if !defined($helper) || !exists($source->{$producer});

	for my $symbol (keys(%$source)) {
		my $body = $source->{$symbol};
		next if scalar(() = $body =~ /^\s*block\s+\^/mg) != 3;
		my ($value) = $body =~
			/^\s+%([A-Za-z0-9_]+)\s*=\s*call i32\s+\@\Q$helper\E\([^\n]*\)\s*$/m;
		next if !defined($value);
		if($body =~ /^\s+return i32 %\Q$value\E\s*$/m) {
			die "$test: control has multiple exact scalar wrappers\n"
				if defined($wrapper);
			$wrapper = $symbol;
		} else {
			$changed = $symbol;
		}
	}
	die "$test: control needs exact-result and changed-result wrappers\n"
		if !defined($wrapper) || !defined($changed);

	my $directory = tempdir('native-scalar-query-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (%mir, %stats);
	for my $level (qw(O0 O1 O2 O3)) {
		my $mir_path = "$directory/$level.mir";
		my $program = "$directory/$level.program";
		my $stderr = "$directory/$level.stats";
		my $status = run_command_capture(
			cmd => [$app, "-$level", '--stats', '--dump-machine-ir',
				$mir_path, '-o', $program, $test],
			stdout => "$directory/$level.compile.stdout",
			stderr => $stderr,
			timeout => 30,
		);
		die "$test: native -$level compile failed\n" . read_file($stderr)
			if $status != 0;
		$mir{$level} = mir_functions($test, read_file($mir_path));
		$stats{$level} = read_file($stderr);
		run_program($test, $program, $level, $directory);
	}

	for my $level (qw(O0 O1 O2)) {
		my $body = $mir{$level}{$wrapper} //
			die "$test: $level lost the scalar wrapper\n";
		die "$test: $level unexpectedly selected scalar sibling transfer\n"
			if $body =~ /^\s+sibling_call\s+\@\Q$helper\E\b/m;
		die "$test: $level lost the ordinary scalar call/return arm\n"
			if $body !~ /^\s+call\s+\@\Q$helper\E\b/m ||
			   $body !~ /^\s+ret\s+/m;
		die "$test: $level unexpectedly retained a terminal call result\n"
			if stats_value($test, $stats{$level},
				'machine_opt_terminal_call_results') != 0;
		die "$test: $level unexpectedly folded a sibling parameter carrier\n"
			if stats_value($test, $stats{$level},
				'machine_opt_sibling_parameter_retains') != 0;
	}

	my $o3_wrapper = $mir{O3}{$wrapper} //
		die "$test: O3 lost the scalar wrapper\n";
	my $sibling_block = block_with(
		$o3_wrapper, qr/^\s+sibling_call\s+\@\Q$helper\E\b/m);
	die "$test: O3 did not select exactly one scalar sibling transfer\n"
		if !defined($sibling_block);
	die "$test: scalar sibling transfer retained unreachable return work\n"
		if $sibling_block =~ /^\s+ret\b/m;
	my ($incoming) = $o3_wrapper =~
		/^\s+param\s+%[A-Za-z0-9_]+\s+->\s+([a-z0-9]+)\s*:\s*ptr\s*$/m;
	die "$test: scalar wrapper has no direct pointer ABI parameter\n"
		if !defined($incoming);
	my $fast_block = block_with($o3_wrapper, qr/^\s+ret\s+/m);
	die "$test: scalar wrapper has no unique fast return block\n"
		if !defined($fast_block);
	die "$test: O3 fast arm did not keep the incoming parameter carrier\n"
		if $fast_block !~ /\[\Q$incoming\E(?:\+|\])/;
	die "$test: O3 retained an eager copy of the incoming wrapper parameter\n"
		if $o3_wrapper =~ /^\s+mov\s+(?!\Q$incoming\E\b)[a-z0-9]+,\s*\Q$incoming\E\s*$/m;

	my $o2_wrapper = $mir{O2}{$wrapper};
	my ($o2_incoming) = $o2_wrapper =~
		/^\s+param\s+%[A-Za-z0-9_]+\s+->\s+([a-z0-9]+)\s*:\s*ptr\s*$/m;
	die "$test: O2 comparison wrapper has no parameter home copy\n"
		if !defined($o2_incoming) ||
		   $o2_wrapper !~ /^\s+mov\s+(?!\Q$o2_incoming\E\b)[a-z0-9]+,\s*\Q$o2_incoming\E\s*$/m;

	my $guard = $mir{O3}{$changed} //
		die "$test: O3 lost the changed-result guard\n";
	die "$test: O3 transferred a call whose result is changed before return\n"
		if $guard =~ /^\s+sibling_call\b/m;
	die "$test: changed-result guard lost its ordinary call\n"
		if $guard !~ /^\s+call\s+\@\Q$helper\E\b/m;

	my $o3_helper = $mir{O3}{$helper} //
		die "$test: O3 lost the terminal slow helper\n";
	my ($return_register) = $o3_helper =~
		/^\s+return i32\s+->\s+([a-z0-9]+)\s*$/m;
	die "$test: slow helper has no scalar ABI return carrier\n"
		if !defined($return_register);
	die "$test: O3 did not retain the call value through store and return\n"
		if $o3_helper !~ /^\s+store\.i32\s+[^,]+,\s*\Q$return_register\E\s*$/m ||
		   $o3_helper !~ /^\s+ret\s+\Q$return_register\E\s*$/m;
	my $o2_helper = $mir{O2}{$helper};
	die "$test: O2 unexpectedly retained the ABI return carrier\n"
		if $o2_helper =~ /^\s+store\.i32\s+[^,]+,\s*\Q$return_register\E\s*$/m &&
		   $o2_helper =~ /^\s+ret\s+\Q$return_register\E\s*$/m;
	die "$test: O3 terminal-result counter did not identify exactly one helper\n"
		if stats_value($test, $stats{O3},
			'machine_opt_terminal_call_results') != 1;
	die "$test: O3 parameter-retention counter did not identify one wrapper\n"
		if stats_value($test, $stats{O3},
			'machine_opt_sibling_parameter_retains') != 1;

	my $driver_input = "$directory/driver.lowir";
	copy($test, $driver_input) or
		die "$test: unable to prepare cppgm++ replay: $!\n";
	my $driver_program = "$directory/driver.program";
	my $status = run_command_capture(
		cmd => [$driver, '-O3', '-o', $driver_program, $driver_input],
		stdout => "$directory/driver.compile.stdout",
		stderr => "$directory/driver.compile.stderr",
		timeout => 60,
	);
	die "$test: O3 cppgm++ replay failed\n" .
		read_file("$directory/driver.compile.stderr") if $status != 0;
	run_program($test, $driver_program, 'driver', $directory);
}

print "O3 scalar sibling queries: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
