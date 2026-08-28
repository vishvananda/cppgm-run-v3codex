#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Temp qw(tempdir);
use FindBin;
use Getopt::Long qw(GetOptions);
use Text::ParseWords qw(shellwords);

my $root = abs_path("$FindBin::Bin/..");
my $baseline = '';
my $candidate = '';
my $manifest = "$root/doc/compiler-refactor-output-cases.tsv";
GetOptions(
	'baseline=s' => \$baseline,
	'candidate=s' => \$candidate,
	'manifest=s' => \$manifest,
) or die "invalid arguments\n";
die "usage: $0 --baseline <tool-dir> --candidate <tool-dir>\n"
	if $baseline eq '' || $candidate eq '';
$baseline = abs_path($baseline) // die "cannot resolve baseline tool directory\n";
$candidate = abs_path($candidate) // die "cannot resolve candidate tool directory\n";
$manifest = abs_path($manifest) // die "cannot resolve output manifest\n";

sub read_bytes
{
	my ($path) = @_;
	open(my $fh, '<:raw', $path) or die "unable to read $path: $!\n";
	local $/;
	my $bytes = <$fh>;
	close($fh) or die "unable to close $path: $!\n";
	return defined($bytes) ? $bytes : '';
}

sub expand_value
{
	my ($value, $input, $output) = @_;
	$value =~ s/\{root\}/$root/g;
	$value =~ s/\{input\}/$input/g;
	$value =~ s/\{output\}/$output/g;
	return $value;
}

sub run_case
{
	my ($tool_root, $case, $tmp) = @_;
	my $tool = "$tool_root/$case->{tool}";
	die "missing refactor-oracle tool $tool\n" if !-x $tool;
	my $input = $case->{input} eq '-' ? '/dev/null' :
		"$root/$case->{input}";
	die "missing refactor-oracle input $input\n" if !-f $input;
	my $output = "$tmp/$case->{name}.output";
	my @args = map { expand_value($_, $input, $output) }
		shellwords($case->{args});
	my $stdout_path = "$tmp/run.stdout";
	my $stderr_path = "$tmp/run.stderr";
	unlink($output, $stdout_path, $stderr_path);

	my $pid = fork();
	die "unable to fork: $!\n" if !defined($pid);
	if ($pid == 0)
	{
		open(STDIN, '<:raw', $case->{stdin} eq 'yes' ? $input : '/dev/null')
			or die "unable to open stdin: $!\n";
		open(STDOUT, '>:raw', $stdout_path)
			or die "unable to open stdout: $!\n";
		open(STDERR, '>:raw', $stderr_path)
			or die "unable to open stderr: $!\n";
		exec {$tool} $tool, @args;
		die "unable to execute $tool: $!\n";
	}
	waitpid($pid, 0);
	my $status = $?;
	my %result = (
		status => $status,
		stdout => read_bytes($stdout_path),
		stderr => read_bytes($stderr_path),
		output => '',
	);
	if ($case->{output} eq 'file')
	{
		die "$case->{name}: tool did not create $output\n" if !-f $output;
		$result{output} = read_bytes($output);
	}
	return \%result;
}

open(my $manifest_fh, '<', $manifest)
	or die "unable to read $manifest: $!\n";
my $header = <$manifest_fh>;
chomp($header) if defined($header);
die "$manifest has an invalid header\n" if !defined($header) ||
	$header ne "name\ttool\tinput\tstdin\targs\toutput";
my @cases;
my %case_name;
my $line = 1;
while (my $row = <$manifest_fh>)
{
	++$line;
	chomp($row);
	next if $row eq '';
	my @field = split /\t/, $row, -1;
	die "$manifest:$line must contain six fields\n"
		if scalar(@field) != 6;
	die "$manifest:$line has duplicate case '$field[0]'\n"
		if $case_name{$field[0]}++;
	die "$manifest:$line has invalid stdin mode '$field[3]'\n"
		if $field[3] !~ /\A(?:yes|no)\z/;
	die "$manifest:$line has invalid output mode '$field[5]'\n"
		if $field[5] !~ /\A(?:none|file)\z/;
	push @cases, {
		name => $field[0], tool => $field[1], input => $field[2],
		stdin => $field[3], args => $field[4], output => $field[5],
	};
}
close($manifest_fh) or die "unable to close $manifest: $!\n";
die "$manifest contains no cases\n" if !@cases;

my @error;
for my $case (@cases)
{
	my $tmp = tempdir('cppgm-refactor-output-XXXXXX', TMPDIR => 1,
		CLEANUP => 1);
	my $before = run_case($baseline, $case, $tmp);
	my $after = run_case($candidate, $case, $tmp);
	for my $field (qw(status stdout stderr output))
	{
		push @error, "$case->{name}: $field differs"
			if $before->{$field} ne $after->{$field};
	}
	print "MATCH $case->{name}\n" if !grep {
		/^\Q$case->{name}\E:/
	} @error;
}

if (@error)
{
	print STDERR "Compiler refactor output comparison failed:\n";
	print STDERR "  $_\n" for @error;
	exit 1;
}
print "Compiler refactor output comparison passed (" . scalar(@cases) .
	" cases).\n";

