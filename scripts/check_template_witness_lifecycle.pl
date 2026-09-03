#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Compare qw(compare);
use File::Spec;
use File::Temp qw(tempdir);

die "Usage: check_template_witness_lifecycle.pl <cppgm++> <source>\n"
  unless @ARGV == 2;

my $compiler = abs_path($ARGV[0]) or die "compiler not found: $ARGV[0]\n";
my $source = abs_path($ARGV[1]) or die "source not found: $ARGV[1]\n";
my $directory = tempdir('template-witness-lifecycle-XXXXXX', TMPDIR => 1,
  CLEANUP => 1);
my $plain = File::Spec->catfile($directory, 'plain.lowir');
my $observed = File::Spec->catfile($directory, 'observed.lowir');
my $witness = File::Spec->catfile($directory, 'observed.witness');

system($compiler, '--emit-lowir', '-O0', '-o', $plain, $source);
my $plain_status = $? == -1 ? 255 : $? >> 8;
die "plain lifecycle fixture exited $plain_status\n" if $plain_status != 0;
system($compiler, '--emit-lowir', '-O0', '-o', $observed,
  '--witness', $witness, $source);
my $observed_status = $? == -1 ? 255 : $? >> 8;
die "observed lifecycle fixture exited $observed_status\n"
  if $observed_status != 0;
die "witness mode changed ordinary LowIR\n" if compare($plain, $observed) != 0;

open(my $input, '<', $witness) or die "unable to read $witness: $!\n";
my %transitions;
my $transition;
while (my $line = <$input>)
{
  if ($line =~ /^  (function-instantiation|require-definition)\n$/)
  {
    $transition = $1;
    next;
  }
  if (defined($transition) && $line =~ /^    entity (.+)\n$/)
  {
    ++$transitions{$transition}{$1};
    undef($transition);
  }
}
close($input) or die "unable to close $witness: $!\n";

my $defaulted = 'external_value<char>::operator=';
die "defaulted member did not retain its definition requirement\n"
  unless ($transitions{'require-definition'}{$defaulted} || 0) == 1;
die "extern class member was reported as newly instantiated\n"
  unless ($transitions{'function-instantiation'}{$defaulted} || 0) == 0;

my $ordinary = 'identity';
die "ordinary function-template lifecycle was not preserved\n"
  unless ($transitions{'function-instantiation'}{$ordinary} || 0) == 1 &&
         ($transitions{'require-definition'}{$ordinary} || 0) == 1;

print "template witness lifecycle: PASS\n";
