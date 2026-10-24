; Relocatable resident N: CIO table.  OPEN enters C only through the bounded
; foreground gateway below; the remaining operations stay deterministic stubs
; until their respective Story 2.5 increments.  The table uses Atari's
; address-minus-one ABI.

        .export _fnresident_module_handler_table
        .export _fnresident_module_marker
        .export _fnresident_module_entry

FNCNOT  = $92
CC65_ZP = $82
CC65_ZP_SIZE = 26

        .import _fn_init
        .import _fnresident_gateway_open
        .import _fnresident_gateway_get
        .import _fnresident_gateway_close
        .import _fnresident_gateway_iocb
        .import _fnresident_gateway_cstack_top
        .import _fnresident_gateway_byte

; The bounded Atari o65 loader copies TEXT and initialized DATA.  This table
; and marker are live post-install metadata, so they must not reside in an
; ld65-only RODATA segment that would be absent from the loaded image.
.segment "DATA"

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

; The transient loader selects its separately-reserved C stack before it
; invokes this entry.  Preserve that exact top for later CIO calls, then let
; the public library initialize normally.  This keeps all post-install C
; dependencies inside the MEMLO-owned resident allocation.
_fnresident_module_entry:
        lda CC65_ZP
        sta _fnresident_gateway_cstack_top
        lda CC65_ZP+1
        sta _fnresident_gateway_cstack_top+1
        jsr _fn_init
        rts

fnresident_open:
        ; CIO entry: X is the IOCB offset and A is caller-owned input.  Do not
        ; use the caller's cc65 ZP bank or C stack.  A nested/interrupt-time
        ; invocation cannot safely share the two library sessions, so reject
        ; it before touching caller state.
        pha
        lda gateway_busy
        bne gateway_reentrant
        pla
        sta caller_a
        stx caller_x
        inc gateway_busy
        jsr save_caller_zp
        ldx caller_x
        stx _fnresident_gateway_iocb
        lda _fnresident_gateway_cstack_top
        sta CC65_ZP
        lda _fnresident_gateway_cstack_top+1
        sta CC65_ZP+1
        jsr _fnresident_gateway_open
        sta gateway_status
        jsr restore_caller_zp
        dec gateway_busy
        lda caller_a
        ldx caller_x
        ldy gateway_status
        rts

gateway_reentrant:
        ldy #FNCNOT
        pla
        rts

fnresident_close:
        jmp fnresident_close_gateway

fnresident_get:
        ; GET returns its byte in A, so unlike OPEN/CLOSE it must not restore
        ; caller A after the foreground public call. X and the caller's cc65
        ; ZP/CPU stack still retain the CIO ABI contract.
        lda gateway_busy
        bne gateway_reentrant_get
        stx caller_x
        inc gateway_busy
        jsr save_caller_zp
        ldx caller_x
        stx _fnresident_gateway_iocb
        lda _fnresident_gateway_cstack_top
        sta CC65_ZP
        lda _fnresident_gateway_cstack_top+1
        sta CC65_ZP+1
        jsr _fnresident_gateway_get
        sta gateway_status
        jsr restore_caller_zp
        dec gateway_busy
        lda _fnresident_gateway_byte
        ldx caller_x
        ldy gateway_status
        rts

gateway_reentrant_get:
        lda #0
        ldy #FNCNOT
        rts

fnresident_close_gateway:
        pha
        lda gateway_busy
        bne gateway_reentrant
        pla
        sta caller_a
        stx caller_x
        inc gateway_busy
        jsr save_caller_zp
        ldx caller_x
        stx _fnresident_gateway_iocb
        lda _fnresident_gateway_cstack_top
        sta CC65_ZP
        lda _fnresident_gateway_cstack_top+1
        sta CC65_ZP+1
        jsr _fnresident_gateway_close
        sta gateway_status
        jsr restore_caller_zp
        dec gateway_busy
        lda caller_a
        ldx caller_x
        ldy gateway_status
        rts

fnresident_put:
fnresident_status:
fnresident_special:
        ldy #FNCNOT
        rts

fnresident_init:
        rts

save_caller_zp:
        ldx #CC65_ZP_SIZE-1
@loop:  lda CC65_ZP,x
        sta caller_zp,x
        dex
        bpl @loop
        rts

restore_caller_zp:
        ldx #CC65_ZP_SIZE-1
@loop:  lda caller_zp,x
        sta CC65_ZP,x
        dex
        bpl @loop
        rts

.segment "BSS"

; This is resident state, not transient loader scratch.  Saving the complete
; documented cc65 ZP bank lets CIO callers and unrelated cc65 programs retain
; their own C stack/register state after a foreground handler invocation.
caller_a:       .res 1
caller_x:       .res 1
gateway_status: .res 1
gateway_busy:   .res 1
caller_zp:      .res CC65_ZP_SIZE
