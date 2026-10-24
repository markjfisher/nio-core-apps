; Story 2.5 increment 1: resident CIO N: handler ABI surface.
; The Atari CIO dispatcher enters these routines with X as the IOCB offset.
; PUT receives its byte in A, GET returns a byte in A, and Y carries the
; CIO status on return.  Networking is deliberately not present yet.

        .export _fnresident_handler_table

FNCNOT  = $92           ; Atari OS: function not implemented in handler.

.segment "RODATA"

_fnresident_handler_table:
        .word fnresident_open-1
        .word fnresident_close-1
        .word fnresident_get-1
        .word fnresident_put-1
        .word fnresident_status-1
        .word fnresident_special-1
        .byte $4c        ; CIO handler-init JMP opcode.
        .word fnresident_init
        .byte 0

.segment "CODE"

fnresident_open:
fnresident_close:
fnresident_put:
fnresident_status:
fnresident_special:
        ldy #FNCNOT
        rts

fnresident_get:
        lda #0           ; Deterministic GET byte while unsupported.
        ldy #FNCNOT
        rts

fnresident_init:
        rts
