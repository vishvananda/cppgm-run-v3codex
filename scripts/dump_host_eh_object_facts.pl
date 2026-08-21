#!/usr/bin/perl

use strict;
use warnings;

sub usage
{
	die "Usage: dump_host_eh_object_facts.pl [--plan <plan-file>] <obj1> [<obj2> ...]\n";
}

sub read_command_lines
{
	my (@cmd) = @_;
	open(my $fh, '-|', @cmd) or die "Unable to run @cmd: $!\n";
	my @lines = <$fh>;
	close($fh) or die "Command failed: @cmd\n";
	return @lines;
}

sub read_command_lines_allow_fail
{
	my (@cmd) = @_;
	open(my $fh, '-|', @cmd) or return ();
	my @lines = <$fh>;
	close($fh);
	return @lines;
}

sub unique_sorted
{
	my %seen;
	for my $value (@_)
	{
		$seen{$value} = 1 if defined($value) && $value ne '';
	}
	return sort keys %seen;
}

sub detect_format
{
	my ($path) = @_;
	open(my $fh, '<:raw', $path) or die "Unable to read $path: $!\n";
	read($fh, my $data, 4);
	close($fh) or die "Unable to close $path: $!\n";
	return 'elf' if defined($data) && $data eq "\x7fELF";
	if(defined($data) && length($data) == 4)
	{
		my $little = unpack('V', $data);
		my $big = unpack('N', $data);
		return 'mach-o' if $little == 0xFEEDFACF || $big == 0xFEEDFACF;
	}
	die "Unsupported object format: $path\n";
}

sub canonical_symbol
{
	my ($fmt, $symbol) = @_;
	$symbol = '' if !defined($symbol);
	$symbol =~ s/^[[:space:]]+//;
	$symbol =~ s/[[:space:]]+$//;
	$symbol =~ s/[:,]$//;
	$symbol =~ s/^_// if $fmt eq 'mach-o';
	return $symbol;
}

sub is_host_eh_symbol
{
	my ($symbol) = @_;
	return 1 if $symbol =~ /^__cxa_/;
	return 1 if $symbol eq '__gxx_personality_v0';
	return 1 if $symbol eq '_Unwind_Resume';
	return 1 if $symbol =~ /^_ZTI/;
	return 1 if $symbol =~ /^_ZTS/;
	return 1 if $symbol =~ /^_ZTVN10__cxxabiv1/;
	return 0;
}

sub is_private_eh_symbol
{
	my ($symbol) = @_;
	return 1 if index($symbol, 'cppgm_eh_') >= 0;
	return 1 if index($symbol, 'cppgm_priv_exc_') >= 0;
	return 0;
}

sub classify_section_raw
{
	my ($name) = @_;
	my %mapping = (
		'__TEXT,__text' => 'text',
		'__TEXT,__eh_frame' => 'eh_frame',
		'__TEXT,__gcc_except_tab' => 'gcc_except_table',
		'__DATA,__data' => 'data',
		'__DATA,__const' => 'rodata',
		'__DATA_CONST,__const' => 'rodata',
		'__LD,__compact_unwind' => 'compact_unwind',
		'.text' => 'text',
		'.data' => 'data',
		'.eh_frame' => 'eh_frame',
		'.gcc_except_table' => 'gcc_except_table',
		'.rodata' => 'rodata',
	);
	return $mapping{$name} if exists($mapping{$name});
	return 'text' if $name =~ /^\.text\./;
	return 'data' if $name =~ /^\.data\./;
	return 'rodata' if $name =~ /^\.rodata\./;
	return $name;
}

sub contract_section
{
	my ($name) = @_;
	return 'host_unwind' if $name eq 'compact_unwind' || $name eq 'eh_frame';
	return 'host_lsda' if $name eq 'gcc_except_table';
	return $name;
}

sub classify_section
{
	my ($name) = @_;
	return contract_section(classify_section_raw($name));
}

sub classify_target
{
	my ($fmt, $raw_target) = @_;
	my $target = $raw_target;
	$target = '' if !defined($target);
	$target =~ s/^[[:space:]]+//;
	$target =~ s/[[:space:]]+$//;
	if($fmt eq 'mach-o' && $target =~ /\(([^)]+)\)/)
	{
		return 'section:' . classify_section($1);
	}
	if($target =~ /^\./)
	{
		return 'section:' . classify_section($target);
	}
	return canonical_symbol($fmt, $target);
}

sub parse_nm_output
{
	my ($text, $fmt) = @_;
	my (@defined, @undefined);
	for my $raw_line (split(/\n/, $text))
	{
		my $line = $raw_line;
		$line =~ s/^[[:space:]]+//;
		$line =~ s/[[:space:]]+$//;
		next if $line eq '';
		my @parts = split(/\s+/, $line);
		my ($kind, $symbol);
		if(scalar(@parts) == 2)
		{
			($kind, $symbol) = @parts;
		}
		elsif(scalar(@parts) >= 3)
		{
			($kind, $symbol) = ($parts[-2], $parts[-1]);
		}
		next if !defined($kind) || !defined($symbol);
		$symbol = canonical_symbol($fmt, $symbol);
		if(uc($kind) eq 'U')
		{
			push @undefined, $symbol;
		}
		else
		{
			push @defined, $symbol;
		}
	}
	return ([unique_sorted(@defined)], [unique_sorted(@undefined)]);
}

sub parse_macho_sections
{
	my ($text) = @_;
	my @sections;
	my ($sectname, $segname);
	my $in_section = 0;
	for my $raw_line (split(/\n/, $text))
	{
		my $line = $raw_line;
		$line =~ s/^[[:space:]]+//;
		$line =~ s/[[:space:]]+$//;
		if($line eq 'Section')
		{
			$in_section = 1;
			($sectname, $segname) = (undef, undef);
			next;
		}
		next if !$in_section;
		if($line =~ /^sectname\s+(\S+)/)
		{
			$sectname = $1;
		}
		elsif($line =~ /^segname\s+(\S+)/)
		{
			$segname = $1;
		}
		elsif($line =~ /^(?:addr|size)\s+/)
		{
			if(defined($sectname) && defined($segname))
			{
				push @sections, classify_section("$segname,$sectname");
				$in_section = 0;
			}
		}
	}
	return unique_sorted(@sections);
}

sub parse_elf_sections
{
	my ($text) = @_;
	my @sections;
	for my $raw_line (split(/\n/, $text))
	{
		if($raw_line =~ /^\s*\[\s*\d+\]\s+(\S+)\s+/)
		{
			push @sections, classify_section($1);
		}
	}
	return unique_sorted(@sections);
}

sub normalize_macho_reloc_type
{
	my ($reloc_type) = @_;
	my $value = lc($reloc_type);
	$value =~ s/^x86_64_reloc_//;
	my %mapping = (
		signed => 'pcrel32',
		branch => 'branch32',
		unsigned => 'abs64',
		unsignd => 'abs64',
		got_ld => 'gotpcrel',
		got => 'got',
	);
	return exists($mapping{$value}) ? $mapping{$value} : $value;
}

sub parse_macho_relocations
{
	my ($text) = @_;
	my @relocs;
	my $current_section;
	for my $raw_line (split(/\n/, $text))
	{
		my $line = $raw_line;
		$line =~ s/^[[:space:]]+//;
		$line =~ s/[[:space:]]+$//;
		if($line =~ /^Relocation information \(([^)]+)\)/)
		{
			$current_section = classify_section($1);
			next;
		}
		next if !defined($current_section);
		next if $line eq '' || $line =~ /^address/;
		if($line =~ /^[0-9A-Fa-f]+\s+\S+\s+.+?\s+(?:True|False)\s+(\S+)\s+(?:True|False)\s+(.+)$/)
		{
			push @relocs, join(' ',
				$current_section,
				normalize_macho_reloc_type($1),
				classify_target('mach-o', $2));
		}
	}
	return unique_sorted(@relocs);
}

sub normalize_elf_reloc_type
{
	my ($reloc_type) = @_;
	my %mapping = (
		R_X86_64_PC32 => 'pcrel32',
		R_X86_64_PLT32 => 'branch32',
		R_X86_64_64 => 'abs64',
		R_X86_64_REX_GOTPCRELX => 'gotpcrel',
		R_X86_64_GOTPCRELX => 'gotpcrel',
		R_X86_64_GOTPCREL => 'gotpcrel',
	);
	return exists($mapping{$reloc_type}) ? $mapping{$reloc_type} : lc($reloc_type);
}

sub parse_elf_relocations
{
	my ($text) = @_;
	my @relocs;
	my $current_section;
	for my $raw_line (split(/\n/, $text))
	{
		my $line = $raw_line;
		$line =~ s/^[[:space:]]+//;
		$line =~ s/[[:space:]]+$//;
		if($line =~ /^Relocation section '([^']+)'/)
		{
			my $raw_name = $1;
			if($raw_name =~ /^\.rela(.+)/)
			{
				$raw_name = $1;
			}
			elsif($raw_name =~ /^\.rel(.+)/)
			{
				$raw_name = $1;
			}
			$raw_name = ".$raw_name" if $raw_name !~ /^\./;
			$current_section = classify_section($raw_name);
			next;
		}
		next if !defined($current_section);
		next if $line eq '' || $line =~ /^Offset/;
		my @parts = split(/\s+/, $line);
		next if scalar(@parts) < 5;
		push @relocs, join(' ',
			$current_section,
			normalize_elf_reloc_type($parts[2]),
			classify_target('elf', $parts[4]));
	}
	return unique_sorted(@relocs);
}

sub is_interesting_reloc
{
	my ($section, $target) = @_;
	return 1 if is_host_eh_symbol($target);
	return 1 if is_private_eh_symbol($target);
	return 1 if $target =~ /^section:(?:host_unwind|host_lsda|text)$/;
	return 1 if $section eq 'host_unwind' || $section eq 'host_lsda';
	return 0;
}

sub relocation_contract_facts
{
	my ($section, $kind, $target) = @_;
	my @facts;
	if(is_private_eh_symbol($target))
	{
		push @facts, "private_symbol $target";
	}
	if(is_host_eh_symbol($target))
	{
		if($section eq 'text' && $kind eq 'branch32')
		{
			push @facts, "reloc_call $target";
		}
		else
		{
			push @facts, "reloc_ref $target";
		}
	}
	if($target =~ /^section:(host_unwind|host_lsda|text)$/)
	{
		push @facts, "reloc_section_ref $1";
	}
	return @facts;
}

sub parse_hex_bytes_from_tool_output
{
	my (@lines) = @_;
	my @bytes;
	for my $raw_line (@lines)
	{
		chomp($raw_line);
		$raw_line =~ s/^[[:space:]]+//;
		$raw_line =~ s/[[:space:]]+$//;
		my @tokens = split(/\s+/, $raw_line);
		next if scalar(@tokens) < 2;
		shift @tokens if $tokens[0] =~ /^(?:0x)?[0-9A-Fa-f]{4,16}$/;
		for my $token (@tokens)
		{
			last if $token !~ /^[0-9A-Fa-f]+$/ || length($token) % 2 != 0;
			for(my $i = 0; $i < length($token); $i += 2)
			{
				push @bytes, hex(substr($token, $i, 2));
			}
		}
	}
	return @bytes;
}

sub read_lsda_bytes
{
	my ($fmt, $obj) = @_;
	if($fmt eq 'mach-o')
	{
		return parse_hex_bytes_from_tool_output(
			read_command_lines_allow_fail('otool', '-s', '__TEXT', '__gcc_except_tab', $obj));
	}
	return parse_hex_bytes_from_tool_output(
		read_command_lines_allow_fail('readelf', '-x', '.gcc_except_table', $obj));
}

sub read_uleb128
{
	my ($bytes, $pos) = @_;
	my $result = 0;
	my $shift = 0;
	while($pos < scalar(@{$bytes}))
	{
		my $byte = $bytes->[$pos++];
		$result |= (($byte & 0x7f) << $shift);
		return ($result, $pos) if ($byte & 0x80) == 0;
		$shift += 7;
		last if $shift > 63;
	}
	return;
}

sub read_sleb128
{
	my ($bytes, $pos) = @_;
	my $result = 0;
	my $shift = 0;
	my $byte = 0;
	while($pos < scalar(@{$bytes}))
	{
		$byte = $bytes->[$pos++];
		$result |= (($byte & 0x7f) << $shift);
		$shift += 7;
		last if ($byte & 0x80) == 0;
		return if $shift > 63;
	}
	return if ($byte & 0x80) != 0;
	if($shift < 64 && ($byte & 0x40))
	{
		$result |= -(1 << $shift);
	}
	return ($result, $pos);
}

sub parse_lsda_candidate
{
	my ($bytes, $start) = @_;
	my $size = scalar(@{$bytes});
	return if $start + 4 > $size;
	my $pos = $start;
	return if $bytes->[$pos++] != 0xff;
	my $ttype_encoding = $bytes->[$pos++];
	return if $ttype_encoding != 0xff && $ttype_encoding != 0x9b;
	my $has_type_table = $ttype_encoding != 0xff;
	if($has_type_table)
	{
		my @ttbase = read_uleb128($bytes, $pos);
		return if scalar(@ttbase) != 2;
		$pos = $ttbase[1];
	}
	my $rest_start = $pos;
	return if $pos >= $size || $bytes->[$pos++] != 0x01;
	my @call_site_length = read_uleb128($bytes, $pos);
	return if scalar(@call_site_length) != 2;
	my $call_site_table_length = $call_site_length[0];
	$pos = $call_site_length[1];
	my $call_site_end = $pos + $call_site_table_length;
	return if $call_site_end > $size;

	my %facts;
	$facts{'lsda present'} = 1;
	$facts{'lsda call_site_table'} = 1;
	$facts{'lsda type_table'} = 1 if $has_type_table;
	my @action_offsets;
	my $call_site_count = 0;
	my $covered_end = 0;
	while($pos < $call_site_end)
	{
		my @start_value = read_uleb128($bytes, $pos);
		return if scalar(@start_value) != 2;
		my @length_value = read_uleb128($bytes, $start_value[1]);
		return if scalar(@length_value) != 2;
		my @landing_value = read_uleb128($bytes, $length_value[1]);
		return if scalar(@landing_value) != 2;
		my @action_value = read_uleb128($bytes, $landing_value[1]);
		return if scalar(@action_value) != 2;
		$pos = $action_value[1];
		$facts{'lsda call_site_has_sparse_gap'} = 1
			if $start_value[0] > $covered_end;
		my $entry_end = $start_value[0] + $length_value[0];
		$covered_end = $entry_end if $entry_end > $covered_end;
		$facts{'lsda call_site_starts_at_zero'} = 1
			if $call_site_count == 0 && $start_value[0] == 0;
		$facts{'lsda call_site_has_landingpad'} = 1 if $landing_value[0] != 0;
		$facts{'lsda call_site_has_no_landingpad'} = 1 if $landing_value[0] == 0;
		$facts{'lsda call_site_has_cleanup'} = 1
			if $landing_value[0] != 0 && $action_value[0] == 0;
		if($action_value[0] != 0)
		{
			$facts{'lsda call_site_has_action'} = 1;
			push @action_offsets, $action_value[0];
		}
		++$call_site_count;
	}
	return if $pos != $call_site_end;
	return if $call_site_count == 0;

	my $action_table_start = $call_site_end;
	for my $action (@action_offsets)
	{
		my $action_pos = $action_table_start + $action - 1;
		my %visited;
		while($action_pos >= $action_table_start && $action_pos < $size)
		{
			last if $visited{$action_pos}++;
			my @filter_value = read_sleb128($bytes, $action_pos);
			last if scalar(@filter_value) != 2;
			my $next_field_pos = $filter_value[1];
			my @next_value = read_sleb128($bytes, $next_field_pos);
			last if scalar(@next_value) != 2;
			$facts{'lsda action_has_cleanup'} = 1 if $filter_value[0] == 0;
			$facts{'lsda action_has_catch'} = 1 if $filter_value[0] > 0;
			$facts{'lsda action_has_filter'} = 1 if $filter_value[0] < 0;
			last if $next_value[0] == 0;
			$action_pos = $next_field_pos + $next_value[0];
		}
	}
	return keys %facts;
}

sub dump_lsda_facts
{
	my (@bytes) = @_;
	my %facts;
	for(my $pos = 0; $pos < scalar(@bytes); ++$pos)
	{
		next if $bytes[$pos] != 0xff;
		for my $fact (parse_lsda_candidate(\@bytes, $pos))
		{
			$facts{$fact} = 1;
		}
	}
	return sort keys %facts;
}

sub dump_object_facts
{
	my ($obj) = @_;
	my $fmt = detect_format($obj);
	my (@facts, @private_symbols);

	my $nm_output = join('', read_command_lines('nm', '-g', $obj));
	my ($defined, $undefined) = parse_nm_output($nm_output, $fmt);
	for my $symbol (@{$defined})
	{
		push @facts, "define $symbol" if is_host_eh_symbol($symbol);
		push @private_symbols, $symbol if is_private_eh_symbol($symbol);
	}
	for my $symbol (@{$undefined})
	{
		push @facts, "undef $symbol" if is_host_eh_symbol($symbol);
		push @private_symbols, $symbol if is_private_eh_symbol($symbol);
	}
	for my $symbol (unique_sorted(@private_symbols))
	{
		push @facts, "private_symbol $symbol";
	}

	my (@sections, @relocs);
	if($fmt eq 'mach-o')
	{
		@sections = parse_macho_sections(join('', read_command_lines('otool', '-l', $obj)));
		@relocs = parse_macho_relocations(join('', read_command_lines('otool', '-rv', $obj)));
	}
	else
	{
		@sections = parse_elf_sections(join('', read_command_lines('readelf', '-SW', $obj)));
		@relocs = parse_elf_relocations(join('', read_command_lines('readelf', '-rW', $obj)));
	}

	for my $section (@sections)
	{
		push @facts, "section $section"
			if $section eq 'text' || $section eq 'host_unwind' ||
			   $section eq 'host_lsda';
	}
	for my $reloc (@relocs)
	{
		my ($section, $kind, $target) = split(/\s+/, $reloc, 3);
		push @facts, relocation_contract_facts($section, $kind, $target)
			if is_interesting_reloc($section, $target);
	}
	for my $fact (dump_lsda_facts(read_lsda_bytes($fmt, $obj)))
	{
		push @facts, $fact;
	}
	return unique_sorted(@facts);
}

sub read_plan_objects
{
	my ($plan_file, $object_count) = @_;
	return (1 .. $object_count) if !defined($plan_file) || $plan_file eq '';
	open(my $fh, '<', $plan_file) or die "Unable to read $plan_file: $!\n";
	my @selected;
	while(my $line = <$fh>)
	{
		chomp($line);
		$line =~ s/#.*$//;
		$line =~ s/^[[:space:]]+//;
		$line =~ s/[[:space:]]+$//;
		next if $line eq '';
		if($line eq 'all')
		{
			@selected = (1 .. $object_count);
			next;
		}
		if($line =~ /^object\s+(\d+)$/)
		{
			push @selected, 0 + $1;
			next;
		}
		if($line =~ /^objects\s+(.+)$/)
		{
			for my $value (split(/\s+/, $1))
			{
				die "Invalid object index '$value' in $plan_file\n" if $value !~ /^\d+$/;
				push @selected, 0 + $value;
			}
			next;
		}
		die "Invalid host-EH facts plan line in $plan_file: $line\n";
	}
	close($fh) or die "Unable to close $plan_file: $!\n";
	@selected = (1 .. $object_count) if scalar(@selected) == 0;
	for my $index (@selected)
	{
		die "Object index $index in $plan_file is out of range\n"
			if $index < 1 || $index > $object_count;
	}
	my %seen;
	for my $index (@selected)
	{
		$seen{$index} = 1;
	}
	return sort { $a <=> $b } keys %seen;
}

my $plan_file = '';
while(scalar(@ARGV) != 0 && $ARGV[0] =~ /^--/)
{
	my $arg = shift(@ARGV);
	if($arg eq '--plan')
	{
		usage() if scalar(@ARGV) == 0;
		$plan_file = shift(@ARGV);
		next;
	}
	usage();
}

usage() if scalar(@ARGV) == 0;

my @objs = @ARGV;
for my $index (read_plan_objects($plan_file, scalar(@objs)))
{
	for my $fact (dump_object_facts($objs[$index - 1]))
	{
		print "object $index $fact\n";
	}
}
