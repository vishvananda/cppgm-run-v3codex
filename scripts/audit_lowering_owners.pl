#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Find;
use FindBin;

my $root = abs_path("$FindBin::Bin/..");
my $manifest = "$root/doc/compiler-lowering-symbol-owners.tsv";
my %actual;
my $class_count = 0;
my $method_count = 0;

my @headers;
find({
	wanted => sub {
		push @headers, $File::Find::name if /\.h\z/;
	},
	no_chdir => 1,
}, "$root/dev/src/lowering");

for my $path (sort @headers)
{
	open(my $fh, '<', $path) or die "unable to read $path: $!\n";
	local $/;
	my $source = <$fh>;
	close($fh) or die "unable to close $path: $!\n";
	$source =~ s/template\s*<[^>]*>//sg;
	my %ordinal;
	while ($source =~ /\b(class|struct)\s+([A-Za-z_][A-Za-z0-9_]*)
		[^\{;]*\{/sgx)
	{
		my ($kind, $symbol) = ($1, $2);
		my $relative = $path;
		$relative =~ s/^\Q$root\E\///;
		my $number = ++$ordinal{"$kind\t$symbol"};
		$actual{"$kind\t$relative\t$symbol\t$number"} = 1;
		++$class_count;
	}
}

my $program_lowerer = "$root/dev/src/lowering/core/program_lowerer.cpp";
open(my $lowerer_fh, '<', $program_lowerer)
	or die "unable to read $program_lowerer: $!\n";
my $in_class = 0;
my $depth = 0;
my %method_ordinal;
while (my $line = <$lowerer_fh>)
{
	my $syntax = $line;
	$syntax =~ s/"(?:\\.|[^"\\])*"/""/g;
	$syntax =~ s!//.*!!;
	$syntax =~ s/__attribute__\s*\(\([^)]*\)\)\s*//g;
	$in_class = 1 if !$in_class && $syntax =~ /^class ProgramLowerer\b/;
	if ($in_class && $depth == 1 &&
		$syntax =~ /^\t(?!friend\b|typedef\b|using\b)(?:explicit\s+)?
			(?:[A-Za-z_][A-Za-z0-9_:<>, &*]*\s+)?
			([A-Za-z_][A-Za-z0-9_]*)\s*\(/x)
	{
		my $symbol = $1;
		my $number = ++$method_ordinal{$symbol};
		$actual{"method\tdev/src/lowering/core/program_lowerer.cpp\t" .
			"$symbol\t$number"} = 1;
		++$method_count;
	}
	my $opens = () = $syntax =~ /\{/g;
	my $closes = () = $syntax =~ /\}/g;
	$depth += $opens - $closes if $in_class;
	$in_class = 0 if $in_class && $depth == 0 && $opens + $closes != 0;
}
close($lowerer_fh) or die "unable to close $program_lowerer: $!\n";

open(my $manifest_fh, '<', $manifest)
	or die "unable to read $manifest: $!\n";
my $header = <$manifest_fh>;
chomp($header) if defined($header);
die "$manifest has an invalid header\n" if !defined($header) ||
	$header ne "kind\tpath\tsymbol\toverload_ordinal\tplanned_owner\tdisposition";

my %recorded;
my @error;
my $review = 0;
my $line_number = 1;
while (my $line = <$manifest_fh>)
{
	++$line_number;
	chomp($line);
	next if $line eq '';
	my @field = split /\t/, $line, -1;
	if (@field != 6)
	{
		push @error, "$manifest:$line_number must contain six fields";
		next;
	}
	my ($kind, $path, $symbol, $ordinal, $owner, $disposition) = @field;
	my $key = "$kind\t$path\t$symbol\t$ordinal";
	push @error, "$manifest:$line_number duplicates $key"
		if $recorded{$key}++;
	push @error, "$manifest:$line_number has invalid kind '$kind'"
		if $kind ne 'class' && $kind ne 'struct' && $kind ne 'method';
	push @error, "$manifest:$line_number has invalid ordinal '$ordinal'"
		if $ordinal !~ /\A[1-9][0-9]*\z/;
	push @error, "$manifest:$line_number has invalid disposition '$disposition'"
		if $disposition ne 'keep' && $disposition ne 'review';
	push @error, "$manifest:$line_number keep owner differs from definition path"
		if $disposition eq 'keep' && $owner ne $path;
	push @error, "$manifest:$line_number review owner is not an R8 route"
		if $disposition eq 'review' && $owner !~ /\AR8:/;
	++$review if $disposition eq 'review';
}
close($manifest_fh) or die "unable to close $manifest: $!\n";

for my $key (sort keys %actual)
{
	push @error, "missing owner row: $key" if !$recorded{$key};
}
for my $key (sort keys %recorded)
{
	push @error, "stale owner row: $key" if !$actual{$key};
}

if (@error)
{
	print STDERR "Lowering symbol-owner audit failed with " . scalar(@error) .
		" error(s):\n";
	print STDERR "  $_\n" for @error;
	exit 1;
}

print "Lowering symbol-owner audit passed: $class_count class/struct " .
	"definitions; $method_count ProgramLowerer methods; $review queued for " .
	"R8 ownership repair.\n";
