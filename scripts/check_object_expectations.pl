#!/usr/bin/perl

use strict;
use warnings;

sub usage
{
	die "Usage: check_object_expectations.pl [--record] <spec-file> <obj1> [<obj2> ...]\n";
}

sub host_os
{
	chomp(my $os = `uname -s 2>/dev/null`);
	$os = lc($os);
	return $os;
}

sub read_command_lines
{
	my (@cmd) = @_;
	open(my $fh, '-|', @cmd) or return ();
	my @lines = <$fh>;
	close($fh);
	return @lines;
}

sub trim_text
{
	my ($text) = @_;
	$text =~ s/^[[:space:]]+//;
	$text =~ s/[[:space:]]+$//;
	return $text;
}

sub canonicalize_symbol
{
	my ($symbol) = @_;
	return '' if !defined($symbol);
	$symbol =~ s/^[[:space:]]+//;
	$symbol =~ s/[[:space:]]+$//;
	$symbol =~ s/[:,]$//;
	$symbol =~ s/^__/_/;
	return $symbol;
}

sub canonical_symbol_matches
{
	my ($symbol, $needle) = @_;
	return 1 if $symbol eq $needle;
	return 1 if $needle eq '_main' && $symbol eq 'main';
	return 0;
}

sub line_has_canonical_symbol
{
	my ($line, $needle) = @_;
	for my $token (split(/\s+/, $line))
	{
		next if $token eq '';
		return 1 if canonical_symbol_matches(canonicalize_symbol($token), $needle);
	}
	return 0;
}

sub line_has_macos_canonical_symbol
{
	my ($line, $needle) = @_;
	return 1 if line_has_canonical_symbol($line, $needle);
	return 0 if $needle =~ /^_/;
	return line_has_canonical_symbol($line, "_$needle");
}

sub read_nm_symbol_entries
{
	my ($obj, $mode) = @_;
	my @cmd = $mode eq 'undefined' ? ('nm', '-u', $obj) : ('nm', $obj);
	my @entries;
	for my $line (read_command_lines(@cmd))
	{
		chomp($line);
		next if $line =~ /^\s*$/;
		if ($mode eq 'undefined')
		{
			my @fields = split(/\s+/, $line);
			my $symbol = $fields[-1];
			if (defined($symbol))
			{
				my $type = scalar(@fields) >= 2 ? $fields[-2] : 'U';
				push @entries, {
					type => $type,
					symbol => canonicalize_symbol($symbol),
					raw_symbol => $symbol,
				};
			}
			next;
		}
		my ($type, $symbol);
		if ($line =~ /^\s*[0-9A-Fa-f]+\s+([A-Za-z?])\s+(\S+)$/)
		{
			($type, $symbol) = ($1, $2);
		}
		elsif ($line =~ /^\s*([A-Za-z?])\s+(\S+)$/)
		{
			($type, $symbol) = ($1, $2);
		}
		next if !defined($type) || !defined($symbol);
		next if uc($type) eq 'U';
		push @entries, {
			type => $type,
			symbol => canonicalize_symbol($symbol),
			raw_symbol => $symbol,
		};
	}
	return @entries;
}

sub read_nm_symbols
{
	my ($obj, $mode) = @_;
	my @symbols = map { $_->{symbol} } read_nm_symbol_entries($obj, $mode);
	return @symbols;
}

sub count_defined_symbols_canonical
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $symbol (read_nm_symbols($obj, 'defined'))
	{
		++$count if canonical_symbol_matches($symbol, $needle);
	}
	return $count;
}

sub count_local_defined_symbols_canonical
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $entry (read_nm_symbol_entries($obj, 'defined'))
	{
		next if !canonical_symbol_matches($entry->{symbol}, $needle);
		++$count if $entry->{type} =~ /^[a-z]$/ && uc($entry->{type}) ne 'U';
	}
	return $count;
}

sub count_weak_symbols_canonical
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $entry (read_nm_symbol_entries($obj, 'defined'))
	{
		next if !canonical_symbol_matches($entry->{symbol}, $needle);
		++$count if $entry->{type} =~ /^[WwVv]$/;
	}
	return $count;
}

sub count_defined_symbols_with_substring
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $entry (read_nm_symbol_entries($obj, 'defined'))
	{
		++$count if index($entry->{symbol}, $needle) >= 0 ||
		            index($entry->{raw_symbol}, $needle) >= 0;
	}
	return $count;
}

sub count_undefined_symbols_canonical
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $symbol (read_nm_symbols($obj, 'undefined'))
	{
		++$count if canonical_symbol_matches($symbol, $needle);
	}
	return $count;
}

sub count_defined_symbols_with_prefix_canonical
{
	my ($obj, $prefix) = @_;
	my $count = 0;
	for my $symbol (read_nm_symbols($obj, 'defined'))
	{
		++$count if index($symbol, $prefix) == 0;
	}
	return $count;
}

sub count_undefined_symbols_with_prefix_canonical
{
	my ($obj, $prefix) = @_;
	my $count = 0;
	for my $symbol (read_nm_symbols($obj, 'undefined'))
	{
		++$count if index($symbol, $prefix) == 0;
	}
	return $count;
}

sub count_macos_relocation_class
{
	my ($obj, $class, $needle) = @_;
	my %kind_for = (
		data_pcrel => 'SIGNED',
		branch_call => 'BRANCH',
		imported_data_got => 'GOT_LD',
	);
	die "Unsupported Mach-O relocation class '$class'\n"
		if !exists($kind_for{$class});
	my $count = 0;
	for my $line (read_command_lines('otool', '-rv', $obj))
	{
		next if index($line, $kind_for{$class}) < 0;
		++$count if line_has_macos_canonical_symbol($line, $needle);
	}
	return $count;
}

sub count_linux_relocation_class
{
	my ($obj, $class, $needle) = @_;
	my %type_re = (
		data_pcrel => qr/\bR_X86_64_PC32\b/,
		branch_call => qr/\bR_X86_64_PLT32\b/,
		imported_data_got => qr/\bR_X86_64_(?:REX_)?GOTPCREL(?:X)?\b/,
	);
	die "Unsupported ELF relocation class '$class'\n"
		if !exists($type_re{$class});
	my $count = 0;
	for my $line (read_command_lines('readelf', '-rW', $obj))
	{
		next if $line !~ $type_re{$class};
		++$count if line_has_canonical_symbol($line, $needle);
	}
	return $count;
}

sub count_relocation_class_canonical
{
	my ($os, $obj, $class, $needle) = @_;
	return count_macos_relocation_class($obj, $class, $needle) if $os eq 'darwin';
	return count_linux_relocation_class($obj, $class, $needle) if $os eq 'linux';
	die "Unsupported host OS for relocation checks: $os\n";
}

sub has_section_named
{
	my ($os, $obj, $needle) = @_;
	if($os eq 'darwin')
	{
		for my $line (read_command_lines('otool', '-l', $obj))
		{
			chomp($line);
			return 1 if trim_text($line) eq "sectname $needle";
		}
		return 0;
	}
	if($os eq 'linux')
	{
		for my $line (read_command_lines('readelf', '-SW', $obj))
		{
			my @fields = split(/\s+/, trim_text($line));
			for my $field (@fields)
			{
				return 1 if $field eq $needle;
			}
		}
		return 0;
	}
	die "Unsupported host OS for section checks: $os\n";
}

sub validate_host_unwind_section
{
	my ($os, $obj) = @_;
	return has_section_named($os, $obj, '__compact_unwind') if $os eq 'darwin';
	return has_section_named($os, $obj, '.eh_frame') if $os eq 'linux';
	die "Unsupported host OS for unwind section checks: $os\n";
}

sub count_macos_relocation_kind_symbol
{
	my ($obj, $kind, $needle) = @_;
	my $count = 0;
	for my $line (read_command_lines('otool', '-rvV', $obj))
	{
		next if index($line, $kind) < 0;
		++$count if line_has_canonical_symbol($line, $needle);
	}
	return $count;
}

sub count_linux_relocation_type_symbol
{
	my ($obj, $type_re, $needle) = @_;
	my $count = 0;
	for my $line (read_command_lines('readelf', '-rW', $obj))
	{
		next if $line !~ $type_re;
		++$count if line_has_canonical_symbol($line, $needle);
	}
	return $count;
}

sub count_linux_defined_tls_symbols
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $line (read_command_lines('readelf', '-Ws', $obj))
	{
		next if !line_has_canonical_symbol($line, $needle);
		next if $line !~ /\bTLS\b/;
		next if $line =~ /\bUND\b/;
		++$count;
	}
	return $count;
}

sub validate_thread_local_import_surface_canonical
{
	my ($os,
	    $obj,
	    $wrapper_symbol,
	    $darwin_global_symbol,
	    $linux_global_symbol) = @_;
	my $canonical_wrapper = canonicalize_symbol($wrapper_symbol);
	my $canonical_darwin_global = canonicalize_symbol($darwin_global_symbol);
	my $canonical_linux_global = canonicalize_symbol($linux_global_symbol);
	my $defined_wrapper = count_defined_symbols_canonical($obj, $canonical_wrapper);
	my $undefined_wrapper = count_undefined_symbols_canonical($obj, $canonical_wrapper);
	if($os eq 'darwin')
	{
		my $defined_global = count_defined_symbols_canonical($obj, $canonical_darwin_global);
		my $undefined_global = count_undefined_symbols_canonical($obj, $canonical_darwin_global);
		return 1 if $undefined_wrapper >= 1 && $defined_global == 0 && $undefined_global == 0;
		return 1 if $defined_wrapper >= 1 &&
			count_undefined_symbols_with_prefix_canonical($obj, '__emutls_v.') >= 1 &&
			count_undefined_symbols_canonical($obj, '__emutls_get_address') >= 1;
		return 0;
	}
	if($os eq 'linux')
	{
		my $defined_global = count_defined_symbols_canonical($obj, $canonical_linux_global);
		my $undefined_global = count_undefined_symbols_canonical($obj, $canonical_linux_global);
		return 1 if $undefined_wrapper >= 1 && $defined_global == 0 && $undefined_global == 0;
		return 1 if $defined_wrapper >= 1 && $defined_global == 0 && $undefined_global >= 1;
		return 0;
	}
	die "Unsupported host OS for TLS import validation: $os\n";
}

sub validate_thread_local_export_surface_canonical
{
	my ($os,
	    $obj,
	    $wrapper_symbol,
	    $darwin_global_symbol,
	    $linux_global_symbol) = @_;
	my $canonical_wrapper = canonicalize_symbol($wrapper_symbol);
	my $canonical_darwin_global = canonicalize_symbol($darwin_global_symbol);
	my $canonical_linux_global = canonicalize_symbol($linux_global_symbol);
	if($os eq 'darwin')
	{
		if (count_defined_symbols_canonical($obj, $canonical_wrapper) != 0 &&
		    count_defined_symbols_canonical($obj, $canonical_darwin_global) != 0 &&
		    has_section_named($os, $obj, '__thread_data') &&
		    has_section_named($os, $obj, '__thread_vars') &&
		    count_macos_relocation_kind_symbol($obj, 'TLV', $canonical_darwin_global) != 0 &&
		    count_macos_relocation_kind_symbol(
			    $obj, 'UNSIGND', canonicalize_symbol('__tlv_bootstrap')) != 0) {
			return 1;
		}
		return 1 if count_defined_symbols_canonical($obj, $canonical_wrapper) != 0 &&
			count_defined_symbols_with_prefix_canonical($obj, '__emutls_v.') != 0 &&
			count_defined_symbols_with_prefix_canonical($obj, '__emutls_t.') != 0 &&
			count_undefined_symbols_canonical($obj, '__emutls_get_address') != 0 &&
			has_section_named($os, $obj, '__const');
		return 0;
	}
	if($os eq 'linux')
	{
		return 0 if count_defined_symbols_canonical($obj, $canonical_wrapper) == 0;
		return 0 if count_linux_defined_tls_symbols($obj, $canonical_linux_global) == 0;
		return 0 if !has_section_named($os, $obj, '.tdata');
		return 0 if count_linux_relocation_type_symbol(
			$obj, qr/\bR_X86_64_TPOFF32\b/, $canonical_linux_global) == 0;
		return 1;
	}
	die "Unsupported host OS for TLS export validation: $os\n";
}

sub count_macos_weak_symbols
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $line (read_command_lines('nm', '-m', $obj))
	{
		next if index($line, $needle) < 0;
		++$count if $line =~ /weak .*external/;
	}
	return $count;
}

sub count_macos_undefined_symbols
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $line (read_command_lines('nm', '-m', $obj))
	{
		next if index($line, $needle) < 0;
		++$count if $line =~ /\(undefined\) external/;
	}
	return $count;
}

sub count_macos_weak_undefined_symbols
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $line (read_command_lines('nm', '-m', $obj))
	{
		next if index($line, $needle) < 0;
		++$count if $line =~ /\(undefined\).*weak .*external/;
	}
	return $count;
}

sub count_linux_weak_symbols
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $line (read_command_lines('readelf', '-Ws', $obj))
	{
		next if index($line, $needle) < 0;
		next if $line !~ /\bWEAK\b/;
		next if $line =~ /\bUND\b/;
		++$count;
	}
	if ($count == 0)
	{
		for my $line (read_command_lines('nm', '-g', $obj))
		{
			next if index($line, $needle) < 0;
			++$count if $line =~ /\sW\s/;
		}
	}
	return $count;
}

sub count_linux_undefined_symbols
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $line (read_command_lines('readelf', '-Ws', $obj))
	{
		next if index($line, $needle) < 0;
		++$count if $line =~ /\bUND\b/;
	}
	if ($count == 0)
	{
		for my $line (read_command_lines('nm', '-u', $obj))
		{
			next if index($line, $needle) < 0;
			++$count;
		}
	}
	return $count;
}

sub count_linux_weak_undefined_symbols
{
	my ($obj, $needle) = @_;
	my $count = 0;
	for my $line (read_command_lines('readelf', '-Ws', $obj))
	{
		next if index($line, $needle) < 0;
		next if $line !~ /\bWEAK\b/;
		next if $line !~ /\bUND\b/;
		++$count;
	}
	if ($count == 0)
	{
		for my $line (read_command_lines('nm', '-u', $obj))
		{
			next if index($line, $needle) < 0;
			++$count if $line =~ /^\s*w\s/;
		}
	}
	return $count;
}

sub count_linux_comdat_groups
{
	my ($obj) = @_;
	my $count = 0;
	for my $line (read_command_lines('readelf', '--section-groups', $obj))
	{
		++$count if $line =~ /COMDAT/;
	}
	if ($count == 0)
	{
		for my $line (read_command_lines('strings', $obj))
		{
			chomp($line);
			++$count if $line =~ /^\.group\./;
		}
	}
	return $count;
}

sub count_linux_comdat_definitions
{
	my ($obj, $needle, $require_relocation) = @_;
	my %symbol_sections;
	for my $line (read_command_lines('readelf', '-Ws', $obj))
	{
		my @fields = split(/\s+/, trim_text($line));
		next if scalar(@fields) < 8 || $fields[0] !~ /^\d+:$/;
		next if $fields[3] ne 'FUNC' || $fields[4] ne 'WEAK';
		next if $fields[6] !~ /^\d+$/;
		next if !canonical_symbol_matches(
			canonicalize_symbol($fields[7]), $needle);
		$symbol_sections{$fields[6]} = 1;
	}
	return 0 if scalar(keys(%symbol_sections)) == 0;

	my $matching_group = 0;
	my $has_body = 0;
	my $has_relocation = 0;
	my $count = 0;
	for my $line (read_command_lines('readelf', '--section-groups', $obj))
	{
		if ($line =~ /COMDAT group section .*\[([^\]]+)\] contains/)
		{
			++$count if $matching_group && $has_body &&
				(!$require_relocation || $has_relocation);
			$matching_group = canonical_symbol_matches(
				canonicalize_symbol($1), $needle);
			$has_body = 0;
			$has_relocation = 0;
			next;
		}
		next if !$matching_group;
		if ($line =~ /^\s*\[\s*(\d+)\]\s+(\S+)/)
		{
			$has_body = 1 if $symbol_sections{$1};
			$has_relocation = 1 if $2 =~ /^\.rela/;
		}
	}
	++$count if $matching_group && $has_body &&
		(!$require_relocation || $has_relocation);
	return $count;
}

sub count_weak_symbols
{
	my ($os, $obj, $needle) = @_;
	return count_macos_weak_symbols($obj, $needle) if $os eq 'darwin';
	return count_linux_weak_symbols($obj, $needle) if $os eq 'linux';
	die "Unsupported host OS for weak symbol checks: $os\n";
}

sub count_undefined_symbols
{
	my ($os, $obj, $needle) = @_;
	return count_macos_undefined_symbols($obj, $needle) if $os eq 'darwin';
	return count_linux_undefined_symbols($obj, $needle) if $os eq 'linux';
	die "Unsupported host OS for undefined symbol checks: $os\n";
}

sub count_weak_undefined_symbols
{
	my ($os, $obj, $needle) = @_;
	return count_macos_weak_undefined_symbols($obj, $needle) if $os eq 'darwin';
	return count_linux_weak_undefined_symbols($obj, $needle) if $os eq 'linux';
	die "Unsupported host OS for weak undefined symbol checks: $os\n";
}

my $record_mode = 0;
if (scalar(@ARGV) >= 1 && $ARGV[0] eq '--record')
{
	$record_mode = 1;
	shift(@ARGV);
}

usage() if scalar(@ARGV) < 2;

my ($spec_file, @objs) = @ARGV;
my $os = host_os();

open(my $spec, '<', $spec_file) or die "Unable to open $spec_file\n";
while (my $line = <$spec>)
{
	chomp($line);
	$line =~ s/\s+$//;
	next if $line =~ /^\s*$/;
	next if $line =~ /^\s*#/;

	my @fields = split(/\s+/, $line);
	my $kind = shift(@fields);
	my $obj_index = shift(@fields);
	die "Invalid expectation line in $spec_file: $line\n"
		if !defined($kind) || !defined($obj_index) || $obj_index !~ /^\d+$/;
	my $obj = $objs[$obj_index - 1];
	die "Expectation references missing object $obj_index in $spec_file\n"
		if !defined($obj);

	if ($kind eq 'weak_symbol')
	{
		my ($needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid weak_symbol expectation in $spec_file: $line\n"
			if !defined($needle) || $min_count !~ /^\d+$/;
		my $count = count_weak_symbols($os, $obj, $needle);
		if ($record_mode)
		{
			die "Expected at least one weak symbol match for '$needle' in object $obj_index; got $count\n"
				if $count == 0;
			print "weak_symbol $obj_index $needle $count\n";
			next;
		}
		die "Expected at least $min_count weak symbol matches for '$needle' in object $obj_index; got $count\n"
			if $count < $min_count;
		print "weak_symbol $obj_index $needle $min_count\n";
		next;
	}

	if ($kind eq 'undefined_symbol')
	{
		my ($needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid undefined_symbol expectation in $spec_file: $line\n"
			if !defined($needle) || $min_count !~ /^\d+$/;
		my $count = count_undefined_symbols($os, $obj, $needle);
		if ($record_mode)
		{
			die "Expected at least one undefined symbol match for '$needle' in object $obj_index; got $count\n"
				if $count == 0;
			print "undefined_symbol $obj_index $needle $count\n";
			next;
		}
		die "Expected at least $min_count undefined symbol matches for '$needle' in object $obj_index; got $count\n"
			if $count < $min_count;
		print "undefined_symbol $obj_index $needle $min_count\n";
		next;
	}

	if ($kind eq 'weak_undefined_symbol')
	{
		my ($needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid weak_undefined_symbol expectation in $spec_file: $line\n"
			if !defined($needle) || $min_count !~ /^\d+$/;
		my $count = count_weak_undefined_symbols($os, $obj, $needle);
		if ($record_mode)
		{
			die "Expected at least one weak undefined symbol match for '$needle' in object $obj_index; got $count\n"
				if $count == 0;
			print "weak_undefined_symbol $obj_index $needle $count\n";
			next;
		}
		die "Expected at least $min_count weak undefined symbol matches for '$needle' in object $obj_index; got $count\n"
			if $count < $min_count;
		print "weak_undefined_symbol $obj_index $needle $min_count\n";
		next;
	}

	if ($kind eq 'duplicate_definition')
	{
		my ($needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid duplicate_definition expectation in $spec_file: $line\n"
			if !defined($needle) || $min_count !~ /^\d+$/;
		my $weak_count = count_weak_symbols($os, $obj, $needle);
		if ($record_mode)
		{
			die "Expected at least one weak duplicate-definition match for '$needle' in object $obj_index; got $weak_count\n"
				if $weak_count == 0;
			if ($os eq 'linux')
			{
				my $group_count = count_linux_comdat_groups($obj);
				die "Expected at least $weak_count COMDAT groups in object $obj_index; got $group_count\n"
					if $group_count < $weak_count;
			}
			print "duplicate_definition $obj_index $needle $weak_count\n";
			next;
		}
		die "Expected at least $min_count weak duplicate-definition matches for '$needle' in object $obj_index; got $weak_count\n"
			if $weak_count < $min_count;
		if ($os eq 'linux')
		{
			my $group_count = count_linux_comdat_groups($obj);
			die "Expected at least $min_count COMDAT groups in object $obj_index; got $group_count\n"
				if $group_count < $min_count;
		}
		print "duplicate_definition $obj_index $needle $min_count\n";
		next;
	}

	if ($kind eq 'comdat_definition')
	{
		my ($needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid comdat_definition expectation in $spec_file: $line\n"
			if !defined($needle) || $min_count !~ /^\d+$/;
		die "comdat_definition is only supported for ELF objects\n"
			if $os ne 'linux';
		my $count = count_linux_comdat_definitions($obj, $needle, 0);
		if ($record_mode)
		{
			die "Expected at least one function-body COMDAT for '$needle' in object $obj_index; got $count\n"
				if $count == 0;
			print "comdat_definition $obj_index $needle $count\n";
			next;
		}
		die "Expected at least $min_count function-body COMDATs for '$needle' in object $obj_index; got $count\n"
			if $count < $min_count;
		print "comdat_definition $obj_index $needle $min_count\n";
		next;
	}

	if ($kind eq 'comdat_relocated_definition')
	{
		my ($needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid comdat_relocated_definition expectation in $spec_file: $line\n"
			if !defined($needle) || $min_count !~ /^\d+$/;
		die "comdat_relocated_definition is only supported for ELF objects\n"
			if $os ne 'linux';
		my $count = count_linux_comdat_definitions($obj, $needle, 1);
		if ($record_mode)
		{
			die "Expected at least one function-and-relocation COMDAT for '$needle' in object $obj_index; got $count\n"
				if $count == 0;
			print "comdat_relocated_definition $obj_index $needle $count\n";
			next;
		}
		die "Expected at least $min_count function-and-relocation COMDATs for '$needle' in object $obj_index; got $count\n"
			if $count < $min_count;
		print "comdat_relocated_definition $obj_index $needle $min_count\n";
		next;
	}

	if ($kind eq 'local_defined_symbol_canonical')
	{
		my ($needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid local_defined_symbol_canonical expectation in $spec_file: $line\n"
			if !defined($needle) || $min_count !~ /^\d+$/;
		my $count = count_local_defined_symbols_canonical($obj, $needle);
		if ($record_mode)
		{
			die "Expected at least one canonical local defined symbol match for '$needle' in object $obj_index; got $count\n"
				if $count == 0;
			print "local_defined_symbol_canonical $obj_index $needle $count\n";
			next;
		}
		die "Expected at least $min_count canonical local defined symbol matches for '$needle' in object $obj_index; got $count\n"
			if $count < $min_count;
		print "local_defined_symbol_canonical $obj_index $needle $min_count\n";
		next;
	}

	if ($kind eq 'absent_weak_symbol_canonical')
	{
		my ($needle) = @fields;
		die "Invalid absent_weak_symbol_canonical expectation in $spec_file: $line\n"
			if !defined($needle);
		my $count = count_weak_symbols_canonical($obj, $needle);
		die "Expected no canonical weak symbol matches for '$needle' in object $obj_index; got $count\n"
			if $count != 0;
		print "absent_weak_symbol_canonical $obj_index $needle 0\n";
		next;
	}

	if ($kind eq 'defined_symbol_canonical')
	{
		my ($needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid defined_symbol_canonical expectation in $spec_file: $line\n"
			if !defined($needle) || $min_count !~ /^\d+$/;
		my $count = count_defined_symbols_canonical($obj, $needle);
		if ($record_mode)
		{
			die "Expected at least one canonical defined symbol match for '$needle' in object $obj_index; got $count\n"
				if $count == 0;
			print "defined_symbol_canonical $obj_index $needle $count\n";
			next;
		}
		die "Expected at least $min_count canonical defined symbol matches for '$needle' in object $obj_index; got $count\n"
			if $count < $min_count;
		print "defined_symbol_canonical $obj_index $needle $min_count\n";
		next;
	}

	if ($kind eq 'undefined_symbol_canonical')
	{
		my ($needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid undefined_symbol_canonical expectation in $spec_file: $line\n"
			if !defined($needle) || $min_count !~ /^\d+$/;
		my $count = count_undefined_symbols_canonical($obj, $needle);
		if ($record_mode)
		{
			die "Expected at least one canonical undefined symbol match for '$needle' in object $obj_index; got $count\n"
				if $count == 0;
			print "undefined_symbol_canonical $obj_index $needle $count\n";
			next;
		}
		die "Expected at least $min_count canonical undefined symbol matches for '$needle' in object $obj_index; got $count\n"
			if $count < $min_count;
		print "undefined_symbol_canonical $obj_index $needle $min_count\n";
		next;
	}

	if ($kind eq 'absent_defined_symbol_canonical')
	{
		my ($needle) = @fields;
		die "Invalid absent_defined_symbol_canonical expectation in $spec_file: $line\n"
			if !defined($needle);
		my $count = count_defined_symbols_canonical($obj, $needle);
		die "Expected no canonical defined symbol matches for '$needle' in object $obj_index; got $count\n"
			if $count != 0;
		print "absent_defined_symbol_canonical $obj_index $needle 0\n";
		next;
	}

	if ($kind eq 'absent_defined_symbol_substring')
	{
		my ($needle) = @fields;
		die "Invalid absent_defined_symbol_substring expectation in $spec_file: $line\n"
			if !defined($needle);
		my $count = count_defined_symbols_with_substring($obj, $needle);
		die "Expected no defined symbol substring matches for '$needle' in object $obj_index; got $count\n"
			if $count != 0;
		print "absent_defined_symbol_substring $obj_index $needle 0\n";
		next;
	}

	if ($kind eq 'absent_undefined_symbol_canonical')
	{
		my ($needle) = @fields;
		die "Invalid absent_undefined_symbol_canonical expectation in $spec_file: $line\n"
			if !defined($needle);
		my $count = count_undefined_symbols_canonical($obj, $needle);
		die "Expected no canonical undefined symbol matches for '$needle' in object $obj_index; got $count\n"
			if $count != 0;
		print "absent_undefined_symbol_canonical $obj_index $needle 0\n";
		next;
	}

	if ($kind eq 'relocation_class_canonical')
	{
		my ($class, $needle, $min_count) = @fields;
		$min_count = 1 if !defined($min_count);
		die "Invalid relocation_class_canonical expectation in $spec_file: $line\n"
			if !defined($class) || !defined($needle) || $min_count !~ /^\d+$/;
		my $count = count_relocation_class_canonical($os, $obj, $class, $needle);
		if ($record_mode)
		{
			die "Expected at least one canonical relocation-class match for '$class' '$needle' in object $obj_index; got $count\n"
				if $count == 0;
			print "relocation_class_canonical $obj_index $class $needle $count\n";
			next;
		}
		die "Expected at least $min_count canonical relocation-class matches for '$class' '$needle' in object $obj_index; got $count\n"
			if $count < $min_count;
		print "relocation_class_canonical $obj_index $class $needle $min_count\n";
		next;
	}

	if ($kind eq 'relocation_class_canonical_exact')
	{
		my ($class, $needle, $expected_count) = @fields;
		die "Invalid relocation_class_canonical_exact expectation in $spec_file: $line\n"
			if !defined($class) || !defined($needle) ||
				!defined($expected_count) || $expected_count !~ /^\d+$/;
		my $count = count_relocation_class_canonical($os, $obj, $class, $needle);
		if ($record_mode)
		{
			print "relocation_class_canonical_exact $obj_index $class $needle $count\n";
			next;
		}
		die "Expected exactly $expected_count canonical relocation-class matches for '$class' '$needle' in object $obj_index; got $count\n"
			if $count != $expected_count;
		print "relocation_class_canonical_exact $obj_index $class $needle $expected_count\n";
		next;
	}

	if ($kind eq 'absent_relocation_class_canonical')
	{
		my ($class, $needle) = @fields;
		die "Invalid absent_relocation_class_canonical expectation in $spec_file: $line\n"
			if !defined($class) || !defined($needle);
		my $count = count_relocation_class_canonical($os, $obj, $class, $needle);
		die "Expected no canonical relocation-class matches for '$class' '$needle' in object $obj_index; got $count\n"
			if $count != 0;
		print "absent_relocation_class_canonical $obj_index $class $needle 0\n";
		next;
	}

	if ($kind eq 'host_unwind_section')
	{
		die "Invalid host_unwind_section expectation in $spec_file: $line\n"
			if scalar(@fields) != 0;
		my $ok = validate_host_unwind_section($os, $obj);
		die "Expected host unwind section in object $obj_index\n" if !$ok;
		print "host_unwind_section $obj_index 1\n";
		next;
	}

	if ($kind eq 'custom_section')
	{
		my ($section_name) = @fields;
		die "Invalid custom_section expectation in $spec_file: $line\n"
			if !defined($section_name) || scalar(@fields) != 1;
		my $ok = has_section_named($os, $obj, $section_name);
		die "Expected section '$section_name' in object $obj_index\n" if !$ok;
		print "custom_section $obj_index $section_name 1\n";
		next;
	}

	if ($kind eq 'absent_custom_section')
	{
		my ($section_name) = @fields;
		die "Invalid absent_custom_section expectation in $spec_file: $line\n"
			if !defined($section_name) || scalar(@fields) != 1;
		my $ok = has_section_named($os, $obj, $section_name);
		die "Expected no section '$section_name' in object $obj_index\n" if $ok;
		print "absent_custom_section $obj_index $section_name 0\n";
		next;
	}

	if ($kind eq 'thread_local_import_surface_canonical')
	{
		my ($wrapper_symbol, $darwin_global_symbol, $linux_global_symbol) = @fields;
		die "Invalid thread_local_import_surface_canonical expectation in $spec_file: $line\n"
			if !defined($wrapper_symbol) || !defined($darwin_global_symbol) ||
			   !defined($linux_global_symbol);
		my $ok = validate_thread_local_import_surface_canonical(
			$os,
			$obj,
			$wrapper_symbol,
			$darwin_global_symbol,
			$linux_global_symbol);
		die "Expected compatible thread_local import surface for '$wrapper_symbol' in object $obj_index\n"
			if !$ok;
		print "thread_local_import_surface_canonical $obj_index " .
		      "$wrapper_symbol $darwin_global_symbol $linux_global_symbol 1\n";
		next;
	}

	if ($kind eq 'thread_local_export_surface_canonical')
	{
		my ($wrapper_symbol, $darwin_global_symbol, $linux_global_symbol) = @fields;
		die "Invalid thread_local_export_surface_canonical expectation in $spec_file: $line\n"
			if !defined($wrapper_symbol) || !defined($darwin_global_symbol) ||
			   !defined($linux_global_symbol);
		my $ok = validate_thread_local_export_surface_canonical(
			$os,
			$obj,
			$wrapper_symbol,
			$darwin_global_symbol,
			$linux_global_symbol);
		die "Expected compatible thread_local export surface for '$wrapper_symbol' in object $obj_index\n"
			if !$ok;
		print "thread_local_export_surface_canonical $obj_index " .
		      "$wrapper_symbol $darwin_global_symbol $linux_global_symbol 1\n";
		next;
	}

	die "Unknown expectation kind '$kind' in $spec_file\n";
}
close($spec);
