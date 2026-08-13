#!/usr/bin/env perl
use strict;
use warnings;

use File::Temp qw(tempfile);
use Text::ParseWords qw(shellwords);

sub split_shell_words {
    my ($text) = @_;
    return () if !defined($text) || $text eq "";
    return shellwords($text);
}

sub run_probe {
    my ($args_ref, $input_text) = @_;
    $input_text = "" if !defined($input_text);

    my ($in_fh, $in_path) = tempfile();
    my ($out_fh, $out_path) = tempfile();
    my ($err_fh, $err_path) = tempfile();
    print {$in_fh} $input_text;
    close($in_fh);
    close($out_fh);
    close($err_fh);

    my $pid = fork();
    if(!defined($pid)) {
        unlink($in_path, $out_path, $err_path);
        return undef;
    }
    if($pid == 0) {
        open(STDIN, "<", $in_path) or exit(127);
        open(STDOUT, ">", $out_path) or exit(127);
        open(STDERR, ">", $err_path) or exit(127);
        exec(@{$args_ref});
        exit(127);
    }

    waitpid($pid, 0);
    my $status = $?;
    my $stdout = slurp_file($out_path);
    my $stderr = slurp_file($err_path);
    unlink($in_path, $out_path, $err_path);

    return {
        status => $status,
        stdout => $stdout,
        stderr => $stderr,
    };
}

sub slurp_file {
    my ($path) = @_;
    open(my $fh, "<", $path) or return "";
    local $/;
    my $text = <$fh>;
    close($fh);
    return defined($text) ? $text : "";
}

sub cxx_args {
    my $host_cxx = $ENV{"CPPGM_BUILD_HOST_CXX"} || "c++";
    my $stdlib_flags = $ENV{"CPPGM_STDLIB_FLAGS"} || "";
    my @args = (split_shell_words($host_cxx), split_shell_words($stdlib_flags));
    return (\@args, $host_cxx, $stdlib_flags);
}

sub raw_string_literal {
    my ($value) = @_;
    my $delimiter = "CPPGM_BUILTIN";
    while(index($value, ")" . $delimiter . "\"") >= 0) {
        $delimiter .= "_X";
    }
    return "R\"$delimiter($value)$delimiter\"";
}

sub string_literal {
    my ($value) = @_;
    $value =~ s/\\/\\\\/g;
    $value =~ s/"/\\"/g;
    $value =~ s/\n/\\n/g;
    $value =~ s/\r/\\r/g;
    return "\"" . $value . "\"";
}

sub parse_include_paths {
    my ($text) = @_;
    my @paths;
    my %seen;
    my $collecting = 0;
    for my $line (split(/\n/, $text)) {
        if(index($line, "#include <...> search starts here:") >= 0) {
            $collecting = 1;
            next;
        }
        next if !$collecting;
        last if index($line, "End of search list.") >= 0;

        $line =~ s/^\s+//;
        $line =~ s/\s+$//;
        next if $line eq "" || index($line, "(framework directory)") >= 0;
        next if $seen{$line}++;
        push(@paths, $line);
    }
    return @paths;
}

sub main {
    my ($base_args_ref, $host_cxx, $stdlib_flags) = cxx_args();
    my @base_args = @{$base_args_ref};

    my $predefined = "";
    my $predefined_probe =
        run_probe([@base_args, "-std=gnu++11", "-dM", "-E", "-x", "c++", "-"], "");
    if(defined($predefined_probe) && $predefined_probe->{status} == 0) {
        $predefined = $predefined_probe->{stdout};
    }

    my @include_paths;
    my $include_probe =
        run_probe([@base_args, "-E", "-x", "c++", "-", "-v"], "");
    if(defined($include_probe)) {
        @include_paths =
            parse_include_paths($include_probe->{stdout} . $include_probe->{stderr});
    }

    my $target_probe = run_probe([@base_args, "-dumpmachine"], "");
    my $version_probe = run_probe([@base_args, "-dumpversion"], "");
    my $search_probe = run_probe([@base_args, "-print-search-dirs"], "");
    my $target = defined($target_probe) && $target_probe->{status} == 0 ?
        $target_probe->{stdout} : "x86_64-unknown-linux-gnu\n";
    my $version = defined($version_probe) && $version_probe->{status} == 0 ?
        $version_probe->{stdout} : "11\n";
    my $search_dirs = defined($search_probe) && $search_probe->{status} == 0 ?
        $search_probe->{stdout} : "";

    print "#pragma once\n\n";
    print "namespace cppgm_builtin_host_config {\n\n";
    print "static const char kHostCxx[] = ", string_literal($host_cxx), ";\n";
    print "static const char kStdlibFlags[] = ", string_literal($stdlib_flags), ";\n";
    print "static const char kTarget[] = ", string_literal($target), ";\n";
    print "static const char kVersion[] = ", string_literal($version), ";\n";
    print "static const char kSearchDirs[] = ",
          raw_string_literal($search_dirs), ";\n";
    print "static const char kHostPredefinedMacros[] = ",
          raw_string_literal($predefined), ";\n";
    print "static const char * const kStandardIncludePaths[] = {\n";
    for my $path (@include_paths) {
        print "  ", string_literal($path), ",\n";
    }
    print "  nullptr\n";
    print "};\n\n";
    print "}  // namespace cppgm_builtin_host_config\n";
}

main();
