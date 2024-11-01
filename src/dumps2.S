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
dump "../bookend-zlib", dump_bookend_zlib
dump "../bookend-lzo", dump_bookend_lzo
dump "../bookend-zstd", dump_bookend_zstd
