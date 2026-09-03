#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Compare qw(compare);
use File::Spec;
use File::Temp qw(tempdir);

die "Usage: check_nondependent_template_base.pl <cppgm++> <valid-source> <invalid-source>...\n"
  unless @ARGV >= 3;

my $compiler = abs_path($ARGV[0]) or die "compiler not found: $ARGV[0]\n";
my $valid = abs_path($ARGV[1]) or die "source not found: $ARGV[1]\n";
my @invalid;
for my $argument (@ARGV[2 .. $#ARGV])
{
  my $path = abs_path($argument) or die "source not found: $argument\n";
  push @invalid, $path;
}
my $directory = tempdir('nondependent-template-base-XXXXXX', TMPDIR => 1,
  CLEANUP => 1);

sub status_of
{
  return $? == -1 ? 255 : $? >> 8;
}

sub run_silent
{
  my (@command) = @_;
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
  return status_of();
}

my $program = File::Spec->catfile($directory, 'program');
system($compiler, '-O0', '-o', $program, $valid);
my $status = status_of();
die "valid template-base program compile exited $status\n" if $status != 0;
system($program);
$status = status_of();
die "valid template-base program run exited $status\n" if $status != 0;

my $plain = File::Spec->catfile($directory, 'plain.lowir');
my $observed = File::Spec->catfile($directory, 'observed.lowir');
my $witness = File::Spec->catfile($directory, 'observed.witness');
system($compiler, '--emit-lowir', '-O0', '-o', $plain, $valid);
$status = status_of();
die "plain template-base compile exited $status\n" if $status != 0;
system($compiler, '--emit-lowir', '-O0', '-o', $observed,
  '--witness', $witness, $valid);
$status = status_of();
die "observed template-base compile exited $status\n" if $status != 0;
die "witness mode changed template-base LowIR\n"
  if compare($plain, $observed) != 0;

open(my $input, '<', $witness) or die "unable to read $witness: $!\n";
my ($fixed_false, $fixed_true) = (0, 0);
my $fixed_event = 0;
while (my $line = <$input>)
{
  if ($line =~ /^  class-use at /)
  {
    $fixed_event = 0;
    next;
  }
  if ($line eq "    template fixed_base\n")
  {
    $fixed_event = 1;
    next;
  }
  next if !$fixed_event;
  ++$fixed_false if $line eq "    bind #1 = false source=explicit\n";
  ++$fixed_true if $line eq "    bind #1 = true source=explicit\n";
}
close($input) or die "unable to close $witness: $!\n";
die "unused nondependent base specialization was not observed once\n"
  unless $fixed_false == 1;
die "instantiated nondependent base specialization was not observed once\n"
  unless $fixed_true == 1;

for my $case (0 .. $#invalid)
{
  my $invalid = $invalid[$case];
  for my $with_witness (0, 1)
  {
    my $invalid_lowir = File::Spec->catfile(
      $directory, "invalid-$case-$with_witness.lowir");
    my $invalid_witness = File::Spec->catfile(
      $directory, "invalid-$case.witness");
    my @command = ($compiler, '--emit-lowir', '-O0', '-o',
      $invalid_lowir);
    if ($with_witness)
    {
      push @command, '--witness', $invalid_witness;
    }
    push @command, $invalid;
    $status = run_silent(@command);
    die "invalid nondependent template base was accepted\n" if $status == 0;
    die "failed template-base analysis emitted plausible LowIR\n"
      if -s $invalid_lowir;
    die "failed template-base analysis emitted a witness\n"
      if -s $invalid_witness;
  }
}

print "nondependent template base validation: PASS\n";
