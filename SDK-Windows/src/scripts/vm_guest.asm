; Kresten VM guest OS v1.0
; 32-bit protected mode, flat segments, no paging, runs inside the VMX guest.
; Loaded at guest physical 0x100000; GDTR points here (VMCS sets GDTR base).
; Hardware:
;   - VGA text mode 80x25 @ 0xB8000 (mapped by EPT to host B8000)
;   - PS/2 keyboard via ports 0x60/0x64 (IN/OUT exit -> passthrough)
;   - Port 0xF4 write = request VM exit (guest shutdown)

[BITS 32]
org 0x100000

; ---------------- GDT (offset 0x000, 48 bytes) ----------------
gdt_start:
        dq 0                          ; null
        dw 0xFFFF, 0x0000, 0x00, 0x9A, 0xCF, 0x00   ; code 0x08
        dw 0xFFFF, 0x0000, 0x00, 0x92, 0xCF, 0x00   ; data 0x10
gdt_end:

; ---------------- entry (offset 0x100) ----------------
        times 0x100 - ($ - $$) db 0
entry:
        ; NOTE: no cli here - external-interrupt exiting is ON, so guest
        ; interrupts VM-exit to the host ISR instead of using the (empty)
        ; guest IDT. IF=1 guarantees the exits fire.
        mov ax, 0x10
        mov ds, ax
        mov es, ax
        mov fs, ax
        mov gs, ax
        mov ss, ax
        mov esp, 0x200000              ; stack below 2MB guest RAM

        mov edi, 0xB8000               ; clear screen
        mov ecx, 2000
        mov eax, 0x0720
        rep stosw

        mov esi, banner
        call print_str
        mov esi, hint
        call print_str
        mov esi, prompt
        call print_str

        mov byte [cmd_len], 0

main_loop:
        call kbd_getc
        test al, al
        jz main_loop

        cmp al, 0x0D                  ; Enter
        je cmd_enter
        cmp al, 0x08                  ; Backspace
        je cmd_bs
        cmp al, 0x1B                  ; Esc = clear line
        je cmd_esc

        movzx ebx, byte [cmd_len]
        cmp bl, 63
        jae main_loop
        mov edi, cmd_buf
        add edi, ebx
        mov [edi], al
        inc byte [cmd_len]
        call putc                    ; echo
        jmp main_loop

cmd_bs:
        movzx ebx, byte [cmd_len]
        test ebx, ebx
        jz main_loop
        dec byte [cmd_len]
        call cursor_back
        mov al, ' '
        call putc
        call cursor_back
        jmp main_loop

cmd_esc:
        movzx ebx, byte [cmd_len]
        test ebx, ebx
        jz main_loop
clear_line:
        call cursor_back
        mov al, ' '
        call putc
        call cursor_back
        dec byte [cmd_len]
        movzx ebx, byte [cmd_len]
        test ebx, ebx
        jnz clear_line
        jmp main_loop

cmd_enter:
        call newline
        movzx ebx, byte [cmd_len]
        mov byte [cmd_buf + ebx], 0
        call run_cmd
        mov byte [cmd_len], 0
        mov esi, prompt
        call print_str
        jmp main_loop

; ---------------- command handling ----------------
run_cmd:
        mov esi, cmd_buf
        cmp byte [esi], 0
        je .done
        ; exit
        mov edi, s_exit
        call str_eq
        jc .do_exit
        ; help
        mov edi, s_help
        call str_eq
        jc .do_help
        ; clear
        mov edi, s_clear
        call str_eq
        jc .do_clear
        ; echo
        mov edi, s_echo
        call str_eq
        jc .do_echo
        ; unknown
        mov esi, s_unknown
        call print_str
        mov esi, cmd_buf
        call print_str
        call newline
        jmp .done
.do_exit:
        mov esi, s_bye
        call print_str
        mov al, 0x10
        out 0xF4, al                  ; request VM exit
        cli
        hlt
.do_help:
        mov esi, s_help_txt
        call print_str
        jmp .done
.do_clear:
        mov edi, 0xB8000
        mov ecx, 2000
        mov eax, 0x0720
        rep stosw
        mov byte [cur_row], 0
        mov byte [cur_col], 0
        jmp .done
.do_echo:
        add esi, 5                    ; skip "echo"
.esk:
        cmp byte [esi], ' '
        jne .edone
        inc esi
        jmp .esk
.edone:
        cmp byte [esi], 0
        je .done
        call print_str
        call newline
.done:
        ret

; ---------------- string helpers ----------------
; esi -> string; prints until NUL (crlf mode via \n)
print_str:
        push eax
.ps:
        lodsb
        test al, al
        jz .ps_done
        cmp al, 0x0A
        je .ps_nl
        call putc
        jmp .ps
.ps_nl:
        call newline
        jmp .ps
.ps_done:
        pop eax
        ret

; str_eq: esi=cmd, edi=name; CF=1 if equal (name NUL-terminated)
str_eq:
        push eax
.sl:
        mov al, [edi]
        test al, al
        jz .end
        cmp al, [esi]
        jne .no
        inc esi
        inc edi
        jmp .sl
.end:
        ; name exhausted: cmd must be at space or NUL
        cmp byte [esi], 0
        je .yes
        cmp byte [esi], ' '
        je .yes
.no:
        clc
        pop eax
        ret
.yes:
        stc
        pop eax
        ret

; ---------------- display helpers ----------------
; putc: al = char (0x08 backspace handled by caller)
putc:
        push eax
        push ebx
        mov dl, al
        movzx ebx, byte [cur_row]
        imul ebx, 80
        movzx eax, byte [cur_col]
        add ebx, eax
        mov byte [0xB8000 + ebx*2], dl
        pop ebx
        pop eax
        inc byte [cur_col]
        movzx eax, byte [cur_col]
        cmp al, 80
        jb .done
        mov byte [cur_col], 0
        call newline
.done:
        ret

newline:
        push eax
        mov byte [cur_col], 0
        movzx eax, byte [cur_row]
        inc eax
        cmp al, 25
        jb .ok
        call scroll
        jmp .fin
.ok:
        mov [cur_row], al
.fin:
        pop eax
        ret

cursor_back:
        push eax
        movzx eax, byte [cur_col]
        test eax, eax
        jz .nl
        dec eax
        mov [cur_col], al
        jmp .done
.nl:
        movzx eax, byte [cur_row]
        test eax, eax
        jz .done
        dec eax
        mov [cur_row], al
        mov byte [cur_col], 79
.done:
        pop eax
        ret

scroll:
        push eax
        push ecx
        push edi
        push esi
        mov esi, 0xB8000 + 160
        mov edi, 0xB8000
        mov ecx, 960                  ; 24 rows * 80 cells * 4 bytes = 3840 bytes
        rep movsd
        mov edi, 0xB8000 + 160*24
        mov ecx, 80
        mov eax, 0x0720
        rep stosd
        mov byte [cur_row], 24
        pop esi
        pop edi
        pop ecx
        pop eax
        ret

; ---------------- keyboard ----------------
; kbd_getc: al = ASCII char (0 if none)
; scancode set1 -> ascii, shift handled roughly
kbd_getc:
        mov al, 0
        in al, 0x64
        test al, 1
        jz .none
        in al, 0x60                    ; scancode
        cmp al, 0x2A
        je .shift_on
        cmp al, 0x36
        je .shift_on
        cmp al, 0xAA
        je .shift_off
        cmp al, 0xB6
        je .shift_off
        test al, 0x80
        jnz .none                      ; other key up
        ; translate
        cmp al, 0x1C
        jne .not_ent
        mov al, 0x0D
        ret
.not_ent:
        cmp al, 0x0E
        jne .not_bs
        mov al, 0x08
        ret
.not_bs:
        cmp al, 0x39
        jne .not_spc
        mov al, ' '
        ret
.not_spc:
        ; digits row 0x02-0x0B
        cmp al, 0x02
        jb .low
        cmp al, 0x0B
        ja .low
        movzx eax, al
        mov al, [digit_tbl + eax - 0x02]
        ret
.low:
        ; alpha row 0x10-0x1C
        cmp al, 0x10
        jb .none
        cmp al, 0x1C
        ja .try_row2
        movzx eax, al
        cmp byte [shift_flag], 0
        je .lo
        mov al, [alpha_tbl_hi + eax - 0x10]
        ret
.lo:
        mov al, [alpha_tbl_lo + eax - 0x10]
        ret
.try_row2:
        ; row 0x1E-0x26 (a..l) with shift + '-=[]'
        cmp al, 0x1E
        jb .try_row3
        cmp al, 0x26
        ja .try_row3
        movzx eax, al
        mov al, [alpha2_tbl + eax - 0x1E]
        ret
.try_row3:
        ; row 0x2C-0x35 (z../) with shift
        cmp al, 0x2C
        jb .none
        cmp al, 0x35
        ja .none
        movzx eax, al
        mov al, [alpha3_tbl + eax - 0x2C]
        ret
.shift_on:
        mov byte [shift_flag], 1
        jmp .none
.shift_off:
        mov byte [shift_flag], 0
.none:
        xor eax, eax
        ret

; ---------------- data ----------------
banner:  db "Kresten VM guest OS v1.0 - VMX virtual machine", 0x0A, 0x0A, 0
hint:    db "commands: help, clear, echo <text>, exit", 0x0A, 0
prompt:  db "guest> ", 0
s_help:  db "help", 0
s_clear: db "clear", 0
s_echo:  db "echo", 0
s_exit:  db "exit", 0
s_unknown: db "unknown command: ", 0
s_bye:   db "bye - exiting virtual machine", 0x0A, 0
s_help_txt: db "help        show this text", 0x0A
           db "clear       clear the screen", 0x0A
           db "echo <txt>  print text", 0x0A
           db "exit        stop the virtual machine", 0x0A, 0

digit_tbl: db "1234567890"
alpha_tbl_lo: db "qwertyuiop", 0x00, 0x00, 0x00
alpha_tbl_hi: db "QWERTYUIOP", 0x00, 0x00, 0x00
alpha2_tbl: db "asdfghjkl", 0x00, 0x00, 0x00, 0x00
alpha3_tbl: db "zxcvbnm,./"

cur_row: db 0
cur_col: db 0
shift_flag: db 0
cmd_len: db 0
cmd_buf: times 64 db 0
