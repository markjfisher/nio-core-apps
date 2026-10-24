; Bounded Atari-side loader for the one Story 2.5 FNR1 cc65 o65 payload.
;
; It deliberately accepts only the format emitted by cc65 module.cfg for this
; image: 16-bit/small o65, no imports or exports, contiguous TEXT/DATA/BSS,
; and WORD/HIGH/LOW relocations against TEXT/DATA/BSS/ZP.  It is not a
; general-purpose o65 loader.

        .export _main
        .macpack longbranch

HATABS          = $031A
MEMTOP          = $02E5
MEMLO           = $02E7
SDLSTL          = $0230
SAVMSC          = $0058

LOADER_START    = $3800
RESULT          = $5400          ; transient inspection only, never reserved
STAGE           = $5800
FNR_HEADER      = 22
CC65_ZP         = $0082
CC65_ZP_SIZE    = 26
STACK_RESERVE   = $0800
HATABS_BYTES    = 33

; FNR1 wrapper offsets.
FNR_VERSION     = STAGE+4
FNR_HSIZE       = STAGE+5
FNR_ENTRY       = STAGE+6
FNR_TABLE       = STAGE+8
FNR_MARKER      = STAGE+10
FNR_MODULE_LEN  = STAGE+12
FNR_ALLOC       = STAGE+14
FNR_INITIALIZED = STAGE+16
FNR_CSTACK      = STAGE+18
FNR_ZP          = STAGE+20

; Versioned transient result offsets.  The layout intentionally preserves the
; original scaffold's fields so the runner can compare both implementations.
R_OUTCOME       = RESULT+5
R_INIT           = RESULT+6
R_OLD_MEMLO      = RESULT+8
R_NEW_MEMLO      = RESULT+10
R_START          = RESULT+12
R_END            = RESULT+14
R_MEMTOP         = RESULT+16
R_STACK_FLOOR    = RESULT+18
R_SDLSTL         = RESULT+20
R_SAVMSC         = RESULT+22
R_HAT_OFFSET     = RESULT+24
R_DETAIL         = RESULT+25
R_TABLE          = RESULT+26
R_VECTORS        = RESULT+28

OUT_INSTALLED    = 0
OUT_OWN          = 1
OUT_INIT_FAILED  = 2
OUT_MEMORY       = 3
OUT_FOREIGN      = 4
OUT_NO_SLOT      = 5
OUT_FORMAT       = 6

RT_WORD          = $80
RT_HIGH          = $40
RT_LOW           = $20
SEG_TEXT         = 2
SEG_DATA         = 3
SEG_BSS          = 4
SEG_ZP           = 5

; The loader temporarily borrows the complete cc65 ZP bank.  It saves this
; bank before parsing and restores it before returning to DOS/BASIC; the
; resident C call reuses the same documented addresses while it executes.
module_ptr        = $82
module_end        = $84
src_ptr           = $86
base_ptr          = $88
existing_base     = $8A
target_end        = $8C
dest_ptr          = $8E
entry_ptr         = $90
table_ptr         = $92
marker_ptr        = $94
reloc_ptr         = $96
target_ptr        = $98
reloc_value       = $9A

.segment "CODE"

_main:
        jsr save_caller_zp
        jsr init_result
        jsr validate_wrapper
        bcc :+
        jmp fail_format
:
        jsr scan_hatabs
        bcc :+
        jmp finish              ; own, foreign, or no-slot result is final
:
        ; Do not derive or touch a proposed destination until ownership has
        ; been resolved.  In particular, a second load sees our existing N:
        ; even when the raised MEMLO could no longer hold another payload.
        jsr calculate_ranges
        bcc :+
        jmp fail_memory
:
        jsr load_module
        bcc :+
        jmp fail_format
:
        lda #$36
        sta R_DETAIL
        jsr prepare_and_init
        bcc :+
        jmp fail_init
:
        lda #$37
        sta R_DETAIL
        jsr publish_hatabs
        jmp finish

finish:
        jsr restore_caller_zp
        rts

; -------------------------------------------------------------------------
; Result and wrapper/range validation.

init_result:
        lda #'N'
        sta RESULT
        lda #'R'
        sta RESULT+1
        lda #'I'
        sta RESULT+2
        lda #'!'
        sta RESULT+3
        lda #2
        sta RESULT+4
        lda #OUT_MEMORY
        sta R_OUTCOME
        lda #$ff
        sta R_INIT
        lda #$92
        sta RESULT+7
        lda #$ff
        sta R_HAT_OFFSET
        sta R_DETAIL
        lda MEMLO
        sta R_OLD_MEMLO
        sta R_NEW_MEMLO
        lda MEMLO+1
        sta R_OLD_MEMLO+1
        sta R_NEW_MEMLO+1
        lda MEMTOP
        sta R_MEMTOP
        lda MEMTOP+1
        sta R_MEMTOP+1
        sec
        sbc #>STACK_RESERVE
        sta R_STACK_FLOOR+1
        lda MEMTOP
        sta R_STACK_FLOOR
        lda SDLSTL
        sta R_SDLSTL
        lda SDLSTL+1
        sta R_SDLSTL+1
        lda SAVMSC
        sta R_SAVMSC
        lda SAVMSC+1
        sta R_SAVMSC+1
        rts

validate_wrapper:
        lda STAGE
        cmp #'F'
        bne wrapper_bad
        lda STAGE+1
        cmp #'N'
        bne wrapper_bad
        lda STAGE+2
        cmp #'R'
        bne wrapper_bad
        lda STAGE+3
        cmp #'1'
        bne wrapper_bad
        lda FNR_VERSION
        cmp #1
        bne wrapper_bad
        lda FNR_HSIZE
        cmp #FNR_HEADER
        bne wrapper_bad
        lda FNR_ZP
        cmp #CC65_ZP_SIZE
        bne wrapper_bad
        lda FNR_ZP+1
        bne wrapper_bad
        lda FNR_MODULE_LEN
        ora FNR_MODULE_LEN+1
        beq wrapper_bad
        ; module pointer and exclusive source bound
        lda #<(STAGE+FNR_HEADER)
        sta module_ptr
        sta src_ptr
        lda #>(STAGE+FNR_HEADER)
        sta module_ptr+1
        sta src_ptr+1
        clc
        lda module_ptr
        adc FNR_MODULE_LEN
        sta module_end
        lda module_ptr+1
        adc FNR_MODULE_LEN+1
        sta module_end+1
        bcs wrapper_bad
        ; Reject stage wrapping and a stage that reaches the live C stack.
        lda module_end+1
        cmp R_STACK_FLOOR+1
        bcc wrapper_ok
        bne wrapper_bad
        lda module_end
        cmp R_STACK_FLOOR
        bcc wrapper_ok
wrapper_bad:
        sec
        rts
wrapper_ok:
        clc
        rts

calculate_ranges:
        lda MEMLO
        sta base_ptr
        lda MEMLO+1
        sta base_ptr+1
        lda base_ptr
        and #1
        beq :+
        inc base_ptr
        bne :+
        inc base_ptr+1
:
        lda base_ptr
        sta R_START
        clc
        adc FNR_ALLOC
        sta target_end
        lda base_ptr+1
        sta R_START+1
        adc FNR_ALLOC+1
        sta target_end+1
        clc
        lda target_end
        adc FNR_CSTACK
        sta target_end
        lda target_end+1
        adc FNR_CSTACK+1
        sta target_end+1
        bcs @bad
        lda target_end
        sta R_END
        lda target_end+1
        sta R_END+1
        ; Keep the dynamic resident image below this transient installer.
        lda target_end+1
        cmp #>LOADER_START
        bcc @after_loader
        bne @bad
        lda target_end
        cmp #<LOADER_START
        bne @bad
@after_loader:
        jsr target_before_stack
        bcs @bad
        lda target_end+1
        cmp R_SDLSTL+1
        bcc :+
        bne @bad
        lda target_end
        cmp R_SDLSTL
        bcs @bad
:
        lda target_end+1
        cmp R_SAVMSC+1
        bcc @ok
        bne @bad
        lda target_end
        cmp R_SAVMSC
        bcs @bad
@ok:   clc
        rts
@bad:  sec
        rts

target_before_stack:
        lda target_end+1
        cmp R_STACK_FLOOR+1
        bcc @ok
        bne @bad
        lda target_end
        cmp R_STACK_FLOOR
        bcc @ok
@bad:  sec
        rts
@ok:   clc
        rts

; -------------------------------------------------------------------------
; HATABS ownership is checked before destination memory is touched.  The
; fixed stage is safe for any payload previously installed by this bounded
; loader because its target is capped below $3800.

scan_hatabs:
        ldx #0
        stx hat_slot
@scan: lda HATABS,x
        beq @empty
        cmp #'N'
        beq @existing
        inx
        inx
        inx
        cpx #HATABS_BYTES
        bcc @scan
        lda #OUT_NO_SLOT
        sta R_OUTCOME
        sec
        rts
@empty:
        stx hat_slot
        clc
        rts
@existing:
        lda HATABS+1,x
        sta table_ptr
        lda HATABS+2,x
        sta table_ptr+1
        ; Derive the original base from our known table offset and require
        ; the marker before treating this as an idempotent NIO install.
        sec
        lda table_ptr
        sbc FNR_TABLE
        sta existing_base
        lda table_ptr+1
        sbc FNR_TABLE+1
        sta existing_base+1
        jcc scan_foreign
        lda existing_base+1
        cmp R_OLD_MEMLO+1
        bcc :+
        jne scan_foreign
        lda existing_base
        cmp R_OLD_MEMLO
        jcs scan_foreign
:
        clc
        lda existing_base
        adc FNR_MARKER
        sta marker_ptr
        lda existing_base+1
        adc FNR_MARKER+1
        sta marker_ptr+1
        ldy #0
        lda (marker_ptr),y
        cmp #'F'
        jne scan_foreign
        iny
        lda (marker_ptr),y
        cmp #'N'
        jne scan_foreign
        iny
        lda (marker_ptr),y
        cmp #'R'
        jne scan_foreign
        iny
        lda (marker_ptr),y
        cmp #'1'
        jne scan_foreign
        stx R_HAT_OFFSET
        lda #OUT_OWN
        sta R_OUTCOME
        lda existing_base
        sta R_START
        clc
        adc FNR_ALLOC
        sta R_END
        lda existing_base+1
        sta R_START+1
        adc FNR_ALLOC+1
        sta R_END+1
        clc
        lda R_END
        adc FNR_CSTACK
        sta R_END
        lda R_END+1
        adc FNR_CSTACK+1
        sta R_END+1
        jsr record_table_and_vectors
        sec
        rts
scan_foreign:
        lda #OUT_FOREIGN
        sta R_OUTCOME
        sec
        rts

; -------------------------------------------------------------------------
; o65 validation, copy, BSS clear, and bounded relocation.

load_module:
        lda #$30
        sta R_DETAIL
        jsr parse_o65_header
        bcc :+
        lda #$e8
        sta R_DETAIL
        jmp load_bad
:
        lda #$31
        sta R_DETAIL
        jsr skip_o65_options
        bcc :+
        jmp load_bad
:
        lda #$32
        sta R_DETAIL
        lda base_ptr
        sta dest_ptr
        lda base_ptr+1
        sta dest_ptr+1
        lda init_size
        sta byte_count
        lda init_size+1
        sta byte_count+1
@copy: lda byte_count
        ora byte_count+1
        beq @zero
        jsr read_byte
        ldy #0
        sta (dest_ptr),y
        jsr inc_dest
        jsr dec_count
        jmp @copy
@zero: lda bss_size
        sta byte_count
        lda bss_size+1
        sta byte_count+1
@clear:lda byte_count
        ora byte_count+1
        beq @imports
        ldy #0
        lda #0
        sta (dest_ptr),y
        jsr inc_dest
        jsr dec_count
        jmp @clear
@imports:
        lda #$33
        sta R_DETAIL
        jsr read_byte
        jne load_bad
        jsr read_byte
        jne load_bad
        lda base_ptr
        sta reloc_ptr
        lda base_ptr+1
        sta reloc_ptr+1
        lda text_size
        sta segment_size
        lda text_size+1
        sta segment_size+1
        jsr relocate_segment
        bcc :+
        lda #$ea
        sta R_DETAIL
        jmp load_bad
:
        lda #$34
        sta R_DETAIL
        clc
        lda reloc_ptr
        adc text_size
        sta reloc_ptr
        lda reloc_ptr+1
        adc text_size+1
        sta reloc_ptr+1
        lda data_size
        sta segment_size
        lda data_size+1
        sta segment_size+1
        jsr relocate_segment
        bcc :+
        lda #$eb
        sta R_DETAIL
        jmp load_bad
:
        lda #$35
        sta R_DETAIL
        jsr read_byte
        jne load_bad
        jsr read_byte
        jne load_bad
        lda src_ptr
        cmp module_end
        jne load_bad
        lda src_ptr+1
        cmp module_end+1
        jne load_bad
        clc
        rts
load_bad:
        sec
        rts

parse_o65_header:
        ; The wrapper guaranteed at least a nonempty source.  Enforce the
        ; exact cc65 module header before interpreting lengths.
        ldy #0
        lda (module_ptr),y
        cmp #$01
        jne parse_bad
        iny
        lda (module_ptr),y
        jne parse_bad
        iny
        lda (module_ptr),y
        cmp #'o'
        jne parse_bad
        iny
        lda (module_ptr),y
        cmp #'6'
        jne parse_bad
        iny
        lda (module_ptr),y
        cmp #'5'
        jne parse_bad
        iny
        lda (module_ptr),y
        jne parse_bad
        iny
        lda (module_ptr),y
        jne parse_bad
        iny
        lda (module_ptr),y
        cmp #$08
        jne parse_bad
        iny
        lda (module_ptr),y             ; text base low
        jne parse_bad
        iny
        lda (module_ptr),y             ; text base high
        jne parse_bad
        iny
        lda (module_ptr),y
        sta text_size
        iny
        lda (module_ptr),y
        sta text_size+1
        iny
        lda (module_ptr),y             ; data base == text length
        cmp text_size
        jne parse_bad
        iny
        lda (module_ptr),y
        cmp text_size+1
        jne parse_bad
        iny
        lda (module_ptr),y
        sta data_size
        iny
        lda (module_ptr),y
        sta data_size+1
        clc
        lda text_size
        adc data_size
        sta init_size
        lda text_size+1
        adc data_size+1
        sta init_size+1
        iny
        lda (module_ptr),y             ; bss base == initialized size
        cmp init_size
        jne parse_bad
        iny
        lda (module_ptr),y
        cmp init_size+1
        jne parse_bad
        iny
        lda (module_ptr),y
        sta bss_size
        iny
        lda (module_ptr),y
        sta bss_size+1
        clc
        lda init_size
        adc bss_size
        php                     ; retain the carry into the allocation high byte
        cmp FNR_ALLOC
        beq :+
        plp
        jmp parse_bad
:
        plp
        lda init_size+1
        adc bss_size+1
        cmp FNR_ALLOC+1
        jne parse_bad
        iny
        lda (module_ptr),y             ; ZP base low/high
        jne parse_bad
        iny
        lda (module_ptr),y
        jne parse_bad
        iny
        lda (module_ptr),y
        cmp FNR_ZP
        jne parse_bad
        iny
        lda (module_ptr),y
        cmp FNR_ZP+1
        jne parse_bad
        iny
        lda (module_ptr),y             ; module stack request must be zero
        jne parse_bad
        iny
        lda (module_ptr),y
        jne parse_bad
        lda module_ptr
        clc
        adc #26
        sta src_ptr
        lda module_ptr+1
        adc #0
        sta src_ptr+1
        clc
        rts
parse_bad:
        lda #$e1
        sta R_DETAIL
        sec
        rts

skip_o65_options:
skip_options_next:
        jsr read_byte
        sta R_DETAIL
        beq skip_options_ok
        cmp #2
        jcc skip_options_bad
        sec
        sbc #1
        sta option_count
skip_options_bytes:
        jsr read_byte
        dec option_count
        jne skip_options_bytes
        jmp skip_options_next
skip_options_ok:
        lda #$90
        sta R_DETAIL
        clc
        rts
skip_options_bad:
        sec
        rts

; Read one staged module byte with an exclusive module-end bound.
read_byte:
        lda src_ptr+1
        cmp module_end+1
        bcc @read
        beq :+
        lda #$e6
        sta R_DETAIL
        jmp fail_format
:
        lda src_ptr
        cmp module_end
        bcc @read
        lda #$e6
        sta R_DETAIL
        jmp fail_format
@read: ldy #0
        lda (src_ptr),y
        pha                     ; retain the byte's N/Z flags for callers
        inc src_ptr
        bne :+
        inc src_ptr+1
:       pla
        rts

inc_dest:
        inc dest_ptr
        bne :+
        inc dest_ptr+1
:       rts

dec_count:
        lda byte_count
        bne :+
        dec byte_count+1
:       dec byte_count
        rts

; Apply exactly the relocation records emitted by this module profile.
relocate_segment:
        lda #$ff
        sta reloc_offset
        sta reloc_offset+1
@next: jsr read_byte
        jeq reloc_done
        cmp #$ff
        bne @delta
        clc
        lda reloc_offset
        adc #$fe
        sta reloc_offset
        lda reloc_offset+1
        adc #0
        sta reloc_offset+1
        jmp @next
@delta:clc
        adc reloc_offset
        sta reloc_offset
        lda reloc_offset+1
        adc #0
        sta reloc_offset+1
        jsr read_byte
        sta reloc_descriptor
        and #$07
        cmp #SEG_TEXT
        beq @module_base
        cmp #SEG_DATA
        beq @module_base
        cmp #SEG_BSS
        beq @module_base
        cmp #SEG_ZP
        beq @zp_base
        sec
        rts
@module_base:
        lda base_ptr
        sta reloc_value
        lda base_ptr+1
        sta reloc_value+1
        jmp @kind
@zp_base:
        lda #<CC65_ZP
        sta reloc_value
        lda #>CC65_ZP
        sta reloc_value+1
@kind:  lda reloc_descriptor
        and #$e0
        cmp #RT_WORD
        beq @word
        cmp #RT_HIGH
        beq @high
        cmp #RT_LOW
        beq @low
        sec
        rts
@target:
        clc
        lda reloc_ptr
        adc reloc_offset
        sta target_ptr
        lda reloc_ptr+1
        adc reloc_offset+1
        sta target_ptr+1
        rts
@low:   jsr target_in_segment
        bcs @bad
        jsr @target
        ldy #0
        clc
        lda (target_ptr),y
        adc reloc_value
        sta (target_ptr),y
        jmp @next
@high:  jsr target_in_segment
        bcs @bad
        jsr read_byte
        clc
        adc reloc_value
        lda reloc_value+1
        adc #0
        sta high_addend
        jsr @target
        ldy #0
        clc
        lda (target_ptr),y
        adc high_addend
        sta (target_ptr),y
        jmp @next
@word:  jsr word_in_segment
        bcs @bad
        jsr @target
        ldy #0
        clc
        lda (target_ptr),y
        adc reloc_value
        sta (target_ptr),y
        iny
        lda (target_ptr),y
        adc reloc_value+1
        sta (target_ptr),y
        jmp @next
@bad:  sec
        rts
reloc_done:
        clc
        rts

; Carry is set if the current relocation address escapes its segment.
target_in_segment:
        lda reloc_offset+1
        cmp segment_size+1
        bcc @ok
        bne @bad
        lda reloc_offset
        cmp segment_size
        bcc @ok
@bad:  sec
        rts
@ok:   clc
        rts

word_in_segment:
        clc
        lda reloc_offset
        adc #1
        sta check_offset
        lda reloc_offset+1
        adc #0
        sta check_offset+1
        lda check_offset+1
        cmp segment_size+1
        bcc @ok
        bne @bad
        lda check_offset
        cmp segment_size
        bcc @ok
@bad:  sec
        rts
@ok:   clc
        rts

; -------------------------------------------------------------------------
; Runtime initialization and publication.

prepare_and_init:
        ; Check the copied marker before invoking any library code.
        clc
        lda base_ptr
        adc FNR_MARKER
        sta marker_ptr
        lda base_ptr+1
        adc FNR_MARKER+1
        sta marker_ptr+1
        ldy #0
        lda (marker_ptr),y
        cmp #'F'
        bne prepare_bad
        iny
        lda (marker_ptr),y
        cmp #'N'
        bne prepare_bad
        iny
        lda (marker_ptr),y
        cmp #'R'
        bne prepare_bad
        iny
        lda (marker_ptr),y
        cmp #'1'
        bne prepare_bad
        ; The loader already saved the caller's $82-$9b bank.  Re-purpose
        ; it now as the documented cc65 ZP allocation for the resident call.
        lda target_end
        sta CC65_ZP                    ; c_sp
        lda target_end+1
        sta CC65_ZP+1
        clc
        lda base_ptr
        adc FNR_ENTRY
        sta entry_ptr
        lda base_ptr+1
        adc FNR_ENTRY+1
        sta entry_ptr+1
        lda entry_ptr
        sta call_entry+1
        lda entry_ptr+1
        sta call_entry+2
call_entry:
        jsr $ffff
        sta init_status
        lda init_status
        sta R_INIT
        beq prepare_ok
prepare_bad:
        sec
        rts
prepare_ok:
        clc
        rts

publish_hatabs:
        lda R_START
        sta base_ptr
        lda R_START+1
        sta base_ptr+1
        lda R_END
        sta target_end
        lda R_END+1
        sta target_end+1
        clc
        lda base_ptr
        adc FNR_TABLE
        sta table_ptr
        lda base_ptr+1
        adc FNR_TABLE+1
        sta table_ptr+1
        jsr validate_table_vectors
        bcc :+
        lda #$e7
        sta R_DETAIL
        jmp fail_format
:
        lda #$38
        sta R_DETAIL
        jsr record_table_and_vectors
        ldx hat_slot
        lda table_ptr
        sta HATABS+1,x
        lda table_ptr+1
        sta HATABS+2,x
        lda #'N'
        sta HATABS,x
        lda target_end
        sta MEMLO
        sta R_NEW_MEMLO
        lda target_end+1
        sta MEMLO+1
        sta R_NEW_MEMLO+1
        stx R_HAT_OFFSET
        lda #OUT_INSTALLED
        sta R_OUTCOME
        rts

record_table_and_vectors:
        lda table_ptr
        sta R_TABLE
        lda table_ptr+1
        sta R_TABLE+1
        ldy #0
@copy: lda (table_ptr),y
        sta R_VECTORS,y
        iny
        cpy #12
        bne @copy
        rts

validate_table_vectors:
        ldy #0
@next: lda (table_ptr),y
        sta target_ptr
        iny
        lda (table_ptr),y
        sta target_ptr+1
        iny
        clc
        lda target_ptr
        adc #1
        sta target_ptr
        bcc :+
        inc target_ptr+1
:
        lda target_ptr+1
        cmp base_ptr+1
        bcc @bad
        bne :+
        lda target_ptr
        cmp base_ptr
        bcc @bad
:
        lda target_ptr+1
        cmp R_END+1
        bcc @ok
        bne @bad
        lda target_ptr
        cmp R_END
        bcs @bad
@ok:   cpy #12
        bne @next
        clc
        rts
@bad:  lda #$e3
        sta R_DETAIL
        sec
        rts

fail_memory:
        lda #OUT_MEMORY
        bne fail_common
fail_format:
        lda #OUT_FORMAT
        bne fail_common
fail_init:
        lda #OUT_INIT_FAILED
fail_common:
        sta R_OUTCOME
        jmp finish

save_caller_zp:
        ldx #CC65_ZP_SIZE-1
@loop:  lda CC65_ZP,x
        sta zp_save,x
        dex
        bpl @loop
        rts

restore_caller_zp:
        ldx #CC65_ZP_SIZE-1
@loop:  lda zp_save,x
        sta CC65_ZP,x
        dex
        bpl @loop
        rts

; Mutable transient-loader state.  This memory may be overwritten by the
; second XEX program after the installer returns; nothing here is resident.
reloc_offset:   .word 0
segment_size:   .word 0
check_offset:   .word 0
text_size:      .word 0
data_size:      .word 0
bss_size:       .word 0
init_size:      .word 0
byte_count:     .word 0
reloc_descriptor:.byte 0
high_addend:    .byte 0
option_count:   .byte 0
hat_slot:       .byte 0
init_status:    .byte 0
zp_save:        .repeat CC65_ZP_SIZE
                .byte 0
                .endrepeat
