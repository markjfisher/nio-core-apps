; player.s -- three-voice SID player for the FujiNet demo.
;
; Plays the tables gen_music.py writes (music.inc): per-voice order lists of
; 16-row patterns, instruments with wavetable programs (arpeggios, drums),
; pulse-width sweeps, delayed vibrato, a two-frame hard restart before each
; new row, and a resonant low-pass sweep on voice 1.
;
; Call music_init once (after is_ntsc is set), then music_play once per frame.
; On NTSC every sixth call is skipped, so the tempo matches PAL.

SID = $d400

.segment "ZEROPAGE"
mptr:   .res 2          ; pattern read pointer
optr:   .res 2          ; order list pointer
mtmp:   .res 1
flo:    .res 1          ; frequency being built
fhi:    .res 1
vlo:    .res 1
vhi:    .res 1
vtmp:   .res 1

.segment "BSS"
tick:           .res 1
mframe:         .res 1
ntsc_skip:      .res 1
fphase:         .res 1
v_ordpos:       .res 3
v_patlo:        .res 3
v_pathi:        .res 3
v_dur:          .res 3
v_durset:       .res 3
v_note:         .res 3
v_cur:          .res 3
v_ins:          .res 3
v_wtpos:        .res 3
v_pwlo:         .res 3
v_pwhi:         .res 3
v_pwdir:        .res 3
v_gate:         .res 3
v_ctrl:         .res 3
v_vib:          .res 3
voice_energy:   .res 3      ; 15 on a new note, then -1 per frame (VU bars)
freq_lo:        .res 96
freq_hi:        .res 96

.segment "RODATA"
sidoff:         .byte 0, 7, 14
vib_shape:      .byte 0, 1, 2, 1, 0, 1, 2, 1
order_lo:       .byte <order1, <order2, <order3
order_hi:       .byte >order1, >order2, >order3

        .include "music.inc"

.segment "CODE"

.proc music_init
        ldx #95
@freq:  lda is_ntsc
        bne @ntsc
        lda freq_pal_lo,x
        sta freq_lo,x
        lda freq_pal_hi,x
        sta freq_hi,x
        jmp @next
@ntsc:  lda freq_ntsc_lo,x
        sta freq_lo,x
        lda freq_ntsc_hi,x
        sta freq_hi,x
@next:  dex
        bpl @freq

        lda #0
        ldx #$18
@sid:   sta SID,x
        dex
        bpl @sid
        lda #FILTER_RES
        sta SID+$17
        lda #$1f                ; low-pass, volume 15
        sta SID+$18

        ldx #2
@voice: lda #0
        sta v_ordpos,x
        sta voice_energy,x
        sta v_note,x
        sta v_ins,x
        sta v_wtpos,x           ; entry 0 with the gate masked off: silent
        sta v_pwlo,x
        sta v_pwhi,x
        sta v_pwdir,x
        sta v_vib,x
        sta v_ctrl,x
        lda #$fe
        sta v_gate,x
        lda #1
        sta v_dur,x
        sta v_durset,x
        jsr next_pattern
        lda mptr
        sta v_patlo,x
        lda mptr+1
        sta v_pathi,x
        dex
        bpl @voice

        lda #0
        sta tick
        sta mframe
        sta ntsc_skip
        sta fphase
        rts
.endproc

.proc music_play
        lda is_ntsc
        beq @go
        inc ntsc_skip
        lda ntsc_skip
        cmp #6
        bcc @go
        lda #0
        sta ntsc_skip
        rts

@go:    inc mframe
        lda tick
        bne @noseq
        ldx #2                  ; new row: voices whose note ended read on
@seq:   dec v_dur,x
        bne @seqnx
        jsr read_events
@seqnx: dex
        bpl @seq

@noseq: lda tick                ; hard restart two frames before the next row
        cmp #SPEED-2
        bne @nohr
        ldx #2
@hr:    lda v_dur,x
        cmp #1
        bne @hrnx
        lda #$fe
        sta v_gate,x
        ldy sidoff,x
        lda #0
        sta SID+5,y
        sta SID+6,y
@hrnx:  dex
        bpl @hr

@nohr:  ldx #2
@frame: jsr voice_frame
        dex
        bpl @frame

        inc fphase
        ldy fphase
        lda filter_sweep,y
        sta SID+$16

        inc tick
        lda tick
        cmp #SPEED
        bcc @done
        lda #0
        sta tick
@done:  rts
.endproc

; Read pattern bytes for voice X until a note or rest.
.proc read_events
        lda v_patlo,x
        sta mptr
        lda v_pathi,x
        sta mptr+1
        ldy #0
@next:  lda (mptr),y
        iny
        cmp #$60
        bcc @note
        cmp #$70
        bcc @ins
        cmp #$c0
        bcc @dur
        cmp #$ff
        beq @end
        lda #$fe                ; $c0: rest
        sta v_gate,x
        jmp @done
@ins:   and #$0f
        sta v_ins,x
        jmp @next
@dur:   and #$3f
        clc
        adc #1
        sta v_durset,x
        jmp @next
@end:   jsr next_pattern
        jmp @next
@note:  sta v_note,x
        sty mtmp
        jsr trigger
        ldy mtmp
@done:  lda v_durset,x
        sta v_dur,x
        tya
        clc
        adc mptr
        sta v_patlo,x
        lda mptr+1
        adc #0
        sta v_pathi,x
        rts
.endproc

; Point mptr at voice X's next pattern (following the order list's loop).
; Returns Y = 0.
.proc next_pattern
        lda order_lo,x
        sta optr
        lda order_hi,x
        sta optr+1
        ldy v_ordpos,x
        lda (optr),y
        cmp #$ff
        bne @ok
        iny
        lda (optr),y            ; loop position
        tay
        lda (optr),y
@ok:    pha
        iny
        tya
        sta v_ordpos,x
        pla
        tay
        lda pat_lo,y
        sta mptr
        lda pat_hi,y
        sta mptr+1
        ldy #0
        rts
.endproc

; Start v_note on voice X with its instrument.
.proc trigger
        ldy v_ins,x
        lda ins_wt,y
        sta v_wtpos,x
        lda ins_pw,y
        sta v_pwhi,x
        lda #0
        sta v_pwlo,x
        sta v_pwdir,x
        sta v_vib,x
        lda #$ff
        sta v_gate,x
        lda #15
        sta voice_energy,x
        lda ins_ad,y
        sta flo
        lda ins_sr,y
        sta fhi
        lda ins_flags,y
        sta vlo
        ldy sidoff,x
        lda flo
        sta SID+5,y
        lda fhi
        sta SID+6,y
        cpx #0                  ; voice 1: route through the filter or not
        bne @done
        lda vlo
        and #2
        beq @dry
        lda #FILTER_RES | 1
        .byte $2c               ; bit abs: skips the next lda
@dry:   lda #FILTER_RES
        sta SID+$17
@done:  rts
.endproc

; One frame of voice X: wavetable step, vibrato, pulse sweep, SID registers.
.proc voice_frame
        ldy v_wtpos,x
        lda wt_wave,y
        cmp #$ff
        bne @wave
        lda wt_note,y           ; jump
        tay
        lda wt_wave,y
@wave:  and v_gate,x
        sta v_ctrl,x
        lda wt_note,y
        bmi @abs
        clc
        adc v_note,x
        jmp @note
@abs:   and #$7f
@note:  cmp #96
        bcc @inrange
        lda #95
@inrange:
        sta v_cur,x
        iny
        tya
        sta v_wtpos,x

        ldy v_cur,x
        lda freq_lo,y
        sta flo
        lda freq_hi,y
        sta fhi

        ldy v_ins,x             ; vibrato after VIB_DELAY frames
        lda ins_flags,y
        and #1
        beq @novib
        lda v_vib,x
        cmp #VIB_DELAY
        bcs @vib
        inc v_vib,x
        bcc @novib
@vib:   lda mframe
        and #7
        tay
        lda vib_shape,y
        beq @novib
        sta vtmp
        ldy v_cur,x
        lda vib_lo,y
        sta vlo
        lda vib_hi,y
        sta vhi
        lda vtmp
        cmp #2
        bne @one
        asl vlo
        rol vhi
@one:   lda mframe
        and #4
        bne @down
        clc
        lda flo
        adc vlo
        sta flo
        lda fhi
        adc vhi
        sta fhi
        jmp @novib
@down:  sec
        lda flo
        sbc vlo
        sta flo
        lda fhi
        sbc vhi
        sta fhi

@novib: ldy v_ins,x             ; pulse width sweep between $200 and $e00
        lda ins_pw_speed,y
        beq @nopwm
        sta vtmp
        lda v_pwdir,x
        bne @pwdown
        clc
        lda v_pwlo,x
        adc vtmp
        sta v_pwlo,x
        lda v_pwhi,x
        adc #0
        sta v_pwhi,x
        cmp #$0e
        bcc @nopwm
        lda #1
        sta v_pwdir,x
        bne @nopwm
@pwdown:
        sec
        lda v_pwlo,x
        sbc vtmp
        sta v_pwlo,x
        lda v_pwhi,x
        sbc #0
        sta v_pwhi,x
        cmp #$02
        bcs @nopwm
        lda #0
        sta v_pwdir,x

@nopwm: ldy sidoff,x
        lda flo
        sta SID+0,y
        lda fhi
        sta SID+1,y
        lda v_pwlo,x
        sta SID+2,y
        lda v_pwhi,x
        sta SID+3,y
        lda v_ctrl,x
        sta SID+4,y
        lda voice_energy,x
        beq @quiet
        dec voice_energy,x
@quiet: rts
.endproc
