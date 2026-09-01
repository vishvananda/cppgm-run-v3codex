#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Find qw(find);
use File::Spec;
use FindBin;

my $root = abs_path("$FindBin::Bin/..");

# E0 freezes the pre-migration ceiling.  Later phases lower these ceilings as
# each family is converted, and E8 sets the generic-policy ceilings to zero.
my %ceiling = (
	logic_throw => 934,
	runtime_throw => 575,
	generic_throw_files => 136,
	generic_return_helper => 1,
	catch_all => 14,
	internal_runtime_catch => 1,
	internal_exception_catch => 3,
	legacy_not_implemented => 0,
	message_policy => 0,
	terminal_untyped_standard_catch => 0,
);

sub read_text
{
	my ($path) = @_;
	open(my $fh, '<', $path) or die "unable to read $path: $!\n";
	local $/;
	my $text = <$fh>;
	close($fh) or die "unable to close $path: $!\n";
	return defined($text) ? $text : '';
}

sub mask_segment
{
	my ($segment) = @_;
	$segment =~ s/[^\n]/ /g;
	return $segment;
}

sub mask_comments_and_literals
{
	my ($text) = @_;
	$text =~ s{
		(?:u8|u|U|L)?R"([^\s()\\]{0,16})\(
		(.*?)
		\)\1"
	}{mask_segment($&)}gsex;
	$text =~ s{
		//[^\n]*
		|/\*.*?\*/
		|"(?:\\.|[^"\\])*"
		|'(?:\\.|[^'\\])*'
	}{mask_segment($&)}gsex;
	return $text;
}

sub relative_path
{
	my ($path) = @_;
	my $relative = File::Spec->abs2rel($path, $root);
	$relative =~ s{\\}{/}g;
	return $relative;
}

sub line_number
{
	my ($text, $offset) = @_;
	return 1 + (substr($text, 0, $offset) =~ tr/\n/\n/);
}

my @files;
find({
	wanted => sub {
		return if !-f $_;
		return if $_ !~ /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)\z/;
		push @files, $File::Find::name;
	},
	no_chdir => 1,
}, "$root/dev/src");
for my $path (glob "$root/dev/*")
{
	push @files, $path if -f $path &&
		$path =~ /\.(?:c|cc|cpp|cxx|h|hh|hpp|hxx)\z/;
}
@files = sort @files;

my %count = map { $_ => 0 } keys %ceiling;
my %generic_file;
my @message_policy;
my @cleanup_catch;
my @terminal_catch;
my @standard_translation;

for my $path (@files)
{
	my $relative = relative_path($path);
	my $text = read_text($path);
	my $code = mask_comments_and_literals($text);
	my ($file_logic, $file_runtime) = (0, 0);
	my ($file_terminal_standard, $file_terminal_compiler) = (0, 0);
	$count{legacy_not_implemented} += () =
		$code =~ /\b(?:NotImplementedException|CPPGM_EXIT_NOT_IMPLEMENTED)\b/g;

	while ($text =~ /\bthrow\s+(?:std::)?logic_error\b/g)
	{
		++$count{logic_throw};
		++$file_logic;
		$generic_file{$relative} = 1;
	}
	while ($text =~ /\bthrow\s+(?:std::)?runtime_error\b/g)
	{
		++$count{runtime_throw};
		++$file_runtime;
		$generic_file{$relative} = 1;
	}
	print "$relative logic=$file_logic runtime=$file_runtime\n"
		if $ENV{CPPGM_EXCEPTION_AUDIT_VERBOSE} &&
		($file_logic || $file_runtime);
	while ($text =~ /\breturn\s+(?:std::)?(?:logic_error|runtime_error)\s*\(/g)
	{
		++$count{generic_return_helper};
	}

	while ($text =~ /\bcatch\s*\(\s*\.\.\.\s*\)/g)
	{
		++$count{catch_all};
		push @cleanup_catch, "$relative:" . line_number($text, $-[0]);
	}
	while ($text =~ /\bcatch\s*\(\s*const\s+(?:std::)?runtime_error\s*&/g)
	{
		++$count{internal_runtime_catch} if $relative =~ m{\Adev/src/};
	}
	while ($text =~ /\bcatch\s*\(\s*const\s+(?:std::)?exception\s*&/g)
	{
		if ($relative =~ m{\Adev/src/})
		{
			++$count{internal_exception_catch};
		}
		else
		{
			++$file_terminal_standard;
			push @terminal_catch,
				"$relative:" . line_number($text, $-[0]);
		}
	}
	if ($relative !~ m{\Adev/src/})
	{
		$file_terminal_compiler += () =
			$text =~ /\bcatch\s*\(\s*const\s+CompilerError\s*&/g;
		$count{terminal_untyped_standard_catch} +=
			$file_terminal_standard - $file_terminal_compiler
			if $file_terminal_standard > $file_terminal_compiler;
	}
	while ($text =~ /\bcatch\s*\(\s*const\s+std::(?:invalid_argument|out_of_range)\s*&/g)
	{
		push @standard_translation,
			"$relative:" . line_number($text, $-[0]);
	}

	# Diagnostic text may be printed at a terminal boundary.  Searching or
	# comparing it is policy and is forbidden.  Masked literals keep message
	# prose and test strings from becoming false positives.
	while ($code =~ /([^\n;{}]*\.what\s*\(\s*\)[^\n;{}]*)/g)
	{
		my $statement = $1;
		next if $statement !~ /(?:==|!=|\.find\s*\(|\.compare\s*\(|regex)/;
		++$count{message_policy};
		push @message_policy,
			"$relative:" . line_number($code, $-[1]);
	}
}

$count{generic_throw_files} = scalar(keys %generic_file);

my @error;
for my $kind (sort keys %ceiling)
{
	push @error, "$kind count $count{$kind} exceeds E0 ceiling $ceiling{$kind}"
		if $count{$kind} > $ceiling{$kind};
}
push @error, "exception-message policy at $_" for @message_policy;

if (@error)
{
	print STDERR "Compiler exception audit failed with " . scalar(@error) .
		" error(s):\n";
	print STDERR "  $_\n" for @error;
	exit 1;
}

print "Compiler exception audit passed at E0 ceilings: " .
	"$count{logic_throw} logic throws, $count{runtime_throw} runtime throws, " .
	"$count{generic_throw_files} files, " .
	"$count{generic_return_helper} generic return helper, " .
	"$count{catch_all} catch-all sites, " .
	"$count{internal_runtime_catch} internal runtime catches, " .
	"$count{internal_exception_catch} internal standard catches, and " .
	"$count{message_policy} message-policy sites; " .
	"$count{legacy_not_implemented} legacy not-implemented references and " .
	"$count{terminal_untyped_standard_catch} untyped terminal fallbacks.\n";
print "Reviewed presentation/cleanup inventory: " . scalar(@terminal_catch) .
	" terminal standard catches, " . scalar(@cleanup_catch) .
	" catch-all sites, and " . scalar(@standard_translation) .
	" narrow standard translations.\n";
