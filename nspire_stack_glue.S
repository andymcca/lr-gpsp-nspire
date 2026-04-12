@ Ndless stack switching for menu / savestate / quit (pairs with libretro core).

.data
.align 2
.global not_crt0_savedsp
not_crt0_savedsp:
	.word 0

.text
.align 2

.global enableAlignmentExceptions
enableAlignmentExceptions:
	mrc p15, 0, r0, c1, c0, 0
	orr r0, r0, #2
	mcr p15, 0, r0, c1, c0, 0
	bx lr

.global clean_dcache
clean_dcache:
	mrc p15, 0, r15, c7, c10, 3
	bne clean_dcache
	mov r0, #0
	mcr p15, 0, r0, c7, c10, 4
	bx lr

.global menu_wrapper
menu_wrapper:
	mov r12, sp
	ldr r3, .Lpool_menu
	ldr sp, [r3]
	stmfd sp!, {r12, lr}
	bl menu
	ldmfd sp, {sp, pc}
.Lpool_menu:
	.word not_crt0_savedsp

.global save_backup_wrapper
save_backup_wrapper:
	mov r12, sp
	ldr r3, .Lpool_sb
	ldr sp, [r3]
	stmfd sp!, {r12, lr}
	bl save_backup
	ldmfd sp, {sp, pc}
.Lpool_sb:
	.word not_crt0_savedsp

.global load_state_wrapper
load_state_wrapper:
	mov r12, sp
	ldr r3, .Lpool_ls
	ldr sp, [r3]
	stmfd sp!, {r12, lr}
	bl load_state
	ldmfd sp, {sp, pc}
.Lpool_ls:
	.word not_crt0_savedsp

.global save_state_wrapper
save_state_wrapper:
	mov r12, sp
	ldr r3, .Lpool_ss
	ldr sp, [r3]
	stmfd sp!, {r12, lr}
	bl save_state
	ldmfd sp, {sp, pc}
.Lpool_ss:
	.word not_crt0_savedsp

.global save_config_file_wrapper
save_config_file_wrapper:
	mov r12, sp
	ldr r3, .Lpool_scf
	ldr sp, [r3]
	stmfd sp!, {r12, lr}
	bl save_config_file
	ldmfd sp, {sp, pc}
.Lpool_scf:
	.word not_crt0_savedsp

.global quit_wrapper
quit_wrapper:
	ldr r3, .Lpool_q
	ldr sp, [r3]
	b quit
.Lpool_q:
	.word not_crt0_savedsp
