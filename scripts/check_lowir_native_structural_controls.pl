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

sub named_frame_offset
{
	my ($body, $name) = @_;
	return $1 if $body =~
		/^\s+(?:temp \%|slot \$)\Q$name\E -> \[rbp([+-]\d+)\]/m;
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

sub preserved_registers
{
	my ($body) = @_;
	return split(/\s+/, $1) if $body =~ /^\s+preserve\s+([^\n]+)$/m;
	return ();
}

sub stat_value
{
	my ($test, $stats, $name) = @_;
	return 0 + $1 if $stats =~ /(?:^|\s)\Q$name\E=(\d+)(?:\s|$)/;
	die "$test: --stats omitted $name\n";
}

sub code_alignment
{
	my ($body) = @_;
	return 0 + $1 if $body =~ /^\s+code_alignment\s+(\d+)$/m;
	return 2;
}

sub object_symbol_value
{
	my ($test, $symbols, $name) = @_;
	for my $line (split(/\n/, $symbols)) {
		return hex($1) if $line =~
			/^\s*\d+:\s+([0-9a-fA-F]+)\s+\d+\s+FUNC\b.*\s\@?\Q$name\E$/;
	}
	die "$test: object has no $name function symbol\n";
}

if (scalar(@ARGV) != 3)
{
	die "Usage: check_lowir_native_structural_controls.pl <lowir2native> <cppgm++> <test-or-directory>\n";
}

	my ($app, $driver, $root) = @ARGV;
my @tests = collect_tests($root,
	qr/(?:acyclic-phi-frame-home|cyclic-choice-region-residency|call-result-plan-reservation|call-free-fast-loop-phi-residency|call-free-callee-save-recoloring|adjacent-frame-compare-forwarding|local-loop-phi-activation|historical-placement-contracts|conditional-fallthrough-layout|deferred-carrier-lifetime-reset|o3-large-function-alignment|medium-copy-direct-chunks|composite-copy-pointer-preservation|selected-parameter-index-home|adjacent-integer-normalizations|o3-common-path-memory|stable-prefix-boundary-replay).*\.t$/);
die "No native structural-control tests found under $root\n" if !@tests;

for my $test (@tests)
{
	my $directory = tempdir('lowir-native-structural-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $mir_path = "$directory/test.mir";
	my $program = "$directory/test.program";
	my $level = $test =~
		/(?:o3-large-function-alignment|composite-copy-pointer-preservation|o3-common-path-memory)/
		? '-O3' : $test =~
		/(?:conditional-fallthrough-layout|deferred-carrier-lifetime-reset|call-result-plan-reservation|adjacent-integer-normalizations)/
		? '-O2' : '-O1';
	my $status = run_command_capture(
		cmd => [$app, $level, '--dump-machine-ir', $mir_path,
			'-o', $program, $test],
		stdout => "$directory/compile.stdout",
		stderr => "$directory/compile.stderr",
		timeout => 30,
	);
	die "$test: optimized native compile failed\n" .
		read_file("$directory/compile.stderr") if $status != 0;
	my $run_status = run_command_capture(
		cmd => [$program],
		stdout => "$directory/program.stdout",
		stderr => "$directory/program.stderr",
		timeout => 30,
	);
	die "$test: generated program failed with status $run_status\n" if $run_status != 0;

	my $mir = read_file($mir_path);
	if ($test =~ /stable-prefix-boundary-replay/) {
		for my $requested_level (qw(-O0 -O1 -O2 -O3)) {
			my $name = substr($requested_level, 1);
			my $level_mir = "$directory/$name.mir";
			my $level_program = "$directory/$name.program";
			$status = run_command_capture(
				cmd => [$app, $requested_level, '--dump-machine-ir',
					$level_mir, '-o', $level_program, $test],
				stdout => "$directory/$name.compile.stdout",
				stderr => "$directory/$name.compile.stderr",
				timeout => 30,
			);
			die "$test: $requested_level native replay failed\n" .
				read_file("$directory/$name.compile.stderr") if $status != 0;
			my $level_text = read_file($level_mir);
			function_body($test, $level_text, 'prefix_query');
			$run_status = run_command_capture(
				cmd => [$level_program],
				stdout => "$directory/$name.program.stdout",
				stderr => "$directory/$name.program.stderr",
				timeout => 30,
			);
			die "$test: $requested_level replay behavior returned " .
				"$run_status\n" if $run_status != 0;
		}
		my $driver_input = "$directory/driver.lowir";
		copy($test, $driver_input) or
			die "$test: unable to prepare driver replay: $!\n";
		my $driver_program = "$directory/driver.program";
		$status = run_command_capture(
			cmd => [$driver, '-O3', '-o', $driver_program, $driver_input],
			stdout => "$directory/driver.compile.stdout",
			stderr => "$directory/driver.compile.stderr",
			timeout => 60,
		);
		die "$test: O3 driver replay failed\n" .
			read_file("$directory/driver.compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$driver_program],
			stdout => "$directory/driver.program.stdout",
			stderr => "$directory/driver.program.stderr",
			timeout => 30,
		);
		die "$test: O3 driver replay behavior returned $run_status\n"
			if $run_status != 0;
		next;
	}
	if ($test =~ /o3-common-path-memory/) {
		my %level_mir;
		for my $request (['O0', '-O0'], ['O1', '-O1'],
			['O2', '-O2'], ['O3', '-O3']) {
			my ($name, $requested_level) = @$request;
			my $level_mir = "$directory/$name.mir";
			my $level_program = "$directory/$name.program";
			$status = run_command_capture(
				cmd => [$app, $requested_level, '--dump-machine-ir',
					$level_mir, '-o', $level_program, $test],
				stdout => "$directory/$name.compile.stdout",
				stderr => "$directory/$name.compile.stderr",
				timeout => 30,
			);
			die "$test: $requested_level native compile failed\n" .
				read_file("$directory/$name.compile.stderr") if $status != 0;
			$run_status = run_command_capture(
				cmd => [$level_program],
				stdout => "$directory/$name.program.stdout",
				stderr => "$directory/$name.program.stderr",
				timeout => 30,
			);
			die "$test: $requested_level generated program failed with status " .
				"$run_status\n" if $run_status != 0;
			$level_mir{$name} = read_file($level_mir);
		}

		for my $name (qw(O0 O1 O2)) {
			my $direct = function_body(
				$test, $level_mir{$name}, 'copy_adjacent_fields');
			die "$test: $name unexpectedly fused adjacent scalar fields\n"
				if $direct =~ /^\s+copy_bytes 16x1,/m;
			die "$test: $name lost the adjacent scalar-copy baseline\n"
				if scalar(() = $direct =~ /^\s+load\.i64\b/mg) < 2 ||
				   scalar(() = $direct =~ /^\s+store\.i64\b/mg) < 2;

			my $staged = function_body(
				$test, $level_mir{$name}, 'copy_through_private_stages');
			die "$test: $name unexpectedly fused private scalar stages\n"
				if $staged =~ /^\s+copy_bytes 16x1,/m;
			my $first_home = named_frame_offset($staged, 'first_stage');
			my $second_home = named_frame_offset($staged, 'second_stage');
			die "$test: $name lost the two private-stage controls\n"
				if !defined($first_home) || !defined($second_home) ||
				   $first_home eq $second_home ||
				   $staged !~ /^\s+store\.i64 \[rbp\Q$first_home\E\],/m ||
				   $staged !~ /^\s+load\.i64 \w+, \[rbp\Q$first_home\E\](?:\s|$)/m ||
				   $staged !~ /^\s+store\.i64 \[rbp\Q$second_home\E\],/m ||
				   $staged !~ /^\s+load\.i64 \w+, \[rbp\Q$second_home\E\](?:\s|$)/m;

			my $guard = function_body(
				$test, $level_mir{$name}, 'guarded_private_stage');
			my $home = named_frame_offset($guard, 'stage');
			my $entry = block_body($guard, 'entry');
			die "$test: $name lost the guarded private stage\n"
				if !defined($home) || !defined($entry) ||
				   $entry !~
					/^\s+load\.ptr (\w+),[^\n]*\n\s+store\.ptr \[rbp\Q$home\E\], \1[^\n]*\n\s+load\.ptr \w+, \[rbp\Q$home\E\](?:\s|$)/m;
		}

		for my $symbol (qw(copy_adjacent_fields
			copy_through_private_stages)) {
			my $body = function_body($test, $level_mir{O3}, $symbol);
			my @copies = $body =~
				/^\s+(copy_bytes 16x1,[^\n]+)$/mg;
			die "$test: O3 did not form exactly one direct 16-byte copy in $symbol\n"
				if scalar(@copies) != 1;
			my ($destination_reg, $destination_offset,
				$source_reg, $source_offset) = $copies[0] =~
				/copy_bytes 16x1, \[(\w+)([+-]\d+)?\], \[(\w+)([+-]\d+)?\]/;
			die "$test: O3 copy in $symbol lost same-object direct ranges\n"
				if !defined($destination_reg) || !defined($source_reg) ||
				   $destination_reg ne $source_reg;
			$destination_offset = 0 if !defined($destination_offset);
			$source_offset = 0 if !defined($source_offset);
			die "$test: O3 copy in $symbol did not keep disjoint 16-byte ranges\n"
				if abs($destination_offset - $source_offset) < 16;
			die "$test: O3 copy in $symbol lost its direct, pointer-preserving encoding\n"
				if $copies[0] !~ /\[encoding=direct_chunks\]/ ||
				   $copies[0] !~ /\[preserve_pointers\]/;
			die "$test: O3 copy in $symbol lost the fused source location\n"
				if $copies[0] !~ /!dbg\(common_path\.cpp, \d+, 5\)$/;
		}

		for my $symbol (qw(copy_overlap_guard copy_different_bases
			copy_volatile_guard copy_reused_scalar_guard
			copy_shared_stage_guard)) {
			my $body = function_body($test, $level_mir{O3}, $symbol);
			die "$test: O3 fused unsafe or unproved scalar copies in $symbol\n"
				if $body =~ /^\s+copy_bytes 16x1,/m;
		}

		my $guard = function_body(
			$test, $level_mir{O3}, 'guarded_private_stage');
		my $home = named_frame_offset($guard, 'stage');
		my $entry = block_body($guard, 'entry');
		my $consume = block_body($guard, 'consume');
		my $bypass = block_body($guard, 'bypass');
		die "$test: O3 lost the guarded-stage relationship\n"
			if !defined($home) || !defined($entry) ||
			   !defined($consume) || !defined($bypass);
		my ($carrier) = $entry =~
			/^\s+load\.ptr (\w+),[^\n]*\n\s+test\.(?:i64|ptr) \1, \1[^\n]*$/m;
		die "$test: O3 did not compare the defining carrier directly\n"
			if !defined($carrier);
		die "$test: O3 direct guard test lost its source location\n"
			if $entry !~
				/^\s+test\.(?:i64|ptr) \Q$carrier\E, \Q$carrier\E !dbg\(common_path\.cpp, 55, 5\)$/m;
		die "$test: O3 still materialized the private stage before the guard\n"
			if $entry =~ /\[rbp\Q$home\E\]/;
		die "$test: O3 did not materialize the stage on the sole consuming arm\n"
			if $consume !~
				/^\s+store\.ptr \[rbp\Q$home\E\], \Q$carrier\E !dbg\(common_path\.cpp, 53, 5\)\n\s+load\.ptr \w+, \[rbp\Q$home\E\] !dbg\(common_path\.cpp, 61, 5\)$/m;
		die "$test: O3 materialized the private stage on the bypass arm\n"
			if $bypass =~ /\[rbp\Q$home\E\]/;

		for my $symbol (qw(guarded_volatile_stage guarded_escaping_stage)) {
			my $body = function_body($test, $level_mir{O3}, $symbol);
			my $guard_home = named_frame_offset($body, 'stage');
			my $guard_entry = block_body($body, 'entry');
			die "$test: O3 sank an observable or escaping stage in $symbol\n"
				if !defined($guard_home) || !defined($guard_entry) ||
				   $guard_entry !~
					/^\s+store\.ptr \[rbp\Q$guard_home\E\], \w+\n\s+load\.ptr \w+, \[rbp\Q$guard_home\E\]/m;
		}

		my $driver_input = "$directory/test.lowir";
		copy($test, $driver_input) or
			die "$test: unable to prepare driver replay: $!\n";
		my $object = "$directory/driver-O3.o";
		$status = run_command_capture(
			cmd => [$driver, '-c', '-O3', '-o', $object, $driver_input],
			stdout => "$directory/driver.compile.stdout",
			stderr => "$directory/driver.compile.stderr",
			timeout => 30,
		);
		die "$test: O3 driver replay failed\n" .
			read_file("$directory/driver.compile.stderr") if $status != 0;
		for my $symbol (qw(copy_adjacent_fields
			copy_through_private_stages)) {
			my $disassembly = "$directory/$symbol.disassembly";
			$status = run_command_capture(
				cmd => ['objdump', '-d', '-Mintel',
					"--disassemble=$symbol", $object],
				stdout => $disassembly,
				stderr => "$disassembly.stderr",
				timeout => 30,
			);
			die "$test: O3 driver $symbol disassembly failed\n"
				if $status != 0;
			my $code = read_file($disassembly);
			die "$test: O3 driver $symbol did not encode one vector transfer\n"
				if scalar(() = $code =~ /\bmovdqu\b/g) != 2;
		}
		next;
	}
	if ($test =~ /composite-copy-pointer-preservation/) {
		my %level_mir;
		for my $request (['O0', '-O0'], ['O1', '-O1'],
			['O2', '-O2'], ['O3', '-O3']) {
			my ($name, $requested_level) = @$request;
			my $level_mir = "$directory/$name.mir";
			my $level_program = "$directory/$name.program";
			$status = run_command_capture(
				cmd => [$app, $requested_level, '--dump-machine-ir',
					$level_mir, '-o', $level_program, $test],
				stdout => "$directory/$name.compile.stdout",
				stderr => "$directory/$name.compile.stderr",
				timeout => 30,
			);
			die "$test: $requested_level native compile failed\n" .
				read_file("$directory/$name.compile.stderr") if $status != 0;
			$run_status = run_command_capture(
				cmd => [$level_program],
				stdout => "$directory/$name.program.stdout",
				stderr => "$directory/$name.program.stderr",
				timeout => 30,
			);
			die "$test: $requested_level generated program failed with status " .
				"$run_status\n" if $run_status != 0;
			$level_mir{$name} = read_file($level_mir);
		}

		for my $name (qw(O0 O1 O2)) {
			my $composite = function_body(
				$test, $level_mir{$name}, 'composite_move');
			my $frame = function_body(
				$test, $level_mir{$name}, 'frame_composite_move');
			die "$test: $name unexpectedly enabled pointer-preserving copies\n"
				if $composite =~ /\[preserve_pointers\]/ ||
				   $frame =~ /\[preserve_pointers\]/;
		}

		my $o3_composite = function_body(
			$test, $level_mir{O3}, 'composite_move');
		my @fixed_copies =
			$o3_composite =~ /^\s+copy_bytes\s+\d+x\d+,[^\n]+$/mg;
		die "$test: composite reducer lost its fixed-copy boundaries\n"
			if scalar(@fixed_copies) < 2;
		my @preserved_fixed = grep { /\[preserve_pointers\]/ } @fixed_copies;
		die "$test: O3 did not limit fixed-copy preservation to direct parameters\n"
			if scalar(@preserved_fixed) != 1;
		die "$test: O3 did not serialize explicit preserved dynamic operands\n"
			if $o3_composite !~
				/^\s+copy_bytes_dynamic,\s+[^,\n]+,\s+[^,\n]+,\s+[^\n]+\[preserve_pointers\]/m;
		my $o2_composite = function_body(
			$test, $level_mir{O2}, 'composite_move');
		die "$test: O3 did not reduce composite-move preserved-register pressure\n"
			if preserve_count($o3_composite) >= preserve_count($o2_composite);

		my $o3_frame = function_body(
			$test, $level_mir{O3}, 'frame_composite_move');
		die "$test: preserved dynamic copy lost its direct frame address\n"
			if $o3_frame !~
				/^\s+copy_bytes_dynamic,[^\n]*\[rbp[^\]]*\][^\n]*\[preserve_pointers\][^\n]*\[address_operands=\d+\]/m;
		my $o2_frame = function_body(
			$test, $level_mir{O2}, 'frame_composite_move');
		die "$test: O3 did not reduce frame-composite preservation pressure\n"
			if preserve_count($o3_frame) >= preserve_count($o2_frame);

		my $dynamic_only = function_body(
			$test, $level_mir{O3}, 'dynamic_only');
		die "$test: dynamic-only guard did not retain the ordinary copy form\n"
			if $dynamic_only !~ /^\s+copy_bytes_dynamic\s*$/m ||
			   $dynamic_only =~ /\[preserve_pointers\]/;
		my $used = function_body(
			$test, $level_mir{O3}, 'used_result_guard');
		die "$test: used-result guard did not retain its ordinary call\n"
			if $used !~ /^\s+call\s+\@memory_copy\b/m ||
			   $used =~ /^\s+copy_bytes_dynamic/m;

		my $driver_input = "$directory/test.lowir";
		copy($test, $driver_input) or
			die "$test: unable to prepare driver replay: $!\n";
		my $object = "$directory/driver-O3.o";
		$status = run_command_capture(
			cmd => [$driver, '-c', '-O3', '-o', $object, $driver_input],
			stdout => "$directory/driver.compile.stdout",
			stderr => "$directory/driver.compile.stderr",
			timeout => 30,
		);
		die "$test: O3 driver replay failed\n" .
			read_file("$directory/driver.compile.stderr") if $status != 0;
		for my $symbol (qw(composite_move frame_composite_move)) {
			my $disassembly = "$directory/$symbol.disassembly";
			$status = run_command_capture(
				cmd => ['objdump', '-d', '-Mintel',
					"--disassemble=$symbol", $object],
				stdout => $disassembly,
				stderr => "$disassembly.stderr",
				timeout => 30,
			);
			die "$test: O3 driver $symbol disassembly failed\n"
				if $status != 0;
			die "$test: O3 driver $symbol lost its dynamic native copy\n"
				if read_file($disassembly) !~ /\brep\s+movs/;
		}
		next;
	}
	if ($test =~ /selected-parameter-index-home/) {
		my %level_mir;
		for my $request (['O0', '-O0'], ['O1', '-O1'],
			['O2', '-O2'], ['O3', '-O3']) {
			my ($name, $requested_level) = @$request;
			my $level_mir = "$directory/$name.mir";
			my $level_program = "$directory/$name.program";
			$status = run_command_capture(
				cmd => [$app, $requested_level, '--dump-machine-ir',
					$level_mir, '-o', $level_program, $test],
				stdout => "$directory/$name.compile.stdout",
				stderr => "$directory/$name.compile.stderr",
				timeout => 30,
			);
			die "$test: $requested_level native compile failed\n" .
				read_file("$directory/$name.compile.stderr") if $status != 0;
			$run_status = run_command_capture(
				cmd => [$level_program],
				stdout => "$directory/$name.program.stdout",
				stderr => "$directory/$name.program.stderr",
				timeout => 30,
			);
			die "$test: $requested_level generated program failed with status " .
				"$run_status\n" if $run_status != 0;
			$level_mir{$name} = read_file($level_mir);
		}

		for my $name (qw(O1 O2 O3)) {
			my $body = function_body(
				$test, $level_mir{$name}, 'copy_then_index');
			my ($source_register) =
				$body =~ /^\s+param\s+%source\s+->\s+(\w+)\s+:\s+ptr$/m;
			die "$test: $name lost the source parameter ABI binding\n"
				if !defined($source_register);
			my ($prefix, $index_register) = $body =~
				/\A(.*?)^\s+lea\s+\w+,\s*\[(\w+)\+144\]\s*$/ms;
			die "$test: $name lost the post-copy constant-index address\n"
				if !defined($index_register);
			if ($index_register ne $source_register) {
				my $direct = $prefix =~
					/^\s+mov\s+\Q$index_register\E,\s*\Q$source_register\E\s*$/m;
				my $through_frame = 0;
				while ($prefix =~
					/^\s+store\.ptr\s+(\[rbp[^\]]*\]),\s*\Q$source_register\E\s*$/mg) {
					my $home = $1;
					if ($prefix =~
						/^\s+load\.ptr\s+\Q$index_register\E,\s*\Q$home\E\s*$/m) {
						$through_frame = 1;
						last;
					}
				}
				die "$test: $name used an uninitialized selected parameter home\n"
					if !$direct && !$through_frame;
			}
		}

		my $driver_input = "$directory/test.lowir";
		copy($test, $driver_input) or
			die "$test: unable to prepare driver replay: $!\n";
		for my $name (qw(O1 O2 O3)) {
			my $driver_program = "$directory/driver-$name.program";
			$status = run_command_capture(
				cmd => [$driver, "-$name", '-o', $driver_program,
					$driver_input],
				stdout => "$directory/driver-$name.compile.stdout",
				stderr => "$directory/driver-$name.compile.stderr",
				timeout => 30,
			);
			die "$test: $name driver replay failed\n" .
				read_file("$directory/driver-$name.compile.stderr")
				if $status != 0;
			$run_status = run_command_capture(
				cmd => [$driver_program],
				stdout => "$directory/driver-$name.program.stdout",
				stderr => "$directory/driver-$name.program.stderr",
				timeout => 30,
			);
			die "$test: $name driver output failed with status $run_status\n"
				if $run_status != 0;
		}
		next;
	}
	if ($test =~ /adjacent-integer-normalizations/) {
		my %level_mir;
		for my $request (['O0', '-O0'], ['O1', '-O1'],
			['O2', '-O2'], ['O3', '-O3']) {
			my ($name, $requested_level) = @$request;
			my $level_mir = "$directory/$name.mir";
			my $level_program = "$directory/$name.program";
			my $level_stderr = "$directory/$name.compile.stderr";
			$status = run_command_capture(
				cmd => [$app, $requested_level, '--dump-machine-ir', $level_mir,
					'-o', $level_program, $test],
				stdout => "$directory/$name.compile.stdout",
				stderr => $level_stderr,
				timeout => 30,
			);
			die "$test: $requested_level native compile failed\n" .
				read_file($level_stderr) if $status != 0;
			$run_status = run_command_capture(
				cmd => [$level_program],
				stdout => "$directory/$name.program.stdout",
				stderr => "$directory/$name.program.stderr",
				timeout => 30,
			);
			die "$test: $requested_level generated program failed with status " .
				"$run_status\n" if $run_status != 0;
			$level_mir{$name} = read_file($level_mir);
		}

		for my $name (qw(O0 O1)) {
			my $load = function_body(
				$test, $level_mir{$name}, 'reinterpreted_unsigned_load');
			die "$test: $name lost the adjacent signed-load normalization baseline\n"
				if $load !~
					/^\s+load\.i8\s+(\w+),[^\n]*\n\s+zext\.i8\s+\1[^\n]*$/m;
			my $signed_load = function_body(
				$test, $level_mir{$name}, 'reinterpreted_signed_load');
			die "$test: $name lost the adjacent unsigned-load normalization baseline\n"
				if $signed_load !~
					/^\s+load\.u8\s+(\w+),[^\n]*\n\s+sext\.i8\s+\1[^\n]*$/m;
			my $zero_chain = function_body(
				$test, $level_mir{$name}, 'zero_then_signed_wider');
			die "$test: $name lost the zero-then-signed baseline chain\n"
				if $zero_chain !~
					/^\s+zext\.i8\s+(\w+)[^\n]*\n\s+sext\.i32\s+\1[^\n]*$/m;
			my $signed_chain = function_body(
				$test, $level_mir{$name}, 'signed_then_signed_wider');
			die "$test: $name lost the repeated signed baseline chain\n"
				if $signed_chain !~
					/^\s+sext\.i8\s+(\w+)[^\n]*\n\s+sext\.i32\s+\1[^\n]*$/m;
		}

		for my $name (qw(O2 O3)) {
			my $load = function_body(
				$test, $level_mir{$name}, 'reinterpreted_unsigned_load');
			die "$test: $name did not select an unsigned narrow load\n"
				if $load !~ /^\s+load\.u8\s+\w+,[^\n]*!dbg\(/m;
			die "$test: $name retained the subsumed load normalization\n"
				if $load =~
					/^\s+load\.i8\s+(\w+),[^\n]*\n\s+zext\.i8\s+\1[^\n]*$/m;
			my $signed_load = function_body(
				$test, $level_mir{$name}, 'reinterpreted_signed_load');
			die "$test: $name did not select a signed narrow load\n"
				if $signed_load !~ /^\s+load\.i8\s+\w+,[^\n]*!dbg\(/m;
			die "$test: $name retained the subsumed signed-load normalization\n"
				if $signed_load =~
					/^\s+load\.u8\s+(\w+),[^\n]*\n\s+sext\.i8\s+\1[^\n]*$/m;
			my $zero_chain = function_body(
				$test, $level_mir{$name}, 'zero_then_signed_wider');
			die "$test: $name retained a wider normalization after zero extension\n"
				if $zero_chain =~
					/^\s+zext\.i8\s+(\w+)[^\n]*\n\s+sext\.i32\s+\1[^\n]*$/m;
			die "$test: $name lost debug metadata on the zero-extension survivor\n"
				if $zero_chain !~ /^\s+zext\.i8\s+\w+[^\n]*!dbg\(/m;
			my $signed_chain = function_body(
				$test, $level_mir{$name}, 'signed_then_signed_wider');
			die "$test: $name retained a repeated signed normalization\n"
				if $signed_chain =~
					/^\s+sext\.i8\s+(\w+)[^\n]*\n\s+sext\.i32\s+\1[^\n]*$/m;
			die "$test: $name lost debug metadata on the sign-extension survivor\n"
				if $signed_chain !~ /^\s+sext\.i8\s+\w+[^\n]*!dbg\(/m;
			my $signed_guard = function_body(
				$test, $level_mir{$name}, 'signed_then_unsigned_guard');
			die "$test: $name removed a value-changing signed-to-unsigned chain\n"
				if $signed_guard !~
					/^\s+sext\.i8\s+(\w+)\n\s+zext\.i32\s+\1$/m;
			my $intervening = function_body(
				$test, $level_mir{$name}, 'intervening_use_guard');
			die "$test: $name crossed an intervening observable store\n"
				if $intervening !~
					/^\s+zext\.i8\s+(\w+)\n\s+store\.u32\s+[^\n]*,\s*\1\n\s+sext\.i32\s+\1$/m;
		}

		my $driver_input = "$directory/test.lowir";
		copy($test, $driver_input) or
			die "$test: unable to prepare driver replay: $!\n";
		my $object = "$directory/driver-O2.o";
		$status = run_command_capture(
			cmd => [$driver, '-c', '-O2', '-o', $object, $driver_input],
			stdout => "$directory/driver.compile.stdout",
			stderr => "$directory/driver.compile.stderr",
			timeout => 30,
		);
		die "$test: O2 driver replay failed\n" .
			read_file("$directory/driver.compile.stderr") if $status != 0;
		my $disassembly = "$directory/load.disassembly";
		$status = run_command_capture(
			cmd => ['objdump', '-d', '-Mintel',
				'--disassemble=reinterpreted_unsigned_load', $object],
			stdout => $disassembly,
			stderr => "$disassembly.stderr",
			timeout => 30,
		);
		die "$test: O2 driver normalization disassembly failed\n"
			if $status != 0;
		die "$test: O2 driver did not encode the selected unsigned load\n"
			if read_file($disassembly) !~ /\bmovzx\s+\w+,BYTE PTR \[/;
		my $signed_disassembly = "$directory/signed-load.disassembly";
		$status = run_command_capture(
			cmd => ['objdump', '-d', '-Mintel',
				'--disassemble=reinterpreted_signed_load', $object],
			stdout => $signed_disassembly,
			stderr => "$signed_disassembly.stderr",
			timeout => 30,
		);
		die "$test: O2 signed driver normalization disassembly failed\n"
			if $status != 0;
		die "$test: O2 driver did not encode the selected signed load\n"
			if read_file($signed_disassembly) !~ /\bmovsx\s+\w+,BYTE PTR \[/;
		next;
	}
	if ($test =~ /medium-copy-direct-chunks/) {
		my %level_mir;
		my %level_stats;
		for my $request (['O0', '-O0'], ['O1', '-O1'],
			['O2', '-O2'], ['O3', '-O3']) {
			my ($name, $requested_level) = @$request;
			my $level_mir = "$directory/$name.mir";
			my $level_program = "$directory/$name.program";
			my $level_stderr = "$directory/$name.compile.stderr";
			$status = run_command_capture(
				cmd => [$app, $requested_level, '--stats',
					'--dump-machine-ir', $level_mir,
					'-o', $level_program, $test],
				stdout => "$directory/$name.compile.stdout",
				stderr => $level_stderr,
				timeout => 30,
			);
			die "$test: $requested_level native compile failed\n" .
				read_file($level_stderr) if $status != 0;
			$run_status = run_command_capture(
				cmd => [$level_program],
				stdout => "$directory/$name.program.stdout",
				stderr => "$directory/$name.program.stderr",
				timeout => 30,
			);
			die "$test: $requested_level generated program failed with status " .
				"$run_status\n" if $run_status != 0;
			$level_mir{$name} = read_file($level_mir);
			$level_stats{$name} = read_file($level_stderr);
		}

		for my $name (qw(O0 O1 O2 O3)) {
			my $medium = function_body(
				$test, $level_mir{$name}, 'medium_copy');
			my $small = function_body(
				$test, $level_mir{$name}, 'small_copy_control');
			my $large = function_body(
				$test, $level_mir{$name}, 'large_copy_control');
			die "$test: $name medium reducer lost its weak 61-byte copy\n"
				if $medium !~ /^\s+copy_bytes 61x1, /m;
			die "$test: $name small control lost its 32-byte copy\n"
				if $small !~ /^\s+copy_bytes 32x1, /m;
			die "$test: $name large control lost its 65-byte copy\n"
				if $large !~ /^\s+copy_bytes 65x1, /m;
			my $selected =
				$medium =~ /\[encoding=direct_chunks\]/ ? 1 : 0;
			die "$test: $name changed the compact medium-copy baseline\n"
				if ($name eq 'O0' || $name eq 'O1') && $selected;
			die "$test: $name did not serialize the optimized medium-copy encoding\n"
				if ($name eq 'O2' || $name eq 'O3') && !$selected;
			die "$test: $name marked the already-small control as medium\n"
				if $small =~ /\[encoding=direct_chunks\]/;
			die "$test: $name marked the oversized control as medium\n"
				if $large =~ /\[encoding=direct_chunks\]/;
			my $count = stat_value($test, $level_stats{$name},
				'machine_opt_medium_copy_chunks');
			die "$test: $name reported optimized medium-copy work\n"
				if ($name eq 'O0' || $name eq 'O1') && $count != 0;
			die "$test: $name did not report its one selected medium copy\n"
				if ($name eq 'O2' || $name eq 'O3') && $count != 1;
		}

		my $driver_input = "$directory/test.lowir";
		copy($test, $driver_input) or
			die "$test: unable to prepare driver replay: $!\n";
		my %driver_medium_rep_moves;
		my %driver_medium_vector_moves;
		my %driver_large_rep_moves;
		for my $name (qw(O0 O1 O2 O3)) {
			my $object = "$directory/driver-$name.o";
			$status = run_command_capture(
				cmd => [$driver, '-c', "-$name", '-o', $object,
					$driver_input],
				stdout => "$object.compile.stdout",
				stderr => "$object.compile.stderr",
				timeout => 30,
			);
			die "$test: $name driver replay failed\n" .
				read_file("$object.compile.stderr") if $status != 0;
			for my $symbol (qw(medium_copy large_copy_control)) {
				my $disassembly = "$object.$symbol.disassembly";
				$status = run_command_capture(
					cmd => ['objdump', '-d', '-Mintel',
						"--disassemble=$symbol", $object],
					stdout => $disassembly,
					stderr => "$disassembly.stderr",
					timeout => 30,
				);
				die "$test: $name driver $symbol disassembly failed\n"
					if $status != 0;
				my $code = read_file($disassembly);
				if($symbol eq 'medium_copy') {
					$driver_medium_rep_moves{$name} =
						scalar(() = $code =~ /\brep\s+movs/g);
					$driver_medium_vector_moves{$name} =
						scalar(() = $code =~ /\bmovdqu\b/g);
				} else {
					$driver_large_rep_moves{$name} =
						scalar(() = $code =~ /\brep\s+movs/g);
				}
			}
		}
		for my $name (qw(O0 O1)) {
			die "$test: driver $name medium copy lost compact string encoding\n"
				if $driver_medium_rep_moves{$name} != 1;
		}
		for my $name (qw(O2 O3)) {
			die "$test: driver $name medium copy retained string encoding\n"
				if $driver_medium_rep_moves{$name} != 0;
			die "$test: driver $name medium copy used no vector chunks\n"
				if $driver_medium_vector_moves{$name} == 0;
			die "$test: driver $name oversized control lost compact fallback\n"
				if $driver_large_rep_moves{$name} != 1;
		}
		next;
	}
	if ($test =~ /o3-large-function-alignment/) {
		my $large = function_body(
			$test, $mir, 'large_alignment_candidate');
		my $small = function_body(
			$test, $mir, 'small_alignment_control');
		my $large_block = block_body($large, 'entry');
		my $small_block = block_body($small, 'entry');
		my $large_count = scalar(() =
			(defined($large_block) ? $large_block : '') =~ /^\s{4}\S/mg);
		my $small_count = scalar(() =
			(defined($small_block) ? $small_block : '') =~ /^\s{4}\S/mg);
		die "$test: large-function reducer no longer reaches the documented threshold\n"
			if $large_count < 64;
		die "$test: small-function control unexpectedly reaches the threshold\n"
			if $small_count >= 64;
		die "$test: O3 did not request 16-byte large-function alignment\n"
			if code_alignment($large) != 16;
		die "$test: O3 over-aligned the small function\n"
			if code_alignment($small) != 2;

		my $baseline_mir = "$directory/baseline.mir";
		my $baseline_program = "$directory/baseline.program";
		$status = run_command_capture(
			cmd => [$app, '-O2', '--dump-machine-ir', $baseline_mir,
				'-o', $baseline_program, $test],
			stdout => "$directory/baseline-compile.stdout",
			stderr => "$directory/baseline-compile.stderr",
			timeout => 30,
		);
		die "$test: O2 native compile failed\n" .
			read_file("$directory/baseline-compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$baseline_program],
			stdout => "$directory/baseline-program.stdout",
			stderr => "$directory/baseline-program.stderr",
			timeout => 30,
		);
		die "$test: O2 generated program failed with status $run_status\n"
			if $run_status != 0;
		my $baseline = read_file($baseline_mir);
		my $baseline_large = function_body(
			$test, $baseline, 'large_alignment_candidate');
		die "$test: O2 unexpectedly requested strong function alignment\n"
			if code_alignment($baseline_large) != 2;

		my $driver_input = "$directory/test.lowir";
		copy($test, $driver_input) or
			die "$test: unable to prepare driver replay: $!\n";
		my $o2_object = "$directory/o2.o";
		my $o3_object = "$directory/o3.o";
		for my $request (['-O2', $o2_object], ['-O3', $o3_object]) {
			my ($driver_level, $object) = @$request;
			$status = run_command_capture(
				cmd => [$driver, '-c', $driver_level, '-o', $object,
					$driver_input],
				stdout => "$directory/driver-$driver_level.stdout",
				stderr => "$directory/driver-$driver_level.stderr",
				timeout => 30,
			);
			die "$test: $driver_level driver replay failed\n" .
				read_file("$directory/driver-$driver_level.stderr")
				if $status != 0;
		}
		my @symbol_tables;
		for my $object ($o2_object, $o3_object) {
			my $symbols_path = "$object.symbols";
			$status = run_command_capture(
				cmd => ['readelf', '-Ws', $object],
				stdout => $symbols_path,
				stderr => "$symbols_path.stderr",
				timeout => 30,
			);
			die "$test: object symbol inspection failed\n" if $status != 0;
			push @symbol_tables, read_file($symbols_path);
		}
		my $o2_value = object_symbol_value(
			$test, $symbol_tables[0], 'large_alignment_candidate');
		my $o3_value = object_symbol_value(
			$test, $symbol_tables[1], 'large_alignment_candidate');
		die "$test: O2 layout control accidentally aligned the large function\n"
			if $o2_value % 16 == 0;
		die "$test: relocatable writer lost O3 large-function alignment\n"
			if $o3_value % 16 != 0;
		next;
	}
	if ($test =~ /call-result-plan-reservation/) {
		my $future = function_body($test, $mir, 'future_claimed_region');
		die "$test: an earlier call result blocked a future cyclic result plan\n"
			if defined(frame_offset($future, 'choice'));
		my $future_later = block_body($future, 'check_one');
		die "$test: future-plan control lost its intervening call\n"
			if !defined($future_later) ||
			   $future_later !~ /^\s+call \@touch\b/m;

		my $six = function_body($test, $mir, 'six_argument_region');
		die "$test: completed call arguments forced a planned result home\n"
			if defined(frame_offset($six, 'choice'));
		my $six_choose = block_body($six, 'choose');
		die "$test: pressure control lost its six-register call\n"
			if !defined($six_choose) ||
			   $six_choose !~
				/^\s+call \@read_choice_six \[args=\([^,]+,[^,]+,[^,]+,[^,]+,[^,]+,[^)]+\)/m;

		my $baseline_mir = "$directory/baseline.mir";
		my $baseline_program = "$directory/baseline.program";
		$status = run_command_capture(
			cmd => [$app, '-O1', '--dump-machine-ir', $baseline_mir,
				'-o', $baseline_program, $test],
			stdout => "$directory/baseline-compile.stdout",
			stderr => "$directory/baseline-compile.stderr",
			timeout => 30,
		);
		die "$test: O1 native compile failed\n" .
			read_file("$directory/baseline-compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$baseline_program],
			stdout => "$directory/baseline-program.stdout",
			stderr => "$directory/baseline-program.stderr",
			timeout => 30,
		);
		die "$test: O1 generated program failed with status $run_status\n"
			if $run_status != 0;
		my $baseline = read_file($baseline_mir);
		my $baseline_future = function_body(
			$test, $baseline, 'future_claimed_region');
		my $baseline_six = function_body(
			$test, $baseline, 'six_argument_region');
		die "$test: O1 control no longer distinguishes future plan reservation\n"
			if !defined(frame_offset($baseline_future, 'choice'));
		die "$test: O1 control no longer distinguishes call-pressure fallback\n"
			if !defined(frame_offset($baseline_six, 'choice'));
		next;
	}
	if ($test =~ /deferred-carrier-lifetime-reset/) {
		my $positive = function_body($test, $mir, 'carrier_lifetimes');
		my @choices = $positive =~
			/^\s+call \@read_choice\b[^\n]*\n\s+mov ([a-z0-9]+), rax$/mg;
		die "$test: reducer lost its two cyclic choice results\n"
			if scalar(@choices) != 2;
		die "$test: a completed carrier lifetime still blocked register reuse\n"
			if $choices[0] ne $choices[1];
		die "$test: pressure setup lost its deferred address consumers\n"
			if scalar(() = $positive =~
				/^\s+load\.i64 [a-z0-9]+, \[[a-z0-9]+\+8\]$/mg) < 4;

		my $baseline_mir = "$directory/baseline.mir";
		my $baseline_program = "$directory/baseline.program";
		$status = run_command_capture(
			cmd => [$app, '-O1', '--dump-machine-ir', $baseline_mir,
				'-o', $baseline_program, $test],
			stdout => "$directory/baseline-compile.stdout",
			stderr => "$directory/baseline-compile.stderr",
			timeout => 30,
		);
		die "$test: O1 native compile failed\n" .
			read_file("$directory/baseline-compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$baseline_program],
			stdout => "$directory/baseline-program.stdout",
			stderr => "$directory/baseline-program.stderr",
			timeout => 30,
		);
		die "$test: O1 generated program failed with status $run_status\n"
			if $run_status != 0;
		my $baseline = function_body(
			$test, read_file($baseline_mir), 'carrier_lifetimes');
		my @baseline_choices = $baseline =~
			/^\s+call \@read_choice\b[^\n]*\n\s+mov ([a-z0-9]+), rax$/mg;
		die "$test: O1 control no longer distinguishes carrier reuse\n"
			if scalar(@baseline_choices) != 2 ||
			   $baseline_choices[0] eq $baseline_choices[1];
		die "$test: lifetime reset changed preserved-register capacity\n"
			if preserve_count($positive) != preserve_count($baseline);
		next;
	}
	if ($test =~ /conditional-fallthrough-layout/) {
		my $trace = function_body($test, $mir, 'pure_trace');
		die "$test: O2 did not place the unconditional trace successor next\n"
			if $trace !~
				/^\s+block \^entry\s*\n.*?^\s+block \^done\s*\n.*?^\s+block \^cold\s*$/ms;
		my $trace_entry = block_body($trace, 'entry');
		die "$test: O2 retained the unconditional trace jump\n"
			if !defined($trace_entry) || $trace_entry =~ /^\s+jmp\b/m;

		my $guarded = function_body(
			$test, $mir, 'conditional_fallthrough');
		die "$test: O2 moved the established conditional fallthrough\n"
			if $guarded !~
				/^\s+block \^guard\s*\n.*?^\s+block \^hot\s*\n/ms;
		my $guard = block_body($guarded, 'guard');
		die "$test: guarded block lost its conditional transfer\n"
			if !defined($guard) || $guard !~ /^\s+j[a-z]+ \^other$/m;
		die "$test: guarded block retained an avoidable unconditional jump\n"
			if $guard =~ /^\s+jmp\b/m;
		next;
	}
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
		die "$test: odd-save reducer lost its call-free address ranges\n"
			if !defined($append);
		my ($carrier) = $append =~
			/^\s+store\.i64 \[([a-z0-9]+)\],[^\n]*\n.*?^\s+store\.i64 \[\1\+8\],[^\n]*\n.*?^\s+store\.i64 \[\1\+16\],/ms;
		die "$test: related stores do not share one address carrier\n"
			if !defined($carrier);
		my %caller_saved = map { $_ => 1 }
			qw(rax rcx rdx rsi rdi r8 r9 r10 r11);
		my @odd_carriers = $append =~ /^\s+lea ([a-z0-9]+), \[/mg;
		die "$test: odd-save reducer lost its two independent address ranges\n"
			if scalar(@odd_carriers) != 2 ||
			   $odd_carriers[0] eq $odd_carriers[1];
		for my $odd_carrier (@odd_carriers) {
			die "$test: odd-save call-free range retained a callee-saved carrier\n"
				if !$caller_saved{$odd_carrier};
		}
		die "$test: reducer lost its exact one-register call annotation\n"
			if $positive !~ /^\s+call \@next_value \[args=\([a-z0-9]+\),/m;

		my $even = function_body(
			$test, $mir, 'even_save_recolor_candidate');
		my $even_append = block_body($even, 'append_column');
		my ($even_carrier) = defined($even_append) ?
			($even_append =~ /^\s+lea ([a-z0-9]+), \[/m) : ();
		die "$test: even-save call-free address was not kept in caller-saved state\n"
			if !defined($even_carrier) || !$caller_saved{$even_carrier};
		die "$test: odd pair did not remove its additional save capacity\n"
			if preserve_count($positive) == 0 ||
			   preserve_count($positive) != preserve_count($even);

		my $guard = function_body($test, $mir, 'cross_call_guard');
		my ($crossing) = $guard =~
			/^\s+load\.i64 ([a-z0-9]+), \@line\s*\n.*?^\s+call \@next_value\b.*?^\s+call \@next_value\b.*?^\s+add \1,/ms;
		die "$test: safety reducer lost its value spanning two calls\n"
			if !defined($crossing);
		my %callee_saved = map { $_ => 1 } qw(rbx r12 r13 r14 r15);
		die "$test: a call-crossing value was recolored to caller-saved state\n"
			if !$callee_saved{$crossing};

		my %level_mir;
		my %level_stats;
		for my $request (['O2', '-O2'], ['O3', '-O3']) {
			my ($name, $requested_level) = @$request;
			my $level_mir = "$directory/$name.mir";
			my $level_program = "$directory/$name.program";
			my $level_stderr = "$directory/$name.compile.stderr";
			$status = run_command_capture(
				cmd => [$app, $requested_level, '--stats',
					'--dump-machine-ir', $level_mir,
					'-o', $level_program, $test],
				stdout => "$directory/$name.compile.stdout",
				stderr => $level_stderr,
				timeout => 30,
			);
			die "$test: $requested_level native compile failed\n" .
				read_file($level_stderr) if $status != 0;
			$run_status = run_command_capture(
				cmd => [$level_program],
				stdout => "$directory/$name.program.stdout",
				stderr => "$directory/$name.program.stderr",
				timeout => 30,
			);
			die "$test: $requested_level generated program failed with status " .
				"$run_status\n" if $run_status != 0;
			$level_mir{$name} = read_file($level_mir);
			$level_stats{$name} = read_file($level_stderr);
		}

		for my $symbol ('recolor_candidate',
			'even_save_recolor_candidate') {
			my $o1 = function_body($test, $mir, $symbol);
			my $o2 = function_body($test, $level_mir{O2}, $symbol);
			my $o3 = function_body($test, $level_mir{O3}, $symbol);
			die "$test: O2 changed the established preservation policy for $symbol\n"
				if join(' ', preserved_registers($o2)) ne
				   join(' ', preserved_registers($o1));
			die "$test: O3 did not remove one proven preservation color from $symbol\n"
				if preserve_count($o3) == 0 ||
				   preserve_count($o3) >= preserve_count($o2);

			my %o3_preserved = map { $_ => 1 } preserved_registers($o3);
			my @dropped = grep { !$o3_preserved{$_} }
				preserved_registers($o2);
			die "$test: O3 preservation reduction has no eliminated source color in $symbol\n"
				if !@dropped;
			for my $dropped (@dropped) {
				die "$test: eliminated source color remains in $symbol\n"
					if $o3 =~ /\b\Q$dropped\E\b/;
			}
		}

		my $o2_guard = function_body(
			$test, $level_mir{O2}, 'cross_call_guard');
		my $o3_guard = function_body(
			$test, $level_mir{O3}, 'cross_call_guard');
		die "$test: O3 changed preservation for a call-crossing range\n"
			if join(' ', preserved_registers($o3_guard)) ne
			   join(' ', preserved_registers($o2_guard));
		my ($o3_crossing) = $o3_guard =~
			/^\s+load\.i64 ([a-z0-9]+), \@line\s*\n.*?^\s+call \@next_value\b.*?^\s+call \@next_value\b.*?^\s+add \1,/ms;
		die "$test: O3 safety reducer lost its value spanning two calls\n"
			if !defined($o3_crossing);
		die "$test: O3 exposed a call-crossing value to call clobbers\n"
			if !$callee_saved{$o3_crossing};

		my %region_carrier;
		for my $name (qw(O2 O3)) {
			my $region = function_body($test, $level_mir{$name},
				'even_save_recolor_candidate');
			my $append = block_body($region, 'append');
			my ($carrier) = defined($append) ? ($append =~
				/^\s+load\.i64 ([a-z0-9]+), \@head\s*\n.*?^\s+imul \1, 24$/ms) : ();
			die "$test: $name reducer lost its edge-live scaled carrier\n"
				if !defined($carrier);
			for my $successor (qw(append_column append_alternate)) {
				my $body = block_body($region, $successor);
				die "$test: $name $successor does not consume the shared carrier\n"
					if !defined($body) ||
					   $body !~ /^\s+lea [a-z0-9]+, \[[^\n]*\b\Q$carrier\E\b[^\n]*\]$/m;
			}
			$region_carrier{$name} = $carrier;
		}
		die "$test: O2 lost the callee-saved edge-live control\n"
			if !$callee_saved{$region_carrier{O2}};
		die "$test: O3 did not recolor the connected call-free region\n"
			if !$caller_saved{$region_carrier{O3}};

		for my $field (qw(machine_opt_block_recolor_candidates
			machine_opt_block_recolor_registers
			machine_opt_block_recolor_blocks)) {
			die "$test: O2 unexpectedly ran region-local callee-save recoloring\n"
				if stat_value($test, $level_stats{O2}, $field) != 0;
			die "$test: O3 did not report bounded region-local recoloring work\n"
				if stat_value($test, $level_stats{O3}, $field) == 0;
		}
		die "$test: O3 reported fewer rewritten blocks than eliminated colors\n"
			if stat_value($test, $level_stats{O3},
				'machine_opt_block_recolor_blocks') <
			   stat_value($test, $level_stats{O3},
				'machine_opt_block_recolor_registers');
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

	my %phi_level_mir;
	for my $request (['O2', '-O2'], ['O3', '-O3']) {
		my ($name, $requested_level) = @$request;
		my $level_mir = "$directory/phi-$name.mir";
		my $level_program = "$directory/phi-$name.program";
		$status = run_command_capture(
			cmd => [$app, $requested_level, '--dump-machine-ir',
				$level_mir, '-o', $level_program, $test],
			stdout => "$directory/phi-$name.compile.stdout",
			stderr => "$directory/phi-$name.compile.stderr",
			timeout => 30,
		);
		die "$test: $requested_level native compile failed\n" .
			read_file("$directory/phi-$name.compile.stderr") if $status != 0;
		$run_status = run_command_capture(
			cmd => [$level_program],
			stdout => "$directory/phi-$name.program.stdout",
			stderr => "$directory/phi-$name.program.stderr",
			timeout => 30,
		);
		die "$test: $requested_level generated program failed with status " .
			"$run_status\n" if $run_status != 0;
		$phi_level_mir{$name} = read_file($level_mir);
	}

	my $o2_transfer = function_body(
		$test, $phi_level_mir{O2}, 'last_transfer_chain');
	my $o3_transfer = function_body(
		$test, $phi_level_mir{O3}, 'last_transfer_chain');
	my $o2_transfer_source = frame_offset($o2_transfer, 'inner_value');
	my $o2_transfer_result = frame_offset($o2_transfer, 'result');
	my $o3_transfer_source = frame_offset($o3_transfer, 'inner_value');
	my $o3_transfer_result = frame_offset($o3_transfer, 'result');
	die "$test: last-transfer reducer lost its two O2 frame homes\n"
		if !defined($o2_transfer_source) || !defined($o2_transfer_result) ||
		   $o2_transfer_source eq $o2_transfer_result;
	die "$test: O3 did not reuse a merge home after its final edge transfer\n"
		if !defined($o3_transfer_source) || !defined($o3_transfer_result) ||
		   $o3_transfer_source ne $o3_transfer_result;
	my $o3_transfer_edge = block_body(
		$o3_transfer, 'inner_value_edge');
	die "$test: O3 final-transfer edge retained an identity move\n"
		if !defined($o3_transfer_edge) ||
		   $o3_transfer_edge =~ /^\s+(?:mov|load|store)(?:\.|\s)/m;
	my $o3_transfer_use = block_body($o3_transfer, 'inner_join');
	die "$test: final-transfer source lost its earlier observable use\n"
		if !defined($o3_transfer_use) ||
		   $o3_transfer_use !~ /^\s+store\.i64 \@transfer_observed,/m;

	my $o3_multi = function_body(
		$test, $phi_level_mir{O3}, 'multi_use_guard');
	my $o3_multi_source = frame_offset($o3_multi, 'inner_value');
	my $o3_multi_result = frame_offset($o3_multi, 'result');
	die "$test: O3 reused a source home that remains live after transfer\n"
		if defined($o3_multi_source) && defined($o3_multi_result) &&
		   $o3_multi_source eq $o3_multi_result;

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
	my $o3_invariant = function_body(
		$test, $phi_level_mir{O3}, 'repeated_invariant_call_guard');
	my $o3_stable = frame_offset($o3_invariant, 'stable');
	my $o3_guard_choice = frame_offset($o3_invariant, 'choice');
	die "$test: O3 final-transfer proof overwrote a loop invariant\n"
		if defined($o3_stable) && defined($o3_guard_choice) &&
		   $o3_stable eq $o3_guard_choice;

}

print "native structural controls: PASS (" . scalar(@tests) . "/" .
	scalar(@tests) . ")\n";
