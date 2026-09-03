#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Compare qw(compare);
use File::Spec;
use File::Temp qw(tempdir);

die "Usage: check_dependent_typename.pl <cppgm++> <invalid-source>... <valid-source>\n"
  unless @ARGV >= 4;

my $compiler_argument = shift @ARGV;
my $compiler = abs_path($compiler_argument)
  or die "compiler not found: $compiler_argument\n";
my $valid_argument = pop @ARGV;
my @invalid = map {
  abs_path($_) or die "source not found: $_\n"
} @ARGV;
my $valid = abs_path($valid_argument)
  or die "source not found: $valid_argument\n";
my $directory = tempdir('dependent-typename-XXXXXX', TMPDIR => 1,
  CLEANUP => 1);

for my $invalid_index (0 .. $#invalid)
{
  for my $observed (0, 1)
  {
    my $label = "invalid-$invalid_index-" .
      ($observed ? 'observed' : 'plain');
    my $output = File::Spec->catfile($directory, "$label.lowir");
    my @command = ($compiler, '--emit-lowir', '-O0', '-o', $output);
    if ($observed)
    {
      push @command, '--witness',
        File::Spec->catfile($directory, "$label.witness");
    }
    push @command, $invalid[$invalid_index];
    system(@command);
    my $status = $? == -1 ? 255 : $? >> 8;
    die "missing dependent typename was accepted\n" if $status == 0;
  }
}

my $plain = File::Spec->catfile($directory, 'valid-plain.lowir');
my $observed = File::Spec->catfile($directory, 'valid-observed.lowir');
my $witness = File::Spec->catfile($directory, 'valid.witness');
system($compiler, '--emit-lowir', '-O0', '-o', $plain, $valid);
my $plain_status = $? == -1 ? 255 : $? >> 8;
die "valid dependent typename fixture exited $plain_status\n"
  if $plain_status != 0;
system($compiler, '--emit-lowir', '-O0', '-o', $observed,
  '--witness', $witness, $valid);
my $observed_status = $? == -1 ? 255 : $? >> 8;
die "observed valid dependent typename fixture exited $observed_status\n"
  if $observed_status != 0;
die "witness mode changed valid dependent typename LowIR\n"
  if compare($plain, $observed) != 0;

print "dependent typename validation: PASS\n";
