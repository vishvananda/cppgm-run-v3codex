#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path);
use FindBin;

my $root = abs_path("$FindBin::Bin/..");
my $manifest = "$root/doc/compiler-native-symbol-owners.tsv";

sub read_source
{
	my ($relative) = @_;
	my $path = "$root/$relative";
	open(my $fh, '<', $path) or die "unable to read $path: $!\n";
	local $/;
	my $source = <$fh>;
	close($fh) or die "unable to close $path: $!\n";
	return $source;
}

sub definition_count
{
	my ($kind, $symbol, $source) = @_;
	my $count = 0;
	if ($kind eq 'function')
	{
		$count++ while $source =~ /\b\Q$symbol\E\s*\([^;{}]*\)\s*;/sg;
	}
	else
	{
		$count++ while $source =~ /\b\Q$kind\E\s+\Q$symbol\E\b
			[^\{;]*\{/sgx;
	}
	return $count;
}

sub implementation_count
{
	my ($symbol, $source) = @_;
	my $count = 0;
	$count++ while $source =~ /(?:\A|\n)
		[A-Za-z_][A-Za-z0-9_:<>, &*\n]*\b\Q$symbol\E\s*
		\([^;{}]*\)\s*(?:const\s*)?\{/sgx;
	return $count;
}

open(my $manifest_fh, '<', $manifest)
	or die "unable to read $manifest: $!\n";
my $header = <$manifest_fh>;
chomp($header) if defined($header);
die "$manifest has an invalid header\n" if !defined($header) ||
	$header ne "kind\tpath\tsymbol\toverload_ordinal\tplanned_owner\t" .
		"implementation_path\tdisposition";

my %recorded;
my %group_max;
my %group_count;
my %source;
my %implementation_source;
my @error;
my $review = 0;
my $rows = 0;
my $line_number = 1;
while (my $line = <$manifest_fh>)
{
	++$line_number;
	chomp($line);
	next if $line eq '';
	my @field = split /\t/, $line, -1;
	if (@field != 7)
	{
		push @error, "$manifest:$line_number must contain seven fields";
		next;
	}
	my ($kind, $path, $symbol, $ordinal, $owner, $implementation,
		$disposition) = @field;
	my $key = "$kind\t$path\t$symbol\t$ordinal";
	my $group = "$kind\t$path\t$symbol";
	push @error, "$manifest:$line_number duplicates $key"
		if $recorded{$key}++;
	push @error, "$manifest:$line_number has invalid kind '$kind'"
		if $kind ne 'class' && $kind ne 'struct' && $kind ne 'enum' &&
			$kind ne 'function';
	push @error, "$manifest:$line_number has invalid ordinal '$ordinal'"
		if $ordinal !~ /\A[1-9][0-9]*\z/;
	push @error, "$manifest:$line_number has invalid disposition '$disposition'"
		if $disposition ne 'keep' && $disposition ne 'review';
	push @error, "$manifest:$line_number keep owner differs from current path"
		if $disposition eq 'keep' && $owner ne $path;
	push @error, "$manifest:$line_number review owner equals current path"
		if $disposition eq 'review' && $owner eq $path;
	push @error, "$manifest:$line_number owner is outside native production code"
		if $owner !~ m{\Adev/src/native/};
	push @error, "$manifest:$line_number implementation is outside native code"
		if $implementation ne '-' &&
			$implementation !~ m{\Adev/src/native/[^\t]+\.cpp\z};

	if (!exists($source{$path}))
	{
		eval { $source{$path} = read_source($path); 1 }
			or push @error, "$manifest:$line_number cannot read $path";
	}
	if (exists($source{$path}))
	{
		$group_count{$group} = definition_count(
			$kind, $symbol, $source{$path});
		push @error, "$manifest:$line_number ordinal $ordinal exceeds " .
			"$group_count{$group} occurrence(s)"
			if $ordinal > $group_count{$group};
	}
	$group_max{$group} = $ordinal
		if !exists($group_max{$group}) || $ordinal > $group_max{$group};

	if ($implementation ne '-' &&
		!exists($implementation_source{$implementation}))
	{
		eval {
			$implementation_source{$implementation} =
				read_source($implementation);
			1;
		} or push @error,
			"$manifest:$line_number cannot read $implementation";
	}
	if ($implementation ne '-' &&
		exists($implementation_source{$implementation}))
	{
		my $count = implementation_count(
			$symbol, $implementation_source{$implementation});
		push @error, "$manifest:$line_number implementation ordinal $ordinal " .
			"exceeds $count definition(s) in $implementation"
			if $ordinal > $count;
	}

	++$review if $disposition eq 'review';
	++$rows;
}
close($manifest_fh) or die "unable to close $manifest: $!\n";

for my $group (sort keys %group_max)
{
	push @error, "owner rows for $group cover $group_max{$group} of " .
		"$group_count{$group} occurrences"
		if $group_count{$group} != $group_max{$group};
}

if (@error)
{
	print STDERR "Native symbol-owner audit failed with " . scalar(@error) .
		" error(s):\n";
	print STDERR "  $_\n" for @error;
	exit 1;
}

print "Native symbol-owner audit passed: $rows routed symbols; " .
	"$review queued for R10 ownership repair.\n";
