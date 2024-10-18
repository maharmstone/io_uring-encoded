.section .note.GNU-stack,"",@progbits

.section .rodata, ""

.global dump_normal
dump_normal:
.incbin "../normal"
dump_normal_end:

.p2align 3
.global dump_normal_length
dump_normal_length:
.quad dump_normal_end - dump_normal

.global dump_zlib
dump_zlib:
.incbin "../zlib"
dump_zlib_end:

.p2align 3
.global dump_zlib_length
dump_zlib_length:
.quad dump_zlib_end - dump_zlib

.global dump_lzo
dump_lzo:
.incbin "../lzo"
dump_lzo_end:

.p2align 3
.global dump_lzo_length
dump_lzo_length:
.quad dump_lzo_end - dump_lzo

.global dump_zstd
dump_zstd:
.incbin "../zstd"
dump_zstd_end:

.p2align 3
.global dump_zstd_length
dump_zstd_length:
.quad dump_zstd_end - dump_zstd

.global dump_inline
dump_inline:
.incbin "../inline"
dump_inline_end:

.p2align 3
.global dump_inline_length
dump_inline_length:
.quad dump_inline_end - dump_inline
