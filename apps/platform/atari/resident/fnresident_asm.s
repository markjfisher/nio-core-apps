; Production N: resident. CA65 only; no library, C ABI or software stack.
; Two CIO owners, 128-byte caches, one 512-byte packet/recovery workspace.
; $82/$83 are borrowed only inside the non-reentrant foreground gateway.
; The o65 loader's 26-byte ZP bank is conservatively charged by the RAM gate.
        .macpack longbranch
        .export _fnresident_module_entry, _fnresident_module_handler_table
        .export _fnresident_module_marker
        .export _fnresident_permanent_stack
_fnresident_permanent_stack = 0
        .segment "ZEROPAGE"
ptr:    .res 2
        .res 24

; Each owner occupies 16 bytes. All accesses use X=0 or 16.
OWN=0
MODE=1
HANDLE=2
OFFSET=4
LEN=8
POS=9
EOF=10
FAULT=11
COMMITTED=12
READING=13
PACKET_MAX=160
        .segment "CODE"
_fnresident_module_entry:
        lda #$ff
        sta owners+OWN
        sta owners+16+OWN
        lda #0
        rts

open:   ldy #0
        beq gateway
close:  ldy #1
        bne gateway
get:    ldy #2
        bne gateway
put:    ldy #3
        bne gateway
status: ldy #4
gateway:
        pha
        lda busy
        beq :+
        pla
        ldy #$92
        rts
:       inc busy
        pla
        sta caller_a
        stx caller_x
        sty operation
        lda ptr
        sta saved_zp
        lda ptr+1
        sta saved_zp+1
        jsr dispatch
        sta result
        lda saved_zp
        sta ptr
        lda saved_zp+1
        sta ptr+1
        dec busy
        lda caller_a
        ldx caller_x
        ldy result
        rts
dispatch:
        txa
        and #$8f
        beq :+
        lda #$86
        rts
:       ldx #16
@find:  lda owners+OWN,x
        cmp caller_x
        beq @found
        txa
        beq @missing
        ldx #0
        beq @find
@missing:
        lda operation
        beq do_open
        lda #$85
        rts
@found: stx slot
        lda operation
        bne :+
        lda #$81
        rts
:       cmp #1
        jeq do_close
        cmp #2
        jeq do_get
        cmp #3
        jeq do_put
        jmp do_status
success:lda #1
        rts
io_error:
        lda #$90
        rts
unsupported:
        lda #$92
        rts
bad_name:
        lda #$a5
        rts

do_open:
        ldx caller_x
        lda $034a,x
        cmp #4
        beq :+
        cmp #8
        beq :+
        cmp #12
        bne unsupported
:       sta open_mode
        ldx #16
        lda owners+OWN,x
        bmi @free
        ldx #0
        lda owners+OWN,x
        bmi @free
        lda #$a1
        rts
@free:  stx slot
        lda #1
        jsr begin_packet
        bcs io_error
        ldx caller_x
        lda $0344,x
        sta ptr
        lda $0345,x
        sta ptr+1
        ora ptr
        beq bad_name
        ldy #0
        lda (ptr),y
        cmp #'N'
        bne bad_name
        iny
        lda (ptr),y
        cmp #'1'
        bne bad_name
        iny
        lda (ptr),y
        cmp #':'
        bne bad_name
        ldx #0
@uri:   iny
        lda (ptr),y
        beq @end
        cmp #$9b
        beq @end
        cpx #128
        beq bad_name
        sta packet+11,x
        inx
        bne @uri
@end:   cpx #0
        beq bad_name
        stx packet+9
        txa
        clc
        adc #19
        sta request_len
        lda open_mode
        cmp #4
        beq @get_method
        lda #4                  ; unknown-length POST/PUT body
        sta packet+8
        lda #3                  ; PUT
        ldx open_mode
        cpx #8
        beq @method
        lda #2                  ; update is POST
        bne @method
@get_method:
        lda #1
@method:sta packet+7
        jsr exchange
        jcs io_error
        lda payload_len
        cmp #7
        jcc io_error
        ldy payload
        lda packet+4,y
        sta handle_tmp
        lda packet+5,y
        sta handle_tmp+1
        and handle_tmp
        cmp #$ff
        jeq io_error
        ldx slot
        ldy #15
        lda #0
@clear: sta owners,x
        inx
        dey
        bpl @clear
        ldx slot
        lda caller_x
        sta owners+OWN,x
        lda open_mode
        sta owners+MODE,x
        lda handle_tmp
        sta owners+HANDLE,x
        lda handle_tmp+1
        sta owners+HANDLE+1,x
        jmp success

; Prepare request without overwriting an outstanding response. Recovery only
; P/R drains the old transaction; it NEVER replays W or application data.
begin_packet:
        sta command
        lda outstanding
        beq @clear
        jsr receive
        bcc @clear
        lda outstanding
        bne @bad
@clear: ldx #0
        lda #0
@zero:  sta packet,x
        inx
        cpx #PACKET_MAX
        bne @zero
        lda #$fd
        sta packet
        lda command
        sta packet+1
        lda #1
        sta packet+6
        clc
@return:rts
@bad:   sec
        rts
session_packet:
        jsr begin_packet
        bcs @return
        ldx slot
        lda owners+HANDLE,x
        sta packet+7
        lda owners+HANDLE+1,x
        sta packet+8
        ldy #0
@offset:lda owners+OFFSET,x
        sta packet+9,y
        inx
        iny
        cpy #4
        bne @offset
        lda #9
        sta request_len
        ldx slot
        clc
@return:rts
cache_index:
        lda slot
        asl
        asl
        asl
        clc
        adc owners+POS,x
        tay
        rts
add_offset:
        ; A=acknowledged byte count. Full 32-bit stream offsets.
        clc
        adc owners+OFFSET,x
        sta owners+OFFSET,x
        bcc @done
        inc owners+OFFSET+1,x
        bne @done
        inc owners+OFFSET+2,x
        bne @done
        inc owners+OFFSET+3,x
@done:  rts
do_get:
        lda owners+MODE,x
        cmp #4
        beq @read
        cmp #12
        jne unsupported
        lda owners+READING,x
        bne @read
        jsr finish_write        ; commit POST before reading response
        cmp #1
        jne io_error
        lda #0
        sta owners+OFFSET,x
        sta owners+OFFSET+1,x
        sta owners+OFFSET+2,x
        sta owners+OFFSET+3,x
        sta owners+LEN,x
        sta owners+POS,x
        sta owners+EOF,x
        inc owners+READING,x
@read:
        lda owners+POS,x
        cmp owners+LEN,x
        jcc @cached
        lda owners+EOF,x
        jne eof
        lda #2
        jsr session_packet
        jcs io_error
        lda #128
        sta packet+13
        lda #15
        sta request_len
        jsr exchange
        jcs io_error
        jsr check_rw
        jcs io_error
        ; Check returned data fits the packet and the 128-byte stream cache.
        lda payload_len
        sec
        sbc #12
        cmp count
        jcc io_error
        ldy payload
        lda packet+1,y
        and #1
        sta owners+EOF,x
        lda count
        sta owners+LEN,x
        jsr add_offset
        lda #0
        sta owners+POS,x
        jsr cache_index
        sty cache_pos
        ldy payload
        ldx #0
@copy:  cpx count
        beq @copied
        lda packet+12,y
        sty temp
        ldy cache_pos
        sta caches,y
        inc cache_pos
        ldy temp
        iny
        inx
        bne @copy
@copied:ldx slot
        lda count
        beq eof
@cached:jsr cache_index
        lda caches,y
        sta caller_a
        inc owners+POS,x
        jmp success
eof:    lda #$88
        rts
do_put:
        lda owners+MODE,x
        cmp #4
        jeq unsupported
        lda owners+FAULT,x
        ora owners+COMMITTED,x
        jne io_error
        lda owners+LEN,x
        sta owners+POS,x
        jsr cache_index
        lda caller_a
        sta caches,y
        inc owners+LEN,x
        lda owners+LEN,x
        cmp #128
        jne success
        jmp flush
flush:
        lda #3
        jsr session_packet
        bcs write_error
        lda owners+LEN,x
        sta packet+13
        clc
        adc #15
        sta request_len
        lda #0
        sta owners+POS,x
        jsr cache_index
        ldx #0
@copy:  cpx packet+13
        beq @send
        lda caches,y
        sta packet+15,x
        iny
        inx
        bne @copy
@send:  jsr exchange
        bcs write_error
        jsr check_rw
        bcs write_error
        lda count
        cmp owners+LEN,x
        bcc @partial
        bne write_error
        jsr add_offset
        lda #0
        sta owners+LEN,x
        jmp success
@partial:
        ; A validated short acknowledgement permits sending only the suffix.
        ; Never resend the acknowledged prefix or retry a failed exchange.
        cmp #0
        beq write_error
        jsr add_offset
        lda owners+LEN,x
        sec
        sbc count
        sta owners+LEN,x
        sta temp
        lda slot
        asl
        asl
        asl
        tay
        clc
        adc count
        tax
@shift: lda caches,x
        sta caches,y
        inx
        iny
        dec temp
        bne @shift
        jmp flush
write_error:
        ldx slot
        lda #1
        sta owners+FAULT,x
        jmp io_error
finish_write:
        lda owners+FAULT,x
        jne io_error
        lda owners+COMMITTED,x
        jne success
        lda owners+LEN,x
        beq @commit
        jsr flush
        cmp #1
        bne @return
@commit:jsr flush                 ; LEN=0: exactly one terminal commit
        cmp #1
        bne @return
        inc owners+COMMITTED,x
@return:rts
do_status:
        lda owners+MODE,x
        cmp #4
        beq @info
        jsr finish_write
        cmp #1
        bne @return
@info:  lda #5
        jsr session_packet
        jcs io_error
        jsr exchange
        jcs io_error
        ldx slot
        lda owners+MODE,x
        cmp #8
        jeq success
        lda owners+EOF,x
        jeq success
        lda owners+POS,x
        cmp owners+LEN,x
        jcs eof
        jmp success
@return:rts
do_close:
        lda #1
        sta close_result
        lda owners+MODE,x
        cmp #4
        beq @close
        jsr finish_write
        sta close_result
@close: lda #4
        jsr session_packet
        bcs @failed
        jsr exchange
        bcc @release
@failed:lda #$90
        sta close_result
@release:
        ldx slot
        lda #$ff
        sta owners+OWN,x
        lda close_result
        rts

; Validate bounded version/handle/count of a READ/WRITE response. The offset
; echo is not trusted for progression: advance only by validated count.
check_rw:
        ldx slot
        lda payload_len
        cmp #12
        bcc @bad
        ldy payload
        lda packet+4,y
        cmp owners+HANDLE,x
        bne @bad
        lda packet+5,y
        cmp owners+HANDLE+1,x
        bne @bad
        lda packet+11,y
        bne @bad
        lda packet+10,y
        cmp #129
        bcs @bad
        sta count
        clc
        rts
@bad:   sec
        rts

; A single, bounded wire transaction. SIO failure preserves outstanding so
; the next operation drains it (or waits for explicit idle) before another W.
exchange:
        lda #0
        sta wire_hi
        lda request_len
        sta packet+2
        sta wire_len
        jsr checksum
        sta packet+4
        lda #1
        sta outstanding         ; even a lost W completion is uncertain
        lda #'W'
        jsr sio
        bne wire_bad
        jsr receive
        bcs wire_bad
        lda wire_hi
        bne wire_bad
        lda wire_len
        cmp #6
        bcc wire_bad
        cmp #PACKET_MAX+1
        bcs wire_bad
        lda packet
        cmp #$fd
        bne wire_bad
        lda packet+1
        cmp command
        bne wire_bad
        lda packet+2
        cmp wire_len
        bne wire_bad
        lda packet+3
        bne wire_bad
        jsr checksum
        cmp packet+4
        bne wire_bad
        ldy #6
        lda packet+5
        beq @payload
        cmp #1                  ; production status descriptor: one byte
        bne wire_bad
        lda packet,y
        bne wire_bad
        iny
@payload:
        sty payload
        lda wire_len
        sec
        sbc payload
        bcc wire_bad
        sta payload_len
        beq @ok                 ; bare successful INFO/CLOSE fixture replies
        cmp #4
        bcc wire_bad
        lda packet,y
        cmp #1
        bne wire_bad
        lda packet+2,y
        ora packet+3,y          ; NetworkService status (u16)
        bne wire_bad
@ok:    clc
        rts
wire_bad:
        sec
        rts
checksum:
        lda #0
        ldx #0
@sum:   cpx #4
        beq @skip
        clc
        adc packet,x
        adc #0
@skip:  inx
        cpx wire_len
        bne @sum
        rts
receive:
        lda #60
        sta polls
@poll:  lda #3
        sta wire_len
        lda #0
        sta wire_hi
        lda #'P'
        jsr sio
        bne @again
        lda packet
        beq @idle
        cmp #2
        beq @ready
@again: dec polls
        bne @poll
        sec
        rts
@idle:  lda #0
        sta outstanding
        ; Recovery may proceed after explicit idle; current exchange fails.
        sec
        rts
@ready: lda packet+2
        sta wire_hi
        cmp #2
        bcc @length
        bne wire_bad
        lda packet+1
        bne wire_bad
@length:lda wire_hi
        bne @read
        lda packet+1
        beq wire_bad           ; zero length cannot be retired with R
@read:  lda packet+1
        sta wire_len
        lda #'R'
        jsr sio
        bne wire_bad
        lda #0
        sta outstanding
        clc
        rts
sio:
        sta $0302
        lda #$7f
        sta $0300
        lda #1
        sta $0301
        lda #<packet
        sta $0304
        lda #>packet
        sta $0305
        lda #60
        sta $0306
        lda #0
        sta $0307
        lda wire_hi
        sta $0309
        sta $030b
        lda wire_len
        sta $0308
        sta $030a
        lda $0302
        cmp #'P'
        bne :+
        lda #0
        sta $030a
        sta $030b
:       lda #$40
        ldx $0302
        cpx #'W'
        bne :+
        asl
:       sta $0303
        jsr $e459
        lda $0303
        cmp #1
        rts
special:ldy #$92
        rts
init:   rts

        .segment "DATA"
_fnresident_module_handler_table:
        .word open-1,close-1,get-1,put-1,status-1,special-1
        .byte $4c
        .word init
        .byte 0
_fnresident_module_marker: .byte "FNA1"
        .segment "BSS"
owners: .res 32
caches: .res 256
packet: .res 512
busy:   .res 1
caller_a:.res 1
caller_x:.res 1
operation:.res 1
result: .res 1
saved_zp:.res 2
slot:   .res 1
open_mode:.res 1
handle_tmp:.res 2
command:.res 1
request_len:.res 1
wire_len:.res 1
wire_hi:.res 1
outstanding:.res 1
polls:  .res 1
payload:.res 1
payload_len:.res 1
count:  .res 1
cache_pos:.res 1
temp:   .res 1
close_result:.res 1
