#!/bin/sh
# Testovaci soubor urceny pro studenty pro
# 2. projekt IZP 2020/2021 (sps.c)
# Ales Smrcka
# 2020-12-05
# Fixed by Baerwin

export LC_ALL=C

SRC=sps.c
BIN=${SRC%.c}
CFLAGS="-std=c99 -Wall -Wextra -g"
VALGRIND_CMDLINE="valgrind --leak-check=full --log-file="

valgrind=
passed_msg=passed
failed_msg=failed
tests_result=0

die() {
    echo "$@" >&2
    exit 1
}

check_env() {
    if ! [ -r $SRC ]; then
        die "error: File sps.c not found or readable."
    fi

    if ! which cc >/dev/null 2>/dev/null; then
        die "error: Compiler (cc command) not found."
    fi

    type valgrind >/dev/null 2>/dev/null &&
        valgrind="$VALGRIND_CMDLINE"

    touch tmp.XYZ 2>/dev/null && rm tmp.XYZ ||
        die "error: Unable to write files. Check permissions."

    if [ -t 1 ]; then
        passed_msg="$(tput setaf 2)passed$(tput sgr0)"
        failed_msg="$(tput setaf 1)failed$(tput sgr0)"
    fi
}

setup() {
    cat >t.txt <<EOF
ahoj,svete,1
hello,world,2
3,4,5
EOF
}

teardown() {
    rm t.txt tab1.txt 2>/dev/null
}

# $1 test name
# $2 filename
report() {
    local result=$?
    local tname="$1"
    local fname="$2"
    shift 2
    if [ $result -eq 0 ]; then
        printf "%-70s %s\n" "$*" "$passed_msg"
    else
        printf "%-70s %s\n" "$*" "$failed_msg"
    fi
    if [ -n "$valgrind" ]; then
        if grep -q "lost: [1-9]" $tname.valgrind.log; then
            echo "       memory problems, cf. $tname.valgrind.log"
        else
            rm $tname.valgrind.log
        fi
    fi
    return $result
}

# $1 file
# $2 row
# $3 column
# $4 value
assert() {
    [ "x$4" = x$(head -n $2 <$1 | tail -n 1 | cut -d, -f$3) ]
}

# $1 = test name
# $2 = SPC command
# $3 = filename
t() {
    local tname="$1"
    local cmd="$2"
    local fname="$3"
    shift 3

    setup
    if [ -n "$valgrind" ]; then
        $valgrind$tname.valgrind.log ./$BIN -d , "$cmd" "$fname"
    else
        ./$BIN -d , "$cmd" "$fname"
    fi
    local i=0
    while [ -n "$3" ]; do
        assert "$fname" $1 $2 $3
        i=$((i+$?))
        shift 3
    done
    [ $i -eq 0 ]
    report $tname $fname "$tname: $cmd" 
    local result=$?
    teardown
    tests_result=$((tests_result+result))
    return $result
}

compile() {
    cc -std=c99 -Wall -Wextra -g sps.c -o sps || exit 1
}

test_basic() {
    t basic_set "[1,1];set x" t.txt 1 1 x
}

test_selection() {
    t sel1 "[1,1];[2,2];set x" t.txt 2 2 x
    # POZN: na souradnicich 1 1 by melo byt "x",
    #       na souradnicich 1 2 by melo byt take "x"
    #       na souradnicich 1 3 by melo byt take "x"
    t sel2 "[1,_];set x" t.txt 1 1 x 1 2 x 1 3 x
    t sel3 "[_,_];set x" t.txt 1 1 x 2 2 x 3 3 x 1 3 x 3 1 x
    t min "[3,_];[min];set x" t.txt 3 1 x
    t max "[_,_];[max];set x" t.txt 3 3 x
    t find "[_,_];[find orld];set x" t.txt 2 2 x
}

test_structure() {
    t irow "[1,1];irow" t.txt 1 1 ""
    t arow "[1,1];arow" t.txt 1 1 ahoj    2 1 ""
    t icol "[1,2];icol" t.txt 1 1 ahoj    1 2 ""
    t acol "[1,2];acol" t.txt 1 1 ahoj    1 3 ""
}

test_change() {
    t clear "[2,2];clear" t.txt 2 1 hello 2 2 "" 2 3 "2"
    t swap "[1,1];swap [2,1]" t.txt 1 1 hello 2 1 ahoj
    t sum "[1,3,2,3];sum [3,3]" t.txt 3 3 "3"
    t avg "[1,3,2,3];avg [3,3]" t.txt 3 3 "1.5"
}

test_vars() {
    t vars1 "[1,1];def _0;[2,1];def _1;use _0;[1,1];use _1" t.txt 1 1 hello 2 1 ahoj
    t vars2 "[1,3];def _9;inc _9;use _9" t.txt 1 3 2
    t vars3 "[1,1];[set];[2,1];[_];set x" t.txt 1 1 x 2 1 hello
}

run_tests() {
    test_basic || die "Neprobehl ani zakladni test, koncim"
    test_selection
    test_structure
    test_change
    test_vars
}

if [ "x$1" = x-h ]; then
    echo "Usage:"
    echo "      $(basename $0)            run tests"
    echo "      $(basename $0) clean      remove files from tests"
    exit 0
elif [ "x$1" = xclean ]; then
    rm *.log $BIN 2>/dev/null
    echo "Tests cleaned"
    exit 0
fi

check_env
compile
trap 't=$?; teardown; exit $?' INT TERM HUP EXIT
run_tests
exit $tests_result
