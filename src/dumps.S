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

.global dump_zero_sector
dump_zero_sector:
.space 4096
dump_zero_sector_end:

.p2align 3
.global dump_zero_sector_length
dump_zero_sector_length:
.quad dump_zero_sector_end - dump_zero_sector
