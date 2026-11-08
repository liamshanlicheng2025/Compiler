	.text
	.attribute	4, 16
	.attribute	5, "rv32i2p0_m2p0"
	.file	"runtime.c"
	.globl	printInt
	.p2align	2
	.type	printInt,@function
printInt:
	lui	a1, %hi(.L.str)
	addi	a1, a1, %lo(.L.str)
	addi	a2, a0, 0
	addi	a0, a1, 0
	addi	a1, a2, 0
	tail	printf
.Lfunc_end0:
	.size	printInt, .Lfunc_end0-printInt

	.globl	printlnInt
	.p2align	2
	.type	printlnInt,@function
printlnInt:
	lui	a1, %hi(.L.str.1)
	addi	a1, a1, %lo(.L.str.1)
	addi	a2, a0, 0
	addi	a0, a1, 0
	addi	a1, a2, 0
	tail	printf
.Lfunc_end1:
	.size	printlnInt, .Lfunc_end1-printlnInt

	.globl	getInt
	.p2align	2
	.type	getInt,@function
getInt:
	addi	sp, sp, -16
	sw	ra, 12(sp)
	sw	zero, 8(sp)
	lui	a0, %hi(.L.str)
	addi	a0, a0, %lo(.L.str)
	addi	a1, sp, 8
	call	scanf
	lw	a0, 8(sp)
	lw	ra, 12(sp)
	addi	sp, sp, 16
	jalr	zero, 0(ra)
.Lfunc_end2:
	.size	getInt, .Lfunc_end2-getInt

	.globl	print
	.p2align	2
	.type	print,@function
print:
	lui	a1, %hi(.L.str.2)
	addi	a1, a1, %lo(.L.str.2)
	addi	a2, a0, 0
	addi	a0, a1, 0
	addi	a1, a2, 0
	tail	printf
.Lfunc_end3:
	.size	print, .Lfunc_end3-print

	.globl	println
	.p2align	2
	.type	println,@function
println:
	lui	a1, %hi(.L.str.3)
	addi	a1, a1, %lo(.L.str.3)
	addi	a2, a0, 0
	addi	a0, a1, 0
	addi	a1, a2, 0
	tail	printf
.Lfunc_end4:
	.size	println, .Lfunc_end4-println

	.type	.L.str,@object
	.section	.rodata.str1.1,"aMS",@progbits,1
.L.str:
	.asciz	"%d"
	.size	.L.str, 3

	.type	.L.str.1,@object
.L.str.1:
	.asciz	"%d\n"
	.size	.L.str.1, 4

	.type	.L.str.2,@object
.L.str.2:
	.asciz	"%s"
	.size	.L.str.2, 3

	.type	.L.str.3,@object
.L.str.3:
	.asciz	"%s\n"
	.size	.L.str.3, 4

	.ident	"Ubuntu clang version 15.0.7"
	.section	".note.GNU-stack","",@progbits
	.addrsig
