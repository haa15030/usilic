#! /usr/bin/perl
# eg. cd ../src; /bin/ls Makefile *.[ch]|xargs -t ../perl/fix.pl /tmp/src

$Alloc = 'alloc';
$Dst = $ARGV[0];
if (!$Dst) {
    print STDERR "fix.pl [destination-directory] [file..] " . "\n"; 
    exit 1;
}
if (!(-d $Dst)) {
    print STDERR "$Dst: Not a directory" . "\n";
    exit 1;
}
$Dst .= '/' if (!($Dst =~ /\/$/));
$Dst0 = $Dst; 
for ($argc = 1; ($Src = $ARGV[$argc]); $argc++) {
    open(SRC, $Src);
    $Dst = $Dst0 . $Src;
    open(DST, '>' . $Dst);
    if ($Src ne $Alloc . '.h') {
	while (<SRC>) {
	    s/ALLOC_calloc\(\042.*\042\, /calloc\(/;
	    s/ALLOC_malloc\(\042.*\042\, /malloc\(/;
	    s/ALLOC_strdup\(\042.*\042\, /strdup\(/;
	    s/ALLOC_free\(\042.*\042\, /free\(/;
	    print DST;
	}
    } else {
print DST <<EOF;
#ifndef _ALLOC_H

int exit_ALLOC(char *name);
void init_ALLOC(void);

#define _ALLOC_H
#else

int exit_ALLOC(char *name)
{
  return 0;
}

void init_ALLOC(void)
{
}

#endif  _ALLOC_H
EOF
    }
	close(SRC);
	close(DST);
}


