#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Compare qw(compare);
use File::Spec;
use File::Temp qw(tempdir);

die "Usage: check_template_witness_pack_dependency.pl <cppgm++> <source>\n"
  unless @ARGV == 2;

my $compiler = abs_path($ARGV[0]) or die "compiler not found: $ARGV[0]\n";
my $source = abs_path($ARGV[1]) or die "source not found: $ARGV[1]\n";
my $directory = tempdir('template-witness-pack-dependency-XXXXXX',
  TMPDIR => 1, CLEANUP => 1);
my $plain = File::Spec->catfile($directory, 'plain.lowir');
my $observed = File::Spec->catfile($directory, 'observed.lowir');
my $witness = File::Spec->catfile($directory, 'observed.witness');

system($compiler, '--emit-lowir', '-O0', '-o', $plain, $source);
my $plain_status = $? == -1 ? 255 : $? >> 8;
die "plain pack-dependency fixture exited $plain_status\n"
  if $plain_status != 0;
system($compiler, '--emit-lowir', '-O0', '-o', $observed,
  '--witness', $witness, $source);
my $observed_status = $? == -1 ? 255 : $? >> 8;
die "observed pack-dependency fixture exited $observed_status\n"
  if $observed_status != 0;
die "witness mode changed ordinary LowIR\n"
  if compare($plain, $observed) != 0;

open(my $input, '<', $witness) or die "unable to read $witness: $!\n";
my @class_uses;
my %variables;
my $class_use;
my $variable = 0;
while (my $line = <$input>)
{
  if ($line =~ /^  class-use at /)
  {
    $class_use = { template => '', bindings => [] };
    push @class_uses, $class_use;
    next;
  }
  if (defined($class_use) && $line =~ /^    template (.+)\n$/)
  {
    $class_use->{template} = $1;
    next;
  }
  if (defined($class_use) && $line =~ /^    bind (.+)\n$/)
  {
    push @{$class_use->{bindings}}, $1;
    next;
  }
  if ($line eq "  variable-instantiation\n")
  {
    undef($class_use);
    $variable = 1;
    next;
  }
  if ($variable && $line =~ /^    entity (.+)\n$/)
  {
    ++$variables{$1};
    $variable = 0;
  }
}
close($input) or die "unable to close $witness: $!\n";

my @selected = grep { $_->{template} eq 'selected' } @class_uses;
die "fixed and dependent source uses were not distinguished\n"
  unless @selected == 1;
die "the fixed selected specialization lost its explicit binding\n"
  unless grep { $_ eq '#1 = 2 source=explicit' }
              @{$selected[0]->{bindings}};
my @owners = grep { $_->{template} eq 'observed' } @class_uses;
die "the demanded owner specialization was not reported once\n"
  unless @owners == 1;
die "the concrete value demand was not retained\n"
  unless ($variables{'selected<2>::value'} || 0) == 1;

print "template witness pack dependency: PASS\n";
