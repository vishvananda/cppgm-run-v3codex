#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path);
use FindBin;

my $root = abs_path("$FindBin::Bin/..");

sub read_file
{
	my ($path) = @_;
	open(my $fh, '<', $path) or die "Unable to read $path: $!\n";
	local $/;
	my $data = <$fh>;
	close($fh) or die "Unable to close $path: $!\n";
	return defined($data) ? $data : '';
}

sub sorted_keys
{
	my ($values) = @_;
	return sort keys %$values;
}

my $model = read_file("$root/dev/src/lowir/model/program.h");
my $parser = read_file("$root/dev/src/lowir/io/parse.cpp");
my $serializer = read_file("$root/dev/src/lowir/io/serialize.cpp");
my $documentation = read_file("$root/pa13/lowir.md");
my $ledger_path = "$root/doc/lowir-contract-ledger.tsv";
my $ledger = read_file($ledger_path);

my @errors;
my @rows;
my %row_identity;
my @lines = split /\n/, $ledger;
my $header = shift @lines;
push @errors, "ledger header does not name the eight contract columns"
	if !defined($header) || $header ne
		"category\tsurface\tproducer\tconsumer\towner\tproperty_test\tdisposition\tevidence";

for (my $index = 0; $index < scalar(@lines); ++$index)
{
	next if $lines[$index] eq '';
	my @fields = split /\t/, $lines[$index], -1;
	my $line_number = $index + 2;
	if (scalar(@fields) != 8)
	{
		push @errors, "ledger line $line_number has " . scalar(@fields) .
			" fields instead of 8";
		next;
	}
	my %row;
	@row{qw(category surface producer consumer owner property disposition evidence)} =
		@fields;
	$row{line} = $line_number;
	my $identity = "$row{category}\t$row{surface}";
	push @errors, "duplicate ledger surface at line $line_number: $identity"
		if $row_identity{$identity}++;
	push @errors, "unresolved ledger disposition '$row{disposition}' at line $line_number"
		if $row{disposition} ne 'keep' && $row{disposition} ne 'remove';
	push @rows, \%row;
}

my @retained = grep { $_->{disposition} eq 'keep' } @rows;
for my $row (@retained)
{
	push @errors, "retained $row->{category} '$row->{surface}' has no semantic producer"
		if $row->{producer} eq '' || $row->{producer} eq 'none' ||
			$row->{producer} eq 'removed' ||
			$row->{producer} eq 'explicit LowIR only';
	push @errors, "retained $row->{category} '$row->{surface}' has no non-transport consumer"
		if $row->{consumer} eq '' || $row->{consumer} eq 'none' ||
			$row->{consumer} =~ /^transport\b/;

	for my $owner (split /;/, $row->{owner})
	{
		push @errors, "retained $row->{category} '$row->{surface}' has missing owner $owner"
			if $owner eq '' || !-f "$root/$owner";
	}
	for my $property (split /;/, $row->{property})
	{
		push @errors, "retained $row->{category} '$row->{surface}' has missing property $property"
			if $property eq '' || $property eq 'MISSING' ||
				!-f "$root/$property";
	}
}

# These prefixes are the human-reviewed set of model enums that can represent a
# public LowIR text choice. Internal presentation and entry-policy enums are
# deliberately outside this census. Values whose only meaning is the canonical
# omitted/default state have no public spelling and are removed below.
my %public_enum;
while ($model =~ /\b((?:LTK|LOP|SR|LLM|SBM|PPM|PALM|CAM|CFXM|CUM|CRM|GSM|IPK|ITEM|INIT|IK|OP)_[A-Z0-9_]+)\b/g)
{
	$public_enum{$1} = 1;
}

my %omitted_or_internal = map { $_ => 1 } qw(
	LTK_INVALID LOP_NONE SR_NONE LLM_DEFAULT SBM_DEFAULT PPM_DIRECT
	PALM_DEFAULT CAM_FIXED CFXM_DEFAULT CUM_DEFAULT CRM_DEFAULT GSM_DEFAULT
	IPK_NONE
);
delete @public_enum{keys %omitted_or_internal};

# lowir_operation_text() and lowir_type_text() are shared parser/serializer
# helpers that happen to be implemented in lowir_parse.cpp. Limit the added
# serializer evidence to those two function bodies: treating the entire parser
# as serializer evidence would hide a newly parsed but unserialized value.
my $shared_text_helpers = '';
if ($parser =~ /(const char \* lowir_operation_text\b.*?)(?=\nbool operator==)/s)
{
	$shared_text_helpers .= $1;
}
else
{
	push @errors, "cannot locate shared LowIR operation serializer helper";
}
if ($parser =~ /(std::string lowir_type_text\b.*?)(?=\nbool InstructionDebugLocation::present)/s)
{
	$shared_text_helpers .= $1;
}
else
{
	push @errors, "cannot locate shared LowIR type serializer helper";
}
my $serializer_surface = $serializer . $shared_text_helpers;

# These two existing branches spell the remaining enum alternative with a
# final else after testing every other value. Keep that deliberate exception
# narrow so adding another enum alternative still fails the audit.
my %serializer_fallback = map { $_ => 1 } qw(ITEM_INTEGER INIT_ADDR);
for my $value (sorted_keys(\%public_enum))
{
	push @errors, "public model enum $value is absent from the parser"
		if $parser !~ /\b\Q$value\E\b/;
	push @errors, "public model enum $value is absent from the serializer"
		if $serializer_surface !~ /\b\Q$value\E\b/ &&
			!$serializer_fallback{$value};
}

my (%parser_key, %serializer_key);
while ($parser =~ /\bkey\s*==\s*"([a-z_]+)"/g)
{
	$parser_key{$1} = 1;
}
while ($parser =~ /\.first\s*[!=]=\s*"([a-z_]+)"/g)
{
	$parser_key{$1} = 1;
}
while ($serializer =~ /metadata\.(?:item|flag)\("([a-z_]+)"/g)
{
	$serializer_key{$1} = 1;
}
while ($serializer =~ /\[([a-z_]+)=/g)
{
	$serializer_key{$1} = 1;
}

for my $key (sorted_keys(\%parser_key))
{
	push @errors, "parser metadata key '$key' is absent from the serializer"
		if !$serializer_key{$key};
	push @errors, "parser metadata key '$key' is absent from pa13/lowir.md"
		if $documentation !~ /\b\Q$key\E\b/;
	my @owners = grep {
		$_->{disposition} eq 'keep' && $_->{category} eq 'metadata' &&
		($_->{surface} eq $key || $_->{surface} =~ /^\Q$key\E=/)
	} @rows;
	push @errors, "parser metadata key '$key' has no retained ledger owner"
		if !@owners;
}
for my $key (sorted_keys(\%serializer_key))
{
	push @errors, "serializer metadata key '$key' is absent from the parser"
		if !$parser_key{$key};
}

my (%parsed_role, %serialized_role);
while ($parser =~ /\{"([a-z_]+)",\s*SR_[A-Z0-9_]+\}/g)
{
	$parsed_role{$1} = 1;
}
while ($serializer =~ /case\s+SR_[A-Z0-9_]+:\s*return\s+"([a-z_]+)"/g)
{
	$serialized_role{$1} = 1;
}
for my $role (sorted_keys(\%parsed_role))
{
	push @errors, "parsed role '$role' is absent from the serializer"
		if !$serialized_role{$role};
	push @errors, "parsed role '$role' is absent from pa13/lowir.md"
		if $documentation !~ /\b\Q$role\E\b/;
	my @owners = grep {
		$_->{disposition} eq 'keep' && $_->{category} eq 'role' &&
		$_->{surface} eq $role
	} @rows;
	push @errors, "parsed role '$role' has no retained ledger owner"
		if !@owners;
}
for my $role (sorted_keys(\%serialized_role))
{
	push @errors, "serialized role '$role' is absent from the parser"
		if !$parsed_role{$role};
}

if (@errors)
{
	print STDERR "LowIR contract audit: FAIL (" . scalar(@errors) .
		" issue(s))\n";
	print STDERR "  $_\n" for @errors;
	exit 1;
}

print "LowIR contract audit: PASS (" . scalar(@rows) . " ledger rows, " .
	scalar(@retained) . " retained, " . scalar(keys %public_enum) .
	" public enum values, " . scalar(keys %parser_key) .
	" metadata keys, " . scalar(keys %parsed_role) . " roles)\n";
