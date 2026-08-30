#! /usr/bin/perl
# eg. mode.pl < mode.h > mode.h.bak; mv mode.h.bak mode.h

for ($i = 1; <STDIN>; ) {
    @mode = split;
    if (index($_, '*') >= 0) { 
	if ($mode[1] eq 'M-?') {
	    print "@mode" . "\n";
	    $i = 80;
	} else {
	    print;
	}
    } else {
	print "#define $mode[1]\t$i\n";
	$i++;
    }
}
