#!/usr/bin/env perl
use strict;
use warnings;

use Cwd qw(abs_path);
use File::Find qw(find);
use FindBin qw($Bin);
use File::Temp qw(tempdir);
use POSIX ();

sub detect_jobs {
  my $jobs = $ENV{CPPGM_TEST_JOBS};
  return $jobs if defined($jobs) && $jobs =~ /^\d+\z/ && $jobs > 0;
  my $detected = `getconf _NPROCESSORS_ONLN 2>/dev/null`;
  $detected = "" unless defined $detected;
  chomp($detected);
  return $detected if $detected =~ /^\d+\z/ && $detected > 0;
  return 1;
}

sub usage {
  die "usage: run_object_lowir_roundtrip_tests.pl [--debuginfo] --app APP (--test-root DIR | --test FILE)...\n";
}

my $app = "../dev/cppgm++";
my $debuginfo = 0;
my @roots;
my @tests;
my $repo_root = abs_path("$Bin/../..");
my $cwd = abs_path(".");

while(@ARGV) {
  my $arg = shift @ARGV;
  if($arg eq "--app") {
    usage() unless @ARGV;
    $app = shift @ARGV;
  } elsif($arg eq "--test-root") {
    usage() unless @ARGV;
    push @roots, shift @ARGV;
  } elsif($arg eq "--test") {
    usage() unless @ARGV;
    push @tests, shift @ARGV;
  } elsif($arg eq "--debuginfo") {
    $debuginfo = 1;
  } elsif($arg eq "-h" || $arg eq "--help") {
    usage();
  } else {
    die "unknown argument: $arg\n";
  }
}

sub collect_tests {
  my @out;
  for my $root (@roots) {
    if(-f $root) {
      push @out, $root;
      next;
    }
    next unless -d $root;
    my @found;
    find(
      {
        wanted => sub {
          return unless -f $_;
          return unless /\.(?:cpp|t)\z/;
          push @found, $File::Find::name;
        },
        no_chdir => 1,
      },
      $root);
    push @out, sort @found;
  }
  push @out, @tests;

  my %seen;
  return grep { !$seen{$_}++ } @out;
}

sub harness_sources {
  my ($test) = @_;
  if($test =~ /\.t\z/) {
    my @numbered;
    for my $candidate (glob("$test.*")) {
      next unless $candidate =~ /\.t\.(\d+)\z/;
      push @numbered, [$1, $candidate];
    }
    my @sources = map { $_->[1] }
      sort { $a->[0] <=> $b->[0] || $a->[1] cmp $b->[1] } @numbered;
    return @sources if @sources;
  }
  return ($test);
}

sub run_command {
  my (@cmd) = @_;
  system { $cmd[0] } @cmd;
  my $status = $?;
  return undef if $status == 0;
  my $exit_code = $status & 127 ? 128 + ($status & 127) : ($status >> 8);
  return "command failed with exit status $exit_code:\n  " . join(" ", @cmd) . "\n";
}

sub read_bytes {
  my ($path) = @_;
  open(my $fh, "<:raw", $path) or die "open $path: $!\n";
  local $/;
  my $data = <$fh>;
  close($fh) or die "close $path: $!\n";
  return defined($data) ? $data : "";
}

sub object_summary {
  my ($path) = @_;
  open(my $fh, "-|", "nm", "-a", $path) or return "";
  my @lines = grep { /\S/ } <$fh>;
  close($fh);
  @lines = sort @lines;
  splice(@lines, 80) if @lines > 80;
  return join("", @lines);
}

sub tool_output {
  my (@cmd) = @_;
  open(my $fh, "-|", @cmd) or return undef;
  local $/;
  my $data = <$fh>;
  return undef unless close($fh);
  return defined($data) ? $data : "";
}

sub gnu_section_relationship_error {
  my ($path, $label) = @_;
  my $sections = tool_output("readelf", "-SW", $path);
  return "$label: unable to inspect ELF sections\n" unless defined $sections;
  my ($section_index) = $sections =~
    /^\s*\[\s*(\d+)\]\s+cppgmsec\s+PROGBITS\b/m;
  return "$label: object has no cppgmsec PROGBITS section\n"
    unless defined $section_index;

  my $symbols = tool_output("readelf", "-sW", $path);
  return "$label: unable to inspect ELF symbols\n" unless defined $symbols;
  my ($symbol_section) = $symbols =~
    /^\s*\d+:\s+\S+\s+\d+\s+\S+\s+\S+\s+\S+\s+(\d+)\s+section_alias\s*$/m;
  return "$label: section_alias is absent from the ELF symbol table\n"
    unless defined $symbol_section;
  return "$label: section_alias is not owned by cppgmsec\n"
    unless $symbol_section == $section_index;

  my $relocations = tool_output("readelf", "-rW", $path);
  return "$label: unable to inspect ELF relocations\n"
    unless defined $relocations;
  my ($section_relocations) = $relocations =~
    /Relocation section '\.relacppgmsec'[^\n]*\n(.*?)(?=\nRelocation section |\z)/s;
  return "$label: cppgmsec has no relocation section\n"
    unless defined $section_relocations;
  return "$label: cppgmsec relocation does not target section_alias\n"
    unless $section_relocations =~ /\bsection_alias\b/;
  return undef;
}

sub safe_name {
  my ($path) = @_;
  $path =~ s/[^A-Za-z0-9]/_/g;
  return $path;
}

sub modes_for {
  my ($test) = @_;
  my @modes = $debuginfo
    ? (["-gline-tables-only", "-O0", "O0"],
       ["-gline-tables-only", "-O1", "O1"],
       ["-gline-tables-only", "-O2", "O2"],
       ["-gline-tables-only", "-O3", "O3"])
    : ([undef, "-O0", "O0"], [undef, "-O1", "O1"],
       [undef, "-O2", "O2"], [undef, "-O3", "O3"]);
  if(!$debuginfo && $test =~ /default-no-optimization/) {
    push @modes, [undef, undef, "default"];
  }
  return @modes;
}

# Scratch basename for one unit. Keeps the descriptive source/mode name the
# serial implementation used -- it shows up in failure messages -- and appends
# the unit index so two sources whose names differ only in punctuation cannot
# collide now that units run concurrently.
sub unit_base {
  my ($source, $mode, $temp, $slot) = @_;
  my $debug_flag = $mode->[0];
  my $level_name = $mode->[2];
  my $debug_label = defined($debug_flag) ? $debug_flag : "nodebug";
  return "$temp/" . safe_name($source) . "." . safe_name("$debug_label.$level_name") . ".u$slot";
}

# One (source, mode) unit: three compiler invocations plus the direct vs
# from-lowir comparison. Modes are independent of one another, so these are the
# units spread across workers -- a single hosted test costs three compiler
# invocations per mode, and running its modes back to back set the floor for
# the whole bucket.
sub check_mode {
  my ($source, $mode, $temp, $slot) = @_;
  my ($debug_flag, $opt, $level_name) = @$mode;
  my $debug_label = defined($debug_flag) ? $debug_flag : "nodebug";
  my $base = unit_base($source, $mode, $temp, $slot);
  my $direct = "$base.direct.o";
  my $lowir = "$base.lowir";
  my $from_lowir = "$base.from-lowir.o";

  my @debug_flags = defined($debug_flag) ? ($debug_flag) : ("-g0");
  my @optimization_flags = defined($opt) ? ($opt) : ();
  my @direct_cmd = ($app, "-c", @debug_flags, @optimization_flags);
  my $direct_error = run_command(@direct_cmd, "-o", $direct, $source);
  return $direct_error if defined $direct_error;
  # The O0 emission must preprocess exactly like the direct compile. An
  # explicit O1+ invocation publishes __OPTIMIZE__; O0 and the default do not.
  my @preprocess_flags =
    (defined($opt) && $opt ne "-O0") ? ("-D__OPTIMIZE__=1") : ();
  my $emit_error = run_command($app,
                               "--emit-lowir",
                               @debug_flags,
                               "-O0",
                               @preprocess_flags,
                               "-o",
                               $lowir,
                               $source);
  return $emit_error if defined $emit_error;
  if($source =~ /gnu-section-attribute/) {
    my $lowir_text = read_bytes($lowir);
    my ($metadata) = $lowir_text =~
      /^global \@section_alias\b[^\n]*\[([^\]]+)\]\s*=\s*addr \@section_alias$/m;
    return "serialized LowIR lost the section_alias global relationship: $source $level_name\n"
      unless defined $metadata;
    return "serialized LowIR lost section=cppgmsec: $source $level_name\n"
      unless $metadata =~ /(?:^|,\s*)section=cppgmsec(?:,|$)/;
  }
  if($source =~ /stable-prefix-query-replay/) {
    my $lowir_text = read_bytes($lowir);
    my ($boundary) = $lowir_text =~
      /^(function \@stable_prefix_query\([^\n]+\)[^\n]+)$/m;
    return "serialized LowIR lost the stable-prefix query: $source $level_name\n"
      unless defined($boundary) && $boundary =~ /\bquery=stable_prefix\b/;
  }
  my $from_lowir_error = run_command($app,
                                     "-c",
                                     @debug_flags,
                                     @optimization_flags,
                                     "-o",
                                     $from_lowir,
                                     $lowir);
  return $from_lowir_error if defined $from_lowir_error;

  if($source =~ /gnu-section-attribute/) {
    my $direct_relationship = gnu_section_relationship_error(
      $direct, "$source $level_name direct");
    return $direct_relationship if defined $direct_relationship;
    my $replayed_relationship = gnu_section_relationship_error(
      $from_lowir, "$source $level_name replayed");
    return $replayed_relationship if defined $replayed_relationship;
  }

  my $direct_bytes = read_bytes($direct);
  my $from_lowir_bytes = read_bytes($from_lowir);
  if($direct_bytes eq $from_lowir_bytes) {
    if($source =~ /stable-prefix-query-replay/) {
      my $direct_object = "$base.direct.obj";
      my $replayed_object = "$base.from-lowir.obj";
      my $object_error = run_command(
        $app, "-c", @debug_flags, @optimization_flags,
        "-o", $direct_object, $source);
      return $object_error if defined $object_error;
      $object_error = run_command(
        $app, "-c", @debug_flags, @optimization_flags,
        "-o", $replayed_object, $lowir);
      return $object_error if defined $object_error;
      return "private compiler object differs after LowIR replay: " .
        "$source $debug_label $level_name\n"
        unless read_bytes($direct_object) eq read_bytes($replayed_object);
      my $program = "$base.replayed-program";
      my $link_error = run_command(
        $app, @optimization_flags, "-o", $program, $replayed_object);
      return $link_error if defined $link_error;
      my $run_error = run_command($program);
      return $run_error if defined $run_error;
    }
    return undef;
  }

  return "object differs after compiling serialized LowIR: $source $debug_label $level_name\n"
    . "direct bytes: " . length($direct_bytes) . "\n"
    . "from-lowir bytes: " . length($from_lowir_bytes) . "\n"
    . "direct symbols:\n" . object_summary($direct)
    . "from-lowir symbols:\n" . object_summary($from_lowir);
}

sub append_keep_going_summary {
  my ($passed, $total, $failed) = @_;
  if(open(my $fh, '>>', "$repo_root/.test_counts")) {
    print $fh "$passed $total\n";
    close($fh);
  }
  if($failed) {
    system('touch', "$cwd/.test_failed");
  }
}

my @selected = collect_tests();
die "no object-roundtrip tests selected\n" unless @selected;

my $keep_going = $ENV{KEEP_GOING};
if(!$keep_going) {
  print "pa37 object-roundtrip";
  print " debuginfo" if $debuginfo;
  print ": running ", scalar(@selected), " test";
  print "s" if @selected != 1;
  print "\n";
}

my $temp = tempdir("cppgm-object-lowir-roundtrip.XXXXXX", TMPDIR => 1, CLEANUP => 1);
my $passed = 0;
my $failed = 0;

# Flatten the work to (source, mode) units. Sources within a test and modes
# within a source are all independent, which exposes far more parallelism than
# one unit per test: the slowest hosted test alone ran nine compiler
# invocations back to back and set the floor for the entire bucket.
my @units;
my @test_units;  # test index -> unit indices, in serial-report order
for my $ti (0 .. $#selected) {
  my $test = $selected[$ti];
  die "missing object-roundtrip test source: $test\n" unless -f $test;
  $test_units[$ti] = [];
  for my $source (harness_sources($test)) {
    die "missing object-roundtrip test source: $source\n" unless -f $source;
    for my $mode (modes_for($test)) {
      push @units, { source => $source, mode => $mode };
      push @{ $test_units[$ti] }, $#units;
    }
  }
}

sub default_optimization_error {
  my ($test, $unit_indices) = @_;
  return undef if $debuginfo || $test !~ /default-no-optimization/;

  my %unit_by_source_level;
  my @sources;
  my %seen_source;
  for my $u (@$unit_indices) {
    my $source = $units[$u]{source};
    push @sources, $source unless $seen_source{$source}++;
    my $level_name = $units[$u]{mode}[2];
    $unit_by_source_level{$source}{$level_name} = $u;
  }

  for my $source (@sources) {
    my $default_u = $unit_by_source_level{$source}{default};
    my $default_path = unit_base($source, $units[$default_u]{mode},
                                 $temp, $default_u) . ".direct.o";
    my $default_bytes = read_bytes($default_path);
    for my $level_name ("O0") {
      my $level_u = $unit_by_source_level{$source}{$level_name};
      my $level_path = unit_base($source, $units[$level_u]{mode},
                                 $temp, $level_u) . ".direct.o";
      my $level_bytes = read_bytes($level_path);
      next if $default_bytes eq $level_bytes;
      return "default object differs from no optimization: $source default/$level_name\n"
        . "default bytes: " . length($default_bytes) . "\n"
        . "$level_name bytes: " . length($level_bytes) . "\n";
    }
  }
  return undef;
}

sub run_unit {
  my ($u) = @_;
  my $error = check_mode($units[$u]{source}, $units[$u]{mode}, $temp, $u);
  return unless defined $error;
  if(open(my $fh, '>', "$temp/error.$u")) {
    print $fh $error;
    close($fh);
  }
}

my $jobs = detect_jobs();
$jobs = scalar(@units) if $jobs > scalar(@units);

if($jobs > 1) {
  STDOUT->flush();
  STDERR->flush();
  my @pids;
  for my $slot (0 .. $jobs - 1) {
    my $pid = fork();
    die "unable to fork object-roundtrip worker: $!\n" unless defined $pid;
    if($pid == 0) {
      # Children must not run the parent's File::Temp CLEANUP handler, so they
      # leave via POSIX::_exit rather than exit.
      run_unit($_) for grep { $_ % $jobs == $slot } 0 .. $#units;
      POSIX::_exit(0);
    }
    push @pids, $pid;
  }
  waitpid($_, 0) for @pids;
} else {
  run_unit($_) for 0 .. $#units;
}

# Reduce unit results back to per-test verdicts in the order the serial
# implementation would have reported them: the first failing mode of the first
# failing source.
my @test_errors;
for my $ti (0 .. $#selected) {
  for my $u (@{ $test_units[$ti] }) {
    next unless -f "$temp/error.$u";
    $test_errors[$ti] = read_bytes("$temp/error.$u");
    last;
  }
  if(!defined($test_errors[$ti])) {
    $test_errors[$ti] = default_optimization_error(
      $selected[$ti], $test_units[$ti]);
  }
}

# Without KEEP_GOING the serial loop stopped at the first failing test, so only
# that failure is reported and only earlier passes are counted.
my $stop_at = $#selected + 1;
if(!$keep_going) {
  for my $ti (0 .. $#selected) {
    if(defined $test_errors[$ti]) {
      $stop_at = $ti;
      last;
    }
  }
}
for my $ti (0 .. $#selected) {
  last if $ti > $stop_at;
  if(defined $test_errors[$ti]) {
    my $error = $test_errors[$ti];
    print STDERR "$selected[$ti]: $error";
    print STDERR "\n" unless $error =~ /\n\z/;
    $failed = 1;
    next;
  }
  ++$passed if $ti < $stop_at;
}

append_keep_going_summary($passed, scalar(@selected), $failed) if $keep_going;

if($failed) {
  print "pa37 object-roundtrip";
  print " debuginfo" if $debuginfo;
  print ": FAIL ($passed/", scalar(@selected), ")\n";
  exit 1;
}

if(!$keep_going) {
  print "pa37 object-roundtrip";
  print " debuginfo" if $debuginfo;
  print ": PASS (", scalar(@selected), "/", scalar(@selected), ")\n";
}
