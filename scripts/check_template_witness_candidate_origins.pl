#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path);
use File::Spec;
use File::Temp qw(tempdir);

die "Usage: check_template_witness_candidate_origins.pl <cppgm++> <source>\n"
  unless @ARGV == 2;

my $compiler = abs_path($ARGV[0]) or die "compiler not found: $ARGV[0]\n";
my $source = abs_path($ARGV[1]) or die "source not found: $ARGV[1]\n";
my $directory = tempdir('template-witness-origins-XXXXXX', TMPDIR => 1,
  CLEANUP => 1);
my $lowir = File::Spec->catfile($directory, 'output.lowir');
my $witness = File::Spec->catfile($directory, 'output.witness');

system($compiler, '--emit-lowir', '-O0', '-o', $lowir,
  '--witness', $witness, $source);
my $status = $? == -1 ? 255 : $? >> 8;
die "candidate-origin fixture exited $status\n" if $status != 0;

open(my $input, '<', $witness) or die "unable to read $witness: $!\n";
my @events;
my $event;
while (my $line = <$input>)
{
  last if $line eq "template-closure-events\n";
  if ($line =~ /^  function-call at /)
  {
    $event = { fields => [], drops => [] };
    push @events, $event;
    next;
  }
  next if !defined($event) || $line !~ /^    (.+)\n$/;
  my $field = $1;
  push @{$event->{fields}}, $field;
  push @{$event->{drops}}, $field if $field =~ /^drop /;
}
close($input) or die "unable to close $witness: $!\n";

sub event_with_callee
{
  my ($callee) = @_;
  my @found;
  for my $candidate (@events)
  {
    push @found, $candidate
      if grep { $_ eq "callee $callee" } @{$candidate->{fields}};
  }
  die "expected one $callee call event, found " . scalar(@found) . "\n"
    if @found != 1;
  return $found[0];
}

my $construction = event_with_callee('pairish<int>::pairish');
die "distinct declared copy/move candidates were coalesced\n"
  if @{$construction->{drops}} != 2 ||
     (grep { $_ ne 'drop pairish<int>::pairish reason=too_many_arguments' }
       @{$construction->{drops}}) != 0;

my $assignment = event_with_callee('box<int>::operator=');
die "one selected declaration was also reported as rejected\n"
  if @{$assignment->{drops}} != 1 ||
     $assignment->{drops}[0] ne
       'drop box<int>::operator= reason=bad_conversion';

print "template witness candidate origins: PASS\n";
