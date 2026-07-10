while (<>) {
    $name = substr($_, 0, index($_, '	'));
    $num = substr($_, index($_, ':')+2, 4);
#    print "*** " . $name . " *** : " . $num . "\n";
    printf "%02d : %s\n", $num, $name;
}
