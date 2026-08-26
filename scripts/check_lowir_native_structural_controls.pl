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
	my ($test, $mir, $symbol) = @_;
	return $1 if $mir =~ /(^function \@\Q$symbol\E\b.*?)(?=^function \@|\z)/ms;
	die "$test: machine IR has no $symbol function\n";
}

sub frame_offset
{
	my ($body, $name) = @_;
	return $1 if $body =~ /^\s+temp \%\Q$name\E -> \[rbp([+-]\d+)\]/m;
	return undef;
}

sub block_body
{
	my ($body, $label) = @_;
	return $1 if $body =~ /(^\s+block \^\Q$label\E\s*\n.*?)(?=^\s+block \^|\z)/ms;
	return undef;
}

sub preserve_count
{
	my ($body) = @_;
	return scalar(split(/\s+/, $1))
		if $body =~ /^\s+preserve\s+([^\n]+)$/m;
	return 0;
}

if (scalar(@ARGV) != 2)
{
	die "Usage: check_lowir_native_structural_controls.pl <lowir2native> <test-or-directory>\n";
}

my ($app, $root) = @ARGV;
my @tests = collect_tests($root,
	qr/(?:acyclic-phi-frame-home|cyclic-choice-region-residency|call-free-fast-loop-phi-residency|call-free-callee-save-recoloring|adjacent-frame-compare-forwarding|local-loop-phi-activation|historical-placement-contracts).*\.t$/);
die "No native structural-control tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-native-structural-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $mir_path = "$directory/test.mir";
	my $program = "$directory/test.program";
	my $status = run_command_capture(
		cmd => [$app, '-O1', '--dump-machine-ir', $mir_path,
			'-o', $program, $test],
		stdout => "$directory/compile.stdout",
		stderr => "$directory/compile.stderr",
		timeout => 30,
	);
	die "$test: O1 native compile failed\n" .
		read_file("$directory/compile.stderr") if $status != 0;
	my $run_status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/program.stdout",
		stderr => "$directory/program.stderr",
		timeout => 30,
	);
	die "$test: generated program failed with status $run_status\n" if $run_status != 0;

	my $mir = read_file($mir_path);
	if ($test =~ /local-loop-phi-activation/) {
		my $positive = function_body($test, $mir, 'late_local_loop');
		die "$test: reducer lost its call-bearing prefix\n"
			if $positive !~ /^\s+call \@observe\b/m;
		my $loop = block_body($positive, 'loop');
		my $body = block_body($positive, 'body');
		die "$test: reducer lost its bypassable loop\n"
			if !defined($loop) || !defined($body);
		for my $name ('index', 'sum') {
			my $home = frame_offset($positive, $name);
			die "$test: reducer lost the $name fallback home\n"
				if !defined($home);
			die "$test: locally resident $name still uses its frame home per iteration\n"
				if $loop =~ /\Q$home\E/ || $body =~ /\Q$home\E/;
		}

		my $pressured = function_body($test, $mir, 'pressured_local_loop');
		my $pressured_loop = block_body($pressured, 'loop');
		my $pressured_body = block_body($pressured, 'body');
		die "$test: pressured reducer lost its bypassable loop\n"
			if !defined($pressured_loop) || !defined($pressured_body);
		for my $name ('index', 'sum') {
			my $home = frame_offset($pressured, $name);
			die "$test: pressured reducer lost the $name fallback home\n"
				if !defined($home);
			die "$test: alternate caller-saved capacity left $name in its frame home per iteration\n"
				if $pressured_loop =~ /\Q$home\E/ ||
				   $pressured_body =~ /\Q$home\E/;
		}
		my $capacity = function_body(
			$test, $mir, 'pressured_capacity_baseline');
		die "$test: local activation added preserved-register capacity\n"
			if preserve_count($pressured) != preserve_count($capacity);

		my $guard = function_body($test, $mir, 'call_in_loop');
		my $guard_body = block_body($guard, 'body');
		die "$test: safety reducer lost its loop call\n"
			if !defined($guard_body) || $guard_body !~ /^\s+call \@observe\b/m;
		for my $name ('index', 'sum') {
			my $home = frame_offset($guard, $name);
			die "$test: call-crossing $name lost its stable frame home\n"
				if !defined($home);
		}

		my $baseline_mir = "$directory/baseline.mir";
		my $baseline_program = "$directory/baseline.program";
		$status = run_command_capture(
			cmd => [$app, '-O0', '--dump-machine-ir', $baseline_mir,
				'-o', $baseline_program, $test],
			stdout => "$directory/baseline-compile.stdout",
			stderr => "$directory/baseline-compile.stderr",
			timeout => 30,
		);
		die "$test: O0 native compile failed\n" .
			read_file("$directory/baseline-compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$baseline_program],
			stdout => "$directory/baseline-program.stdout",
			stderr => "$directory/baseline-program.stderr",
			timeout => 30,
		);
		die "$test: O0 generated program failed with status $run_status\n"
			if $run_status != 0;
		my $baseline = function_body(
			$test, read_file($baseline_mir), 'late_local_loop');
		die "$test: local loop activation changed preserved capacity\n"
			if preserve_count($positive) != preserve_count($baseline);
		$loop = block_body($baseline, 'loop');
		$body = block_body($baseline, 'body');
		for my $name ('index', 'sum') {
			my $home = frame_offset($baseline, $name);
			die "$test: O0 $name baseline lost its frame traffic\n"
				if !defined($home) ||
				   ((defined($loop) ? $loop : '') !~ /\Q$home\E/ &&
				    (defined($body) ? $body : '') !~ /\Q$home\E/);
		}
		my $pressured_baseline = function_body(
			$test, read_file($baseline_mir), 'pressured_local_loop');
		$pressured_loop = block_body($pressured_baseline, 'loop');
		$pressured_body = block_body($pressured_baseline, 'body');
		for my $name ('index', 'sum') {
			my $home = frame_offset($pressured_baseline, $name);
			die "$test: O0 pressured $name baseline lost its frame traffic\n"
				if !defined($home) ||
				   ((defined($pressured_loop) ? $pressured_loop : '') !~
				      /\Q$home\E/ &&
				    (defined($pressured_body) ? $pressured_body : '') !~
				      /\Q$home\E/);
		}
		next;
	}
	if ($test =~ /adjacent-frame-compare-forwarding/) {
		my $positive = function_body($test, $mir, 'pressured_compare');
		die "$test: reducer lost its six-register pressure call\n"
			if $positive !~
				/^\s+call \@make_probe \[args=\((?:[a-z0-9]+,){5}[a-z0-9]+\)/m;
		die "$test: sole-use result was not compared directly from a register\n"
			if $positive !~ /^\s+cmp\.i32 [a-z0-9]+, 17$/m;
		die "$test: sole-use result retained a frame compare\n"
			if $positive =~ /^\s+cmp\.i32 \[rbp[^\]]*\], 17$/m;

		my $guard = function_body($test, $mir, 'multi_use_guard');
		my ($guard_home) = $guard =~
			/^\s+call \@make_probe\b.*?^\s+store\.i32 (\[rbp[^\]]*\]), [a-z0-9]+[ \t]*\n.*?^\s+cmp\.i32 \1, 23$/ms;
		die "$test: multi-use safety value lost its stable frame home\n"
			if !defined($guard_home);

		my $baseline_mir = "$directory/baseline.mir";
		my $baseline_program = "$directory/baseline.program";
		$status = run_command_capture(
			cmd => [$app, '-O0', '--dump-machine-ir', $baseline_mir,
				'-o', $baseline_program, $test],
			stdout => "$directory/baseline-compile.stdout",
			stderr => "$directory/baseline-compile.stderr",
			timeout => 30,
		);
		die "$test: O0 native compile failed\n" .
			read_file("$directory/baseline-compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$baseline_program],
			stdout => "$directory/baseline-program.stdout",
			stderr => "$directory/baseline-program.stderr",
			timeout => 30,
		);
		die "$test: O0 generated program failed with status $run_status\n"
			if $run_status != 0;
		my $baseline = function_body(
			$test, read_file($baseline_mir), 'pressured_compare');
		die "$test: O0 baseline unexpectedly omitted its frame comparison\n"
			if $baseline !~
				/^\s+store\.i32 (\[rbp[^\]]*\]), [a-z0-9]+[ \t]*\n\s+cmp\.i32 \1, 17$/m;
		next;
	}
	if ($test =~ /call-free-callee-save-recoloring/) {
		my $positive = function_body($test, $mir, 'recolor_candidate');
		my $append = block_body($positive, 'append');
		die "$test: reducer lost its call-free multi-store address range\n"
			if !defined($append);
		my ($carrier) = $append =~
			/^\s+store\.i64 \[([a-z0-9]+)\],[^\n]*\n\s+store\.i64 \[\1\+8\],[^\n]*\n\s+store\.i64 \[\1\+16\],/m;
		die "$test: related stores do not share one address carrier\n"
			if !defined($carrier);
		my %caller_saved = map { $_ => 1 }
			qw(rax rcx rdx rsi rdi r8 r9 r10 r11);
		die "$test: call-free address range retained a callee-saved carrier\n"
			if !$caller_saved{$carrier};
		die "$test: reducer lost its exact one-register call annotation\n"
			if $positive !~ /^\s+call \@next_value \[args=\([a-z0-9]+\),/m;

		my $guard = function_body($test, $mir, 'cross_call_guard');
		my ($crossing) = $guard =~
			/^\s+load\.i64 ([a-z0-9]+), \@line\s*\n.*?^\s+call \@next_value\b.*?^\s+call \@next_value\b.*?^\s+add \1,/ms;
		die "$test: safety reducer lost its value spanning two calls\n"
			if !defined($crossing);
		my %callee_saved = map { $_ => 1 } qw(rbx r12 r13 r14 r15);
		die "$test: a call-crossing value was recolored to caller-saved state\n"
			if !$callee_saved{$crossing};
		next;
	}
	if ($test =~ /historical-placement-contracts/) {
		my $free = function_body($test, $mir, 'call_free_branch');
		die "$test: eligible call-free branch value retained a frame home\n"
			if defined(frame_offset($free, 'sum'));
		die "$test: call-free branch added preserved-register capacity\n"
			if preserve_count($free) != 0;

		my $crossing = function_body(
			$test, $mir, 'call_crossing_branch');
		die "$test: call-crossing branch left its value unprotected\n"
			if !defined(frame_offset($crossing, 'sum')) &&
			   preserve_count($crossing) == 0;

		my $tail = function_body(
			$test, $mir, 'post_call_free_capacity');
		die "$test: post-call call-free tail retained a frame home\n"
			if defined(frame_offset($tail, 'tail_one')) ||
			   defined(frame_offset($tail, 'tail_two'));
		die "$test: call-crossing prefix did not exercise preserved pressure\n"
			if preserve_count($tail) != 5;

		my $single = function_body($test, $mir, 'single_use_crossing');
		die "$test: eligible single-use call-crossing value kept a frame home\n"
			if defined(frame_offset($single, 'scaled'));
		die "$test: single-use call-crossing value has no call-safe capacity\n"
			if preserve_count($single) == 0;

		my $floating = function_body(
			$test, $mir, 'fallback_free_float_loop');
		die "$test: fully resident loop value retained an eager fallback home\n"
			if defined(frame_offset($floating, 'stable'));

		my $baseline_mir = "$directory/baseline.mir";
		my $baseline_program = "$directory/baseline.program";
		$status = run_command_capture(
			cmd => [$app, '-O0', '--dump-machine-ir', $baseline_mir,
				'-o', $baseline_program, $test],
			stdout => "$directory/baseline-compile.stdout",
			stderr => "$directory/baseline-compile.stderr",
			timeout => 30,
		);
		die "$test: O0 native compile failed\n" .
			read_file("$directory/baseline-compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$baseline_program],
			stdout => "$directory/baseline-program.stdout",
			stderr => "$directory/baseline-program.stderr",
			timeout => 30,
		);
		die "$test: O0 generated program failed with status $run_status\n"
			if $run_status != 0;
		my $baseline = read_file($baseline_mir);
		my $baseline_single = function_body(
			$test, $baseline, 'single_use_crossing');
		my $baseline_float = function_body(
			$test, $baseline, 'fallback_free_float_loop');
		die "$test: O0 single-use baseline unexpectedly omitted its home\n"
			if !defined(frame_offset($baseline_single, 'scaled'));
		die "$test: O0 loop baseline unexpectedly omitted its home\n"
			if !defined(frame_offset($baseline_float, 'stable'));
		next;
	}
	if ($test =~ /call-free-fast-loop-phi-residency/) {
		my $positive = function_body($test, $mir, 'call_free_fast_loop');
		die "$test: eligible call-free fast loop retained its index frame home\n"
			if defined(frame_offset($positive, 'index'));
		die "$test: eligible call-free fast loop retained its sum frame home\n"
			if defined(frame_offset($positive, 'sum'));
		die "$test: fast-loop residency added or lost preserved capacity\n"
			if preserve_count($positive) != 5;

		my $guard = function_body($test, $mir, 'call_reaching_loop');
		die "$test: call-reaching loop unsafely lost its index frame home\n"
			if !defined(frame_offset($guard, 'index'));
		die "$test: call-reaching loop unsafely lost its sum frame home\n"
			if !defined(frame_offset($guard, 'sum'));
		my $after = block_body($guard, 'after');
		die "$test: negative loop no longer reaches its guarded call\n"
			if !defined($after) || $after !~ /^\s+call \@touch_five\b/m;

		my $baseline_mir = "$directory/baseline.mir";
		my $baseline_program = "$directory/baseline.program";
		$status = run_command_capture(
			cmd => [$app, '-O0', '--dump-machine-ir', $baseline_mir,
				'-o', $baseline_program, $test],
			stdout => "$directory/baseline-compile.stdout",
			stderr => "$directory/baseline-compile.stderr",
			timeout => 30,
		);
		die "$test: O0 native compile failed\n" .
			read_file("$directory/baseline-compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$baseline_program],
			stdout => "$directory/baseline-program.stdout",
			stderr => "$directory/baseline-program.stderr",
			timeout => 30,
		);
		die "$test: O0 generated program failed with status $run_status\n"
			if $run_status != 0;
		my $baseline = function_body(
			$test, read_file($baseline_mir), 'call_free_fast_loop');
		die "$test: O0 baseline unexpectedly applied fast-loop residency\n"
			if !defined(frame_offset($baseline, 'index')) ||
			   !defined(frame_offset($baseline, 'sum'));
		die "$test: O1 changed the fast function's preserved-register count\n"
			if preserve_count($baseline) != preserve_count($positive);
		next;
	}
	if ($test =~ /cyclic-choice-region-residency/) {
		my $positive = function_body($test, $mir, 'cyclic_choice');
		die "$test: eligible iteration-local choice retained a frame home\n"
			if defined(frame_offset($positive, 'choice'));
		my $later = block_body($positive, 'check_one');
		die "$test: choice-region reducer lost its intervening call\n"
			if !defined($later) || $later !~ /^\s+call \@touch\b/m;
		die "$test: choice was not consumed after the intervening call\n"
			if $later !~ /^\s+(?:cmp|test)\.i64\s+[^\n]*,\s*1\s*$/m;

		my $baseline_mir = "$directory/baseline.mir";
		my $baseline_program = "$directory/baseline.program";
		$status = run_command_capture(
			cmd => [$app, '-O0', '--dump-machine-ir', $baseline_mir,
				'-o', $baseline_program, $test],
			stdout => "$directory/baseline-compile.stdout",
			stderr => "$directory/baseline-compile.stderr",
			timeout => 30,
		);
		die "$test: O0 native compile failed\n" .
			read_file("$directory/baseline-compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$baseline_program],
			stdout => "$directory/baseline-program.stdout",
			stderr => "$directory/baseline-program.stderr",
			timeout => 30,
		);
		die "$test: O0 generated program failed with status $run_status\n"
			if $run_status != 0;
		my $baseline = function_body(
			$test, read_file($baseline_mir), 'cyclic_choice');
		die "$test: O0 baseline unexpectedly applied cyclic residency\n"
			if !defined(frame_offset($baseline, 'choice'));
		next;
	}

	my $positive = function_body($test, $mir, 'single_use_chain');
	my $inner = frame_offset($positive, 'inner_value');
	my $result = frame_offset($positive, 'result');
	die "$test: eligible acyclic phi chain retained distinct frame homes\n"
		if defined($inner) && defined($result) && $inner ne $result;
	my $edge = block_body($positive, 'inner_join');
	die "$test: eligible acyclic phi edge retained an identity transfer\n"
		if defined($edge) && $edge =~ /^\s+(?:mov|load|store)(?:\.|\s)/m;

	my $multi = function_body($test, $mir, 'multi_use_guard');
	$inner = frame_offset($multi, 'inner_value');
	$result = frame_offset($multi, 'result');
	die "$test: multi-use phi values unsafely share a frame home\n"
		if defined($inner) && defined($result) && $inner eq $result;

	my $loop = function_body($test, $mir, 'loop_carried_guard');
	my $seed = frame_offset($loop, 'seed');
	my $value = frame_offset($loop, 'value');
	die "$test: loop-carried phi unsafely shares its seed frame home\n"
		if defined($seed) && defined($value) && $seed eq $value;

	my $repeated = function_body($test, $mir, 'repeated_merge_guard');
	$seed = frame_offset($repeated, 'seed');
	my $choice = frame_offset($repeated, 'choice');
	die "$test: repeated acyclic merge unsafely overwrites its invariant\n"
		if defined($seed) && defined($choice) && $seed eq $choice;

	my $local = function_body($test, $mir, 'repeated_local_chain');
	$inner = frame_offset($local, 'inner_value');
	$choice = frame_offset($local, 'choice');
	die "$test: iteration-local phi chain retained distinct frame homes\n"
		if defined($inner) && defined($choice) && $inner ne $choice;
	$edge = block_body($local, 'inner_value_edge');
	die "$test: iteration-local phi edge retained an identity transfer\n"
		if defined($edge) && $edge =~ /^\s+(?:mov|load|store)(?:\.|\s)/m;

	my $call_phi = function_body($test, $mir, 'immediate_call_phi_home');
	my $call_choice = frame_offset($call_phi, 'choice');
	my $call_source = frame_offset($call_phi, 'left_value');
	my $call_left = block_body($call_phi, 'left');
	die "$test: immediate-call reducer lost its pressured incoming call\n"
		if !defined($call_left) || $call_left !~ /^\s+call \@identity\b/m;
	if(defined($call_choice)) {
		die "$test: eligible immediate-call source retained a distinct frame home\n"
			if defined($call_source) && $call_source ne $call_choice;
		die "$test: incoming value was not written directly to the merge home\n"
			if $call_left !~ /^\s+store\.i64 \[rbp\Q$call_choice\E\],/m;
		my ($after_pressure_call) = $call_left =~
			/^\s+call \@identity\b[^\n]*\n(.*?)^\s+jmp \^join$/ms;
		die "$test: immediate-call edge retained a post-call identity transfer\n"
			if !defined($after_pressure_call) ||
			   $after_pressure_call =~ /^\s+(?:mov|load|store)(?:\.|\s)/m;
	}
	my $delayed = function_body(
		$test, $mir, 'non_immediate_call_phi_home');
	my $delayed_source = frame_offset($delayed, 'left_value');
	my $delayed_choice = frame_offset($delayed, 'choice');
	my $delayed_left = block_body($delayed, 'left');
	if(defined($delayed_source) && defined($delayed_choice)) {
		die "$test: non-immediate control unsafely shared incoming homes\n"
			if $delayed_source eq $delayed_choice;
		die "$test: non-immediate control lost its ordinary edge transfer\n"
			if !defined($delayed_left) ||
			   $delayed_left !~ /^\s+load\.i64\b.*\Q$delayed_source\E/m ||
			   $delayed_left !~
			     /^\s+store\.i64 \[rbp\Q$delayed_choice\E\],/m;
	}

	my $invariant = function_body(
		$test, $mir, 'repeated_invariant_call_guard');
	my $stable = frame_offset($invariant, 'stable');
	my $guard_choice = frame_offset($invariant, 'choice');
	die "$test: repeated invariant unsafely donated its frame home\n"
		if defined($stable) && defined($guard_choice) &&
		   $stable eq $guard_choice;

}

print "native structural controls: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
