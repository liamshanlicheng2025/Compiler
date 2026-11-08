// 运行时库：我们 IR 里调用的内建函数名 → REIMU 提供的 libc（printf/scanf/exit）
// 预编译成 runtime.s 随仓库提交；BUILD 时拷到 {runtime} 参与每个程序的链接
//（调用约定 ILP32，目标 rv32im，与评测 runner 一致）

int printf(const char* fmt, ...);
int scanf(const char* fmt, ...);

void printInt(int x)   { printf("%d", x); }
void printlnInt(int x) { printf("%d\n", x); }
int  getInt(void)      { int v = 0; scanf("%d", &v); return v; }

void print(const char* s)   { printf("%s", s); }
void println(const char* s) { printf("%s\n", s); }
