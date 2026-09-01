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
	while($text =~ /(function\s+\@([^\s(]+).*?\n\})\s*
	                 (?=^function\s+\@|\z)/sgmx) {
		die "$test: duplicate LowIR function symbol $2\n"
			if exists($result{$2});
		$result{$2} = $1;
	}
	return \%result;
}

sub mir_function
{
	my ($test, $text, $symbol) = @_;
	return $1 if $text =~
		/(^function \@\Q$symbol\E\b.*?)(?=^function \@|\z)/ms;
	die "$test: machine IR has no structurally selected function\n";
}

sub parameter_extent
{
	my ($body) = @_;
	return $1 if $body =~
		/^function\s+\@[^\s(]+\(\s*%[A-Za-z0-9_]+\s*:\s*ptr
		 \s*\[[^\]]*\bobject_bytes=([1-9][0-9]*)\b/x;
	return 0;
}

sub call_position
{
	my ($body) = @_;
	return index($body, ' call i64 ');
}

sub constant_field_indexes
{
	my ($body) = @_;
	my @result;
	while($body =~ /^\s+(%[A-Za-z0-9_]+)\s+=\s+index\s+i8
	               \s+\[projection=field\]\s+%[A-Za-z0-9_]+,
	               \s*([1-9][0-9]*)$/mgx) {
		push(@result, [$1, 0 + $2, $-[0]]);
	}
	return @result;
}

sub constant_indexes
{
	my ($body) = @_;
	my @result;
	while($body =~ /^\s+(%[A-Za-z0-9_]+)\s+=\s+index\s+i8
	               (?:\s+\[projection=field\])?
	               \s+%[A-Za-z0-9_]+,\s*([1-9][0-9]*)$/mgx) {
		push(@result, [$1, 0 + $2, $-[0]]);
	}
	return @result;
}

sub preserved_registers
{
	my ($body) = @_;
	return split(/\s+/, $1) if $body =~ /^\s+preserve\s+([^\n]+)$/m;
	return ();
}

sub stack_size
{
	my ($body) = @_;
	return 0 + $1 if $body =~ /^\s+stack_size\s+([0-9]+)$/m;
	return 0;
}

sub stats_value
{
	my ($test, $stats, $name) = @_;
	return 0 + $1 if $stats =~ /(?:^| )\Q$name\E=([0-9]+)/;
	die "$test: optimizer statistics omitted $name\n";
}

if(scalar(@ARGV) != 4)
{
	die "Usage: check_lowir_native_parameter_rematerialization.pl " .
		"<lowiropt> <lowir2native> <cppgm++> <test-or-directory>\n";
}

my ($optimizer, $native, $driver, $root) = @ARGV;
my @tests = collect_tests(
	$root, qr/o3-parameter-address-rematerialization.*\.t$/);
die "No O3 parameter-address rematerialization controls found under $root\n"
	if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('native-parameter-remat-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my $source = read_file($test);
	my $source_functions = lowir_functions($test, $source);
	my $selected;
	my @selected_offsets;
	for my $symbol (keys(%$source_functions)) {
		my $body = $source_functions->{$symbol};
		next if !parameter_extent($body);
		my $call = call_position($body);
		next if $call < 0;
		my @indexes = constant_field_indexes($body);
		my @before = grep { $_->[2] < $call } @indexes;
		next if @before < 4;
		my %used_after;
		for my $index (@before) {
			my $value = $index->[0];
			$used_after{$index->[1]} = 1
				if substr($body, $call) =~
				   /^\s+%[A-Za-z0-9_]+ = load i64 \Q$value\E$/m;
		}
		next if scalar(keys(%used_after)) < 4;
		die "$test: multiple functions match the rematerialization relation\n"
			if defined($selected);
		$selected = $symbol;
		@selected_offsets = sort { $a <=> $b } keys(%used_after);
	}
	die "$test: no bounded parameter function defines four post-call field " .
		"addresses before the call\n" if !defined($selected);

	my (%optimized, %stats, %mir, %program);
	for my $level (qw(O2 O3)) {
		$optimized{$level} = "$directory/$level.lowir";
		my $stderr = "$directory/$level.optimizer.stderr";
		my $status = run_command_capture(
			cmd => [$optimizer, "-$level", '-o', $optimized{$level},
				'--stats', $test],
			stdout => "$directory/$level.optimizer.stdout",
			stderr => $stderr,
			timeout => 30,
		);
		die "$test: lowiropt -$level failed\n" . read_file($stderr)
			if $status != 0;
		$stats{$level} = read_file($stderr);
		$mir{$level} = "$directory/$level.mir";
		$program{$level} = "$directory/$level.program";
		$status = run_command_capture(
			cmd => [$native, "-$level", '--dump-machine-ir', $mir{$level},
				'-o', $program{$level}, $optimized{$level}],
			stdout => "$directory/$level.native.stdout",
			stderr => "$directory/$level.native.stderr",
			timeout => 30,
		);
		die "$test: $level optimized native compile failed\n" .
			read_file("$directory/$level.native.stderr") if $status != 0;
		$status = run_command_capture(
			cmd => [$program{$level}],
			stdout => "$directory/$level.run.stdout",
			stderr => "$directory/$level.run.stderr",
			timeout => 30,
		);
		die "$test: $level generated behavior failed with status $status\n"
			if $status != 0;
	}

	my $o2_functions = lowir_functions($test, read_file($optimized{O2}));
	my $o3_functions = lowir_functions($test, read_file($optimized{O3}));
	for my $level_body ($o2_functions->{$selected}, $o3_functions->{$selected}) {
		die "$test: optimization lost the selected function or object extent\n"
			if !defined($level_body) || !parameter_extent($level_body);
	}
	my $o2_call = call_position($o2_functions->{$selected});
	my $o3_call = call_position($o3_functions->{$selected});
	my (%o2_before, %o3_after);
	for my $index (constant_field_indexes($o2_functions->{$selected})) {
		$o2_before{$index->[1]} = 1 if $index->[2] < $o2_call;
	}
	for my $index (constant_indexes($o3_functions->{$selected})) {
		$o3_after{$index->[1]} = 1 if $index->[2] > $o3_call;
	}
	for my $offset (@selected_offsets) {
		die "$test: O2 lost the pre-call address baseline at a selected offset\n"
			if !$o2_before{$offset};
		die "$test: O3 did not rematerialize a selected address after the call\n"
			if !$o3_after{$offset};
	}
	die "$test: O2 unexpectedly ran O3 parameter-address rematerialization\n"
		if stats_value($test, $stats{O2},
			'parameter_address_rematerializations') != 0;
	die "$test: O3 did not report the structurally observed rematerializations\n"
		if stats_value($test, $stats{O3},
			'parameter_address_rematerializations') < @selected_offsets;
	die "$test: O3 exhausted the parameter-address rematerialization budget\n"
		if stats_value($test, $stats{O3},
			'parameter_address_rematerialization_budget_skips') != 0;

	my $o2_mir = mir_function($test, read_file($mir{O2}), $selected);
	my $o3_mir = mir_function($test, read_file($mir{O3}), $selected);
	my @o2_preserved = preserved_registers($o2_mir);
	my @o3_preserved = preserved_registers($o3_mir);
	die "$test: native O3 did not reduce call-preserved address pressure\n"
		if @o3_preserved + 2 > @o2_preserved;
	die "$test: native O3 increased the selected function's stack size\n"
		if stack_size($o3_mir) > stack_size($o2_mir);
	my ($o2_before_call) = $o2_mir =~ /(.*?^\s+call\s+\@)/ms;
	my ($o3_before_call) = $o3_mir =~ /(.*?^\s+call\s+\@)/ms;
	my $o2_leas = defined($o2_before_call) ?
		scalar(() = $o2_before_call =~ /^\s+lea\s+/mg) : 0;
	my $o3_leas = defined($o3_before_call) ?
		scalar(() = $o3_before_call =~ /^\s+lea\s+/mg) : 0;
	die "$test: native O3 did not remove pre-call derived-address setup\n"
		if $o2_leas < @selected_offsets || $o3_leas >= $o2_leas;
	my ($o3_after_call) = $o3_mir =~ /^\s+call\s+\@[^\n]+\n(.*)$/ms;
	my %native_offsets;
	while(defined($o3_after_call) &&
	      $o3_after_call =~ /^\s+load\.i64\s+\w+,\s+
	                         \[\w+\+([1-9][0-9]*)\]/mgx) {
		$native_offsets{0 + $1} = 1;
	}
	for my $offset (@selected_offsets) {
		die "$test: native O3 lost a post-call direct field load\n"
			if !$native_offsets{$offset};
	}

	# Exercise the ordinary driver path as well as the explicit staged path.
	my $driver_input = "$directory/driver.lowir";
	copy($test, $driver_input) or
		die "$test: unable to prepare driver replay: $!\n";
	my $driver_program = "$directory/driver.program";
	my $status = run_command_capture(
		cmd => [$driver, '-O3', '-o', $driver_program, $driver_input],
		stdout => "$directory/driver.stdout",
		stderr => "$directory/driver.stderr",
		timeout => 60,
	);
	die "$test: O3 driver integration compile failed\n" .
		read_file("$directory/driver.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$driver_program],
		stdout => "$directory/driver.run.stdout",
		stderr => "$directory/driver.run.stderr",
		timeout => 30,
	);
	die "$test: O3 driver integration behavior failed with status $status\n"
		if $status != 0;
}

print "native O3 parameter-address rematerialization: PASS (" .
	scalar(@tests) . "/" . scalar(@tests) . ")\n";
