#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Compare qw(compare);
use File::Spec;
use File::Temp qw(tempdir);

die "Usage: check_template_witness_entity_presentation.pl <cppgm++> <source>\n"
  unless @ARGV == 2;

my $compiler = abs_path($ARGV[0]) or die "compiler not found: $ARGV[0]\n";
my $source = abs_path($ARGV[1]) or die "source not found: $ARGV[1]\n";
my $directory = tempdir('template-witness-entity-presentation-XXXXXX',
  TMPDIR => 1, CLEANUP => 1);
my $plain = File::Spec->catfile($directory, 'plain.lowir');
my $observed = File::Spec->catfile($directory, 'observed.lowir');
my $witness = File::Spec->catfile($directory, 'observed.witness');

system($compiler, '--emit-lowir', '-O0', '-o', $plain, $source);
my $plain_status = $? == -1 ? 255 : $? >> 8;
die "plain entity-presentation fixture exited $plain_status\n"
  if $plain_status != 0;
system($compiler, '--emit-lowir', '-O0', '-o', $observed,
  '--witness', $witness, $source);
my $observed_status = $? == -1 ? 255 : $? >> 8;
die "observed entity-presentation fixture exited $observed_status\n"
  if $observed_status != 0;
die "witness mode changed entity-presentation LowIR\n"
  if compare($plain, $observed) != 0;

open(my $input, '<', $witness) or die "unable to read $witness: $!\n";
my %source_specializations;
my %closure_specializations;
my $descriptor_use = 0;
my $variable = 0;
while (my $line = <$input>)
{
  if ($line =~ /^  class-use at /)
  {
    $descriptor_use = 0;
    $variable = 0;
    next;
  }
  if ($line eq "    template descriptor\n")
  {
    $descriptor_use = 1;
    next;
  }
  if ($descriptor_use && $line =~ /^    bind #1 = (.+) source=explicit\n$/)
  {
    ++$source_specializations{$1};
    $descriptor_use = 0;
    next;
  }
  if ($line eq "  variable-instantiation\n")
  {
    $descriptor_use = 0;
    $variable = 1;
    next;
  }
  if ($variable && $line =~ /^    entity descriptor<(.+)>::value\n$/)
  {
    ++$closure_specializations{$1};
    $variable = 0;
  }
}
close($input) or die "unable to close $witness: $!\n";

my @expected = ('void ()', 'void () const', 'void () &');
for my $identity (@expected)
{
  die "source specialization did not preserve both uses: $identity\n"
    unless ($source_specializations{$identity} || 0) == 2;
  die "closure specialization was not reported once: $identity\n"
    unless ($closure_specializations{$identity} || 0) == 1;
}
die "unexpected descriptor source specialization\n"
  unless keys(%source_specializations) == @expected;
die "unexpected descriptor closure specialization\n"
  unless keys(%closure_specializations) == @expected;

print "template witness entity presentation: PASS\n";
