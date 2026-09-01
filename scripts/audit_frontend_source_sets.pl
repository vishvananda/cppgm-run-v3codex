#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Find qw(find);
use File::Spec;
use FindBin;

my $root = abs_path("$FindBin::Bin/..");
my $source_sets_path = "$root/dev/frontend_source_sets.mk";
my $dev_make_path = "$root/dev/Makefile";

open(my $fh, '<', $source_sets_path)
	or die "unable to read $source_sets_path: $!\n";
my @logical_line;
my $logical = '';
my $start_line = 0;
my $line_number = 0;
while (my $line = <$fh>)
{
	++$line_number;
	$line =~ s/\r?\n\z//;
	$start_line = $line_number if $logical eq '';
	if ($line =~ s/\\\s*\z//)
	{
		$logical .= "$line ";
		next;
	}
	$logical .= $line;
	push @logical_line, [$start_line, $logical];
	$logical = '';
}
close($fh) or die "unable to close $source_sets_path: $!\n";
die "$source_sets_path:$start_line has an unterminated continuation\n"
	if $logical ne '';

my %value;
my %definition_line;
my @error;

sub expand_value
{
	my ($value, $line) = @_;
	my $guard = 0;
	while ($value =~ /\$\(([^()]+)\)/)
	{
		my $name = $1;
		if (!exists($value{$name}))
		{
			push @error, "$source_sets_path:$line references undefined variable '$name'";
			$value =~ s/\$\(\Q$name\E\)//g;
			next;
		}
		$value =~ s/\$\(\Q$name\E\)/$value{$name}/g;
		die "$source_sets_path:$line has recursive expansion\n" if ++$guard > 1000;
	}
	$value =~ s/^\s+|\s+$//g;
	$value =~ s/\s+/ /g;
	return $value;
}

for my $entry (@logical_line)
{
	my ($line, $text) = @$entry;
	$text =~ s/#.*\z//;
	next if $text =~ /^\s*\z/;
	next if $text !~ /^\s*([^\s:=]+)\s*:=\s*(.*?)\s*\z/;
	my ($name, $raw_value) = ($1, $2);
	push @error, "$source_sets_path:$line redefines '$name'"
		if exists($value{$name});
	$value{$name} = expand_value($raw_value, $line);
	$definition_line{$name} = $line;
}

my $target_variable = 'FRONTEND_SOURCE_SET_TARGETS';
die "$source_sets_path does not define $target_variable\n"
	if !exists($value{$target_variable});
my @target = split / /, $value{$target_variable};
my %target;
for my $target (@target)
{
	push @error, "$source_sets_path:$definition_line{$target_variable} " .
		"duplicates tool '$target'"
		if $target{$target}++;
}
open(my $make_fh, '<', $dev_make_path)
	or die "unable to read $dev_make_path: $!\n";
local $/;
my $make_text = <$make_fh>;
close($make_fh) or die "unable to close $dev_make_path: $!\n";
$make_text =~ s/\\\r?\n/ /g;
die "$dev_make_path does not define TARGETS\n"
	if $make_text !~ /^TARGETS\s*=\s*(.*?)\s*$/m;
my @make_target = split /\s+/, $1;
my %make_target;
for my $target (@make_target)
{
	push @error, "$dev_make_path TARGETS duplicates tool '$target'"
		if $make_target{$target}++;
}
push @error, "$dev_make_path TARGETS and frontend source-set targets differ"
	if join("\0", sort @make_target) ne join("\0", sort @target);
my @defined_target = sort map {
	/^FRONTEND_OBJ_BASENAMES_(.+)\z/ ? $1 : ()
} keys %value;
for my $target (@target)
{
	push @error, "$source_sets_path has no expanded source set for tool '$target'"
		if !exists($value{"FRONTEND_OBJ_BASENAMES_$target"});
}
for my $target (@defined_target)
{
	push @error, "$source_sets_path defines source set for unknown tool '$target'"
		if !$target{$target};
}

my @set_variable = sort grep {
	/^FRONTEND_(?:SOURCE_IDS|OBJ_BASENAMES)_/
} keys %value;
my %owner;
for my $name (@set_variable)
{
	my @source = $value{$name} eq '' ? () : split / /, $value{$name};
	my %seen;
	for my $source (@source)
	{
		push @error, "$source_sets_path:$definition_line{$name} '$name' " .
			"expands duplicate source ID '$source'"
			if $seen{$source}++;
		push @error, "$source_sets_path:$definition_line{$name} '$name' " .
			"has invalid source ID '$source'"
			if $source !~ m{\A[A-Za-z0-9_+.-]+(?:/[A-Za-z0-9_+.-]+)*\z} ||
				$source =~ /(?:\A|\/)\.{1,2}(?:\/|\z)/;
		push @error, "$source_sets_path:$definition_line{$name} '$name' " .
			"references missing dev/src/$source.cpp"
			if !-f "$root/dev/src/$source.cpp";
	}
}

for my $target (@target)
{
	my $name = "FRONTEND_OBJ_BASENAMES_$target";
	next if !exists($value{$name});
	$owner{$_}{$target} = 1 for split / /, $value{$name};
}
for my $special (qw(FRONTEND_TEST_RUNNER_SOURCE_ID
	FRONTEND_BUILTIN_CONFIG_SOURCE_ID))
{
	push @error, "$source_sets_path does not define $special"
		if !exists($value{$special});
	next if !exists($value{$special});
	my $source = $value{$special};
	push @error, "$source_sets_path:$definition_line{$special} $special " .
		"references missing dev/src/$source.cpp"
		if !-f "$root/dev/src/$source.cpp";
	$owner{$source}{'<shared-runner>'} = 1
		if $special eq 'FRONTEND_TEST_RUNNER_SOURCE_ID';
}

my @production_source;
find({
	wanted => sub {
		return if !-f $_ || $_ !~ /\.cpp\z/;
		my $relative = File::Spec->abs2rel($File::Find::name,
			"$root/dev/src");
		$relative =~ s{\\}{/}g;
		$relative =~ s/\.cpp\z//;
		push @production_source, $relative;
	},
	no_chdir => 1,
}, "$root/dev/src");
@production_source = sort @production_source;
for my $source (@production_source)
{
	push @error, "dev/src/$source.cpp has no intended tool owner"
		if !exists($owner{$source});
}
for my $source (sort keys %owner)
{
	push @error, "source-set owner remains for absent dev/src/$source.cpp"
		if !-f "$root/dev/src/$source.cpp";
}

if (@error)
{
	print STDERR "Frontend source-set audit failed with " . scalar(@error) .
		" error(s):\n";
	print STDERR "  $_\n" for @error;
	exit 1;
}

print "Frontend source-set audit passed: " . scalar(@target) .
	" tool sets and " . scalar(@set_variable) . " responsibility sets; " .
	scalar(@production_source) .
	" production sources have current paths and intended owners.\n";
