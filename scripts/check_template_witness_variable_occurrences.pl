#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Compare qw(compare);
use File::Spec;
use File::Temp qw(tempdir);

die "Usage: check_template_witness_variable_occurrences.pl <cppgm++> <source> <invalid-source>\n"
  unless @ARGV == 3;

my $compiler = abs_path($ARGV[0]) or die "compiler not found: $ARGV[0]\n";
my $source = abs_path($ARGV[1]) or die "source not found: $ARGV[1]\n";
my $invalid = abs_path($ARGV[2]) or die "source not found: $ARGV[2]\n";
my $directory = tempdir('template-witness-variable-XXXXXX', TMPDIR => 1,
  CLEANUP => 1);
my $plain = File::Spec->catfile($directory, 'plain.lowir');
my $observed = File::Spec->catfile($directory, 'observed.lowir');
my $witness = File::Spec->catfile($directory, 'observed.witness');

system($compiler, '--emit-lowir', '-O0', '-o', $plain, $source);
my $plain_status = $? == -1 ? 255 : $? >> 8;
die "plain variable fixture exited $plain_status\n" if $plain_status != 0;
system($compiler, '--emit-lowir', '-O0', '-o', $observed,
  '--witness', $witness, $source);
my $observed_status = $? == -1 ? 255 : $? >> 8;
die "observed variable fixture exited $observed_status\n"
  if $observed_status != 0;
die "witness mode changed ordinary LowIR\n" if compare($plain, $observed) != 0;

open(my $input, '<', $witness) or die "unable to read $witness: $!\n";
my %variables;
my $variable = 0;
while (my $line = <$input>)
{
  if ($line eq "  variable-instantiation\n")
  {
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

die "evaluated demanded member value was not instantiated once\n"
  unless ($variables{'observed<int>::runtime_value'} || 0) == 1;
die "an unused member value became a variable instantiation\n"
  unless ($variables{'observed<int>::unused_value'} || 0) == 0;
die "an unevaluated reference became a variable instantiation\n"
  unless ($variables{'observed<int>::unevaluated_reference'} || 0) == 0;

for my $with_witness (0, 1)
{
  my $invalid_lowir = File::Spec->catfile(
    $directory, "invalid-$with_witness.lowir");
  my @command = ($compiler, '--emit-lowir', '-O0', '-o', $invalid_lowir);
  if ($with_witness)
  {
    push @command, '--witness',
      File::Spec->catfile($directory, 'invalid.witness');
  }
  push @command, $invalid;
  my $pid = fork();
  die "unable to fork compiler: $!\n" if !defined($pid);
  if ($pid == 0)
  {
    open(STDOUT, '>', File::Spec->devnull()) or exit 255;
    open(STDERR, '>', File::Spec->devnull()) or exit 255;
    exec {$command[0]} @command;
    exit 255;
  }
  waitpid($pid, 0);
  my $invalid_status = $? == -1 ? 255 : $? >> 8;
  die "invalid dependent static initializer was accepted\n"
    if $invalid_status == 0;
  die "failed dependent initializer emitted plausible LowIR\n"
    if -s $invalid_lowir;
  die "failed dependent initializer emitted a witness\n"
    if -s File::Spec->catfile($directory, 'invalid.witness');
}

print "template witness variable occurrences: PASS\n";
