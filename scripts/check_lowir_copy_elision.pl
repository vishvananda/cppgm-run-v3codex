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
	my ($app, $test, $directory, $label, $level) = @_;
	my $output = "$directory/$label.lowir";
	my $stderr = "$directory/$label.stderr";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '--stats', '-o', $output, $test],
		stdout => "$directory/$label.stdout",
		stderr => $stderr,
		timeout => 30,
	);
	die "$test: lowiropt -$level failed\n" . read_file($stderr)
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

sub stat_value
{
	my ($test, $stats, $name) = @_;
	return 0 + $1 if $stats =~ /(?:^|\s)\Q$name\E=(\d+)(?:\s|$)/;
	die "$test: optimizer stats omit $name\n";
}

sub check_o0_permission
{
	my ($test, $body, $expects_eh) = @_;
	my ($destination, $source) = $body =~
		/^\s+call void \@transfer\((%\w+), (%\w+)\) \[elision=copy\]$/m;
	die "$test: O0 lost a marked transfer or merged its operands\n"
		if !defined($source) || $destination eq $source;
	my $producers = () = $body =~
		/^\s+call void \@construct_(?:left|right)\(\Q$source\E\)$/mg;
	die "$test: O0 permission lacks two independent source producers\n"
		if $producers != 2;
	my $destructors = () = $body =~
		/^\s+call void \@destroy\(\Q$source\E\)$/mg;
	my $expected_destructors = $expects_eh ? 2 : 1;
	die "$test: O0 changed the permitted source lifetime\n"
		if $destructors != $expected_destructors;
	die "$test: O0 lost the transfer exception region\n"
		if $expects_eh && $body !~ /^\s+eh_try \^/m;
	return ($destination, $source);
}

sub check_consumed
{
	my ($test, $body) = @_;
	die "$test: O2/O3 retained consumed copy-elision permission\n"
		if $body =~ /\[elision=copy\]/;
	die "$test: O2/O3 retained the coalesced source slot or transfer\n"
		if $body =~ /^\s+slot \$source\b/m ||
		   $body =~ /^\s+call void \@transfer\(/m;
	die "$test: O2/O3 retained a cleanup of the coalesced source\n"
		if $body =~ /^\s+call void \@destroy\(%\w+\)$/m;
	my ($left) = $body =~ /^\s+call void \@construct_left\((%\w+)\)$/m;
	my ($right) = $body =~ /^\s+call void \@construct_right\((%\w+)\)$/m;
	die "$test: O2/O3 lost or split the direct destination producers\n"
		if !defined($left) || !defined($right) || $left ne $right;
}

sub compile_and_run
{
	my ($driver, $test, $directory, $label, $input, $level) = @_;
	my $program = "$directory/$label.program";
	my $status = run_command_capture(
		cmd => [$driver, "-$level", '-o', $program, $input],
		stdout => "$directory/$label.native.stdout",
		stderr => "$directory/$label.native.stderr",
		timeout => 60,
	);
	die "$test: optimized LowIR did not compile at -$level\n" .
		read_file("$directory/$label.native.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/$label.program.stdout",
		stderr => "$directory/$label.program.stderr",
		timeout => 30,
	);
	die "$test: optimized copy-elision control failed with status $status\n"
		if $status != 0;
}

if (scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_copy_elision.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/copy-elision-permission\.t$/);
die "No copy-elision permission controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-copy-elision-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my ($o0_path, $o0) = optimize($app, $test, $directory, 'o0', 'O0');
	my ($o1_path, $o1) = optimize($app, $test, $directory, 'o1', 'O1');
	my ($o2_path, $o2, $o2_stats) =
		optimize($app, $test, $directory, 'o2', 'O2');
	my ($o3_path, $o3, $o3_stats) =
		optimize($app, $test, $directory, 'o3', 'O3');

	check_o0_permission(
		$test, function_body($test, $o0, 'eligible'), 0);
	check_o0_permission(
		$test, function_body($test, $o0, 'eh_eligible'), 1);
	check_o0_permission(
		$test, function_body($test, $o1, 'eligible'), 0);
	die "$test: O1 consumed protected copy-elision permission\n"
		if function_body($test, $o1, 'eh_eligible') !~ /\[elision=copy\]/;
	for my $optimized ($o2, $o3)
	{
		check_consumed($test,
			function_body($test, $optimized, 'eligible'));
		check_consumed($test,
			function_body($test, $optimized, 'eh_eligible'));
		my $unmarked = function_body($test, $optimized, 'unmarked');
		die "$test: optimizer coalesced a transfer without permission\n"
			if $unmarked !~ /^\s+call void \@transfer\(%\w+, %\w+\)$/m;
		my $escaping = function_body($test, $optimized, 'escaping');
		my ($escaped_source) = $escaping =~
			/^\s+call void \@observe\((%\w+)\)$/m;
		die "$test: optimizer consumed permission despite an escaping source\n"
			if !defined($escaped_source) ||
			   $escaping !~ /^\s+call void \@transfer\(%\w+, \Q$escaped_source\E\) \[elision=copy\]$/m;
		my $observed = function_body(
			$test, $optimized, 'observed_destination');
		my ($observed_destination) = $observed =~
			/^\s+call void \@observe\((%\w+)\)$/m;
		die "$test: optimizer redirected construction before an existing destination use\n"
			if !defined($observed_destination) ||
			   $observed !~ /^\s+call void \@transfer\(\Q$observed_destination\E, %\w+\) \[elision=copy\]$/m;
	}
	for my $stats ($o2_stats, $o3_stats)
	{
		die "$test: optimizer did not report both proven elisions\n"
			if stat_value($test, $stats, 'copy_elisions') != 2;
		die "$test: optimizer did not redirect all four producers\n"
			if stat_value($test, $stats, 'copy_elision_producers') != 4;
		die "$test: optimizer did not report the protected transfer\n"
			if stat_value($test, $stats, 'copy_elision_eh_regions') != 1;
	}
	my ($fixed_path, $fixed) = optimize(
		$app, $o2_path, $directory, 'o2-fixed', 'O2');
	die "$test: O2 copy-elision output is not a fixed point\n"
		if $fixed ne $o2;
	compile_and_run($driver, $test, $directory, 'o1', $o1_path, 'O1');
	compile_and_run($driver, $test, $directory, 'o2', $o2_path, 'O2');
	compile_and_run($driver, $test, $directory, 'o3', $o3_path, 'O3');
}

print "LowIR copy-elision permissions: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
