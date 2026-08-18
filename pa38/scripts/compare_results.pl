#!/usr/bin/perl
use strict;
use warnings;
use FindBin;
use File::Basename qw(dirname);
my $repo_root = dirname(dirname($FindBin::Bin));

if (scalar(@ARGV) != 3)
{
	die "Usage: compare_results.pl <ref_suffix> <my_suffix> <testlocation>";
}

my $tests_root = $ARGV[2];
my $mode = $tests_root =~ m{(?:^|/)behavior(?:/|$)} ? "mir_t" : "mir_structural_t";

exec("perl", "$repo_root/scripts/compare_results_common.pl", $mode, @ARGV)
	or die "exec failed: $!";
