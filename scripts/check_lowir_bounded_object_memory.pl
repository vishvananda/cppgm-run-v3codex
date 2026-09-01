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

sub run_optimizer
{
	my ($app, $test, $directory, $level) = @_;
	my $output = "$directory/$level.lowir";
	my $stderr = "$directory/$level.stderr";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '-o', $output, '--stats', $test],
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
	                 (?=\nfunction\s+\@|\z)/sgx) {
		die "$test: duplicate optimized function symbol $2\n"
			if exists($result{$2});
		$result{$2} = $1;
	}
	die "$test: optimized LowIR contained no functions\n" if !%result;
	return \%result;
}

sub parameter_extent
{
	my ($body) = @_;
	my ($parameters) = $body =~ /^function \@[^\s(]+\((.*?)\)\s*->/s;
	return 0 if !defined($parameters);
	return $1 if $parameters =~
		/^\s*%[A-Za-z0-9_]+\s*:\s*ptr\s*\[[^\]]*\bobject_bytes=([1-9][0-9]*)\b/;
	return 0;
}

sub load_count
{
	my ($body) = @_;
	my @loads = $body =~ /^\s+%[A-Za-z0-9_]+ = load i64 /mg;
	return scalar(@loads);
}

sub pointer_call_count
{
	my ($body) = @_;
	my @calls = $body =~ /^\s+%[A-Za-z0-9_]+ = call ptr /mg;
	return scalar(@calls);
}

sub field_offsets
{
	my ($body) = @_;
	my %offsets;
	while($body =~ /^\s+(%[A-Za-z0-9_]+)\s+=\s+index\s+i8
	               \s+\[projection=field\]\s+
	               (%[A-Za-z0-9_]+),\s*([0-9]+)$/mgx) {
		my ($value, $base, $offset) = ($1, $2, $3);
		$offsets{$value} = (exists($offsets{$base}) ? $offsets{$base} : 0) +
			$offset;
	}
	return \%offsets;
}

sub repeated_load_interval
{
	my ($body) = @_;
	my %loads;
	while($body =~ /^\s+%[A-Za-z0-9_]+\s+=\s+load\s+i64
	               \s+(%[A-Za-z0-9_]+)$/mgx) {
		++$loads{$1};
	}
	my @repeated = grep { $loads{$_} >= 2 } keys(%loads);
	return if scalar(@repeated) != 1;
	my $offsets = field_offsets($body);
	return if !exists($offsets->{$repeated[0]});
	return ($offsets->{$repeated[0]}, $offsets->{$repeated[0]} + 8);
}

sub pointer_calls
{
	my ($body) = @_;
	my @calls;
	while($body =~ /^\s+%[A-Za-z0-9_]+\s+=\s+call\s+ptr
	               \s+\@([^\s(]+)\((%[A-Za-z0-9_]+)/mgx) {
		push(@calls, [$1, $2]);
	}
	return @calls;
}

sub intervals_overlap
{
	my ($left_begin, $left_end, $right_begin, $right_end) = @_;
	return $left_begin < $right_end && $right_begin < $left_end;
}

sub stats_value
{
	my ($test, $stats, $name) = @_;
	return $1 if $stats =~ /(?:^| )\Q$name\E=([0-9]+)/;
	die "$test: optimizer statistics omitted $name\n";
}

sub compile_and_run
{
	my ($driver, $test, $directory, $level, $lowir) = @_;
	my $program = "$directory/behavior-$level";
	my $status = run_command_capture(
		cmd => [$driver, "-$level", '-o', $program, $lowir],
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
	die "$test: $level optimized behavior failed with status $status\n"
		if $status != 0;
}

if(scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_bounded_object_memory.pl " .
		"<lowiropt> <cppgm++> <test-or-directory>\n";
}

my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root, qr/o3-bounded-object-memory.*\.t$/);
die "No O3 bounded-object-memory controls found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('cppgm-bounded-object-memory-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my (%outputs, %stats, %paths, %records);
	for my $level ('O0', 'O1', 'O2', 'O3') {
		($outputs{$level}, $stats{$level}, $paths{$level}) =
			run_optimizer($app, $test, $directory, $level);
		$records{$level} = functions($test, $outputs{$level});
	}

	my %extent;
	for my $name (keys(%{$records{O0}}))
	{
		$extent{$name} = parameter_extent($records{O0}{$name});
	}

	my ($disjoint, $precise_body, $overlap, $unbounded, $masked, $dynamic,
		$writes, $loop);
	for my $name (keys(%{$records{O0}}))
	{
		my $body = $records{O0}{$name};
		next if !$extent{$name};
		my $pointer_calls = pointer_call_count($body);
		if($body =~ /^\s+zeroinit /m && $body =~ /^\s+atomic_store /m) {
			$writes = $name;
			next;
		}
		if($body =~ /\[projection=array_element\]/) {
			if($body =~ /^\s+%[A-Za-z0-9_]+ = binary and i(?:8|16|32|64) /m) {
				$masked = $name;
			} else {
				$dynamic = $name;
			}
			next;
		}
		if($body =~ /^\s+%[A-Za-z0-9_]+ = call i64 /m &&
		   defined((repeated_load_interval($body))[0])) {
			$precise_body = $name;
			next;
		}
		if($pointer_calls == 2) {
			$disjoint = $name;
			next;
		}
		if($pointer_calls == 1 && $body =~ /^\s+branch /m &&
		   $body =~ /^\s+store i(?:8|16|32|64) [0-9]+, /m) {
			$loop = $name;
			next;
		}
		if($pointer_calls == 1) {
			my @calls = pointer_calls($body);
			if(@calls == 1 && $extent{$calls[0][0]}) {
				$overlap = $name;
			} else {
				$unbounded = $name;
			}
		}
	}
	my ($address_target, $address_caller);
	for my $name (keys(%{$records{O0}}))
	{
		my $body = $records{O0}{$name};
		my ($parameter) = $body =~
			/^function\s+\@[^\s(]+\(%([A-Za-z0-9_]+)\s*:\s*ptr\s*
			 \[[^\]]*pass=by_address[^\]]*\]/x;
		if(defined($parameter) &&
		   $body =~ /^\s+%[A-Za-z0-9_]+\s+=\s+load\s+i64\s+%\Q$parameter\E$/m)
		{
			$address_target = $name;
			last;
		}
	}
	die "$test: control has no structurally identifiable by-address scalar reader\n"
		if !defined($address_target);
	for my $name (keys(%{$records{O0}}))
	{
		my $body = $records{O0}{$name};
		my ($value) = $body =~
			/^\s+%([A-Za-z0-9_]+)\s+=\s+load\s+i64\s+/m;
		if(defined($value) &&
		   $body =~ /^\s+%[A-Za-z0-9_]+\s+=\s+cmp\s+eq\s+i64\s+%\Q$value\E,\s*0$/m &&
		   $body =~ /^\s+%[A-Za-z0-9_]+\s+=\s+call\s+i64\s+
		             \@\Q$address_target\E\(%\Q$value\E\)$/mx)
		{
			$address_caller = $name;
			last;
		}
	}
	die "$test: control has no equality-edge by-address call relationship\n"
		if !defined($address_caller);
	for my $pair ([disjoint => $disjoint], [precise_body => $precise_body],
	              [overlap => $overlap],
	              [unbounded => $unbounded], [masked => $masked],
	              [dynamic => $dynamic], [writes => $writes],
	              [loop => $loop]) {
		die "$test: control has no structurally identifiable $pair->[0] case\n"
			if !defined($pair->[1]);
	}
	my ($precise_target) = $records{O0}{$precise_body} =~
		/^\s+%[A-Za-z0-9_]+ = call i64 \@([^\s(]+)/m;
	die "$test: precise-body case does not call a bounded read-only body\n"
		if !defined($precise_target) || !$extent{$precise_target} ||
		   $records{O0}{$precise_target} !~ /^\s+%[A-Za-z0-9_]+ = load /m ||
		   $records{O0}{$precise_target} =~
			/^\s+(?:store|atomic_|copyobj|zeroinit|call)\b/m;

	# Establish that the positive capture case really has holes around the
	# repeated field, while the bounded negative case really overlaps it.
	my ($load_begin, $load_end) = repeated_load_interval(
		$records{O0}{$disjoint});
	die "$test: disjoint case has no single repeated field load\n"
		if !defined($load_begin);
	my $disjoint_offsets = field_offsets($records{O0}{$disjoint});
	for my $call (pointer_calls($records{O0}{$disjoint})) {
		my $begin = exists($disjoint_offsets->{$call->[1]}) ?
			$disjoint_offsets->{$call->[1]} : 0;
		my $end = $begin + $extent{$call->[0]};
		die "$test: positive capture interval overlaps its repeated field\n"
			if !$extent{$call->[0]} ||
			   intervals_overlap($load_begin, $load_end, $begin, $end);
	}
	($load_begin, $load_end) = repeated_load_interval(
		$records{O0}{$overlap});
	my @overlap_calls = pointer_calls($records{O0}{$overlap});
	my $overlap_offsets = field_offsets($records{O0}{$overlap});
	my $capture_begin = $overlap_offsets->{$overlap_calls[0][1]};
	my $capture_end = $capture_begin + $extent{$overlap_calls[0][0]};
	die "$test: bounded negative control does not actually overlap the load\n"
		if !defined($load_begin) || !defined($capture_begin) ||
		   !intervals_overlap(
			$load_begin, $load_end, $capture_begin, $capture_end);

	# The masked dynamic store's entire finite range must precede its repeated
	# load. The unmasked companion intentionally supplies no such proof.
	my ($mask) = $records{O0}{$masked} =~
		/^\s+%[A-Za-z0-9_]+\s+=\s+binary\s+and\s+i(?:8|16|32|64)
		 \s+%[A-Za-z0-9_]+,\s*([0-9]+)$/mx;
	my ($element_bytes) = $records{O0}{$masked} =~
		/index\s+obj<([1-9][0-9]*)x[1-9][0-9]*>
		 \s+\[projection=array_element\]/x;
	($load_begin, $load_end) = repeated_load_interval($records{O0}{$masked});
	die "$test: masked-store control does not prove a finite disjoint range\n"
		if !defined($mask) || !defined($element_bytes) ||
		   !defined($load_begin) || ($mask + 1) * $element_bytes > $load_begin;

	for my $level ('O0', 'O1', 'O2') {
		die "$test: $level applied the O3-only bounded-memory analysis\n"
			if stats_value($test, $stats{$level},
				'memory_gvn_object_extent_parameters') != 0 ||
			   stats_value($test, $stats{$level},
				'edge_integer_equalities') != 0 ||
			   stats_value($test, $stats{$level},
				'constant_loop_phi_edges') != 0 ||
			   stats_value($test, $stats{$level},
				'parameter_address_rematerializations') != 0;
		my %baseline = (
			$disjoint => 4, $precise_body => 2,
			$overlap => 2, $unbounded => 2,
			$masked => 2, $dynamic => 2, $writes => 3, $loop => 4,
		);
		for my $name (keys(%baseline)) {
			die "$test: $level changed the $baseline{$name}-load baseline " .
				"for a structurally selected case\n"
				if load_count($records{$level}{$name}) != $baseline{$name};
		}
	}

	my %expected_o3 = (
		$disjoint => 3, $precise_body => 1,
		$overlap => 2, $unbounded => 2,
		$masked => 1, $dynamic => 2, $writes => 3, $loop => 3,
	);
	for my $name (keys(%expected_o3)) {
		die "$test: O3 load count for a structurally selected case was " .
			load_count($records{O3}{$name}) . ", expected $expected_o3{$name}\n"
			if load_count($records{O3}{$name}) != $expected_o3{$name};
	}

	for my $name (qw(memory_gvn_object_extent_parameters
	                memory_gvn_exclusive_parameters
	                memory_gvn_capture_ranges memory_gvn_parameter_classes)) {
		my $value = stats_value($test, $stats{O3}, $name);
		die "$test: O3 $name was not exercised or exceeded its test bound\n"
			if $value == 0 || $value > 1000;
	}
	for my $name (qw(edge_integer_equalities constant_loop_phi_edges
	                parameter_address_rematerializations)) {
		die "$test: O3 $name was not exercised\n"
			if stats_value($test, $stats{O3}, $name) == 0;
	}
	die "$test: O3 parameter-address rematerialization exhausted its budget\n"
		if stats_value($test, $stats{O3},
			'parameter_address_rematerialization_budget_skips') != 0;

	# Equality propagation may replace value uses, but a by-address call needs
	# the address of the original scalar temporary rather than a literal with
	# the same value.
	my ($address_argument) = $records{O3}{$address_caller} =~
		/^\s+%[A-Za-z0-9_]+\s+=\s+call\s+i64\s+
		  \@\Q$address_target\E\(([^)]+)\)$/mx;
	die "$test: O3 equality propagation made a by-address call non-addressable\n"
		if !defined($address_argument) ||
		   $address_argument !~ /^%[A-Za-z0-9_]+$/;

	# The literal store backedge is threaded only after the branch-edge
	# equality exposes the constant loop input.
	my ($o2_store_target) = $records{O2}{$loop} =~
		/block\s+\^[^:]+:.*?^\s+store\s+i(?:8|16|32|64)\s+[0-9]+, .*?
		 ^\s+jump\s+\^([^\s]+)$/msx;
	my ($o3_store_target) = $records{O3}{$loop} =~
		/block\s+\^[^:]+:.*?^\s+store\s+i(?:8|16|32|64)\s+[0-9]+, .*?
		 ^\s+jump\s+\^([^\s]+)$/msx;
	die "$test: loop control did not expose distinct pre/post threading edges\n"
		if !defined($o2_store_target) || !defined($o3_store_target) ||
		   $o2_store_target eq $o3_store_target;

	# Select a field offset by relationship: its address is defined before the
	# call at O2, consumed after the call, and defined after the call at O3.
	my $o2_call = index($records{O2}{$loop}, ' call ptr ');
	my $o3_call = index($records{O3}{$loop}, ' call ptr ');
	my %o2_before;
	while($records{O2}{$loop} =~ /^\s+%[A-Za-z0-9_]+\s+=\s+index\s+i8
	       \s+\[projection=field\]\s+%[A-Za-z0-9_]+,\s*([0-9]+)$/mgx) {
		$o2_before{$1} = 1 if $-[0] < $o2_call;
	}
	my $rematerialized = 0;
	while($records{O3}{$loop} =~ /^\s+%[A-Za-z0-9_]+\s+=\s+index\s+i8
	       (?:\s+\[projection=field\])?\s+%[A-Za-z0-9_]+,\s*([0-9]+)$/mgx) {
		$rematerialized = 1 if $-[0] > $o3_call && $o2_before{$1};
	}
	die "$test: O3 did not move a post-call field address to its use region\n"
		if $o2_call < 0 || $o3_call < 0 || !$rematerialized;

	compile_and_run($driver, $test, $directory, 'O0', $paths{O0});
	compile_and_run($driver, $test, $directory, 'O3', $paths{O3});
}

print "O3 bounded object memory: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
