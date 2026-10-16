; Independently transcribed from SkyrimSE 1.5.97 caller RVA B27570 and
; callee RVA DD6780. Does not include the C++ hook declaration.
EXTERN observedSelf:QWORD
EXTERN observedResult:QWORD
EXTERN observedA:QWORD
EXTERN observedB:QWORD
.code
InvokeNative PROC
    mov rax,rcx
    mov rcx,rdx
    mov rdx,r8
    mov r8,r9
    mov r9,QWORD PTR [rsp+28h]
    sub rsp,28h
    call rax
    add rsp,28h
    ret
InvokeNative ENDP
NativeOriginal PROC
    mov observedSelf,rcx
    mov observedResult,rdx
    mov observedA,r8
    mov observedB,r9
    mov BYTE PTR [rdx],1
    mov rax,rdx
    ret
NativeOriginal ENDP
END
