; Relocatable resident N: CIO table, intentionally stub-only in this
; increment.  The table uses the Atari address-minus-one ABI.

        .export _fnresident_module_handler_table
        .export _fnresident_module_marker

FNCNOT  = $92

.segment "RODATA"

_fnresident_module_handler_table:
        .word fnresident_open-1
        .word fnresident_close-1
        .word fnresident_get-1
        .word fnresident_put-1
        .word fnresident_status-1
        .word fnresident_special-1
        .byte $4c
        .word fnresident_init
        .byte 0

; The loader derives an existing payload base from HATABS minus the generated
; table offset and checks this marker before treating N: as its own.
_fnresident_module_marker:
        .byte "FNR1"

.segment "CODE"

fnresident_open:
fnresident_close:
fnresident_put:
fnresident_status:
fnresident_special:
        ldy #FNCNOT
        rts

fnresident_get:
        lda #0
        ldy #FNCNOT
        rts

fnresident_init:
        rts
