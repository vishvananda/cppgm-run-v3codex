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
	my ($app, $test, $directory, $level) = @_;
	my $output = "$directory/$level.lowir";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '-o', $output, $test],
		stdout => "$directory/$level.stdout",
		stderr => "$directory/$level.stderr",
		timeout => 30,
	);
	die "$test: lowiropt -$level failed\n" .
		read_file("$directory/$level.stderr") if $status != 0;
	return ($output, read_file($output));
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
	return \%result;
}

sub call_targets
{
	my ($body) = @_;
	return $body =~ /^\s+call\s+void\s+\@([^\s(]+)\(/mg;
}

sub count_calls
{
	my ($body, $target) = @_;
	return scalar(() = $body =~
		/^\s+call\s+void\s+\@\Q$target\E\(/mg);
}

sub parameter_names
{
	my ($body) = @_;
	my ($parameters) = $body =~ /^function\s+\@[^\s(]+\((.*?)\)\s*->/s;
	return () if !defined($parameters);
	return $parameters =~ /(%[A-Za-z0-9_]+)\s*:/g;
}

sub compile_and_run
{
	my ($driver, $test, $directory, $level, $lowir) = @_;
	my $program = "$directory/behavior-$level";
	my $status = run_command_capture(
		cmd => [$driver, '-O0', '-o', $program, $lowir],
		stdout => "$directory/compile-$level.stdout",
		stderr => "$directory/compile-$level.stderr",
		timeout => 60,
	);
	die "$test: $level optimized LowIR did not compile\n" .
		read_file("$directory/compile-$level.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/run-$level.stdout",
		stderr => "$directory/run-$level.stderr",
		timeout => 30,
	);
	die "$test: $level behavior failed with status $status\n"
		if $status != 0;
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_fast_path_versioning.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-fast-path-versioning\.t$/);
die "No O3 fast-path-versioning controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-fast-path-versioning-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (%paths, %outputs, %records);
	for my $level (qw(O0 O1 O2 O3)) {
		($paths{$level}, $outputs{$level}) =
			optimize($app, $test, $directory, $level);
		$records{$level} = functions($test, $outputs{$level});
		compile_and_run($driver, $test, $directory, $level, $paths{$level});
	}

	for my $level (qw(O0 O1 O2)) {
		my $body = $records{$level}->{versionable} //
			die "$test: $level lost the versionable reducer\n";
		die "$test: $level unexpectedly versioned the fast path\n"
			if count_calls($body, 'observe') != 4;
	}

	my $wrapper = $records{O3}->{versionable} //
		die "$test: O3 lost the fast wrapper's original identity\n";
	my @wrapper_calls = call_targets($wrapper);
	die "$test: O3 wrapper must have one shared slow transfer\n"
		if scalar(@wrapper_calls) != 1 || $wrapper_calls[0] eq 'observe';
	my $slow_target = $wrapper_calls[0];
	my $slow = $records{O3}->{$slow_target} //
		die "$test: O3 wrapper's slow target $slow_target has no definition\n";
	die "$test: complete slow body did not retain all four observable arms\n"
		if count_calls($slow, 'observe') != 4;
	die "$test: fast wrapper retained an observable slow-path call\n"
		if count_calls($wrapper, 'observe') != 0;

	my @parameters = parameter_names($wrapper);
	my ($arguments) = $wrapper =~
		/^\s+call\s+void\s+\@\Q$slow_target\E\(([^)]*)\)\s*$/m;
	die "$test: slow transfer is not a direct scalar-parameter call\n"
		if !defined($arguments);
	my @arguments = $arguments =~ /(%[A-Za-z0-9_]+)/g;
	die "$test: slow transfer did not preserve the wrapper parameter tuple\n"
		if join(' ', @arguments) ne join(' ', @parameters);

	my ($fallback_label) = $wrapper =~
		/^\s*block\s+\^([^:]+):\n\s+call\s+void\s+\@\Q$slow_target\E\([^\n]*\)\n\s+return\s+void/m;
	die "$test: wrapper slow transfer is not a shared call/return block\n"
		if !defined($fallback_label);
	my $bailouts = scalar(() = $wrapper =~ /\^\Q$fallback_label\E/g) - 1;
	die "$test: wrapper did not share at least three pre-effect bailouts\n"
		if $bailouts < 3;
	die "$test: fast wrapper retained the short-path merge phi\n"
		if $wrapper =~ /^\s+%\w+\s*=\s*phi\b/m;
	die "$test: complete slow body lost the original merge relationship\n"
		if $slow !~ /^\s+%\w+\s*=\s*phi\b/m;

	my $guard = $records{O3}->{effect_before_bailout} //
		die "$test: O3 lost the effect-before-bailout guard reducer\n";
	die "$test: O3 versioned a path after an externally visible store\n"
		if count_calls($guard, 'observe') != 4 ||
		   $guard !~ /^\s+store\s+i64\s+%\w+,\s+%\w+/m;
}

print "O3 fast-path versioning: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
