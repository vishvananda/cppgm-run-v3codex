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

sub compile_level
{
	my ($app, $test, $directory, $level) = @_;
	my $mir = "$directory/$level.mir";
	my $program = "$directory/$level.program";
	my $status = run_command_capture(
		cmd => [$app, "-$level", '--dump-machine-ir', $mir,
			'-o', $program, $test],
		stdout => "$directory/$level.stdout",
		stderr => "$directory/$level.stderr",
		timeout => 30,
	);
	die "$test: lowir2native -$level failed\n" .
		read_file("$directory/$level.stderr") if $status != 0;
	$status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/$level-program.stdout",
		stderr => "$directory/$level-program.stderr",
		timeout => 30,
	);
	die "$test: generated -$level program failed with status $status\n"
		if $status != 0;
	return read_file($mir);
}

sub function_body
{
	my ($test, $mir, $name) = @_;
	return $1 if $mir =~
		/(^function \@\Q$name\E\b.*?)(?=^function \@|\z)/ms;
	die "$test: machine IR has no $name function\n";
}

sub block_body
{
	my ($test, $function, $name) = @_;
	return $1 if $function =~
		/(^\s+block \^\Q$name\E\s*\n.*?)(?=^\s+block \^|\z)/ms;
	die "$test: function has no $name block\n";
}

sub frame_offset
{
	my ($body, $name) = @_;
	return $1 if $body =~ /^\s+temp %\Q$name\E -> (\[rbp[^\]]*\])/m;
	return undef;
}

sub preserve_count
{
	my ($body) = @_;
	return scalar(split(/\s+/, $1))
		if $body =~ /^\s+preserve\s+([^\n]+)$/m;
	return 0;
}

if(scalar(@ARGV) != 2)
{
	die "Usage: check_lowir_native_survivor_properties.pl " .
		"<lowir2native> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root,
	qr/(?:400-loop-invariant-call-crossing-placement|405-deferred-compare-across-call|406-volatile-access-emission|410-eh-edge-placement-barrier|420-loop-and-eh-placement|425-native-layout-policy-guards|426-staged-home-selection|427-rematerialized-storage-addresses|428-dominated-post-call-use-tails)\.t$/);
die "No native survivor-property tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('native-survivor-property-XXXXXX',
		TMPDIR => 1, CLEANUP => 1);
	my $o0 = compile_level($app, $test, $directory, 'O0');
	my $o1 = compile_level($app, $test, $directory, 'O1');
	my $optimized = $test =~
		/(?:400-loop-invariant-call-crossing-placement|410-eh-edge-placement-barrier)/
		? compile_level($app, $test, $directory, 'O2') : $o1;

	if($test =~ /400-loop-invariant-call-crossing-placement/) {
		my $baseline = function_body($test, $o0, 'main');
		my $positive = function_body($test, $optimized, 'main');
		die "$test: O0 lost the call-crossing stable-value home\n"
			if !defined(frame_offset($baseline, 'stable'));
		die "$test: optimized call-crossing value retained its frame home\n"
			if defined(frame_offset($positive, 'stable'));
		die "$test: call-crossing value received no call-safe capacity\n"
			if preserve_count($positive) == 0;
		my $floating = function_body($test, $optimized, 'float_loop');
		die "$test: call-free floating loop consumed integer preserved capacity\n"
			if preserve_count($floating) != 0;
		next;
	}
	if($test =~ /405-deferred-compare-across-call/) {
		my $probe = function_body($test, $o1, 'probe');
		my $call = index($probe, 'call @sink');
		my $compare = index($probe, 'cmp.i64');
		my @loads = $probe =~ /^\s+load\.i64 /mg;
		die "$test: compare operands were not loaded before the clobbering call\n"
			if scalar(@loads) != 2 || $call < 0;
		die "$test: compare was not deferred until after the call\n"
			if $compare < $call;
		die "$test: deferred compare operands lack call-safe capacity\n"
			if preserve_count($probe) < 2;
		next;
	}
	if($test =~ /406-volatile-access-emission/) {
		for my $mir ($o0, $o1) {
			my $body = function_body($test, $mir, 'spin_flag');
			die "$test: native lowering removed or merged volatile traffic\n"
				if scalar(() = $body =~ /^\s+store\.i32 /mg) != 2 ||
				   scalar(() = $body =~ /^\s+load\.i32 /mg) != 2;
		}
		next;
	}
	if($test =~ /410-eh-edge-placement-barrier/) {
		my $baseline = function_body($test, $o0, 'main');
		my $positive = function_body($test, $optimized, 'main');
		die "$test: O0 lost the EH-crossing stable-value home\n"
			if !defined(frame_offset($baseline, 'stable'));
		die "$test: optimized EH-crossing value retained its frame home\n"
			if defined(frame_offset($positive, 'stable'));
		die "$test: EH-crossing value has no call-safe capacity\n"
			if preserve_count($positive) == 0;
		die "$test: host-EH function incorrectly omitted its frame pointer\n"
			if $positive !~ /^\s+frame_pointer keep$/m;
		next;
	}

	if($test =~ /420-loop-and-eh-placement/) {
		my $baseline = function_body($test, $o0, 'walk_unavoidable');
		my $positive = function_body($test, $o1, 'walk_unavoidable');
		for my $name ('cursor', 'count', 'limit') {
			die "$test: O0 lost the unavoidable-loop $name home baseline\n"
				if !defined(frame_offset($baseline, $name));
			die "$test: O1 retained the unavoidable-loop $name frame home\n"
				if defined(frame_offset($positive, $name));
		}
		my $guarded = function_body($test, $o1, 'walk_guarded');
		for my $name ('cursor', 'count') {
			die "$test: guarded loop unsafely lost its $name frame home\n"
				if !defined(frame_offset($guarded, $name));
		}
		my $call_free = function_body($test, $o1, 'call_free_eh_load');
		die "$test: call-free EH load retained a value frame home\n"
			if defined(frame_offset($call_free, 'value'));
		die "$test: call-free EH load consumed preserved capacity\n"
			if preserve_count($call_free) != 0;
		my $crossing = function_body(
			$test, $o1, 'call_crossing_eh_value');
		die "$test: call-crossing EH value retained a frame home\n"
			if defined(frame_offset($crossing, 'value'));
		die "$test: call-crossing EH value has no call-safe capacity\n"
			if preserve_count($crossing) == 0;
		next;
	}
	if($test =~ /425-native-layout-policy-guards/) {
		for my $name ('identity', 'frameless_leaf', 'frameless_call',
			'direct_returns') {
			my $baseline = function_body($test, $o0, $name);
			my $positive = function_body($test, $o1, $name);
			die "$test: O0 $name lost the conservative layout baseline\n"
				if $baseline !~ /^\s+frame_pointer keep$/m ||
				   $baseline !~ /^\s+epilogues shared$/m;
			die "$test: eligible O1 $name did not select frameless direct layout\n"
				if $positive !~ /^\s+frame_pointer omit$/m ||
				   $positive !~ /^\s+epilogues direct$/m;
		}
		for my $name ('frame_operand', 'dynamic_stack',
			'floating_scratch', 'host_eh') {
			my $positive = function_body($test, $o1, $name);
			die "$test: guarded O1 $name incorrectly omitted its frame pointer\n"
				if $positive !~ /^\s+frame_pointer keep$/m;
			die "$test: guarded O1 $name did not retain direct epilogues\n"
				if $positive !~ /^\s+epilogues direct$/m;
		}
		next;
	}
	if($test =~ /426-staged-home-selection/) {
		my $baseline = function_body($test, $o0, 'copy_to_edge_home');
		my $positive = function_body($test, $o1, 'copy_to_edge_home');
		my ($value_register) = $positive =~
			/^\s+param %value -> ([a-z0-9]+) : i64$/m;
		my $copy_home = frame_offset($positive, 'copied');
		die "$test: copy reducer lost its parameter or edge home\n"
			if !defined($value_register) || !defined($copy_home);
		die "$test: O1 did not store the identity copy directly to its home\n"
			if $positive !~
			   /^\s+store\.i64 \Q$copy_home\E, \Q$value_register\E$/m;
		die "$test: O0 baseline already had the direct identity-home store\n"
			if $baseline =~
			   /^\s+store\.i64 \Q$copy_home\E, \Q$value_register\E$/m;

		$baseline = function_body($test, $o0, 'call_result_to_edge_home');
		$positive = function_body($test, $o1, 'call_result_to_edge_home');
		my $value_home = frame_offset($positive, 'value');
		die "$test: call-result reducer lost its stable edge home\n"
			if !defined($value_home);
		die "$test: O1 did not store the first scalar call result directly\n"
			if $positive !~
			   /^\s+call \@identity\b[^\n]*\n\s+store\.i64 \Q$value_home\E, rax$/m;
		die "$test: O0 baseline already had the direct call-result store\n"
			if $baseline =~
			   /^\s+call \@identity\b[^\n]*\n\s+store\.i64 \Q$value_home\E, rax$/m;

		my $dead = function_body($test, $o1, 'load_with_dead_address');
		my ($dead_carrier) = $dead =~
			/^\s+load\.ptr ([a-z0-9]+), \@number_pointer\s*\n\s+load\.i64 \1, \[\1\]$/m;
		die "$test: final-use address register was not taken over by load\n"
			if !defined($dead_carrier);
		my $live = function_body($test, $o1, 'load_with_live_address');
		my ($address, $value) = $live =~
			/^\s+load\.ptr ([a-z0-9]+), \@number_pointer\s*\n\s+load\.i64 ([a-z0-9]+), \[\1\]$/m;
		die "$test: live-address control lost its paired loads\n"
			if !defined($address) || !defined($value);
		die "$test: live address was overwritten by its load result\n"
			if $address eq $value;
		my $left = block_body($test, $dead, 'left');
		die "$test: eligible three-operand add did not use indexed lea\n"
			if $left !~ /^\s+lea [a-z0-9]+, \[[a-z0-9]+\+[a-z0-9]+\]$/m;
		next;
	}
	if($test =~ /427-rematerialized-storage-addresses/) {
		my $frame = function_body($test, $o1, 'frame_storage_after_call');
		die "$test: frame storage was materialized into an address register\n"
			if $frame =~ /^\s+lea [^,]+, \[rbp/m;
		die "$test: frame storage did not remain a direct load/store operand\n"
			if $frame !~ /^\s+store\.i64 \[rbp[^\]]*\], 19$/m ||
			   $frame !~ /^\s+load\.i64 [a-z0-9]+, \[rbp[^\]]*\]$/m;
		my $global = function_body($test, $o1, 'global_storage_after_call');
		die "$test: local global storage did not remain a direct operand\n"
			if $global !~ /^\s+load\.i64 [a-z0-9]+, \@global_value$/m;
		die "$test: local global address was unnecessarily materialized\n"
			if $global =~ /^\s+(?:mov|lea) [a-z0-9]+, \@global_value$/m;
		my $constant = function_body(
			$test, $o1, 'constant_field_after_call');
		die "$test: constant frame field did not fold into direct operands\n"
			if $constant =~ /^\s+lea [^,]+, \[rbp/m ||
			   $constant !~ /^\s+store\.i64 \[rbp[^\]]*\], 23$/m ||
			   $constant !~ /^\s+load\.i64 [a-z0-9]+, \[rbp[^\]]*\]$/m;
		my $variable = function_body(
			$test, $o1, 'variable_index_after_call');
		my ($carrier) = $variable =~
			/^\s+lea ([a-z0-9]+), \[rbp[^\]]*\]$/m;
		die "$test: variable frame index was not materialized\n"
			if !defined($carrier) ||
			   $variable !~ /^\s+store\.i64 \[\Q$carrier\E\], 29$/m ||
			   $variable !~ /^\s+load\.i64 [a-z0-9]+, \[\Q$carrier\E\]$/m;
		next;
	}
	if($test =~ /428-dominated-post-call-use-tails/) {
		my $baseline = function_body($test, $o0, 'post_call_tails');
		my $positive = function_body($test, $o1, 'post_call_tails');
		die "$test: O0 lost the crossing-value frame baseline\n"
			if !defined(frame_offset($baseline, 'c'));
		die "$test: O1 failed to use the available call-safe capacity\n"
			if defined(frame_offset($positive, 'c'));
		die "$test: pressure reducer no longer fills preserved capacity\n"
			if preserve_count($baseline) != 5 ||
			   preserve_count($positive) != 5;
		my $baseline_tail = block_body($test, $baseline, 'tail');
		my $positive_tail = block_body($test, $positive, 'tail');
		my @before = $baseline_tail =~ /^\s+load\./mg;
		my @after = $positive_tail =~ /^\s+load\./mg;
		die "$test: planned release did not reduce dominated-tail reloads\n"
			if scalar(@after) >= scalar(@before);
		next;
	}
	die "$test: no native survivor-property predicate selected\n";
}

print "native survivor properties: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
