#!/usr/bin/perl
$Err = "Can't locate "; 
$Ext = ".ph";
if ($ARGV[0] eq '-r') {
    require $ARGV[1];
    exit 0;
}    
if (index($ARGV[0], $Ext) < 0) {
    print STDERR "setup.pl [FILE$Ext].\n";
    exit 1;
}
while (1) {
    open(ERR, "setup.pl -r $ARGV[0] 2>&1|");
    for ($i = -1; $i < 0 && ($line = <ERR>); ) {
	chop($line);
	$i = index($line, $Err);
    }
    close(ERR);
    exit 0 if ($i < 0);
    exit 1 if ($line_b eq $line);
    $line_b = $line;
    $line =~ s/$Err//;
    $line =~ s/$Ext.*/\.h/;
    system("cd /usr/include; h2ph $line; cd -");
}
