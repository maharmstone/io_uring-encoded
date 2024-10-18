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
