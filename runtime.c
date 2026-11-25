// 运行时库：我们 IR 里调用的内建函数名 → 评测环境提供的 libc（printf/scanf）
// 预编译成 runtime.s 随仓库提交；BUILD 时拷到 {runtime} 参与每个程序的链接
//（调用约定 ILP32，目标 rv32im，与评测 runner 一致）

int printf(const char* fmt, ...);
int scanf(const char* fmt, ...);

void printInt(int x)   { printf("%d", x); }
void printlnInt(int x) { printf("%d\n", x); }
int  getInt(void)      { int v = 0; scanf("%d", &v); return v; }

void print(const char* s)   { printf("%s", s); }
void println(const char* s) { printf("%s\n", s); }

// Box/Vec 的堆分配：bump allocator（只增不减——测试程序没有 drop 语义，
// 64MB 静态堆对评测规模足够）。8 字节对齐，与 i64/指针的自然对齐一致
static char heap[64 * 1024 * 1024];
static unsigned long heap_off = 0;
void* rx_alloc(int n) {
    n = (n + 7) & ~7;
    void* p = heap + heap_off;
    heap_off += (unsigned)n;
    return p;
}
