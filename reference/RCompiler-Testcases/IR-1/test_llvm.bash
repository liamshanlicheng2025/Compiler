#!/bin/bash

# Usage: scripts/test_llvm_ir.bash <compiler> <testcase> [builtin] [tempdir]
# The builtin and tempdir are optional. If there are three arguments, the
# script will check whether the third argument is a file. If it is a file,
# then it will be treated as the builtin file. Otherwise, it will be treated
# as the tempdir.
# Example:
#     scripts/test.bash 'bin/mxc -emit-llvm' testcases/codegen/t1.mx bin/builtin.ll
# The script will
# 1. Get an temporary directory
# 2. Execute <compiler> < <testcase> > "$TEMPDIR/output.ll"
# 3. Get the test.in and test.ans from <testcase> using sed
# 4. Execute clang -S --target=riscv32-unknown-elf
# 5. Execute ravel --input-file="$TEMPDIR/test.in" --output-file="$TEMPDIR/test.out" "$TEMPDIR/builtin.s" "$TEMPDIR/output.s" > "$TEMPDIR/ravel_output.txt"
# 6. Compare the output and exit code

# NOTE: You should have ravel installed in your system.

# Get the clang
# LLVM makes a breaking change starting from LLVM 15, making opaque pointers
# the default. We need to use the newer version of llc to compile the builtin
# functions.
# If you are using an old version of llc, you may need to change the following
# code to use the correct version of llc. For example, if you are using LLVM 14,
# you need to comment or delete the following lines in get_clang() and replace
# them with 'echo llc-14'.
# For maintainers: please update the following code when a new version of LLVM
# is released.
get_clang() {
    (which clang-15 > /dev/null 2> /dev/null && echo clang-15) || \
    (which clang-16 > /dev/null 2> /dev/null && echo clang-16) || \
    (which clang-17 > /dev/null 2> /dev/null && echo clang-17) || \
    (which clang-18 > /dev/null 2> /dev/null && echo clang-18) || \
    (which clang > /dev/null 2> /dev/null && echo clang) || \
    (echo "clang not found" >&2 && exit 1)
}
CLANG=$(get_clang)

# Usage
if [ $# -ne 2 ] && [ $# -ne 3 ]; then
    cat << EOF >&2 
Usage: $0 <testcase> <builtin> [tempdir]
       The tempdir is optional.
       Your compiler is run using 'make run' in the current directory.
EOF
    exit 1
fi

# Set variables
TESTCASE=$1
BUILTIN=$2

if [ ! -f "$BUILTIN" ]; then
    echo "Error: builtin file $BUILTIN does not exist." >&2
    exit 1
fi

# Test whether the testcase file and builtin file exist or not
if [ ! -d "src/$TESTCASE" ] || [ ! -f "src/$TESTCASE/${TESTCASE}.rx" ] || [ ! -f "src/$TESTCASE/${TESTCASE}.in" ] || [ ! -f "src/$TESTCASE/${TESTCASE}.out" ]; then
    echo "Error: testcase $TESTCASE does not exist or test files are missing." >&2
    exit 1
fi

test_bin() {
    which $1 > /dev/null 2> /dev/null
    if [ $? -ne 0 ]; then
        echo "$1 not found" >&2
        exit 1
    fi
}

print_red_msg() {
    echo -e "\033[31m$1\033[0m" >&2
}
print_green_msg() {
    echo -e "\033[32m$1\033[0m" >&2
}


# Test whether ravel is installed
# If not installed, please follow the document at
# <https://github.com/Engineev/ravel>.
# Note: If you just follow the steps in the README, you need to put the last
# line (export PATH="/usr/local/opt/bin:$PATH") in your .bashrc or .zshrc
# (depending on which shell you are using).
test_bin reimu

# 1. Make temp directory
if [ $# -eq 3 ]; then
    USER_DEFINED_TEMPDIR=1
    TEMPDIR=$3
else
    USER_DEFINED_TEMPDIR=0
    TEMPDIR="$(mktemp -d -p /tmp mxc.XXXXXXXXXX)"
    if [ $? -ne 0 ]; then
        echo "Error: Failed to create temp directory." >&2
        exit 1
    fi
fi
if [ ! -d "${TEMPDIR}" ]; then
    echo "Error: temp directory does not exist." >&2
    exit 1
fi

# clean cleans up the temp directory
clean() {
    if [ $USER_DEFINED_TEMPDIR -eq 0 ]; then
        rm -rf "${TEMPDIR}"
    fi
}

# print_temp_dir prints the temp directory
# This function is used when the test fails
print_temp_dir() {
    cat << EOF >&2
All generated files are at '${TEMPDIR}'. You may check some files there.
For example, you may check the output of your compiler at '${TEMPDIR}/output.ll'.
Use the following command to clean up the temp directory:
    rm -rf '${TEMPDIR}'
EOF
}

# 2. Compile the testcase with your compiler
echo "Compiling '$TESTCASE' with your compiler..." >&2
make run < "src/$TESTCASE/${TESTCASE}.rx" > "${TEMPDIR}/output.ll"
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile $TESTCASE." >&2
    clean
    exit 1
fi

# 3. Get the test.in and test.out
cp "src/${TESTCASE}/${TESTCASE}.in" "${TEMPDIR}/test.in"
cp "src/${TESTCASE}/${TESTCASE}.out" "${TEMPDIR}/test.ans"

# 4. Compile the LLVM IR code with clang into RISC-V assembly
echo "Compiling your output '${TEMPDIR}/output.ll' with clang..." >&2
$CLANG -S --target=riscv32-unknown-elf "${TEMPDIR}/output.ll" -o "${TEMPDIR}/output.s.source" >&2
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile '${TEMPDIR}/output.ll'." >&2
    print_temp_dir
    exit 1
fi
echo "Compiling your builtin '${BUILTIN}' with clang..." >&2
$CLANG -S --target=riscv32-unknown-elf -O2 -fno-builtin "${BUILTIN}" -o "${TEMPDIR}/builtin.s.source" >&2
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile '${BUILTIN}.c'." >&2
    print_temp_dir
    exit 1
fi
# remove the '@plt' suffix of the function name that is not supported by ravel
remove_plt() {
    sed 's/@plt//g' "$1" > "$2"
}
remove_plt "${TEMPDIR}/output.s.source" "${TEMPDIR}/test.s"
remove_plt "${TEMPDIR}/builtin.s.source" "${TEMPDIR}/builtin.s"

# 5. Execute the code
cd $TEMPDIR
echo "Executing the code..." >&2
reimu -i="${TEMPDIR}/test.in" -o="${TEMPDIR}/test.out" > "${TEMPDIR}/reimu_output.txt" 2> "${TEMPDIR}/reimu_err.txt"
REIMU_EXIT_CODE=$?
if [ $REIMU_EXIT_CODE -ne 0 ]; then
    cat << EOF >&2
Error: Reimu exits with a non-zero value ${REIMU_EXIT_CODE}.
EOF
    print_temp_dir
    exit 1
fi

# 6. Compare the output and exit code
HAS_PROBLEM=0
diff -ZB "$TEMPDIR/test.out" "$TEMPDIR/test.ans" >&2
if [ $? -ne 0 ]; then
    echo "Error: Output mismatch." >&2
    print_temp_dir
    HAS_PROBLEM=1
fi

if [ $HAS_PROBLEM -eq 0 ]; then
    print_green_msg "Passed"
    print_temp_dir
    exit 0
else
    print_red_msg "Failed"
    print_temp_dir
    exit 1
fi
