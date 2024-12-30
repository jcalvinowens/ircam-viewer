.global builtin_ttf_start
.global builtin_ttf_end

.section .rodata
builtin_ttf_start:
.incbin "fonts/deja_vu_sans_mono.ttf"
builtin_ttf_end:

.section .note.GNU-stack, "", %progbits
