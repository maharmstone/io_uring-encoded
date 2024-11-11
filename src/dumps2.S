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

dump "dumps/normal", dump_normal
dump "dumps/zlib", dump_zlib
dump "dumps/lzo", dump_lzo
dump "dumps/zstd", dump_zstd
dump "dumps/inline", dump_inline
dump "dumps/inline-zlib", dump_inline_zlib
dump "dumps/inline-lzo", dump_inline_lzo
dump "dumps/inline-zstd", dump_inline_zstd
dump "dumps/bookend-zlib", dump_bookend_zlib
dump "dumps/bookend-lzo", dump_bookend_lzo
dump "dumps/bookend-zstd", dump_bookend_zstd
