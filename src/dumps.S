.section .note.GNU-stack,"",@progbits

.section .rodata, ""

.macro dump file, name
    .global \name
    \name:
    .incbin "\file"
    \name\()_end:

    .p2align 3
    .global \name\()_length
    \name\()_length:
    .quad \name\()_end - \name
.endm

dump "../normal", dump_normal
dump "../zlib", dump_zlib
dump "../lzo", dump_lzo
dump "../zstd", dump_zstd
dump "../inline", dump_inline
dump "../inline-zlib", dump_inline_zlib
dump "../inline-lzo", dump_inline_lzo
dump "../inline-zstd", dump_inline_zstd

.global dump_zero_sector
dump_zero_sector:
.space 4096
dump_zero_sector_end:

.p2align 3
.global dump_zero_sector_length
dump_zero_sector_length:
.quad dump_zero_sector_end - dump_zero_sector
