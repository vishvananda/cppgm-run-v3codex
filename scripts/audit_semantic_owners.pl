#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Find;
use FindBin;

my $root = abs_path("$FindBin::Bin/..");
my $manifest = "$root/doc/compiler-semantic-symbol-owners.tsv";

my %actual;
find({
	wanted => sub {
		return if !/\.cpp\z/;
		my $path = $File::Find::name;
		open(my $fh, '<', $path) or die "unable to read $path: $!\n";
		my $relative = $path;
		$relative =~ s/^\Q$root\E\///;
		my %ordinal;
		while (my $line = <$fh>)
		{
			while ($line =~ /\bAnalyzer::([A-Za-z_][A-Za-z0-9_]*)/g)
			{
				my $method = $1;
				my $number = ++$ordinal{$method};
				$actual{"$relative\t$method\t$number"} = 1;
			}
		}
		close($fh) or die "unable to close $path: $!\n";
	},
	no_chdir => 1,
}, "$root/dev/src");

open(my $fh, '<', $manifest) or die "unable to read $manifest: $!\n";
my $header = <$fh>;
chomp($header) if defined($header);
die "$manifest has an invalid header\n" if !defined($header) ||
	$header ne "path\tmethod\toverload_ordinal\tplanned_owner\tdisposition";

my %recorded;
my @error;
my $review = 0;
my $line_number = 1;
while (my $line = <$fh>)
{
	++$line_number;
	chomp($line);
	next if $line eq '';
	my @field = split /\t/, $line, -1;
	if (@field != 5)
	{
		push @error, "$manifest:$line_number must contain five fields";
		next;
	}
	my ($path, $method, $ordinal, $owner, $disposition) = @field;
	my $key = "$path\t$method\t$ordinal";
	push @error, "$manifest:$line_number duplicates $key"
		if $recorded{$key}++;
	push @error, "$manifest:$line_number has invalid ordinal '$ordinal'"
		if $ordinal !~ /\A[1-9][0-9]*\z/;
	push @error, "$manifest:$line_number has invalid disposition '$disposition'"
		if $disposition ne 'keep' && $disposition ne 'review';
	push @error, "$manifest:$line_number keep owner differs from definition path"
		if $disposition eq 'keep' && $owner ne $path;
	push @error, "$manifest:$line_number review owner is not an R6 route"
		if $disposition eq 'review' && $owner !~ /\AR6:/;
	++$review if $disposition eq 'review';
}
close($fh) or die "unable to close $manifest: $!\n";

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
	print STDERR "Semantic symbol-owner audit failed with " . scalar(@error) .
		" error(s):\n";
	print STDERR "  $_\n" for @error;
	exit 1;
}

print "Semantic symbol-owner audit passed: " . scalar(keys %actual) .
	" Analyzer definitions; $review queued for R6 ownership repair.\n";
