#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Find qw(find);
use File::Spec;
use FindBin;

my $root = abs_path("$FindBin::Bin/..");
my $legacy_path = "$root/doc/compiler-layout-legacy.tsv";
my $contract_path = "$root/doc/compiler-pa-contract-allowlist.tsv";

sub read_text
{
	my ($path) = @_;
	open(my $fh, '<', $path) or die "unable to read $path: $!\n";
	local $/;
	my $text = <$fh>;
	close($fh) or die "unable to close $path: $!\n";
	return defined($text) ? $text : '';
}

sub mask_segment
{
	my ($segment) = @_;
	$segment =~ s/[^\n]/ /g;
	return $segment;
}

sub mask_comments_and_literals
{
	my ($text) = @_;

	# Mask raw strings first.  Their bodies can contain arbitrary quote and
	# comment spellings which the ordinary literal matcher must never see.
	$text =~ s{
		(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(
		(.*?)
		\)\1"
	}{mask_segment($&)}gsex;

	$text =~ s{
		//[^\n]*
		|/\*.*?\*/
		|"(?:\\.|[^"\\])*"
		|'(?:\\.|[^'\\])*'
	}{mask_segment($&)}gsex;
	return $text;
}

sub relative_path
{
	my ($path) = @_;
	return File::Spec->abs2rel($path, $root);
}

sub line_number
{
	my ($text, $offset) = @_;
	return 1 + (substr($text, 0, $offset) =~ tr/\n/\n/);
}

my @legacy;
my @legacy_lines = split /\n/, read_text($legacy_path);
my $header = shift @legacy_lines;
die "$legacy_path has an invalid header\n"
	if !defined($header) || $header ne "kind\tpattern\tdestination";
for (my $i = 0; $i < scalar(@legacy_lines); ++$i)
{
	next if $legacy_lines[$i] eq '';
	my @fields = split /\t/, $legacy_lines[$i], -1;
	die "$legacy_path:" . ($i + 2) . " must contain three fields\n"
		if scalar(@fields) != 3;
	die "$legacy_path:" . ($i + 2) . " has an invalid kind\n"
		if $fields[0] !~ /\A(?:path|include|namespace|identifier)\z/;
	my $compiled = eval { qr/$fields[1]/ };
	die "$legacy_path:" . ($i + 2) . " has invalid regex: $@"
		if !defined($compiled);
	push @legacy, {
		kind => $fields[0], pattern => $compiled, text => $fields[1],
		destination => $fields[2], line => $i + 2, matches => 0,
	};
}

my @contract;
my @contract_lines = split /\n/, read_text($contract_path);
my $contract_header = shift @contract_lines;
die "$contract_path has an invalid header\n"
	if !defined($contract_header) ||
		$contract_header ne "kind\tpath\ttoken\treason";
my %contract_key;
for (my $i = 0; $i < scalar(@contract_lines); ++$i)
{
	next if $contract_lines[$i] eq '';
	my @fields = split /\t/, $contract_lines[$i], -1;
	die "$contract_path:" . ($i + 2) . " must contain four fields\n"
		if scalar(@fields) != 4;
	die "$contract_path:" . ($i + 2) . " has an invalid kind\n"
		if $fields[0] !~ /\A(?:comment|diagnostic|error-prose)\z/;
	die "$contract_path:" . ($i + 2) . " has an invalid path\n"
		if $fields[1] !~ m{\Adev/(?:src/)?[^\t]+\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)\z};
	die "$contract_path:" . ($i + 2) . " has an invalid token\n"
		if $fields[2] !~ /\A(?:PA\d+|pa\d[A-Za-z0-9_]*)\z/;
	my $key = join "\t", @fields[0 .. 2];
	die "$contract_path:" . ($i + 2) . " duplicates an earlier row\n"
		if $contract_key{$key}++;
	push @contract, {
		kind => $fields[0], path => $fields[1], token => $fields[2],
		reason => $fields[3], line => $i + 2, matches => 0,
	};
}

my @files;
find({
	wanted => sub {
		return if !-f $_;
		return if $_ !~ /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)\z/;
		push @files, $File::Find::name;
	},
	no_chdir => 1,
}, "$root/dev/src");
for my $path (glob "$root/dev/*")
{
	push @files, $path if -f $path &&
		$path =~ /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)\z/;
}
@files = sort @files;

my @finding;
my @contract_finding;
my %namespace_name;
my @code_by_file;

sub add_finding
{
	my ($kind, $value, $path, $line) = @_;
	push @finding, {
		kind => $kind, value => $value, path => $path, line => $line,
		allowed => 0,
	};
}

sub add_contract_finding
{
	my ($kind, $token, $path, $line) = @_;
	push @contract_finding, {
		kind => $kind, token => $token, path => $path, line => $line,
		allowed => 0,
	};
}

sub find_contract_references
{
	my ($text, $path) = @_;
	while ($text =~ m{
		(?<raw>(?:u8|u|U|L)?R"(?<delimiter>[^\s()\\]{0,16})\(.*?\)\k<delimiter>")
		|(?<line>//[^\n]*)
		|(?<block>/\*.*?\*/)
		|(?<string>(?:u8|u|U|L)?"(?:\\.|[^"\\])*")
		|(?<character>(?:u8|u|U|L)?'(?:\\.|[^'\\])*')
	}gsx)
	{
		my $segment = $&;
		my $offset = $-[0];
		my $is_comment = defined($+{line}) || defined($+{block});
		next if defined($+{character});
		while ($segment =~ /\b(pa\d[A-Za-z0-9_]*)\b/gi)
		{
			my $token = $1;
			my $token_offset = $-[1];
			my $kind = $is_comment ? 'comment' :
				($token =~ /\Apa/ ? 'diagnostic' : 'error-prose');
			add_contract_finding($kind, $token, $path,
				line_number($text, $offset + $token_offset));
		}
	}
}

for my $path (@files)
{
	my $rel = relative_path($path);
	if ($rel =~ m{\Adev/src/(?:.*/)?pa\d[^/]*\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)\z}i)
	{
		add_finding('path', $rel, $rel, 1);
	}

	my $text = read_text($path);
	find_contract_references($text, $rel);
	while ($text =~ /^\s*#\s*include\s+"(pa\d[^\"]*)"/gim)
	{
		add_finding('include', $1, $rel, line_number($text, $-[0]));
	}

	my $code = mask_comments_and_literals($text);
	push @code_by_file, [$rel, $code];
	while ($code =~ /\bnamespace\s+(pa\d[A-Za-z0-9_]*)\b/gi)
	{
		$namespace_name{$1} = 1;
		add_finding('namespace', $1, $rel, line_number($code, $-[1]));
	}
}

for my $finding (@contract_finding)
{
	for my $allowed (@contract)
	{
		next if $allowed->{kind} ne $finding->{kind};
		next if $allowed->{path} ne $finding->{path};
		next if $allowed->{token} ne $finding->{token};
		$finding->{allowed} = 1;
		++$allowed->{matches};
		last;
	}
}

for my $entry (@code_by_file)
{
	my ($rel, $code) = @$entry;
	while ($code =~ /\b([A-Za-z_][A-Za-z0-9_]*)\b/g)
	{
		my $name = $1;
		my $offset = $-[1];
		next if $namespace_name{$name};
		next if $name !~ /pa\d/i;
		add_finding('identifier', $name, $rel,
			line_number($code, $offset));
	}
}

for my $finding (@finding)
{
	for my $legacy (@legacy)
	{
		next if $legacy->{kind} ne $finding->{kind};
		next if $finding->{value} !~ $legacy->{pattern};
		$finding->{allowed} = 1;
		++$legacy->{matches};
		last;
	}
}

my @error;
for my $finding (@finding)
{
	next if $finding->{allowed};
	push @error, "$finding->{path}:$finding->{line}: unclassified " .
		"$finding->{kind} '$finding->{value}'";
}
for my $legacy (@legacy)
{
	push @error, "$legacy_path:$legacy->{line}: stale $legacy->{kind} " .
		"pattern '$legacy->{text}' for $legacy->{destination}"
		if !$legacy->{matches};
}
for my $finding (@contract_finding)
{
	next if $finding->{allowed};
	push @error, "$finding->{path}:$finding->{line}: unclassified " .
		"$finding->{kind} PA reference '$finding->{token}'";
}
for my $allowed (@contract)
{
	push @error, "$contract_path:$allowed->{line}: stale " .
		"$allowed->{kind} entry for $allowed->{path} '$allowed->{token}'"
		if !$allowed->{matches};
}

if (@error)
{
	print "Compiler layout audit failed with " . scalar(@error) .
		" error(s):\n";
	print "  $_\n" for @error;
	exit 1;
}

my %count = map { $_ => 0 } qw(path include namespace identifier);
++ $count{$_->{kind}} for @finding;
my %contract_count = map { $_ => 0 } qw(comment diagnostic error-prose);
++$contract_count{$_->{kind}} for @contract_finding;
print "Compiler layout audit passed: " . scalar(@files) . " files; " .
	"$count{path} legacy paths, $count{include} legacy includes, " .
	"$count{namespace} legacy namespace declarations, and " .
	"$count{identifier} legacy identifier uses remain; " .
	"$contract_count{diagnostic} diagnostic, " .
	"$contract_count{'error-prose'} error-prose, and " .
	"$contract_count{comment} comment PA references are contract-approved.\n";
