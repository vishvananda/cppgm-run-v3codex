#!/usr/bin/perl

use strict;
use warnings;

use Cwd qw(abs_path);
use File::Find qw(find);
use File::Spec;
use FindBin;

my $root = abs_path("$FindBin::Bin/..");

# E8 closes the migration.  Generic exception policy and catch-all handlers
# are forbidden; allocation rollback is represented by scoped ownership.
my %ceiling = (
	logic_throw => 0,
	runtime_throw => 0,
	generic_throw_files => 0,
	generic_return_helper => 0,
	catch_all => 0,
	internal_runtime_catch => 0,
	internal_exception_catch => 0,
	legacy_not_implemented => 0,
	message_policy => 0,
	terminal_untyped_standard_catch => 0,
	foreign_explicit_throw => 0,
);

my %explicit_throw_type = map { $_ => 1 } qw(
	Error
	parser.Error
	InvocationError
	InputOutputError
	InternalCompilerError
	SourceError
	SyntaxError
	SemanticError
	HardSemanticError
	SerializedInputError
	ResourceLimitError
	std::bad_alloc
);

# Every generic standard catch is a last-resort executable boundary paired
# with a CompilerError catch.  Counts make additions and moves explicit.
my %terminal_allowlist = (
	'dev/abimangle.cpp' => 1,
	'dev/cppgm++.cpp' => 1,
	'dev/ctrlexpr.cpp' => 1,
	'dev/cy86.cpp' => 1,
	'dev/lowir2cy86.cpp' => 1,
	'dev/lowir2native.cpp' => 1,
	'dev/lowiropt.cpp' => 1,
	'dev/macro.cpp' => 1,
	'dev/nsdecl.cpp' => 1,
	'dev/nsinit.cpp' => 1,
	'dev/posttoken.cpp' => 1,
	'dev/pptoken.cpp' => 1,
	'dev/preproc.cpp' => 1,
	'dev/recog.cpp' => 2,
);

# std::stoll/stoull expose only these standard types.  The ABI parser converts
# them immediately to coded project-owned fact-input failures.
my %translation_allowlist = (
	'dev/src/abi/itanium/abi_mangle_parse.cpp' => 4,
);

# Cold throw-helper interfaces deliberately expose declarations without the
# exception taxonomy.  Pulling the full hierarchy back through one of these
# broadly included headers recreates per-translation-unit RTTI/EH work even
# though successful compilation does not throw.
my %lightweight_error_header = map { $_ => 1 } qw(
	dev/src/abi/itanium/abi_mangle_errors.h
	dev/src/compiler_object/errors.h
	dev/src/cy86/errors.h
	dev/src/lowering/support/errors.h
	dev/src/lowir/optimize/errors.h
	dev/src/native/errors.h
	dev/src/support/driver_errors.h
	dev/src/support/exceptions.h
);
my $taxonomy_include_ceiling = 32;

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
my %terminal_seen;
my %translation_seen;
my @foreign_explicit_throw;
my $explicit_throw_sites = 0;
my @bad_alloc_throw;
my @heavy_error_header;
my @taxonomy_include;

for my $path (@files)
{
	my $relative = relative_path($path);
	my $text = read_text($path);
	my $code = mask_comments_and_literals($text);
	if ($text =~ /^\s*#\s*include\s*[<"]support\/exception_types\.h[>"]/m)
	{
		push @taxonomy_include, $relative;
	}
	if ($lightweight_error_header{$relative} &&
		($text =~ /^\s*#\s*include\s*[<"]support\/exception_types\.h[>"]/m ||
		 $code =~ /\bthrow\b/))
	{
		push @heavy_error_header, $relative;
	}
	my ($file_logic, $file_runtime) = (0, 0);
	my ($file_terminal_standard, $file_terminal_compiler) = (0, 0);
	$count{legacy_not_implemented} += () =
		$code =~ /\b(?:NotImplementedException|CPPGM_EXIT_NOT_IMPLEMENTED)\b/g;
	while ($code =~ /\bthrow\s+(?!;)\s*(parser\s*\.\s*Error|(?:std::)?[A-Za-z_]\w*)\b/g)
	{
		++$explicit_throw_sites;
		my $type = $1;
		$type =~ s/\s+//g;
		if ($type eq 'std::bad_alloc')
		{
			push @bad_alloc_throw,
				"$relative:" . line_number($code, $-[0]);
		}
		if (!$explicit_throw_type{$type} ||
			($type eq 'Error' && $relative !~
				m{\Adev/src/syntax/parser/(?:cursor\.h|parser\.cpp)\z}) ||
			($type eq 'parser.Error' && $relative !~
				m{\Adev/src/syntax/}))
		{
			++$count{foreign_explicit_throw};
			push @foreign_explicit_throw,
				"$relative:" . line_number($code, $-[0]) . " ($type)";
		}
	}

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
			++$terminal_seen{$relative};
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
		++$translation_seen{$relative};
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
	push @error, "$kind count $count{$kind} exceeds architecture limit $ceiling{$kind}"
		if $count{$kind} > $ceiling{$kind};
}
push @error, "exception-message policy at $_" for @message_policy;
push @error, "foreign explicit exception type at $_"
	for @foreign_explicit_throw;
push @error, "allocator-protocol bad_alloc inventory is " .
	scalar(@bad_alloc_throw) . ", expected exactly 2 in optimizer arena"
	if @bad_alloc_throw != 2 ||
		grep { $_ !~ m{\Adev/src/lowir/optimize/pipeline\.cpp:} }
			@bad_alloc_throw;
push @error, "lightweight throw-helper header owns taxonomy or throw code: $_"
	for @heavy_error_header;
push @error, "full exception taxonomy include fanout is " .
	scalar(@taxonomy_include) . ", architecture ceiling is " .
	$taxonomy_include_ceiling
	if @taxonomy_include > $taxonomy_include_ceiling;
for my $path (sort keys %terminal_seen)
{
	push @error, "unreviewed terminal standard catch in $path"
		if !exists($terminal_allowlist{$path});
}
for my $path (sort keys %terminal_allowlist)
{
	my $actual = $terminal_seen{$path} || 0;
	push @error, "terminal catch count in $path is $actual, expected " .
		$terminal_allowlist{$path}
		if $actual != $terminal_allowlist{$path};
}
for my $path (sort keys %translation_seen)
{
	push @error, "unreviewed standard-exception translation in $path"
		if !exists($translation_allowlist{$path});
}
for my $path (sort keys %translation_allowlist)
{
	my $actual = $translation_seen{$path} || 0;
	push @error, "standard translation count in $path is $actual, expected " .
		$translation_allowlist{$path}
		if $actual != $translation_allowlist{$path};
}

if (@error)
{
	print STDERR "Compiler exception audit failed with " . scalar(@error) .
		" error(s):\n";
	print STDERR "  $_\n" for @error;
	exit 1;
}

print "Compiler exception audit passed at E8 architecture limits: " .
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
	" narrow standard translations; $explicit_throw_sites explicit typed " .
	"throw sites.  Full taxonomy fanout is " . scalar(@taxonomy_include) .
	" source files; throw-helper interfaces remain lightweight.\n";
