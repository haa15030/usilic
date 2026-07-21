#!/usr/bin/perl
# This script will create the directory `~/.tcb.pl/'.

$Rc = '
-EDITOR
        vi
-KEY0
        ^X        #        0
        ^J        #        1
        ^X^K        #        2
        ^O        #        3
        ^[        #        4        
-KEY1        
        ^G        #        0:M_cansel
        j        #        1:M_dn_line
        k        #        2:M_up_line
        ^P        #        3:M_prev_line
        ^N        #        4:M_next_line
        ^[<        #        5:M_top_page
        ^[>        #        6:M_bot_page
        ^V        #        7:M_dn_page
        ^[v        #        8:M_up_page
        $3^K        #        9:M_parent
        $0^F        #        10:M_FILE
        $0^D        #        11:M_DIR
        $1d        #        12:M_DU
        $1^D        #        13:M_DU2
        $1l        #        14:M_LOCATE
        $1^L        #        15:M_LOCATE2
        ^R        #        16:M_HISTORY
        ^M        #        17:M_cr
        $1e        #        18:M_EDIT
        ^S        #        19:M_GREP
        $1^S        #        20:M_GREP2
        ^L        #        21:M_clear
        $1p        #        22:M_PROG
        $0r        #        23:M_RM
';

eval 'sub M_cansel ()        {$Mkeys[0];}';
eval 'sub M_dn_line ()        {$Mkeys[1];}';
eval 'sub M_up_line ()        {$Mkeys[2];}';
eval 'sub M_prev_line()        {$Mkeys[3];}';
eval 'sub M_next_line()        {$Mkeys[4];}';
eval 'sub M_top_page ()        {$Mkeys[5];}';
eval 'sub M_bot_page ()        {$Mkeys[6];}';
eval 'sub M_dn_page ()        {$Mkeys[7];}';
eval 'sub M_up_page ()        {$Mkeys[8];}';
eval 'sub M_parent ()        {$Mkeys[9];}';
eval 'sub M_FILE ()        {$Mkeys[10];}';
eval 'sub M_DIR ()        {$Mkeys[11];}';
eval 'sub M_DU ()        {$Mkeys[12];}';
eval 'sub M_DU2 ()        {$Mkeys[13];}';
eval 'sub M_LOCATE ()        {$Mkeys[14];}';
eval 'sub M_LOCATE2 ()        {$Mkeys[15];}';
eval 'sub M_HISTORY ()        {$Mkeys[16];}';
eval 'sub M_cr ()        {$Mkeys[17];}';
eval 'sub M_EDIT ()        {$Mkeys[18];}';
eval 'sub M_GREP ()        {$Mkeys[19];}';
eval 'sub M_GREP2 ()        {$Mkeys[20];}';
eval 'sub M_clear ()        {$Mkeys[21];}';
eval 'sub M_PROG ()        {$Mkeys[22];}';
eval 'sub M_RM ()        {$Mkeys[23];}';

%List_list = %List_dir = %List_file = %List_du = ();
%Bin = ('~du',                'du -ab',
        '~ls',                'ls -laF',
        '~locate',        'locate', 
        '~file',        'file',
        '~grep',        'grep -n',
        '.gz',                'zcat',
        '.z',                'zcat',
        '.Z',                'zcat',
        '.tar',                'tar tvf',
        '.tar.gz',        'tar tvfz',
        '.tgz',                'tar tvfz',
        '.deb',                'dpkg-deb -c',
        '.00',                'dpkg-deb -c',
        '.dvi',                'xdvi',
);

@Lines_du = @Lines_ls = @Lines_locate = @Lines_file = @Lines_grep = ();
@History_dir = @History_file = ();
@Pkeys = @Mkeys = ();
@Ip = (0, 0, 0, 0, 0, 0);
# ip[0] : Top line.
# ip[1] : Current line.
# ip[2] : Bottom line.
# ip[3] : Current row.
# ip[4] : Current column.
# ip[5] : Stand flag.

$Dir_ls = $Dir_du = $F_tstp = $F_cont = $F_disp = $F_tty = 0;
$Dir_ls = $Dir_du = $F_disp = $F_tty = 0;
$Termios = 'llllcc19';        # struct termios
$Winsize = 'ssss';        # struct winsize
$Ikey = "\cA\cB\cD\cF\cH\cI\cK\cU";
$Home = $ENV{HOME} . '/.tcb.pl/';

if (!(-f "/etc/termcap")) {
    printf STDERR "/etc/termcap: No such file.\n";
    exit 1;
}
#if (system("setup.pl termios.ph")) {
#    printf STDERR "setup.pl: exit 1.\n";
#    exit 1;
#}
if (-f "/usr/include/ioctls.h") {
    if (system("setup.pl ioctls.ph")) {
        printf STDERR "setup.pl: exit 1.\n";
        exit 1;
    }
}
if (!&check_pid) {
    my($f_home); 
    if (!(-d $Home)) {
        my($rc);
        unlink($Home) if (-f $Home); 
        system("mkdir $Home");
        $rc = $Home . 'rc';
        open(TCBRC, '>' . $rc);
        printf TCBRC "%s", $Rc;
        close(TCBRC);
        $f_home = 1;
    }
    require 'asm/termios.ph';
    require 'termcap.pl';
    if (-f "/usr/include/ioctls.h") {
        require 'ioctls.ph';
    }
    $ENV{TERM} = 'vt100';
    &Tgetent($ENV{TERM});
    &tc_sc;
    $SIG{INT} = 'int_tcb';
    $SIG{HUP} = 'hup_tcb';
    $SIG{TSTP} = 'IGNORE';
    $SIG{QUIT} = 'quit_tcb';
    $SIG{WINCH} = 'winch_tcb';
    &init_key;
    &open_tcb;
    $F_tstp = $F_cont = 1;
    &tcb;
    $F_tstp = $F_cont = 0;
    &tc_cl;
    &close_tcb;
    &unlink_lock;
    print "Cteated $Home\n" if ($f_home);
}
sub unlink_lock {
    unlink($Home . 'lock');
}
sub check_pid {
    my($lock, $pid, $line, @ps);
    $lock = $Home . 'lock';
    while (1) {
        if (open(TCBLOCK, $lock)) {
            chop($pid = <TCBLOCK>);
            close(TCBLOCK);
            open(TCBLOCK, "ps|");
            while (<TCBLOCK>) {
                next if (index($_, 'perl') < 0);
                $line = $_;
                @ps = split;
                $_ = $ps[0];
                last if ($_ == $pid);
            }
            $pid = 0 if (!$_);
            close(TCBLOCK);
        }
        if ($pid) {
            chop($line);
            print  "tcb: Already running.  Restart ? (y/n) ";
            read(STDIN, $key, 1);
            if ($key =~ /^[yY]/) {
                kill('QUIT', $pid);
                kill('HUP', $pid);
                kill('CONT', $pid);
                while (kill('', $pid)) {
                    sleep(1);
                }
                $pid = 0;
                next;
            } else {
                $pid = 1;
                last;
            }
        } else {
            $lock = '>' . $lock;
            open(TCBLOCK, $lock);
            printf TCBLOCK "%d\n", $$;
            close(TCBLOCK);
            last;
        }
    }
    $pid;
}
sub tcb {
    my($f_so, $name, $old, $prev, $bin, @stack, @lines, @ip);
    &load_list(\@stack);
    if (@stack) {
        $name = pop(@stack);
        $prev = pop(@stack);
    }
    if ($name) {
        if (-d $name) {
            $old = $name;
        } elsif (-f $name) {
            $old = get_dir($name);
        } else {
            $name = $old = &get_pwd;
        }
        chdir($old);
        $old .= '/' if (strlen($old) > 1 && !($old =~ /\/$/));
    } else {
        $name = $old = &get_pwd;
        $prev = '';
    }
    if (!$List_dir{$old}) {
        &save_ip($old, @ip);
    }
    $old = $name;
    if (-d $name) {
        @lines = @Lines_ls = &bin($name, $Bin{"~ls"}, 1);
        $f_so = 1;
        $bin = $Bin{'~ls'};
    } else {
        push(@stack, $prev);
        $bin = get_bin($name);
        @lines = @Lines_file = ($bin && !($bin =~ /^ /)) 
            ? &bin($name, $bin, 0) : &cat($name);
        $f_so = 0;
    }
    while (1) {
        $name = &mode(\$f_so, $name, $bin, $prev, \@lines);
        last if ($f_so < 0);
        $f_so = 1;
        @lines = ();
        $bin = '';
        if ($name eq &M_cansel) {
            $name = pop(@stack);
            $prev = pop(@stack);
            push(@stack, $prev);
            if ($name eq 'DU') {
                $name = 'DU2';
            } elsif ($name =~ /^\//) {
                if (-d $name) {
                    if ($Dir_ls) {
                        pop(@stack) while (!(-d $name));
                        $name = $Dir_ls;
                        $Dir_ls = '';
                    } else {
                        if (!@Lines_ls) {
                            @Lines_ls = &bin($name, $Bin{"~ls"}, 1);
                        }
                        @lines = @Lines_ls;
                        $old = $name;
                        next;
                    }
                    $bin = $Bin{'~ls'};
                } else {
                    @lines = @Lines_file;
                    $bin = get_bin($name);
                    $old = $name;
                    next;
                }
            }
        } else {
            push(@stack, $old);
            $prev = $old;
        }
        if ($name eq 'FILE') {
            @lines = sort(keys %List_file);
        } elsif ($name eq 'DIR') {
            @lines = sort(keys %List_dir);
        } elsif ($name eq 'DU' || ($name eq 'DU2' && !$Dir_du)) {
            $name = 'DU' if ($name eq 'DU2');
            $Dir_du = &get_pwd;
            @lines = @Lines_du = &bin('', $Bin{"~du"}, 0);
            %List_du = ();
            $bin = $Bin{'~du'};
        } elsif ($name eq 'DU2') {
            chdir($Dir_du);
            @lines = @Lines_du;
            $name = 'DU';
            $bin = $Bin{'~du'};
        } elsif (($name ne 'LOCATE2' && index($name, 'LOCATE') >= 0) 
                 || ($name eq 'LOCATE2' && !$#Lines_locate)) {
            $name = 'LOCATE' if ($name eq 'LOCATE2');
            if ($name = substr($name, 6)) {
                @lines = @Lines_locate = &bin($name, $Bin{"~locate"}, 0);
                pop(@stack);
                $prev = pop(@stack);
                push(@stack, $prev);
            } else {
                @lines = ();
            }
            $name = 'LOCATE';
        } elsif ($name eq 'LOCATE2') {
            @lines = @Lines_locate;
            $name = 'LOCATE';
        } elsif (($name ne 'GREP2' && index($name, 'GREP') >= 0) 
                 || ($name eq 'GREP2' && !$#Lines_grep)) {
            $name = 'GREP' if ($name eq 'GREP2');
            if ($name = substr($name, 4)) {
                pop(@stack);
                $prev = pop(@stack);
                push(@stack, $prev);
                $name .= " $prev";
                @lines = @Lines_grep = &bin($name, $Bin{"~grep"}, 0);
            } else {
                @lines = ();
            }
            $name = 'GREP';
        } elsif ($name eq 'GREP2') {
            @lines = @Lines_grep;
            $name = 'GREP';
        } elsif ($name eq 'HISTORY') {
            $old = pop(@stack);
            if ($old eq 'DIR') {
                @lines = @History_dir;
            } elsif ($old eq 'FILE') {
                @lines = @History_file;
            }
            push(@stack, $old);
        } elsif (-d $name || !(-f $name)) {
            $name = $Home if (!(-d $name));
            while (pop(@stack)) {
                ;
            }
            chdir($name);
            $name = &get_pwd;
            @lines = @Lines_ls = &bin($name, $Bin{"~ls"}, 1);
            $prev = '';
            $bin = $Bin{'~ls'};
        } else {
            while (pop(@stack)) {
                ;
            }
            $prev = &get_dir($name);
            push(@stack, $prev);
            &save_dir($name);
            $bin = get_bin($name);
            @lines = @Lines_file = ($bin && !($bin =~ /^ /)) 
                ? &bin($name, $bin, 0) : &cat($name);
            $f_so = 0;
        }
        $old = $name;
    }
    push(@stack, $name);
    save_list(\@stack);
}    
sub load_list {
    my($p_stack, $tcb_list) = @_;
    if (open (TCBLIST, $Home . 'history')) {
        read_hist(TCBLIST, $p_stack);
        read_hist(TCBLIST, \@History_dir);
        read_hist(TCBLIST, \@History_file);
        read_list(TCBLIST, \%List_list);
        read_list(TCBLIST, \%List_dir);
        read_list(TCBLIST, \%List_file);
        read_list(TCBLIST, \%List_du);
        close(TCBLIST);
    }
}
sub save_list {
    my($p_stack, $tcb_list) = @_;
    if (open (TCBLIST, '>' . $Home . 'history')) {
        write_hist(TCBLIST, $p_stack);
        write_hist(TCBLIST, \@History_dir);
        write_hist(TCBLIST, \@History_file);
        write_list(TCBLIST, \%List_list);
        write_list(TCBLIST, \%List_dir);
        write_list(TCBLIST, \%List_file);
        write_list(TCBLIST, \%List_du);
        close(TCBLIST);
    }
}
sub read_hist {
    my($handle, $p_list) = @_;
    chop($_ = <TCBLIST>);
    @{$p_list} = split if ($_);
    $$p_list[1] = 0 if ($$p_list[1] < 0);
}
sub write_hist {
    my($handle, $p_list) = @_;
    print $handle "@$p_list\n";
}
sub read_list {
    my($handle, $p_list, $name, @ip) = @_;
    while (<$handle>) {
        chop;
        last if (!$_);
        ($name, @ip) = split;
        push(@Ipp, @ip);
        $$p_list{$name} = scalar(@Ipp);
    }
}
sub write_list {
    my($handle, $p_list, @ip) = @_;
    if (%$p_list) {
        foreach (keys(%$p_list)) {
            @ip = &load_ip($$p_list{$_});
            print $handle "$_ @ip\n";
        }
    }
    print $handle "\n";
}
sub set_prev {
    my($line, $p_i) = @_;
    my($i);
    for ($prev = '', $i = 0; $line; $i++) {
        if ($line =~ /^\^/) {
            $line =~ s/\^//;
            $prev .= pack('c', unpack('c', $line) 
                          - unpack('c', 'A') + 1);
        } elsif ($line =~ /^\$/) {
            $prev .= $Pkeys[substr($line, 1, 1)];
            $line =~ s/.//;
        } else {
            $prev .= $line;
        }
        $line =~ s/.//;
    }
    ${$p_i} = $i;
    $prev;
}    
sub init_key {
    my($i, $j, $i_p, $i_a, $i_c, $line, $prev, $p_keys, @a_keys);
    $p_a = [0,];
    $line = $Home . 'rc';
    open(TCBRC, $line);
    $line = '';
    while (1) {
        if (!$line) {
            last if (!($line = <TCBRC>));
        }
        if (index($line, '-')) {
            $line = '';
            next;
        }
        if (index($line, 'KEY0') >= 1) { # -KEY0 
	    my($p_p, $p_a);
	    $p_p = \@Pkeys;
	    $p_a = \@a_keys;
	    my($i, $n, $prev);
	    $i_p = $i_a = 0;
	    while (<TCBRC>) {
		last if (!$_ || !index($_, '-'));
		next if (!($_ = &cut_end($_, '#')));
		s/\s//g;
		next if (!$_);
		$prev = &set_prev($_, \$n);
		for ($i = 1; $i <= $n; $i++) {
		    for (@$p_p) {
			last if (substr($_, 0, $i) eq substr($_, 0, $i));
		    }
		    if (!$_) {
			(($i == $n) ? $$p_p[$i_p++] : $$p_a[$i_a++])
			    = substr($prev, 0, $i);
		    }
		}
	    }
	    $line = $_;
        } elsif (index($line, 'KEY1') == 1) {
            $p_keys = \@Mkeys;
            $i_p = 0;
            for ($i = 0; ; $i++) {
                $line = <TCBRC>;
                last if (!$line || !index($line, '-'));
                next if (!($line = &cut_end($line, '#')));
                $line =~ s/\s//g;
                next if (!$line);
                $prev = &set_prev($line, \$i_c);
                $$p_keys[$i_p++] = $prev . ' ';
            }
        } elsif (index($line, 'EDITOR') == 1) {
            while (($line = <TCBRC>) && index($line, '-')) {
                next if (!($line = &cut_end($line, '#')));
                next if (!($line =~ s/\s//g));
                $Editor = $line;
                last;
            }
        } else {
            $line = '';
        }
    }
    push(@Pkeys, @a_keys) if (scalar(@a_keys));
    @a_keys = ();
    $i_a = 0;
    foreach $prev (@Mkeys) {
        $line = $prev;
        chop($line);
        next if (($i_c = strlen($line)) == 1);
        for ($j = 1; $j < $i_c; $j++) {
            for ($i = 0; $line = $Pkeys[$i]; $i++) {
                last if (substr($line, 0, $j) eq substr($prev, 0, $j));
            }
            $a_keys[$i_a++] = substr($prev, 0, $j) if (!$line);
        }
    }
    push(@Pkeys, @a_keys) if (scalar(@a_keys));
if (1) {
    foreach (@Pkeys) {
        printf STDERR "Pkeys=***%s***\n", $_;
    }
    foreach (@Mkeys) {
        printf STDERR "Mkeys=***%s***\n", $_;
    }
}
    close(TCBRC);
}
sub mode {
    my($p_f_so, $name, $bin, $old, $p_lines) = @_;
    my($key, $mkey, $line, $path, %list, @ip);
    my($rin, $win, $ein);
    @ip = @Ip;
    %list = &get_list($name);
    if (($key = $list{$name})) {
        @ip = &load_ip($key);
    } else {
        $ip[5] = -1;
    }
    if ($ip[5] <= -2) {                # -2 <== 'HISTORY'
        $ip[5] = $$p_f_so;
        if ($#{$p_lines} == -1) {
            $ip[0] = $ip[1] = $ip[2] = $ip[3] = 0;
        } else {
            &bot_page($name, '', \@ip, $p_lines);
        }
        &save_ip($name, @ip);
    } else {
        shift_top(\@ip, $p_lines);
        $ip[2] = &disp_win(\@ip, $name, $p_lines);
        if ($ip[5] == -1) {        # ($list{$name} && $ip[5] == -1) <== 'DU' 
            $ip[1] = $ip[3] = 0;
            $ip[5] = $$p_f_so;
            &save_ip($name, @ip);
        }
    }
    &disp_mline($name, $bin, $old, $#{$p_lines}, @ip);
    $line = &cut_line($$p_lines[$ip[1]]);
    $ip[4] = &so_col($name, @{$p_lines});
    if ($ip[5]) {
        &so_name($line, @ip);
    } else {
        &tc_move($ip[3], 0);
    }
    until ($F_hup || (read(STDIN, $key, 1) == 1 && $key eq 'q')) {
        if ($F_disp) {
            $F_disp = 0;
            shift_top(\@ip, $p_lines);
            $ip[2] = &disp_win(\@ip, $name, $p_lines);
            &disp_mline($name, $bin, $old, $#{$p_lines}, @ip);
            &disp_line($line, @ip);
        }
        if ($key) {
            $mkey .= $key;
            if (&is_keys($mkey, \@Pkeys)) {
                $key = '';
                next;
            } elsif (is_keys($mkey . ' ', \@Mkeys)) {
                $key = $mkey . ' ';
                $mkey = '';
            } elsif (is_keys($key . ' ', \@Mkeys)) {
                $key .= ' ';
                $mkey = '';
            } elsif (!($key = is_bkeys($key))) {
                $mkey = $key = '';
                next;
            }
        }
        if ($key eq &M_FILE) {
            &save_ip($name, @ip);
            $path = 'FILE';
            last;
        } elsif ($key eq &M_DIR) {
            &save_ip($name, @ip);
            $path = 'DIR';
            last;
        } elsif ($key eq &M_DU) {
            &save_ip($name, @ip);
            $path = 'DU';
            last;
        } elsif ($key eq &M_DU2) {
            &save_ip($name, @ip);
            $path = 'DU2';
            last;
        } elsif ($key eq &M_LOCATE) {
            &save_ip($name, @ip);
            $path = 'LOCATE';
            $ip[5] = -1;
            &save_ip($path, @ip);
            last;
        } elsif ($key eq &M_LOCATE2) {
            &save_ip($name, @ip);
            $path = 'LOCATE2';
            last;
#        } elsif ($key eq &M_GREP) { 
#            &save_ip($name, @ip);
#            $path = 'GREP';
#            $ip[5] = -1;
#            &save_ip($path, @ip);
#            last;
#        } elsif ($key eq &M_GREP2) {
#            &save_ip($name, @ip);
#            $path = 'GREP2';
#            last;
        } elsif ($key eq &M_HISTORY) {
            if ($name ne 'DIR' && $name ne 'FILE') {
                print "\cG";
                $key = '';
                next;
            }
            &save_ip($name, @ip);
            $path = 'HISTORY';
            $ip[5] = -2;
            &save_ip($path, @ip);
            last;
        } elsif ($key eq &M_parent) {
            &save_ip($name, @ip);
            chdir('..');
            $path = &get_pwd;
            last;
        } elsif ($key eq &M_PROG) {
            $key = &key_input('PROG', $old, \@ip, $p_lines);
            redo if ($key eq &M_cansel);
            prog($key, $name);
            $F_disp = 1;
            $key = '';
        } elsif ($key eq &M_RM) {
            if (! -d $name) {
                print "\cG";
                $key = '';
                next;
            }
            $key = &key_input('RM', $old, \@ip, $p_lines);
            if (!($key =~ /^y/) && !($key =~ /^Y/)) {
                $key = '';
                next;
            } else {
                $key = '';
                system("rm -rf " . &chop_name(substr($line, $ip[4], 1000)));
                &save_ip($name, @ip);
                $path = $name;
                last;
            }
        } elsif ($key eq &M_EDIT) {
            if (!&edit($name, $bin, @ip)) {
                print "\cG";
                $key = '';
                next;
            } else {
                &save_ip($name, @ip);
                $path = $name;
                last;
            }
        } elsif ($key eq &M_clear) {
            $F_disp = 1;
            $key = '';
            next;
        } elsif ($key eq &M_cr && $ip[5]) {
            &save_ip($name, @ip);
            $line = $$p_lines[$ip[1]];
            if ($name eq 'FILE' || $name eq 'DIR' 
                || $name eq 'HISTORY' || $name eq 'LOCATE'
                || $name eq 'GREP' || $name eq 'PROG') {
                ;
            } elsif ($name eq 'DU') {
                $line = substr($line, rindex($line, "\t") + 3, 1000);
                $line = $Dir_du . $line;
            } else {
                $line = &chop_name(substr($line, $ip[4], 1000));
                $line = &get_pwd . $line;
            }
            $path = $line;
            last;
        } elsif ($key eq &M_cansel) {
            &save_ip($name, @ip);
            if ($name eq 'DU') {
                $path = &get_pwd;
            } elsif (-d $name) {
                if ($#History_dir) {
                    chdir($path = $History_dir[$#History_dir-1]);
                } else {
                    print "\cG";
                    $key = '';
                    next;
                }
            } else {
                $path = $key;
            }
            last;
        } elsif ($key eq &M_dn_line) {
            &dn_line($line, \@ip, $#{$p_lines});
        } elsif ($key eq &M_up_line) {
            &up_line($line, \@ip);
        } elsif ($key eq &M_prev_line) {
            &prev_line($line, \@ip);
        } elsif ($key eq &M_next_line) { 
            &next_line($line, \@ip, $#{$p_lines});
        } elsif ($key eq &M_top_page) {
            &top_page($name, $line, \@ip, $p_lines);
        } elsif ($key eq &M_bot_page) {
            &bot_page($name, $line, \@ip, $p_lines);
        } elsif ($key eq &M_dn_page || $key eq ' ' || $key eq "\cF") {
            &dn_page($name, $line, \@ip, $p_lines);
        } elsif ($key eq &M_up_page || $key eq 'b' || $key eq "\cB") {
            &up_page($name, $line, \@ip, $p_lines);
        }
        if ($key) {
            $mkey = $key = '';
            $line = &cut_line($$p_lines[$ip[1]]);
            &disp_mline($name, $bin, $old, $#{$p_lines}, @ip);
            &disp_line($line, @ip);
        } elsif ($name eq 'LOCATE' || $name eq 'FILE' 
                 || $name eq 'DIR' || $name eq 'HISTORY'
                 || $name eq 'GREP' || $name eq 'PROG') {
            $key = &key_input($name, $old, \@ip, $p_lines);
            redo if ($key eq &M_cansel || $key eq &M_HISTORY);
            $path = (index($key, "\cM")) ? $name . $key : substr($key, 1);
            last;
        }
    }
    $key = 'q' if ($F_hup);
    if ($key eq 'q') {
        $$p_f_so = -1; 
        &save_ip($name, @ip);
        $path = $name;
    }
    $path;
}
sub close_tcb {
    $SIG{CONT} = 'open_tcb' if ($F_cont);
    &reset_console;
    if ($F_tstp) {
        $F_disp = 1;
        $SIG{TSTP} = 'DEFAULT';
        kill('TSTP', $$);
    }
}
sub open_tcb {
    $SIG{TSTP} = 'close_tcb';
    &set_console(1);
}    
sub set_console {
    my($f_cl) = @_;
    open(TTY, "/dev/tty");
    if (!$F_tty) {
        my($iflag, $oflag, $cflag, $lflag, $line, @cc);
        $F_tty = 1;
        $Tty0 = $Tty1 = pack($Termios,0);
        ioctl(TTY, &TCGETS, $Tty0);
        ($iflag, $oflag, $cflag, $lflag, $line, @cc) = unpack($Termios, $Tty0);
        $iflag &= ~(&IGNBRK|&BRKINT|&PARMRK|&ISTRIP|&INLCR|&IGNCR|&ICRNL|&IXON);
        $oflag &= ~(&OPOST|&ONLCR);
        $lflag &= ~(&ECHO|&ECHONL|&ICANON|&ISIG|&IEXTEN|&NOFLSH);
        $cflag &= ~(&CSIZE|&PARENB);
        $cflag |= &CS8;
        $cc[&VMIN] = 0;
        $cc[&VTIME] = 1;
        $Tty1 = pack($Termios, $iflag, $oflag, $cflag, $lflag, $line, @cc);
    }
    ioctl(TTY, &TCSETSW, $Tty1);
    close(TTY);
    &get_winsize;
    if ($f_cl) {
        &tc_cl;
        &tc_move(0, 0);
    }
}
sub reset_console {
    &tc_cs($I_row-1, 0);
    &tc_rc;
    print "\n";
    open(TTY, "/dev/tty");
    ioctl(TTY, &TCSETSW, $Tty0);
    close(TTY);
}
sub get_winsize {
    my($winsize) = pack($Winsize, 0);
    open(TTY, "/dev/tty");
    ioctl(TTY, &TIOCGWINSZ, $winsize);
    ($I_row, $I_col) = unpack($Winsize, $winsize);
    &tc_cs($I_row-2, 0);
    close(TTY);
}
sub int_tcb {
    $F_hup = 1;
}
sub quit_tcb {
    $SIG{TTOU} = 'IGNORE';
    $SIG{TTIN} = 'IGNORE';
}
sub hup_tcb {
    $F_hup = 1;
}
sub winch_tcb {
    &get_winsize;
    $F_disp = 1;
}
sub disp_line {
    my($line, @ip) = @_;
    &tc_move($ip[3], 0);
    print "$line";
    if ($ip[5]) {
        &so_name($line, @ip);
    } else {
        &tc_move($ip[3], 0);
    }
}
sub disp_mline {
    my($name, $bin, $old, $n_lines, @ip) = @_;
    my($i, $len, $str);
    &tc_move($I_row-1, 0);
    &tc_so;
    if ($old) {
        for ($i = $I_col; $i; $i--) {
            print ' ';
        }
        &tc_move($I_row-1, 0);
    }
    if ($name =~ /^\// && !(-d $name)) {
        $name = &get_name($name);
    }
    print "$name";
    &tc_se;
    print ' ';
    &tc_so;
    $len = $n_lines + 1;
    $len = index($len . ' ', ' ');
    $str = '%0' . $len . 'd/%' . $len . 'd';
    if ($n_lines < 0) {
        printf $str, 0, 0, 100;
    } else {
        printf $str, $ip[1]+1, $n_lines+1, ($ip[2]+1) * 100 / ($n_lines+1);
    }
    if ($n_lines < 0 || ($i = ($ip[2]+1) * 100 / ($n_lines+1)) == 100) {
        print ' END';
    } else {
        printf "%3d%", $i;
    }
    $i = $I_col - (&strlen($name) + 2*$len + 7);
    &tc_se;
    print ' ';
    if ($bin eq 'KEY') {
        &tc_move($I_row-1, $I_col/2);
    } elsif ($bin) {
        &tc_so;
        if ($bin =~ /^ /) {
            printf "%s", substr($bin, 1);
        } else {
            while (index($bin, ' ') >= 0) {
                while (chop($bin) ne ' ') {
                    ;
                }
            }
            printf "(%s)", $bin;
        }
        &tc_se;
        print ' ';
    }
    if ($old) {
        &tc_so;
        print "$old";
    }
    &tc_se;
    &strlen($name) + 1 + $len + 1 + $len + 1 + 3 + 1;
}    
sub strlen {
    my($string) = @_;
    my($len);
    for ($len = 0; substr($string, $len); $len++) {
        ;
    }
    $len;
}
sub get_pwd {
    my($path) = `pwd`;
    chop($path);
    $path .= '/' if (!($path =~ /\/$/));
    $path;
}
sub so_col {
    my($name, @lines) = @_;
    my($line, $i_col);
    if ($name =~ /\/$/) {        # <== File selector
        while ($line = shift(@lines)) {
            last if (($i_col = index($line, '..')) >= 0);
        }
    } else {
        $i_col = 0;
    }
    $i_col;
}   
sub load_ip {
    my($i) = @_;
    my($j, @ip) = (0, @Ip);
    for ($i -= scalar(@Ip); $j < scalar(@Ip); $j++) {
        $ip[$j] = $Ipp[$i+$j];
    }
    @ip;
}
sub save_ip {
    my($path, @ip) = @_;
    my($i, $j, %list);
    %list = &get_list($path);
    if ($i = $list{$path}) {
        for ($j = 0, $i -= scalar(@Ip); $j < scalar(@Ip); $j++) {
            $Ipp[$i+$j] = $ip[$j];
        }
    } else {
        push(@Ipp, @ip);
        $list{$path} = scalar(@Ipp);
    }
    &set_list($path, %list);
}
sub get_list {
    my($path) = @_;
    my(%list) = ();
    if (!($path =~ /^\//)) {
        %list = %List_list;
    } elsif (-d $path) {
        %list = %List_dir;
        if ($History_dir[$#History_dir] ne $path) {
            push(@History_dir, $path);
        }
    } else {
        %list = %List_file;
        if ($History_file[$#History_file] ne $path) {
            push(@History_file, $path);
        }
    }
    %list;
}
sub set_list {
    my($path, %list) = @_;
    if (!($path =~ /^\//)) {
        %List_list = %list;
    } elsif (-d $path) {
        %List_dir = %list;
    } else {
        %List_file = %list;
    }
}
sub save_dir {
    my($name) = @_;
    my($dir, @lines, @ip, %list);
    $dir = substr($name, 0, rindex($name, '/')+1);
    %list = &get_list($dir);
    if (!$list{$dir}) {
        @ip = @Ip;
        $ip[5] = -1;
        &save_ip($dir, @ip);
    }
    chdir($dir);
    $Dir_ls = $dir;
}
sub bin {
    my($name, $bin, $f_skip) = @_;
    my(@lines);
    open (TCB, "$bin $name|");
    if ($f_skip) {
        <TCB>;
        chop(@lines = <TCB>);
    } else {
        @lines = &read_line;
    }
    close(TCB);
    @lines;
}
sub cat {
    my($name, $line) = @_;
    my($i, @lines);
    open(TCB, "kcc -c $name|");
    chop($line = <TCB>);
    close(TCB);
    if ($line && index($line, "JIS")) {
        open(TCB, "kcc -e $name|");
    } else {
        open(TCB, $name);
    }
    @lines = &read_line;
    close(TCB);
    @lines;
}
sub read_line {
    my($i, $str, @lines);
    $i = 0;
    while ($line = <TCB>) {
        chop($line);
        while ($str = substr($line, $I_col)) {
            $lines[$i++] = substr($line, 0, $I_col-1);
            $line = $str;
        }
        $lines[$i++] = $line;
    }
    @lines;
}
sub chop_name {
    my($name) = @_;
    chop($name) while (index($name, '*') >= 0 || index($name, ' ') >= 0);
    $name;
}
sub sf_line {
    my($line, $p_ip) = @_;
    &se_name($line, @{$p_ip}) if ($$p_ip[5]);
    $$p_ip[3] = &tc_sf;
    $$p_ip[0]++;
    $$p_ip[1] = ++$$p_ip[2];
}
sub sr_line {
    my($line, $p_ip) = @_;
    &se_name($line, @{$p_ip}) if ($$p_ip[5]);
    $$p_ip[3] = &tc_sr;
    $$p_ip[2]--;
    $$p_ip[1] = --$$p_ip[0] if ($$p_ip[0]);
}
sub so_name {
    my($line, @ip) = @_;
    my($name) = substr($line, $ip[4], 1000);
    &tc_so;
    &tc_move($ip[3], $ip[4]);
    print "$name";
    &tc_se;
}
sub se_name {
    my($line, @ip) = @_;
    my($name) = substr($line, $ip[4], 1000);
    &tc_se;
    &tc_move($ip[3], $ip[4]);
    print "$name";
}
sub disp_win {
    my($p_ip, $name, $p_lines) = @_;
    my($i_row, $line) = (0, ''); 
    &tc_cl;
    for ($i_row = 0; $i_row < $I_row-1; ) {
        if ($i_row > $#{$p_lines}) {
            last;
        } else {
            &tc_move($i_row, 0);
            $line = $$p_lines[$i_row + $$p_ip[0]]; 
            if (substr($line, $I_col)) {
                $line = substr($line, 0, $I_col);
            }
            print "$line";
            $i_row++;
        }
    }
    $i_row + $$p_ip[0] - 1;
}
sub tc_move {
    my($row, $col) = @_;
    print &Tgoto($TC{'cm'},$col,$row);
};
sub tc_cl {
    print "$TC{'cl'}";
#    &Tputs($TC{'cl'}, 0, 1);
};
sub tc_ce {
    print "$TC{'ct'}";
#    &Tputs($TC{'cl'}, 0, 1);
};
sub tc_sf {
    &tc_move($I_row-2, 0);
#    print "$TC{'sf'}";
    print "\n";
    $I_row-2;
}
sub tc_sr {
    &tc_move(0, 0);
    print "$TC{'sr'}";
    0;
}
sub tc_so {
    print "$TC{'so'}";
}
sub tc_se {
    print "$TC{'se'}";
}
sub tc_cs {
    my($row, $col) = @_;
    print &Tgoto($TC{'cs'}, $row, $col);
}    
sub tc_sc {
    print "$TC{'sc'}"; print "";
}
sub tc_rc {
    print "$TC{'rc'}"; print "";
}
sub get_du {
    my($name, $du) = @_;
    chop($name) if ($name =~ /\/$/);
    (($du = substr($name, rindex($name, '/')+1, 100)) . '/');
}
sub up_line {
    my($line, $p_ip) = @_;
    &se_name($line, @{$p_ip}) if ($$p_ip[5] && !$$p_ip[0]);
    if (!$$p_ip[0]) { 
        $$p_ip[1] = $$p_ip[3] = 0 if ($$p_ip[3]);
    } else {
        &sr_line($line, $p_ip);
    }
}
sub dn_line {
    my($line, $p_ip, $n_lines) = @_;
    &se_name($line, @{$p_ip}) if ($$p_ip[5] && $$p_ip[2] == $n_lines);
    if ($$p_ip[2] == $n_lines) { 
        if ($$p_ip[1] < $$p_ip[2]) {
            $$p_ip[1] = $$p_ip[2];
        }
        $$p_ip[3] = ($n_lines < $I_row-2) ? $$p_ip[2] : $I_row-2 
    } else {
        &sf_line($line, $p_ip);
    }
}
sub prev_line {
    my($line, $p_ip) = @_;
    &se_name($line, @{$p_ip}) if ($$p_ip[5] && !$$p_ip[0]);
    if ($$p_ip[3] || $$p_ip[0]) {
        if ($$p_ip[5]) {
            &se_name($line, @{$p_ip});
        }
        if ($$p_ip[3]) {
            $$p_ip[3]--;
            $$p_ip[1]-- if ($$p_ip[1] > 0);
        } else {
            &sr_line($line, $p_ip);
        }
    }
}
sub next_line {
    my($line, $p_ip, $n_lines) = @_;
    &se_name($line, @{$p_ip}) if ($$p_ip[5] && $$p_ip[2] == $n_lines);
    if ($$p_ip[2] < $n_lines || $$p_ip[1] < $n_lines) {
        if ($$p_ip[5]) {
            &se_name($line, @{$p_ip});
        }
        if ($$p_ip[3] < $I_row-2) {
            $$p_ip[3]++;
            $$p_ip[1]++;
        } else {
            &sf_line($line, $p_ip);
        }
    }
}
sub top_page {
    my($name, $line, $p_ip, $p_lines) = @_;
    my($i);
    &se_name($line, @{$p_ip}) if ($$p_ip[5] && !$$p_ip[0]);
    $$p_ip[2] = ($#{$p_lines} < $I_row-2) ? $#{$p_lines} : $I_row-2;
    $$p_ip[1] = $$p_ip[3] = 0;
    if ($$p_ip[0]) {
        $$p_ip[0] = 0;
        $$p_ip[2] = &disp_win($p_ip, $name, $p_lines);
    }
}
sub bot_page {
    my($name, $line, $p_ip, $p_lines) = @_;
    my($i);
    if ($line) {
        &se_name($line, @{$p_ip}) if ($$p_ip[5] && $$p_ip[2] == $#{$p_lines});
        $$p_ip[1] = $$p_ip[2];
    } else {
        $$p_ip[2] = $#{$p_lines} - 1 ; # force to execute &disp_win;
        $$p_ip[1] = $$p_ip[2] + 1;
    }
    $$p_ip[3] = &to_bot($p_ip, $p_lines);
    $$p_ip[2] = &disp_win($p_ip,$name,$p_lines) if ($$p_ip[2] < $#{$p_lines});
}
sub to_bot {
    my($p_ip, $p_lines) = @_;
    my($i);
    $i = ($#{$p_lines} < $I_row-2) ? $#{$p_lines} : $I_row-2;
    $$p_ip[0] = $#{$p_lines} - $i;
    $i;
}
sub dn_page {
    my($name, $line, $p_ip, $p_lines) = @_;
    my($i);
    if ($$p_ip[2] == $#{$p_lines}) {
        $i = ($#{$p_lines} < $I_row-2) ? $#{$p_lines} : $I_row-2;
        if ($$p_ip[3] < $I_row-2) {
            &se_name($line, @{$p_ip}) if ($$p_ip[5]);
            $$p_ip[1] = $$p_ip[2];
            $$p_ip[3] = $i;
        }
    } else {
        for ($i = 0; $i < $I_row-1 && $$p_ip[2] < $#{$p_lines}; $i++) {
            $$p_ip[0]++;
            $$p_ip[2]++;
        }
        $$p_ip[1] = $$p_ip[2];
        $$p_ip[3] = $I_row-2;
        if ($i < $I_row-1) {
            for ($i--; $i; $i--) {
                $$p_ip[1]-- if ($$p_ip[1] > 0);
                $$p_ip[3]--;
            }
        }
        $$p_ip[2] = &disp_win($p_ip, $name, $p_lines);
    }
}
sub up_page {
    my($name, $line, $p_ip, $p_lines) = @_;
    my($i);
    if (!$$p_ip[0]) {
        if ($$p_ip[3]) {
            &se_name($line, @{$p_ip}) if ($$p_ip[5]);
            $$p_ip[1] = $$p_ip[0];
            $$p_ip[3] = 0;
        }
    } else {
        for ($i = 0; $i < $I_row-1 && $$p_ip[0]; $i++) {
            $$p_ip[0]--;
            $$p_ip[2]--;
        }
        $$p_ip[1] = $$p_ip[0];
        $$p_ip[3] = 0;
        if ($i < $I_row-1) {
            for ($i--; $i; $i--) {
                $$p_ip[1]++;
                $$p_ip[3]++;
            }        
        }
        $$p_ip[2] = &disp_win($p_ip, $name, $p_lines);
    }
}
sub key_input {
    my($name, $old, $p_ip, $p_lines) = @_;
    my($col, $col0, $key, $mkey, $i, $line, $f_cm, @keys, $f_prog, $f_rm);
    $col0 = $col = &disp_mline($name, 'KEY', $old, $#{$p_lines}, @ip);
    &eeol($I_row-1, $col, $I_col/2-$col);
    if (!($f_prog = $name eq 'PROG')) {
        if (!($f_rm = $name eq 'RM')) {
            $line = $$p_lines[$$p_ip[1]]; 
            print "$line";
            $f_cm = (@keys = copy_keys($line));
        } else {
            $line = 'Delete ? [y/n] y';
            print "$line";
            @keys = ('y');
            $f_cm = 0;
            $f_prog = 1;
        }
    }
    $col = $col0 + scalar(@keys);
    while (1) {
        if ($F_disp) {
            $F_disp = 0;
            shift_top($p_ip, $p_lines);
            $$p_ip[2] = &disp_win($p_ip, $name, $p_lines);
            &disp_mline($name, 'KEY', $old, $#{$p_lines}, @{$p_ip});
            &disp_line($line, @{$p_ip}) if (!$f_prog);
            &eeol($I_row-1, $col0, $I_col/2-$col0);
            print "$line" if (!$f_prog);
        }
        next if (!read(STDIN, $key, 1));
        last if ($key . ' ' eq &M_cansel || $key . ' ' eq &M_HISTORY);
        if ($key && $key ne ' ' && index($Ikey, $key) < 0) {
            unless ($key =~ /\w/ && !$mkey) {
                $mkey .= $key;
                if (&is_keys($mkey, \@Pkeys)) {
                    $key = '';
                    next;
                } elsif (is_keys($mkey . ' ', \@Mkeys)) {
                    $key = $mkey . ' ';
                    $mkey = '';
                } elsif (is_keys($key . ' ', \@Mkeys)) {
                    $key .= ' ';
                    $mkey = '';
                }
            }
        }
        if ($key eq &M_cr) {
            &save_ip($name, @{$p_ip});
            $key = '';
            foreach (@keys) {
                $key .= $_;
            }
            if ($name eq 'FILE' || $name eq 'DIR') {
                if (($i = index($key, '//')) >= 0) {
                    $key = substr($key, $i + 1);
                }
                if (! -e $key) {
                    @keys = ();
                    $col = $col0;
                    &eeol($I_row-1, $col0, $I_col/2-$col0);
                } else {
                    $f_cm = 1;
                    last;
                }
            } else {
                $f_cm = 0 if ($f_prog);
                last;
            }
        } elsif ($key eq "\cH") {
            if ($col > $col0) {
                pop(@keys);
                &tc_move($I_row-1, --$col);
                print ' ';
                &tc_move($I_row-1, $col);
                $f_cm = 0;
            }
            $key = '';
        } elsif ($key eq &M_prev_line) {
            &prev_line($line, $p_ip) if (!$f_prog);
        } elsif ($key eq &M_next_line) { 
            &next_line($line, $p_ip, $#{$p_lines}) if (!$f_prog);
        } elsif ($key eq &M_top_page) {
            &top_page($name, $line, \@ip, $p_lines) if (!$f_prog);
        } elsif ($key eq &M_bot_page) {
            &bot_page($name, $line, \@ip, $p_lines) if (!$f_prog);
        } elsif ($key eq &M_dn_page) {
            &dn_page($name, $line, $p_ip, $p_lines) if (!$f_prog);
        } elsif ($key eq &M_up_page) {
            &up_page($name, $line, $p_ip, $p_lines) if (!$f_prog);
        } elsif ($key eq &M_clear) {
            $F_disp = 1;
            $key = '';
            next;
        } elsif ($key eq "\cU") {
            @keys = ();
            $col = $col0;
            &eeol($I_row-1, $col0, $I_col/2-$col0);
        } elsif ($key) {
            push(@keys, $key);
            &tc_move($I_row-1, $col++);
            print "$key";
            $mkey = $key = '';
        }
        if ($key) {
            $line = $$p_lines[$$p_ip[1]] if (!$f_prog);
            &disp_mline($name, 'KEY', $old, $#{$p_lines}, @{$p_ip});
            &disp_line($line, @{$p_ip}) if (!$f_prog);
            &eeol($I_row-1, $col0, $I_col/2-$col0);
            print "$line" if (!$f_prog);
            $f_cm = (@keys = copy_keys($line));
            $col = $col0 + scalar(@keys);
            $mkey = $key = '';
        }
    }
    if ($key . ' ' eq &M_cansel || $key . ' ' eq &M_HISTORY) {
        &save_ip($name, @{$p_ip});
        $f_cm = 0;
        $key .= ' ';
    } elsif (($i = index($key, '//')) >= 0) {
        $key = substr($key, $i + 1);
    }
    $key = "\cM" . $key if ($f_cm);
    $key;
}
sub copy_keys {
    my($key) = @_;
    my($i, @keys);
    while ($key) {
        unshift(@keys, chop($key));
    }
    @keys;
}
sub eeol {
    my($row, $col, $i) = @_;
    my($spc);
    &tc_move($row, $col);
    while ($i--) {
        $spc .= ' ';
    }
    print $spc;
    &tc_move($row, $col);
}
sub get_bin {
    my($name, $tmp, $len, $bin) = @_;
    $len = 1024;
    foreach (keys(%Bin)) {
        if (($tmp = index($name, $_)) >= 0 && $tmp < $len) {
            $len = $tmp;
            $bin = $Bin{$_};
        }
    }
    if (!$bin) {
        if (-B $name) { 
            $bin = $Bin{'~file'};
        } else {
            $bin = ' ' . (stat($name))[7];
        }
    }
    $bin;
}
sub get_name {
    my($name) = @_;
    my($c, @str);
    while (($c = chop($name)) ne '/') {
        push(@str, $c);
    }
    $name = '';
    while (($c = pop(@str)) || $c eq '0') {
        $name .= $c;
    }
    $name;
}
sub get_dir {
    my($name, $c) = @_;
    while (($c = chop($name)) ne '/') {
        ;
    }
    $name .= '/';
    $name;
}
sub is_keys {
    my($key, $p_keys) = @_;
    my($i, $k);
    for ($i = 0; ($k = $$p_keys[$i]) && $key ne $k; $i++) {
        ;
    }
    $k;
}
sub is_bkeys {
    my($key) = @_;
    if ($key eq ' ') {
        $key = &M_dn_page;
    } elsif ($key eq 'b') {
        $key = &M_up_page;
    }
    $key;
}
sub cut_end {
    my($line, $c) = @_;
    while (index($line, $c) >= 0) {
        while (chop($line) ne $c) {
            ;
        }
    }
    $line;
}
sub edit {
    my($name, $bin, @ip) = @_;
    my($i_row);
    if ($bin =~ /^ /) {
        $i_row = $ip[1]+1;
        &reset_console;
        system("$Editor +$i_row $name"); 
        &set_console(1);
    }
    $i_row;
}
sub prog {
    my($prog, $name) = @_;
    if ($name =~ /^\//) {
        &reset_console;
        system("$prog $name"); 
        &set_console(0);
        tc_move($I_row-2, 0);
        tc_so;
        print "\n\n -- Hit any key -- ";
        tc_se;
        while (!<STDIN>) {
            ;
        }
    }
}
sub cut_line {
    my($line) = @_;
    if (substr($line, $I_col)) {
        $line = substr($line, 0, $I_col);
    }
    $line;
}
sub shift_top {
    my($p_ip, $p_lines) = @_;
    my($i);
    if ($#{$p_lines} >= $$p_ip[2]) {
        ;
    } elsif (!$$p_ip[0] && $#{$p_lines} <= $I_row - 2) {
        if ($$p_ip[3] > $#{$p_lines}) {
            $$p_ip[1] = $$p_ip[3] = $$p_ip[2] = $#{$p_lines};
        }
    } elsif (($i = $I_row-2 - ($#{$p_lines} - $$p_ip[0])) > 0) {
        while ($i-- && $$p_ip[0]) {
            $$p_ip[0]--;
            $$p_ip[1]--;
            $$p_ip[2]--;
        }
        $$p_ip[3] = $$p_ip[1] if ($$p_ip[3] > $$p_ip[1]);
    }
    while ($$p_ip[3] > $I_row-2) { # window-size changed
        $$p_ip[1]--;
        $$p_ip[3]--;
    }
    $$p_ip[1] = 0 if ($$p_ip[1] < 0);
}

sub init_Pkeys {
    my($p_p, $p_a, $fd) = @_; 
    my($i, $n, $i_p, $i_a, $prev);
	    while($p_p) {
		print STDERR;
		print STDERR "\n";
	    }
    while (<$fd>) {
		print STDERR;
		print STDERR "\n";
	last if (!$_ || !index($_, '-'));
	next if (!($_ = &cut_end($_, '#')));
	s/\s//g;
	next if (!$_);
	$prev = &set_prev($_, \$n);
	for ($i = 1; $i <= $n; $i++) {
	    for (@$p_p) {
		last if (substr($_, 0, $i) eq substr($_, 0, $i));
	    }
	    if (!$_) {
		(($i == $n) ? $p_p[$i_p++] : $p_a[$i_a++])
		    = substr($prev, 0, $i);
	    }
	}
    }
    $_;
}

