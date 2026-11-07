; fujidemo.s -- FujiNet demo: FujiNet logo, sprites, starfield, border
; bars that follow the music, a smooth scroller, and the date and time read
; live from FujiNet (IEC device 30). Runs in a loop; works without FujiNet too.
;
; Build: make TARGET=c64 boot-disk (ca65/ld65 from cc65, python3 for the
; generated data).
;
; Memory: program $0801-$33ff, sprites $3400-$37bf, character set $3800
; (ROM font copied at start + logo tiles), screen $0400, variables $c000.
; Raster interrupts go through the KERNAL's $0314 vector, so the KERNAL stays
; banked in for the serial-bus calls that talk to FujiNet.

        .setcpu "6502"
        .import __BSS_RUN__

; ---------------------------------------------------------------- hardware
VIC      = $d000
SCREEN   = $0400
COLRAM   = $d800
CHARSET  = $3800
SPRPTR   = SCREEN + $03f8
SPRITE_BASE = $3400 / 64

ST       = $90          ; KERNAL I/O status
MSGFLG   = $9d          ; KERNAL message switch
SECOND   = $ff93
TKSA     = $ff96
ACPTR    = $ffa5
CIOUT    = $ffa8
UNTLK    = $ffab
UNLSN    = $ffae
LISTEN   = $ffb1
TALK     = $ffb4
IRQ_EXIT = $ea81        ; restore registers, rti

; ------------------------------------------------------------------ layout
LOGO_ROW    = 1         ; 5 rows
SUB_ROW     = 7
STAR_ROW    = 8         ; 10 rows of stars
STAR_ROWS   = 10
STAR_COUNT  = 30
STAR_CHAR   = 224       ; 224..253
LINE_CHAR   = 254
INFO_ROW    = 19
TIME_ROW    = 20
LINE_ROW1   = 21
SCROLL_ROW  = 22
LINE_ROW2   = 23
FOOT_ROW    = 24

IRQ_TOP_LINE    = 2             ; sprites (~7 lines), then the top border bars
TOP_BARS_FIRST  = 16
TOP_BARS_N      = 34            ; lines 16..49
IRQ_SPLIT_LINE  = 51 + SCROLL_ROW*8 - 4   ; fine scroll on for the scroller
IRQ_BOTTOM_LINE = 51 + SCROLL_ROW*8 + 7   ; ... and off again, then music
VU_FIRST        = 260           ; PAL only: music bars in the lower border
VU_N            = 26

SCENE_BLOCKS    = 5             ; x 128 frames per sprite formation
SCROLL_SPEED    = 2

CLK_DEV = 30                    ; the FujiNet device on the IEC bus
NIO_CHANNEL = $60               ; SECOND and TKSA: data on channel 0
CLOCK_DEVICE = $45              ; FujiBus: the clock
REPLY_DATA = 7                  ; after the 6-byte header and the status
FN_REPLY_MAX = 24

; Text is written in ASCII and stored as screen codes
.repeat 26, I
        .charmap $41 + I, 1 + I       ; not 'A' + I: that is mapped too
.endrepeat

; Length-prefixed text for print_centered
.macro TEXT str
        .byte .strlen(str), str
.endmacro

; ------------------------------------------------------------- zero page
.segment "ZEROPAGE"
sptr:     .res 2
dptr:     .res 2
cptr:     .res 2
gptr:     .res 2
text_ptr: .res 2
tmp:      .res 1
tmp2:     .res 1

; -------------------------------------------------------------- variables
.segment "BSS"
is_ntsc:      .res 1
frame:        .res 2
frame_flag:   .res 1
scroll_fine:  .res 1
scroll_d016:  .res 1
spr_ready:    .res 1
spr_xlo:      .res 8
spr_xhi:      .res 8
spr_y:        .res 8
spr_col:      .res 8
spr_ptr:      .res 8
spr_msb:      .res 1
scene:        .res 1
scene_blocks: .res 1
sp_t:         .res 1
wave_ph:      .res 1
lis_phi:      .res 1
bph:          .res 3
bar_tab:      .res TOP_BARS_N + 1
vu_tab:       .res VU_N + 1
star_xlo:     .res STAR_COUNT
star_xhi:     .res STAR_COUNT
star_old_lo:  .res STAR_COUNT
star_old_hi:  .res STAR_COUNT
clk_buf:      .res 8
clk_state:    .res 1        ; 0 searching, 1 online, 2 not found
clk_hh:       .res 1
clk_mm:       .res 1
clk_ss:       .res 1
fps_cnt:      .res 1
resync_secs:  .res 1
time_line:    .res 40       ; length + up to 35 characters
req_at:       .res 1
req_end:      .res 1
req_cmd:      .res 1
fn_reply:     .res FN_REPLY_MAX + 1
fn_len:       .res 1
tz_text:      .res 9
tz_len:       .res 1
print_color:  .res 1

; ------------------------------------------------------------ BASIC stub
.segment "LOADADDR"
        .word $0801
.segment "STARTUP"
        .word @last, 2026
        .byte $9e, "2061", 0    ; 2026 SYS2061
@last:  .word 0

; ------------------------------------------------------------------- code
.segment "CODE"

start:
        sei
        cld
        lda #$7f                ; no CIA interrupts: only our raster IRQs
        sta $dc0d
        sta $dd0d
        lda $dc0d
        lda $dd0d
        lda #0
        sta MSGFLG              ; KERNAL must not print I/O errors
        jsr clear_bss
        jsr detect_video
        jsr build_charset
        jsr setup_screen
        jsr init_stars
        jsr init_sprites
        jsr music_init

        lda #$1e                ; screen $0400, characters $3800
        sta VIC+$18
        lda #0
        sta VIC+$20
        sta VIC+$21
        lda #$08
        sta VIC+$16
        lda #$1b
        sta VIC+$11
        lda #<irq_top
        sta $0314
        lda #>irq_top
        sta $0315
        lda #IRQ_TOP_LINE
        sta VIC+$12
        lda #1
        sta VIC+$1a
        lda #$ff
        sta VIC+$19
        cli

; ------------------------------------------------------------- main loop
main_loop:
        lda frame_flag
        beq main_loop
        lda #0
        sta frame_flag
        jsr update_sprites      ; first: copied to the VIC at line 2
        jsr update_bars
        jsr update_scroller
        jsr update_colors
        jsr update_stars
        jsr clock_frame
        lda resync_secs
        bne main_loop
        jsr clock_sync          ; KERNAL serial I/O, so never from an IRQ
        jmp main_loop

; ---------------------------------------------------------- interrupts
irq_top:
        lda #$ff
        sta VIC+$19
        lda spr_ready           ; sprite registers, unless the main loop is
        beq @bars               ; halfway through computing them
        ldx #7
@spr:   txa
        asl
        tay
        lda spr_xlo,x
        sta VIC+0,y
        lda spr_y,x
        sta VIC+1,y
        lda spr_col,x
        sta VIC+$27,x
        lda spr_ptr,x
        sta SPRPTR,x
        dex
        bpl @spr
        lda spr_msb
        sta VIC+$10
        lda #0
        sta spr_ready
@bars:  lda #TOP_BARS_FIRST - 2 ; wait for line TOP_BARS_FIRST - 1 (or later:
@w0:    cmp VIC+$12             ; never wait for the next frame), then one colour
        bcs @w0                 ; per line, set just after it starts (h-blank)
        ldy #0
        ldx bar_tab
@wait:  lda VIC+$12
@w1:    cmp VIC+$12
        beq @w1
        stx VIC+$20
        ldx bar_tab+1,y
        iny
        cpy #TOP_BARS_N + 1
        bne @wait
        lda #<irq_split
        sta $0314
        lda #>irq_split
        sta $0315
        lda #IRQ_SPLIT_LINE
        sta VIC+$12
        jmp IRQ_EXIT

irq_split:
        lda #$ff
        sta VIC+$19
        lda scroll_d016         ; 38 columns + fine scroll for the scroller row
        sta VIC+$16
        lda #<irq_bottom
        sta $0314
        lda #>irq_bottom
        sta $0315
        lda #IRQ_BOTTOM_LINE
        sta VIC+$12
        jmp IRQ_EXIT

irq_bottom:
        lda #$ff
        sta VIC+$19
        lda #$08
        sta VIC+$16
        jsr music_play
        inc frame
        bne :+
        inc frame+1
:       lda #1
        sta frame_flag
        lda is_ntsc             ; NTSC has no lines below 262
        bne @next
        bit VIC+$11             ; too late this frame? skip the bars
        bpl @vu
        lda VIC+$12
        cmp #<(VU_FIRST - 1)
        bcs @next
@vu:    lda #<(VU_FIRST - 1)
@w0:    cmp VIC+$12
        bne @w0
        ldy #0
        ldx vu_tab
@wait:  lda VIC+$12
@w1:    cmp VIC+$12
        beq @w1
        stx VIC+$20
        ldx vu_tab+1,y
        iny
        cpy #VU_N + 1
        bne @wait
@next:  lda #<irq_top
        sta $0314
        lda #>irq_top
        sta $0315
        lda #IRQ_TOP_LINE
        sta VIC+$12
        jmp IRQ_EXIT

; --------------------------------------------------------------- setup
.proc clear_bss
        lda #0
        tax
@l:     sta __BSS_RUN__,x
        sta __BSS_RUN__+$100,x
        sta __BSS_RUN__+$200,x
        sta __BSS_RUN__+$300,x
        inx
        bne @l
        rts
.endproc

; PAL has 312 lines (last $137), NTSC 263 or 262 (last $106/$105).
.proc detect_video
@hi:    bit VIC+$11
        bpl @hi
        lda #0
        sta tmp
@l:     lda VIC+$12
        cmp tmp
        bcc @s
        sta tmp
@s:     bit VIC+$11
        bmi @l
        ldx #0
        lda tmp
        cmp #$20
        bcs @pal
        inx
@pal:   stx is_ntsc
        rts
.endproc

; ROM font (upper case + graphics) -> $3800, then the logo tiles at 64..,
; empty star characters at 224.. and the separator line at 254.
.proc build_charset
        lda #$33                ; character ROM at $d000 (I/O out; IRQs are off)
        sta $01
        ldx #0
@rom:   lda $d000,x
        sta CHARSET,x
        lda $d100,x
        sta CHARSET+$100,x
        lda $d200,x
        sta CHARSET+$200,x
        lda $d300,x
        sta CHARSET+$300,x
        lda $d400,x
        sta CHARSET+$400,x
        lda $d500,x
        sta CHARSET+$500,x
        lda $d600,x
        sta CHARSET+$600,x
        lda $d700,x
        sta CHARSET+$700,x
        inx
        bne @rom
        lda #$37
        sta $01

        lda #0
@clr:   sta CHARSET + STAR_CHAR*8,x
        inx
        bne @clr
        lda #$ff
        sta CHARSET + LINE_CHAR*8
        sta CHARSET + LINE_CHAR*8 + 1

        lda #<logo_chars        ; copy LOGO_NUM_CHARS*8 bytes
        sta sptr
        lda #>logo_chars
        sta sptr+1
        lda #<(CHARSET + LOGO_FIRST_CHAR*8)
        sta dptr
        lda #>(CHARSET + LOGO_FIRST_CHAR*8)
        sta dptr+1
        ldx #>(LOGO_NUM_CHARS*8)
        ldy #0
@page:  cpx #0
        beq @rest
@cp:    lda (sptr),y
        sta (dptr),y
        iny
        bne @cp
        inc sptr+1
        inc dptr+1
        dex
        jmp @page
@rest:  cpy #<(LOGO_NUM_CHARS*8)
        beq @done
        lda (sptr),y
        sta (dptr),y
        iny
        jmp @rest
@done:  rts
.endproc

.proc setup_screen
        ldx #0
@clr:   lda #32
        sta SCREEN,x
        sta SCREEN+$100,x
        sta SCREEN+$200,x
        sta SCREEN+$2e8,x
        lda #0
        sta COLRAM,x
        sta COLRAM+$100,x
        sta COLRAM+$200,x
        sta COLRAM+$2e8,x
        inx
        bne @clr

        ldx #199                ; logo
@logo:  lda logo_map,x
        sta SCREEN + LOGO_ROW*40,x
        dex
        cpx #$ff
        bne @logo

        ldx #39                 ; separator lines and scroller colours
@lines: lda #LINE_CHAR
        sta SCREEN + LINE_ROW1*40,x
        sta SCREEN + LINE_ROW2*40,x
        lda scroll_colors,x
        sta COLRAM + SCROLL_ROW*40,x
        dex
        bpl @lines

        lda #11
        sta print_color
        lda #FOOT_ROW
        ldx #<txt_foot
        ldy #>txt_foot
        jsr print_centered
        lda #15
        sta print_color
        lda #SUB_ROW
        ldx #<txt_sub
        ldy #>txt_sub
        jsr print_centered
        lda #12
        sta print_color
        lda #INFO_ROW
        ldx #<txt_search
        ldy #>txt_search
        jsr print_centered

        lda #<scroll_text
        sta text_ptr
        lda #>scroll_text
        sta text_ptr+1
        lda #7
        sta scroll_fine
        sta scroll_d016
        rts
.endproc

; A = row, X/Y = length-prefixed text; colour in print_color.
; Clears the row first.
.proc print_centered
        stx sptr
        sty sptr+1
        tax
        lda row_lo,x
        sta dptr
        sta cptr
        lda row_hi,x
        sta dptr+1
        clc
        adc #>(COLRAM - SCREEN)
        sta cptr+1
        ldy #39
        lda #32
@clr:   sta (dptr),y
        dey
        bpl @clr
        ldy #0
        lda (sptr),y            ; length
        sta tmp
        lda #40
        sec
        sbc tmp
        lsr
        clc
        adc dptr                ; start column
        sta dptr
        sta cptr
        bcc :+
        inc dptr+1
        inc cptr+1
:       inc sptr
        bne :+
        inc sptr+1
:       ldy tmp
        beq @done
@l:     dey
        lda (sptr),y
        sta (dptr),y
        lda print_color
        sta (cptr),y
        tya
        bne @l
@done:  rts
.endproc

.proc init_stars
        ldx #STAR_COUNT - 1
@l:     lda star_x0_lo,x
        sta star_xlo,x
        lda star_x0_hi,x
        sta star_xhi,x
        lda #<SCREEN            ; harmless first "previous cell"
        sta star_old_lo,x
        lda #>SCREEN
        sta star_old_hi,x
        dex
        bpl @l
        rts
.endproc

.proc init_sprites
        lda #$ff
        sta VIC+$15             ; all eight on
        lda #0
        sta VIC+$1c             ; hi-res
        sta VIC+$17
        sta VIC+$1d
        sta VIC+$1b             ; in front of the text
        jsr update_sprites
        rts
.endproc

; ------------------------------------------------------------- effects
.proc update_sprites
        lda #0
        sta spr_ready
        inc sp_t
        lda frame
        and #$7f
        bne @same
        inc scene_blocks
        lda scene_blocks
        cmp #SCENE_BLOCKS
        bcc @same
        lda #0
        sta scene_blocks
        lda scene
        eor #1
        sta scene
@same:  lda scene
        bne icons

        ; FUJINET + wifi as a swaying sine wave
        ldy sp_t
        lda sway_x,y
        sta tmp
        lda wave_ph
        clc
        adc #3
        sta wave_ph
        lda frame
        lsr
        lsr
        sta tmp2
        ldx #0
@l:     lda tmp
        clc
        adc letter_dx,x
        sta spr_xlo,x
        lda #0
        adc #0
        sta spr_xhi,x
        lda wave_ph
        clc
        adc letter_dph,x
        tay
        lda wave_y,y
        sta spr_y,x
        txa
        clc
        adc tmp2
        and #15
        tay
        lda sprite_rainbow,y
        sta spr_col,x
        txa
        clc
        adc #SPRITE_BASE
        sta spr_ptr,x
        inx
        cpx #8
        bne @l
        jmp msb

        ; eight icons on a 1:2 Lissajous curve whose phase drifts
icons:  ldx #0
@l:     lda sp_t
        clc
        adc icon_dx,x
        tay
        lda orbit_x_lo,y
        sta spr_xlo,x
        lda orbit_x_hi,y
        sta spr_xhi,x
        lda sp_t
        asl
        clc
        adc icon_dy,x
        clc
        adc lis_phi
        tay
        lda orbit_y,y
        sta spr_y,x
        lda icon_colors,x
        sta spr_col,x
        txa
        clc
        adc #SPRITE_BASE + 7
        sta spr_ptr,x
        inx
        cpx #8
        bne @l
        lda frame
        and #3
        bne msb
        inc lis_phi

msb:    ldx #7
@m:     lda spr_xhi,x
        lsr
        rol spr_msb
        dex
        bpl @m
        lda #1
        sta spr_ready
        rts
.endproc

; Top border: three gradient bars on sine paths. Lower border (PAL): one
; band per SID voice, lit by its note energy.
.proc update_bars
        lda #0
        ldx #TOP_BARS_N
@clr:   sta bar_tab,x
        dex
        bpl @clr
        ldx #2
@bar:   lda bph,x
        clc
        adc bar_speed,x
        sta bph,x
        tay
        lda bar_y,y
        sta tmp                 ; line in bar_tab
        lda bar_first,x
        sta tmp2                ; colour in bar_colors
        lda #9
        sta gptr
@line:  ldy tmp2
        lda bar_colors,y
        ldy tmp
        sta bar_tab,y
        inc tmp
        inc tmp2
        dec gptr
        bne @line
        dex
        bpl @bar

        ldy #0                  ; VU bands: 8 lines per voice, 1-line gaps
        ldx #0
@voice: stx tmp
        lda voice_energy,x
        lsr
        sta tmp2                ; 0..7
        txa
        asl
        asl
        asl
        sta gptr                ; ramp offset
        ldx #0
@vline: lda tmp2
        sec
        sbc vu_dist,x
        bcs :+
        lda #0
:       clc
        adc gptr
        stx gptr+1
        tax
        lda vu_ramps,x
        sta vu_tab,y
        ldx gptr+1
        iny
        inx
        cpx #8
        bne @vline
        lda #0
        sta vu_tab,y            ; gap (the last one is the closing black)
        iny
        ldx tmp
        inx
        cpx #3
        bne @voice
        rts
.endproc

.proc update_scroller
        lda scroll_fine
        sec
        sbc #SCROLL_SPEED
        bcs @fine
        and #7
        sta scroll_fine
        ldx #0
@shift: lda SCREEN + SCROLL_ROW*40 + 1,x
        sta SCREEN + SCROLL_ROW*40,x
        inx
        cpx #39
        bne @shift
        ldy #0
        lda (text_ptr),y
        bne @char
        lda #<scroll_text
        sta text_ptr
        lda #>scroll_text
        sta text_ptr+1
        lda (text_ptr),y
@char:  sta SCREEN + SCROLL_ROW*40 + 39
        inc text_ptr
        bne @set
        inc text_ptr+1
        jmp @set
@fine:  sta scroll_fine
@set:   lda scroll_fine         ; 38-column mode hides the edge columns
        sta scroll_d016
        rts
.endproc

; Rainbow waves through the logo (diagonal) and the separator lines, and the
; subtitle flashing with the drums.
.proc update_colors
        lda frame
        lsr
        eor #$ff
        and #31
        sta tmp                 ; logo wave phase, moving right
        .repeat 5, R
        lda tmp
        clc
        adc #R*2
        and #31
        clc
        adc #<rainbow_ext
        sta .ident(.sprintf("logo%d", R))+1
        lda #>rainbow_ext
        adc #0
        sta .ident(.sprintf("logo%d", R))+2
        .endrepeat
        lda frame               ; separator lines move the other way
        and #31
        clc
        adc #<rainbow_ext
        sta line1+1
        lda #>rainbow_ext
        adc #0
        sta line1+2
        ldx #39
cloop:
        .repeat 5, R
.ident(.sprintf("logo%d", R)):
        lda rainbow_ext,x
        sta COLRAM + (LOGO_ROW+R)*40,x
        .endrepeat
line1:  lda rainbow_ext,x
        sta COLRAM + LINE_ROW1*40,x
        sta COLRAM + LINE_ROW2*40,x
        dex
        bpl cloop

        lda voice_energy        ; voice 1 carries bass and drums
        lsr
        tay
        lda pulse_ramp,y
        ldx #39
@sub:   sta COLRAM + SUB_ROW*40,x
        dex
        bpl @sub
        rts
.endproc

; 30 stars in three speeds; each is its own character with one lit pixel.
.proc update_stars
        ldx #STAR_COUNT - 1
@l:     txa
        clc
        adc #STAR_CHAR
        sta tmp                 ; this star's character
        sec                     ; move left, wrap from 0 to 319
        lda star_xlo,x
        sbc star_speed,x
        sta star_xlo,x
        lda star_xhi,x
        sbc #0
        sta star_xhi,x
        bcs @inside
        lda star_xlo,x
        clc
        adc #<320
        sta star_xlo,x
        lda star_xhi,x
        adc #>320
        sta star_xhi,x
@inside:
        lda star_xhi,x          ; new cell = row + x / 8
        lsr
        lda star_xlo,x
        ror
        lsr
        lsr
        clc
        adc star_row_lo,x
        sta dptr
        lda star_row_hi,x
        adc #0
        sta dptr+1
        ldy #0
        lda dptr
        cmp star_old_lo,x
        bne @cell
        lda dptr+1
        cmp star_old_hi,x
        beq @glyph              ; same cell: only the pixel moves
@cell:  lda star_old_lo,x       ; take the star off its old cell
        sta sptr
        lda star_old_hi,x
        sta sptr+1
        lda (sptr),y
        cmp tmp
        bne @new
        lda #32
        sta (sptr),y
@new:   lda dptr
        sta star_old_lo,x
        lda dptr+1
        sta star_old_hi,x
        lda (dptr),y            ; only on empty cells
        cmp #32
        bne @glyph
        lda tmp
        sta (dptr),y
        lda dptr+1
        clc
        adc #>(COLRAM - SCREEN)
        sta dptr+1
        lda star_color,x
        sta (dptr),y
@glyph: txa                     ; its character: one pixel on its own line
        asl
        asl
        asl
        ora star_line,x
        sta gptr
        lda #>(CHARSET + STAR_CHAR*8)
        sta gptr+1
        lda star_xlo,x
        and #7
        tay
        lda bit_mask,y
        ldy #0
        sta (gptr),y
        dex
        bmi @done
        jmp @l
@done:  rts
.endproc

; ---------------------------------------------------------- FujiNet clock
; NIO's packet channel, docs/iec/nio-channel.md in fujinet-nio: LISTEN 30,
; SECOND $60, a FujiBus packet, and the UNLISTEN runs it; then TALK 30, TKSA $60
; and the response. A response stays until the next request, so stopping at
; FN_REPLY_MAX is fine.
;   time:      command 3, version 1, format 0 (simple binary) -> status, version,
;              format, century, year, month, day, hour, minute, second
;              (in FujiNet's own time zone)
;   time zone: command 4 -> status, version, length, POSIX time zone string
; A missing device shows in ST bit 7 once the packet has gone, and nothing talks
; after that: a talk to an absent device would wait for ever if a disk drive is
; on the bus.

; Device, command, length (2), checksum, descriptor (0: no parameters), payload.
; The checksum is the sum of the other bytes, with carries folded back in.
nio_requests:
req_time:
        .byte CLOCK_DEVICE, 3, 8, 0, $51, 0, 1, 0
req_tz:
        .byte CLOCK_DEVICE, 4, 6, 0, $4f, 0

; X = the request's offset in nio_requests. Reply in fn_reply / fn_len, checked
; as this request's and successful. Carry set on failure.
.proc nio_call
        stx req_at
        lda nio_requests+1,x
        sta req_cmd
        txa
        clc
        adc nio_requests+2,x
        sta req_end
        lda #0
        sta fn_len
        sta ST
        lda #CLK_DEV
        jsr LISTEN
        lda #NIO_CHANNEL
        jsr SECOND
        lda ST
        bmi @unlisten
@send:  ldx req_at
        lda nio_requests,x
        jsr CIOUT
        inc req_at
        lda req_at
        cmp req_end
        bne @send
@unlisten:
        jsr UNLSN
        lda ST
        bmi @fail
        lda #CLK_DEV
        jsr TALK
        lda #NIO_CHANNEL
        jsr TKSA
        lda ST
        bmi @untalk
@read:  jsr ACPTR
        ldx fn_len
        sta fn_reply,x
        lda ST
        and #$83                ; no byte, or no device
        bne @untalk
        inc fn_len
        lda ST
        and #$40                ; EOI comes with the last byte
        bne @got
        lda fn_len
        cmp #FN_REPLY_MAX
        bcc @read
@got:   jsr UNTLK
        lda fn_len
        cmp #REPLY_DATA
        bcc @fail
        lda fn_reply
        cmp #CLOCK_DEVICE
        bne @fail
        lda fn_reply+1
        cmp req_cmd
        bne @fail
        lda fn_reply+5          ; the descriptor: one byte parameter, the status
        cmp #1
        bne @fail
        lda fn_reply+6
        bne @fail
        clc
        rts
@untalk:
        jsr UNTLK
@fail:  sec
        rts
.endproc

.proc read_clock
        ldx #req_time - nio_requests
        jsr nio_call
        bcs @done
        lda fn_len
        cmp #REPLY_DATA + 2 + 7
        bcc @short
        lda fn_reply+REPLY_DATA+1 ; the format, echoed
        bne @short
        ldx #6
@copy:  lda fn_reply+REPLY_DATA+2,x
        sta clk_buf,x
        dex
        bpl @copy
        clc
@done:  rts
@short: sec
        rts
.endproc

; FujiNet's time zone up to the first comma ("EST5EDT,M3.2.0,..." -> "EST5EDT"),
; at most 9 characters, as screen codes in tz_text (tz_len 0 if unknown).
.proc read_tz
        lda #0
        sta tz_len
        ldx #req_tz - nio_requests
        jsr nio_call
        bcs @done
        lda fn_len
        sec
        sbc #REPLY_DATA + 2
        bcc @done
        sta tmp
        ldx #0
@l:     cpx tmp
        beq @done
        lda fn_reply+REPLY_DATA+2,x
        beq @done
        cmp #','
        beq @done
        jsr petscii_to_screen
        sta tz_text,x
        inx
        stx tz_len
        cpx #9
        bne @l
@done:  rts
.endproc

.proc clock_sync
        jsr read_clock
        bcs @offline
        ldx #6                  ; sanity: every field below its limit
@check: lda clk_buf,x
        cmp clk_limit,x
        bcs @offline
        dex
        bpl @check
        lda clk_buf+2           ; month and day start at 1
        beq @offline
        lda clk_buf+3
        beq @offline
        jsr read_tz
        lda clk_buf+4
        sta clk_hh
        lda clk_buf+5
        sta clk_mm
        lda clk_buf+6
        sta clk_ss
        lda #0
        sta fps_cnt
        lda #60
        sta resync_secs
        lda clk_state
        cmp #1
        beq @time
        lda #1
        sta clk_state
        lda #13
        sta print_color
        lda #INFO_ROW
        ldx #<txt_online
        ldy #>txt_online
        jsr print_centered
@time:  jmp draw_time

@offline:
        lda #10
        sta resync_secs
        lda clk_state
        cmp #2
        beq @done
        lda #2
        sta clk_state
        lda #10
        sta print_color
        lda #INFO_ROW
        ldx #<txt_offline
        ldy #>txt_offline
        jsr print_centered
        lda #11
        sta print_color
        lda #TIME_ROW
        ldx #<txt_hint
        ldy #>txt_hint
        jsr print_centered
@done:  rts
.endproc

; Once per frame: count seconds; the clock runs locally between syncs.
.proc clock_frame
        inc fps_cnt
        lda fps_cnt
        ldx is_ntsc
        cmp fps_for,x
        bcc @done
        lda #0
        sta fps_cnt
        lda resync_secs
        beq :+
        dec resync_secs
:       lda clk_state
        cmp #1
        bne @done
        inc clk_ss
        lda clk_ss
        cmp #60
        bcc @draw
        lda #0
        sta clk_ss
        inc clk_mm
        lda clk_mm
        cmp #60
        bcc @draw
        lda #0
        sta clk_mm
        inc clk_hh
        lda clk_hh
        cmp #24
        bcc @draw
        lda #0
        sta clk_hh
        sta resync_secs         ; new day: fetch the date again
@draw:  jmp draw_time
@done:  rts
.endproc

; "SUN 01 NOV 2026  14:05:33 UTC"
.proc draw_time
        jsr weekday
        ldy #0
        cmp #7
        bcs @noday
        sta tmp
        asl
        adc tmp                 ; x 3
        tax
@day:   lda day_names,x
        sta time_line+1,y
        inx
        iny
        cpy #3
        bne @day
        jmp @date
@noday: lda #32
@blank: sta time_line+1,y
        iny
        cpy #3
        bne @blank
@date:  lda #32
        sta time_line+4
        sta time_line+7
        sta time_line+11
        sta time_line+16
        sta time_line+17
        lda clk_buf+3
        ldx #5
        jsr put2
        lda clk_buf+2           ; month 1..12
        sec
        sbc #1
        sta tmp
        asl
        adc tmp
        tax
        ldy #0
@mon:   lda month_names,x
        sta time_line+8,y
        inx
        iny
        cpy #3
        bne @mon
        lda clk_buf
        ldx #12
        jsr put2
        lda clk_buf+1
        ldx #14
        jsr put2
        lda clk_hh
        ldx #18
        jsr put2
        lda #':'
        sta time_line+20
        lda clk_mm
        ldx #21
        jsr put2
        lda #':'
        sta time_line+23
        lda clk_ss
        ldx #24
        jsr put2
        lda #25
        ldx tz_len
        beq @len
        lda #32
        sta time_line+26
        ldx #0
@tz:    lda tz_text,x
        sta time_line+27,x
        inx
        cpx tz_len
        bne @tz
        txa
        clc
        adc #26
@len:   sta time_line
        lda #1
        sta print_color
        lda #TIME_ROW
        ldx #<time_line
        ldy #>time_line
        jmp print_centered
.endproc

; Day of the week (0 = Sunday) for 20YY-MM-DD, Sakamoto's method; in this
; century y + y/4 - y/100 + y/400 is 2485 + yy + yy/4 and 2485 = 7 * 355.
; A = 0..6, or $ff outside 2001..2099.
.proc weekday
        lda clk_buf
        cmp #20
        bne @bad
        lda clk_buf+1
        ldx clk_buf+2
        cpx #3
        bcs :+
        sec                     ; January and February count with the year before
        sbc #1
        bcc @bad
:       sta tmp
        lsr
        lsr
        clc
        adc tmp
        adc dow_months-1,x
        adc clk_buf+3
@mod7:  cmp #7
        bcc @done
        sbc #7
        bcs @mod7
@done:  rts
@bad:   lda #$ff
        rts
.endproc

; A (0..99) -> two digits at time_line+X
.proc put2
        ldy #'0'
@tens:  cmp #10
        bcc @ones
        sbc #10
        iny
        bne @tens
@ones:  ora #'0'
        sta time_line+1,x
        tya
        sta time_line,x
        rts
.endproc

; PETSCII -> screen code for text from FujiNet: $41-$5a and $c1-$da are both
; shown as capitals; anything else outside $20-$5f becomes a space.
.proc petscii_to_screen
        cmp #$c1
        bcc :+
        cmp #$db
        bcs @blank
        and #$7f
:       cmp #$20
        bcc @blank
        cmp #$40
        bcc @same
        cmp #$60
        bcs @blank
        and #$3f
@same:  rts
@blank: lda #32
        rts
.endproc

; ------------------------------------------------------------------ data
.segment "RODATA"

row_lo:
.repeat 25, I
        .byte <(SCREEN + I*40)
.endrepeat
row_hi:
.repeat 25, I
        .byte >(SCREEN + I*40)
.endrepeat

letter_dx:  .byte 0, 28, 56, 84, 112, 140, 168, 196
letter_dph: .byte 0, 20, 40, 60, 80, 100, 120, 140
icon_dx:    .byte 0, 32, 64, 96, 128, 160, 192, 224
icon_dy:    .byte 0, 64, 128, 192, 0, 64, 128, 192
bar_speed:  .byte 2, 3, 5
bar_first:  .byte 0, 9, 18
bit_mask:   .byte $80, $40, $20, $10, $08, $04, $02, $01
fps_for:    .byte 50, 60
clk_limit:  .byte 22, 100, 13, 32, 24, 60, 60
dow_months: .byte 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4
day_names:  .byte "SUNMONTUEWEDTHUFRISAT"
month_names: .byte "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC"

; stars: row, pixel line, speed and colour by layer, start position
star_row_lo:
.repeat STAR_COUNT, I
        .byte <(SCREEN + (STAR_ROW + I .mod STAR_ROWS)*40)
.endrepeat
star_row_hi:
.repeat STAR_COUNT, I
        .byte >(SCREEN + (STAR_ROW + I .mod STAR_ROWS)*40)
.endrepeat
star_line:
.repeat STAR_COUNT, I
        .byte (I*5 + I/STAR_ROWS*3) .mod 8
.endrepeat
star_speed:
.repeat STAR_COUNT, I
        .byte 1 + I/STAR_ROWS
.endrepeat
star_color:
.repeat STAR_COUNT, I
        .byte (I/STAR_ROWS = 0)*11 + (I/STAR_ROWS = 1)*12 + (I/STAR_ROWS = 2)*1
.endrepeat
star_x0_lo:
.repeat STAR_COUNT, I
        .byte <((I*137 + I*I*11) .mod 320)
.endrepeat
star_x0_hi:
.repeat STAR_COUNT, I
        .byte >((I*137 + I*I*11) .mod 320)
.endrepeat

txt_sub:     TEXT "F U J I N E T   N I O"
txt_foot:    TEXT "C64 + FUJINET  -  FUJINET.ONLINE"
txt_search:  TEXT "LOOKING FOR FUJINET..."
txt_online:  TEXT "FUJINET ONLINE  -  TIME FROM DEVICE 30"
txt_offline: TEXT "NO FUJINET FOUND ON THE SERIAL BUS"
txt_hint:    TEXT "(THE DEMO RUNS ANYWAY)"

scroll_text:
        .byte "                                        "
        .byte "WELCOME TO FUJINET NIO ... A COMMODORE 64 AND A FUJINET, "
        .byte "TOGETHER AT LAST!     "
        .byte "THE DATE AND TIME ABOVE COME LIVE FROM FUJINET, DEVICE 30 ON THE "
        .byte "IEC BUS ...     "
        .byte "FUJINET GIVES YOUR C64 A WHOLE NETWORK: DISK IMAGES FROM TNFS "
        .byte "SERVERS ALL OVER THE WORLD, AND MUCH MORE ...     "
        .byte "THIS DEMO LIVES ON THE BOOT DISK. TO RUN IT AGAIN, PRESS  F5  "
        .byte "IN FUJINET CONFIG, OR TYPE  LOAD", $22, "FUJIDEMO", $22, ",8  AND  RUN ...     "
        .byte "THE MUSIC IS ", $22, "POCKET PACKETS", $22, ", AN ORIGINAL TUNE "
        .byte "FOR THE SID: KICK, SNARE AND BASS SHARE ONE VOICE, THE CHORDS "
        .byte "ARPEGGIATE ON THE SECOND, THE MELODY SINGS ON THE THIRD ...     "
        .byte "GREETINGS TO EVERYONE IN THE FUJINET COMMUNITY, AND TO ALL THE "
        .byte "8-BIT FANS STILL MAKING THESE MACHINES SING ...     "
        .byte "FUJINET.ONLINE ...     AND NOW, ONCE MORE FROM THE TOP ...     "
        .byte 0

        .include "gfx.inc"
        .include "player.s"
