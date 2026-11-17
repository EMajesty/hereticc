/*
 * Adapted from AM Mod's MIT-licensed step_abi.S at
 * 6cdc67276f43cd39f64871cb7e36782481244c65.
 * Copyright (c) 2026 AM Mod contributors.
 */
.arch armv8.3-a
.text

/* Vtable calls are indirect; provide an explicit BTI landing pad before the
 * Rust callback body. */
.global neurogenic_m04a_step_paint_bridge
.hidden neurogenic_m04a_step_paint_bridge
.type neurogenic_m04a_step_paint_bridge,%function
neurogenic_m04a_step_paint_bridge:
 bti c
 b neurogenic_m04a_step_paint
.size neurogenic_m04a_step_paint_bridge,.-neurogenic_m04a_step_paint_bridge

/* The native add factory returns a nontrivial 16-byte shared_ptr in x8 and
 * receives its ninth argument on the stack. Keep that C++ ABI out of Rust. */
.global neurogenic_step_add_result
.hidden neurogenic_step_add_result
.type neurogenic_step_add_result,%function
neurogenic_step_add_result:
 bti c
 paciasp
 ldr x8,[sp]
 ldr x16,[sp,#8]
 sub sp,sp,#32
 stp x29,x30,[sp,#16]
 add x29,sp,#16
 str xzr,[sp]
 blr x16
 ldp x29,x30,[sp,#16]
 add sp,sp,#32
 autiasp
 ret
.size neurogenic_step_add_result,.-neurogenic_step_add_result

/* The single-remove factory also returns shared_ptr through x8. */
.global neurogenic_step_remove_result
.hidden neurogenic_step_remove_result
.type neurogenic_step_remove_result,%function
neurogenic_step_remove_result:
 bti c
 mov x8,x4
 br x5
.size neurogenic_step_remove_result,.-neurogenic_step_remove_result

.section .note.GNU-stack,"",%progbits
