#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Compare qw(compare);
use File::Spec;
use File::Temp qw(tempdir);

die "Usage: check_qualified_member_template_hiding.pl <cppgm++> <function-source> <invalid-variable-source>\n"
  unless @ARGV == 3;

my $compiler = abs_path($ARGV[0]) or die "compiler not found: $ARGV[0]\n";
my $source = abs_path($ARGV[1]) or die "source not found: $ARGV[1]\n";
my $invalid_variable = abs_path($ARGV[2])
  or die "source not found: $ARGV[2]\n";
my $directory = tempdir('qualified-member-template-hiding-XXXXXX',
  TMPDIR => 1, CLEANUP => 1);
my $program = File::Spec->catfile($directory, 'program');
my $plain = File::Spec->catfile($directory, 'plain.lowir');
my $observed = File::Spec->catfile($directory, 'observed.lowir');
my $witness = File::Spec->catfile($directory, 'observed.witness');

system($compiler, '-O0', '-o', $program, $source);
my $compile_status = $? == -1 ? 255 : $? >> 8;
die "qualified hiding fixture compile exited $compile_status\n"
  if $compile_status != 0;
system($program);
my $run_status = $? == -1 ? 255 : $? >> 8;
die "hidden base function affected call selection (exit $run_status)\n"
  if $run_status != 0;

system($compiler, '--emit-lowir', '-O0', '-o', $plain, $source);
my $plain_status = $? == -1 ? 255 : $? >> 8;
die "plain qualified hiding fixture exited $plain_status\n"
  if $plain_status != 0;
system($compiler, '--emit-lowir', '-O0', '-o', $observed,
  '--witness', $witness, $source);
my $observed_status = $? == -1 ? 255 : $? >> 8;
die "observed qualified hiding fixture exited $observed_status\n"
  if $observed_status != 0;
die "witness mode changed qualified hiding LowIR\n"
  if compare($plain, $observed) != 0;

open(my $input, '<', $witness) or die "unable to read $witness: $!\n";
my $derived_calls = 0;
my $root_drops = 0;
while (my $line = <$input>)
{
  ++$derived_calls if $line eq "    callee derived::choose\n";
  ++$root_drops if $line =~ /^    drop root::choose /;
}
close($input) or die "unable to close $witness: $!\n";
die "derived member template call was not reported once\n"
  unless $derived_calls == 1;
die "hidden base function entered the witness candidate set\n"
  unless $root_drops == 0;

for my $observed_invalid (0, 1)
{
  my @command = ($compiler, '--emit-lowir', '-O0', '-o',
    File::Spec->catfile($directory, 'invalid-variable.lowir'));
  if ($observed_invalid)
  {
    push @command, '--witness',
      File::Spec->catfile($directory, 'invalid-variable.witness');
  }
  push @command, $invalid_variable;
  system(@command);
  my $status = $? == -1 ? 255 : $? >> 8;
  die "hidden base variable satisfied a template name without arguments\n"
    if $status == 0;
}

print "qualified template-name hiding: PASS\n";
