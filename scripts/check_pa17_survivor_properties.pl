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
	die "$test: generated LowIR has no $name definition\n";
}

sub compile_source
{
	my ($app, $test, $directory) = @_;
	my $output = "$directory/test.lowir";
	my $status = run_command_capture(
		cmd => [$app, '--emit-lowir', '-O0', '-o', $output, $test],
		stdout => "$directory/source.stdout",
		stderr => "$directory/source.stderr",
		timeout => 60,
	);
	die "$test: source-to-LowIR compile failed\n" .
		read_file("$directory/source.stderr") if $status != 0;
	return ($output, read_file($output));
}

sub compile_and_run
{
	my ($app, $test, $directory, $lowir) = @_;
	my $program = "$directory/program";
	my $status = run_command_capture(
		cmd => [$app, '-O0', '-o', $program, $lowir],
		stdout => "$directory/native.stdout",
		stderr => "$directory/native.stderr",
		timeout => 60,
	);
	die "$test: generated LowIR did not compile\n" .
		read_file("$directory/native.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/program.stdout",
		stderr => "$directory/program.stderr",
		timeout => 30,
	);
	die "$test: generated program failed with status $status\n"
		if $status != 0;
}

if(scalar(@ARGV) != 2)
{
	die "Usage: check_pa17_survivor_properties.pl " .
		"<cppgm++> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root,
	qr/(?:constructor-alias-boundaries|enclosing-temporary-lifetime|out-of-class-move-assignment-boundary|conditional-copy-elision-permission|stable-prefix-query-boundary|parameter-object-extent-boundary|rejected-stable-prefix-query-[^.]+)\.cpp$/);
die "No PA17 survivor-property tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('pa17-survivor-property-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	if($test =~ /rejected-stable-prefix-query/) {
		my $output = "$directory/rejected.lowir";
		my $status = run_command_capture(
			cmd => [$app, '--emit-lowir', '-O0', '-o', $output, $test],
			stdout => "$directory/source.stdout",
			stderr => "$directory/source.stderr",
			timeout => 60,
		);
		die "$test: frontend accepted an invalid stable-prefix query\n"
			if $status == 0;
		next;
	}
	my ($path, $lowir) = compile_source($app, $test, $directory);

	if($test =~ /constructor-alias-boundaries/) {
		my @constructors = $lowir =~
			/^(function \@[^\n]+\([^\n]+\)[^\n]*\bobject=_[^\n]*(?:C1ERKS_|C1EOS_)[^\n]*\{)/mg;
		die "$test: copy/move constructor definitions are missing\n"
			if scalar(@constructors) != 2;
		for my $header (@constructors) {
			my @noalias = $header =~ /\bptr \[[^\]]*alias=noalias[^\]]*\]/g;
			die "$test: constructor destination/source are not both noalias\n"
				if scalar(@noalias) != 2;
		}
		my ($assignment) = $lowir =~
			/^(function \@[^\n]+\([^\n]+\)[^\n]*\bobject=_[^\n]*aSERKS_[^\n]*\{)/m;
		die "$test: copy-assignment control definition is missing\n"
			if !defined($assignment);
		die "$test: assignment operator incorrectly received constructor noalias facts\n"
			if $assignment =~ /\balias=noalias\b/;
		compile_and_run($app, $test, $directory, $path);
		next;
	}
	if($test =~ /enclosing-temporary-lifetime/) {
		my $main = function_body($test, $lowir, 'main');
		my ($outer, $inner, $operator) = $main =~
			/^\s+(%\w+) = addr \$\w+.*?^\s+(%\w+) = addr \$\w+.*?^\s+%\w+ = call i32 \@(\w+)\(\1, \2\)$/ms;
		die "$test: nested temporary/operator relationship is missing\n"
			if !defined($operator);
		my $use = index($main, "call i32 \@$operator($outer, $inner)");
		my ($destructor) = substr($main, $use) =~
			/^\s+call void \@(\w+)\(\Q$inner\E\)\n\s+call void \@\1\(\Q$outer\E\)$/m;
		die "$test: selected operand and enclosing temporary were not " .
			"destroyed after use in reverse order\n"
			if !defined($destructor);
		my $cleanup = $main =~
			/^\s+block \^\w+:\n(?:\s+.*\n)*?\s+call void \@\Q$destructor\E\(\Q$outer\E\)\n\s+jump \^/m;
		die "$test: inner-construction failure does not clean the enclosing temporary\n"
			if !$cleanup;
		compile_and_run($app, $test, $directory, $path);
		next;
	}
	if($test =~ /out-of-class-move-assignment-boundary/) {
		my ($assignment) = $lowir =~
			/^(function \@[^\n]+\bobject=_ZN7MoveBoxaSEOS_[^\n]*\{)$/m;
		die "$test: out-of-class move assignment did not retain its " .
			"declared rvalue-reference ABI identity\n"
			if !defined($assignment);
		compile_and_run($app, $test, $directory, $path);
		next;
	}
	if($test =~ /conditional-copy-elision-permission/) {
		my $main = function_body($test, $lowir, 'main');
		my ($callee, $destination, $source) = $main =~
			/^\s+call void \@(\w+)\((%\w+), (%\w+)\) \[elision=copy\]$/m;
		die "$test: conditional class prvalue did not emit copy-elision permission\n"
			if !defined($source) || $destination eq $source;
		my $marker = index($main, "[elision=copy]");
		my $before = substr($main, 0, $marker);
		my $after = substr($main, $marker);
		my @blocks = $before =~ /^\s*block \^(\w+):/mg;
		die "$test: permitted transfer is not in a merge block\n" if !@blocks;
		my $merge = $blocks[-1];
		my $incoming = () = $before =~ /^\s+jump \^\Q$merge\E$/mg;
		my $producers = () = $before =~
			/^\s+call void \@\w+\(\Q$source\E(?:,|\))/mg;
		die "$test: conditional operands do not independently construct the source temporary\n"
			if $incoming < 2 || $producers < 2;
		die "$test: O0 permission removed the ordinary source cleanup\n"
			if $after !~ /^\s+call void \@\w+\(\Q$source\E\)$/m;
		compile_and_run($app, $test, $directory, $path);
		next;
	}
	if($test =~ /stable-prefix-query-boundary/) {
		my ($header) = $lowir =~
			/^(function \@stable_prefix_query\([^\n]+\)[^\n]+\{)$/m;
		die "$test: generated LowIR has no stable-prefix query boundary\n"
			if !defined($header);
		die "$test: source attribute did not emit query=stable_prefix\n"
			if $header !~ /\bquery=stable_prefix\b/;
		die "$test: query boundary lacks its final integer index or scalar result\n"
			if $header !~ /%\w+\s*:\s*(?:i|u)(?:8|16|32|64)\)\s*->\s*(?:i|u)(?:8|16|32|64)\b/;
		compile_and_run($app, $test, $directory, $path);
		next;
	}
	if($test =~ /parameter-object-extent-boundary/) {
		my @headers = $lowir =~ /^(?:declare )?function \@[^\n]+$/mg;
		my @indirect = $lowir =~
			/\bptr\s+\[[^\]]*pass=indirect_result[^\]]*object_bytes=([1-9][0-9]*)[^\]]*\]/g;
		my @by_address = $lowir =~
			/\bptr\s+\[[^\]]*pass=by_address[^\]]*object_bytes=([1-9][0-9]*)[^\]]*\]/g;
		my @object_parameters = $lowir =~
			/\bptr\s+\[object_bytes=([1-9][0-9]*)\]/g;
		my @indirect_call_extents = $lowir =~
			/^\s+%[A-Za-z0-9_]+\s+=\s+call\s+i64\s+%[A-Za-z0-9_]+\([^\n]*
			 \bptr\s+\[object_bytes=([1-9][0-9]*)\]/mgx;
		die "$test: O0 LowIR lacks an indirect-result object extent\n"
			if !@indirect;
		die "$test: O0 LowIR lacks a by-address object extent\n"
			if !@by_address;
		die "$test: O0 LowIR lacks a member-object parameter extent\n"
			if !@object_parameters;
		die "$test: an indirect member call lost its object extent signature\n"
			if !@indirect_call_extents;
		my %indirect_sizes = map { $_ => 1 } @indirect;
		my $shared_extent = scalar(grep { $indirect_sizes{$_} }
			(@by_address, @object_parameters));
		die "$test: related class boundaries disagree on their semantic extent\n"
			if !$shared_extent;
		my $ordinary_pointer = scalar(grep {
			/\blinkage=c\b/ && /\([^)]*:\s*ptr\)/ &&
				!/[\[][^\]]*object_bytes=/
		} @headers);
		die "$test: an ordinary source pointer incorrectly requires object metadata\n"
			if !$ordinary_pointer;
		die "$test: source lowering attached object_bytes to a non-pointer\n"
			if $lowir =~ /\b(?:i1|i8|u8|i16|u16|i32|u32|i64|f32|f64|f80)\s+\[[^\]]*object_bytes=/;
		compile_and_run($app, $test, $directory, $path);
		next;
	}
	die "$test: no PA17 survivor predicate selected\n";
}

print "PA17 survivor properties: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
