`test` is a simple case to test the REIMU simulator. In `test.ll` you can see a sample of llvm IR and how to utilize `memset`, `memcpy` and other built-in functions.

The test script is `test_llvm.bash`, which compiles your `builtin.c` and the llvm IR that your compiler generates and runs on the REIMU simulator.

In your inplementation, include a `Makefile` to run your compiler which reads from stdin and writes to stdout. The test script will use it to compile the test case.
