#!/usr/bin/env perl

use strict;
use warnings;
use Cwd qw(abs_path getcwd);
use File::Spec;
use File::Temp qw(tempdir);

die "Usage: check_template_witness_contract.pl <cppgm++> <source>\n"
  unless @ARGV == 2;

my ($compiler_arg, $source_arg) = @ARGV;
my $compiler = abs_path($compiler_arg)
  or die "compiler not found: $compiler_arg\n";
my $source = abs_path($source_arg)
  or die "source not found: $source_arg\n";
my $directory = tempdir('template-witness-contract-XXXXXX', TMPDIR => 1,
  CLEANUP => 1);

sub slurp
{
  my ($path) = @_;
  open(my $input, '<', $path) or die "unable to read $path: $!\n";
  local $/;
  my $value = <$input>;
  close($input) or die "unable to close $path: $!\n";
  return $value;
}

sub run_compiler
{
  my (@arguments) = @_;
  system($compiler, '--emit-lowir', '-O0', @arguments);
  return $? == -1 ? 255 : $? >> 8;
}

sub run_compiler_silent
{
  my @arguments = @_;
  my $pid = fork();
  die "unable to fork compiler: $!\n" if !defined($pid);
  if ($pid == 0)
  {
    open(STDERR, '>', File::Spec->devnull()) or exit 255;
    exec {$compiler} $compiler, '--emit-lowir', '-O0', @arguments;
    exit 255;
  }
  waitpid($pid, 0);
  return $? == -1 ? 255 : $? >> 8;
}

my $plain_lowir = File::Spec->catfile($directory, 'plain.lowir');
my $observed_lowir = File::Spec->catfile($directory, 'observed.lowir');
my $unexpected_witness = File::Spec->catfile($directory, 'unexpected.witness');
my $witness = File::Spec->catfile($directory, 'observed.witness');

my $status = run_compiler('-o', $plain_lowir, $source);
die "compile without witness exited $status\n" if $status != 0;
die "compile without --witness created an artifact\n"
  if -e $unexpected_witness;

$status = run_compiler('-o', $observed_lowir, '--witness', $witness, $source);
die "compile with witness exited $status\n" if $status != 0;
die "compile with witness did not create its artifact\n" if !-f $witness;
die "--witness changed ordinary LowIR output\n"
  if slurp($plain_lowir) ne slurp($observed_lowir);

my $bad_output = File::Spec->catfile($directory, 'bad-output.lowir');
$status = run_compiler_silent(
  '-o', $bad_output, '--witness', $directory, $source);
die "unwritable witness destination did not fail\n" if $status == 0;

my @lines = split /\n/, slurp($witness);
die "witness has no translation-unit header\n"
  if !@lines || $lines[0] ne 'translation-unit';

my @events;
my $event;
my $in_closure = 0;
my @closure;
for my $line (@lines[1 .. $#lines])
{
  if ($line eq 'template-closure-events')
  {
    $in_closure = 1;
    $event = undef;
    next;
  }
  if (!$in_closure &&
      $line =~ /^  ([a-z-]+) at (.+):(\d+):(\d+)$/)
  {
    $event = {
      kind => $1,
      path => $2,
      line => 0 + $3,
      column => 0 + $4,
      fields => [],
      bindings => [],
    };
    push @events, $event;
    next;
  }
  if (!$in_closure && $line =~ /^    (.+)$/)
  {
    die "witness field has no source event: $line\n" if !defined($event);
    push @{$event->{fields}}, $1;
    if ($1 =~ /^bind #(\d+) = (.+) source=(explicit|deduced|defaulted)$/)
    {
      push @{$event->{bindings}}, [0 + $1, $2, $3];
    }
    next;
  }
  push @closure, $line if $in_closure && $line ne '';
}

die "witness has no source events\n" if !@events;
for my $index (1 .. $#events)
{
  my $previous = $events[$index - 1];
  my $current = $events[$index];
  die "source events are not ordered by location\n"
    if $previous->{path} gt $current->{path} ||
       ($previous->{path} eq $current->{path} &&
        ($previous->{line} > $current->{line} ||
         ($previous->{line} == $current->{line} &&
          $previous->{column} > $current->{column})));
}

my $relative_source = File::Spec->abs2rel($source, getcwd());
$relative_source =~ s{\\}{/}g;
for my $source_event (@events)
{
  die "source path is not relative to the invocation directory: " .
      "$source_event->{path}\n"
    if $source_event->{path} ne $relative_source;
}

my @source_lines = split /\n/, slurp($source);
for my $source_event (@events)
{
  next unless grep { $_ eq 'template box' || $_ eq 'callee inspect' }
    @{$source_event->{fields}};
  my $needle = (grep { $_ eq 'callee inspect' }
    @{$source_event->{fields}}) ? 'inspect' : 'box';
  my $source_line = $source_lines[$source_event->{line} - 1];
  die "reported source line is outside the fixture\n"
    if !defined($source_line);
  my $at = index($source_line, $needle);
  die "reported source line does not contain $needle\n" if $at < 0;
  die "$needle source column is not its written terminal name\n"
    if $source_event->{column} != $at + 1;
}

sub has_field
{
  my ($source_event, $wanted) = @_;
  return scalar grep { $_ eq $wanted } @{$source_event->{fields}};
}

my @box_events = grep {
  $_->{kind} eq 'class-use' && has_field($_, 'template box')
} @events;
die "expected two primary box source uses\n" if @box_events != 2;
for my $box (@box_events)
{
  die "box did not select its primary template\n"
    if !has_field($box, 'selected primary');
  die "box binding shape changed\n" if @{$box->{bindings}} != 2;
  die "box's written argument is not explicit\n"
    if $box->{bindings}[0][2] ne 'explicit';
  die "box's omitted argument is not defaulted\n"
    if $box->{bindings}[1][1] ne 'long' ||
       $box->{bindings}[1][2] ne 'defaulted';
}
my @written_types = sort map { $_->{bindings}[0][1] } @box_events;
die "changing a written type did not change only its typed binding\n"
  if join(',', @written_types) ne 'char,int';
my @box_shapes;
for my $box (@box_events)
{
  my @shape = @{$box->{fields}};
  for (@shape)
  {
    s/^bind #1 = .+ source=explicit$/bind #1 = <written-type> source=explicit/;
  }
  push @box_shapes, join("\n", @shape);
}
die "changing a written type changed the class-use event shape\n"
  if $box_shapes[0] ne $box_shapes[1];

my @inspect_calls = grep {
  $_->{kind} eq 'function-call' && has_field($_, 'callee inspect')
} @events;
die "two evaluated inspect calls were not both published\n"
  if @inspect_calls != 2;
for my $call (@inspect_calls)
{
  die "inspect did not select a template instantiation\n"
    if !has_field($call, 'selected instantiation');
  die "inspect bindings were not both deduced\n"
    if @{$call->{bindings}} != 2 ||
       grep { $_->[2] ne 'deduced' } @{$call->{bindings}};
}
die "non-template overload selection was published as a template call\n"
  if grep { has_field($_, 'callee shadowed') } @events;

my $closure_text = join("\n", @closure);
my $inspect_instantiations = () =
  $closure_text =~ /  function-instantiation\n    entity inspect(?:\n|$)/g;
my $inspect_requirements = () =
  $closure_text =~ /  require-definition\n    entity inspect(?:\n|$)/g;
die "repeated calls did not deduplicate function instantiation\n"
  if $inspect_instantiations != 1;
die "repeated calls did not deduplicate definition demand\n"
  if $inspect_requirements != 1;

print "template witness contract: PASS\n";
