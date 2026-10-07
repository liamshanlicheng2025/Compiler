`overflow` is a demo in which IR answers (expected output of your IR) that we provided fail.

The issue is that, when we run `rust` on 64-bit architecture, `usize` is in fact 64-bit wide. However our target is 32-bit `rv32i/m`. So some operations (`mul`, `shl` in llvm and `sll` `slli` in risc-v, etc.), used on `usize`, may cause overflow in your IR, but `i32` operations will not (why? Our code runs correctly in rust, i.e. no runtime panics).

Fortunately, our IR testcases seldom contain such issues (that have been found up to 12/07/2025). But if you find such issues, you may **contact TA** to fix them.

The files `stderr.txt` and `stdout.txt` are produced by [REIMU](https://github.com/DarkSharpness/REIMU/). `overflow.out` contains the output by 64-bit machine (your host machine) when running the same program compiled by `rustc`. `test.out` is the output computed by REIMU (actually the output on 32-bit machine). You may compare them to see the differences.
