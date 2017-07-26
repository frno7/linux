/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 01 by Ralf Baechle
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 2001 MIPS Technologies, Inc.
 * Copyright (C) 2012-2013 Juergen Urban
 */

#include <linux/errno.h>
#include <asm/asm.h>
#include <asm/asmmacro.h>
#include <asm/irqflags.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>
#include <asm/thread_info.h>
#include <asm/unistd.h>

/* MIPS ABI N32 register names are different to O32. */
/* Register t0, t1, t2 and t3 should not be used, use t4, t5, t6 and t7 instead. */
#define a4 t0
#define a5 t1
#define a6 t2
#define a7 t3


/* This duplicates the definition from <linux/sched.h> */
#define PT_TRACESYS	0x00000002	/* tracing system calls */

/* This duplicates the definition from <asm/signal.h> */
#define SIGILL		4		/* Illegal instruction (ANSI).  */

	.set push
	.set arch=r5900

	.align  5
NESTED(handle_sys, PT_SIZE, sp)
	.set	noat
#ifdef CONFIG_CPU_R5900
	nop
	nop
#endif
	SAVE_SOME
	SAVE_AT
	SAVE_TEMP			# This will be restored in syscall_exit and n32_syscall_exit.
	SAVE_STATIC
					# N32 callers expect a4/t0 to be saved.
	TRACE_IRQS_ON_RELOAD		# Caution: This can overwrite a4/t0.
	STI				# Caution: This overwrites a4/t0.
	.set	at

	dsubu	t4, v0, __NR_N32_Linux	# check syscall number
	sltiu	t6, t4,	__NR_N32_Linux_syscalls + 1

	ld	t5, PT_EPC(sp)		# skip syscall on return
	daddiu	t5, 4			# skip to next instruction
	sd	t5, PT_EPC(sp)
	beqz	t6, not_n32_scall

	dsll	t4, t4, 3		# offset into table
	la	t5, sysn32_call_table	# Syscall table address
	addu	t4, t5
	lw	t6, 0(t4)		# pointer to syscall function
	lw	t7, 4(t4)		# >= 0 if we need stack arguments

	sd	a3, PT_R26(sp)		# save a3 for syscall restarting
	bgez	t7, stackargs

stack_done:
	li	t5, _TIF_SYSCALL_TRACE | _TIF_SYSCALL_AUDIT
	LONGI_L	t4, TI_FLAGS($28)	# syscall tracing enabled?
	and	t4, t5, t4
	bnez	t4, n32_syscall_trace_entry

	jalr	t6			# Do The Real Thing (TM)

	li	t4, -EMAXERRNO - 1	# error?
	sltu	t4, t4, v0
	sd	t4, PT_R7(sp)		# set error flag
	beqz	t4, 1f

	dnegu	v0			# error
	sd	v0, PT_R0(sp)		# set flag for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

.global n32_syscall_exit
n32_syscall_exit:
	local_irq_disable		# make sure need_resched and
					# signals dont change between
					# sampling and return
	LONGI_L  a2, TI_FLAGS($28)	# current->work
	li	t4, _TIF_ALLWORK_MASK
	and	t4, a2, t4
	bnez	t4, n32_syscall_exit_work

	j	restore_all

n32_syscall_exit_work:
	RESTORE_STATIC
	j	syscall_exit_work_partial

/* ------------------------------------------------------------------------ */

n32_syscall_trace_entry:
	move	s0, t6
	move	a0, sp
	li	a1, 0
	jal	syscall_trace_enter

	move	t4, s0
	RESTORE_STATIC
	ld	a0, PT_R4(sp)		# Restore argument registers
	ld	a1, PT_R5(sp)
	ld	a2, PT_R6(sp)
	ld	a3, PT_R7(sp)
	jalr	t4

	li	t4, -EMAXERRNO - 1	# error?
	sltu	t4, t4, v0
	sd	t4, PT_R7(sp)		# set error flag
	beqz	t4, 1f

	dnegu	v0			# error
	sd	v0, PT_R0(sp)		# set flag for syscall restarting
1:	sd	v0, PT_R2(sp)		# result

	j	syscall_exit

/* ------------------------------------------------------------------------ */

	/*
	 * More than four arguments. Try to deal with it by copying the
	 * stack arguments from the registers (ABI N32) to the kernel stack (ABI O32).
	 */
stackargs:
	/* Ok, copy the args from the register to the kernel stack.
	 * t7 is the precomputed number of instruction bytes needed to
	 * load or store arguments 6-8.
	 */
	LONGD_L	a4, PT_R8(sp)
	la	t5, 1f			# load up to 3 arguments
	subu	t5, t7
	sw	a4, 16(sp)		# argument #5 to ksp
	jr	t5			# Jump to label 1f if t7 is 0 (5 arguments).

	.set    push
	.set    noreorder
	.set	nomacro
	LONGD_L	a7, PT_R11(sp)
	sw	a7, 28(sp)		# argument #8 to ksp
	LONGD_L	a6, PT_R10(sp)
	sw	a6, 24(sp)		# argument #7 to ksp
	LONGD_L	a5, PT_R9(sp)
	sw	a5, 20(sp)		# argument #6 to ksp
1:
	j	stack_done		# go back
	 nop
	.set	pop

not_n32_scall:
	/* This is not an n32 compatibility syscall, pass it on to
	   the o32 syscall handlers.  */
	j	handle_sys32

	END(handle_sys)

	.macro  sys function, nargs
	PTR	\function
	LONG	(\nargs << 3) - (5 << 3)
	.endm

EXPORT(sysn32_call_table)
	sys	sys_read		3	/* 6000 */
	sys	sys_write		3
	sys	sys_open		3
	sys	sys_close		1
	sys	sys_stat64		2
	sys	sys_fstat64		2	/* 6005 */
	sys	sys_lstat64		2
	sys	sys_poll		3
	sys	syso64_lseek		0
	sys	sysn32_mips_mmap	0
	sys	sys_mprotect		3	/* 6010 */
	sys	sys_munmap		2
	sys	sys_brk			1
	sys	sys_rt_sigaction	4
	sys	sys_rt_sigprocmask	4
	sys	sys_ioctl		3	/* 6015 */
	sys	sysn32_pread64		0
	sys	sysn32_pwrite64		0
	sys	sys_readv		3
	sys	sys_writev		3
	sys	sys_access		2	/* 6020 */
	sys	sysm_pipe		0
	sys	sys_select		5
	sys	sys_sched_yield		0
	sys	sys_mremap		5
	sys	sys_msync		3	/* 6025 */
	sys	sys_mincore		3
	sys	sys_madvise		3
	sys	sys_shmget		3
	sys	sys_shmat		3
	sys	sys_shmctl		3	/* 6030 */
	sys	sys_dup			1
	sys	sys_dup2		2
	sys	sys_pause		0
	sys	sys_nanosleep		2
	sys	sys_getitimer		2	/* 6035 */
	sys	sys_setitimer		3
	sys	sys_alarm		1
	sys	sys_getpid		0
	sys	sys_sendfile		4
	sys	sys_socket		3	/* 6040 */
	sys	sys_connect		3
	sys	sys_accept		3
	sys	sys_sendto		6
	sys	sys_recvfrom		6
	sys	sys_sendmsg		3	/* 6045 */
	sys	sys_recvmsg		3
	sys	sys_shutdown		2
	sys	sys_bind		3
	sys	sys_listen		2
	sys	sys_getsockname		3	/* 6050 */
	sys	sys_getpeername		3
	sys	sys_socketpair		4
	sys	sys_setsockopt		5
	sys	sys_getsockopt		5
	sys	__sys_clone		0	/* 6055 */
	sys	__sys_fork		0
	sys	sys_execve		0
	sys	sys_exit		1
	sys	sys_wait4		4
	sys	sys_kill		2	/* 6060 */
	sys	sys_newuname		1
	sys	sys_semget		3
	sys	sys_semop		3
	sys	sys_semctl		4
	sys	sys_shmdt		1	/* 6065 */
	sys	sys_msgget		2
	sys	sys_msgsnd		4
	sys	sys_msgrcv		5
	sys	sys_msgctl		3
	sys	sys_fcntl		3	/* 6070 */
	sys	sys_flock		2
	sys	sys_fsync		1
	sys	sys_fdatasync		1
	sys	sys_truncate		2
	sys	sys_ftruncate		2	/* 6075 */
	sys	sys_getdents		3
	sys	sys_getcwd		2
	sys	sys_chdir		1
	sys	sys_fchdir		1
	sys	sys_rename		2	/* 6080 */
	sys	sys_mkdir		2
	sys	sys_rmdir		1
	sys	sys_creat		2
	sys	sys_link		2
	sys	sys_unlink		1	/* 6085 */
	sys	sys_symlink		2
	sys	sys_readlink		3
	sys	sys_chmod		2
	sys	sys_fchmod		2
	sys	sys_chown		3	/* 6090 */
	sys	sys_fchown		3
	sys	sys_lchown		3
	sys	sys_umask		1
	sys	sys_gettimeofday	2
	sys	sys_getrlimit		2	/* 6095 */
	sys	sys_getrusage		2
	sys	sys_sysinfo		1
	sys	sys_times		1
	sys	sys_ptrace		4
	sys	sys_getuid		0	/* 6100 */
	sys	sys_syslog		3
	sys	sys_getgid		0
	sys	sys_setuid		1
	sys	sys_setgid		1
	sys	sys_geteuid		0	/* 6105 */
	sys	sys_getegid		0
	sys	sys_setpgid		2
	sys	sys_getppid		0
	sys	sys_getpgrp		0
	sys	sys_setsid		0	/* 6110 */
	sys	sys_setreuid		2
	sys	sys_setregid		2
	sys	sys_getgroups		2
	sys	sys_setgroups		2
	sys	sys_setresuid		3	/* 6115 */
	sys	sys_getresuid		3
	sys	sys_setresgid		3
	sys	sys_getresgid		3
	sys	sys_getpgid		1
	sys	sys_setfsuid		1	/* 6120 */
	sys	sys_setfsgid		1
	sys	sys_getsid		1
	sys	sys_capget		2
	sys	sys_capset		2
	sys	sys_rt_sigpending	2	/* 6125 */
	sys	sys_rt_sigtimedwait	4
	sys	sys_rt_sigqueueinfo	3
	sys	sys_rt_sigsuspend	0
	sys	sys_sigaltstack		0
	sys	sys_utime		2	/* 6130 */
	sys	sys_mknod		3
	sys	sys_personality		1
	sys	sys_ustat		2
	sys	sys_statfs		2
	sys	sys_fstatfs		2	/* 6135 */
	sys	sys_sysfs		3
	sys	sys_getpriority		2
	sys	sys_setpriority		3
	sys	sys_sched_setparam	2
	sys	sys_sched_getparam	2	/* 6140 */
	sys	sys_sched_setscheduler	3
	sys	sys_sched_getscheduler	1
	sys	sys_sched_get_priority_max	1
	sys	sys_sched_get_priority_min	1
	sys	sys_sched_rr_get_interval	2	/* 6145 */
	sys	sys_mlock		2
	sys	sys_munlock		2
	sys	sys_mlockall		1
	sys	sys_munlockall		0
	sys	sys_vhangup		0	/* 6150 */
	sys	sys_pivot_root		2
	sys	sys_sysctl		1
	sys	sys_prctl		5
	sys	sys_adjtimex		1
	sys	sys_setrlimit		2	/* 6155 */
	sys	sys_chroot		1
	sys	sys_sync		0
	sys	sys_acct		1
	sys	sys_settimeofday	2
	sys	sys_mount		5	/* 6160 */
	sys	sys_umount		2
	sys	sys_swapon		2
	sys	sys_swapoff		1
	sys	sys_reboot		3
	sys	sys_sethostname		2	/* 6165 */
	sys	sys_setdomainname	2
	sys	sys_ni_syscall		0	/* was create_module */
	sys	sys_init_module		5
	sys	sys_delete_module	1
	sys	sys_ni_syscall		0	/* 6170, was get_kernel_syms */
	sys	sys_ni_syscall		0	/* was query_module */
	sys	sys_quotactl		4
	sys	sys_ni_syscall		3	/* was nfsservctl */
	sys	sys_ni_syscall		0	/* res. for getpmsg */
	sys	sys_ni_syscall		0	/* 6175  for putpmsg */
	sys	sys_ni_syscall		0	/* res. for afs_syscall */
	sys	sys_ni_syscall		0	/* res. for security */
	sys	sys_gettid		0
	sys	sysn32_readahead	0
	sys	sys_setxattr		5	/* 6180 */
	sys	sys_lsetxattr		5
	sys	sys_fsetxattr		5
	sys	sys_getxattr		4
	sys	sys_lgetxattr		4
	sys	sys_fgetxattr		4	/* 6185 */
	sys	sys_listxattr		3
	sys	sys_llistxattr		3
	sys	sys_flistxattr		3
	sys	sys_removexattr		2
	sys	sys_lremovexattr	2	/* 6190 */
	sys	sys_fremovexattr	2
	sys	sys_tkill		2
	sys	sys_ni_syscall		0
	sys	sys_futex		6
	sys	sys_sched_setaffinity	3	/* 6195 */
	sys	sys_sched_getaffinity	3
	sys	sys_cacheflush		3
	sys	sys_cachectl		3
	sys	sys_sysmips		4
	sys	sys_io_setup		2	/* 6200 */
	sys	sys_io_destroy		1
	sys	sys_io_getevents	5
	sys	sys_io_submit		3
	sys	sys_io_cancel		3
	sys	sys_exit_group		1	/* 6205 */
	sys	sysn32_lookup_dcookie	0
	sys	sys_epoll_create	1
	sys	sys_epoll_ctl		4
	sys	sys_epoll_wait		3
	sys	sys_remap_file_pages	5	/* 6210 TBD: pgoff, is this a file offset. If yes, it can be larger than 32 bit. */
	sys	sys_rt_sigreturn	0
	sys	sys_fcntl64		3
	sys	sys_set_tid_address	1
	sys	sys_restart_syscall	0
	sys	sys_semtimedop		4	/* 6215 */
	sys	sysn32_fadvise64_64	0
	sys	sys_statfs64		3
	sys	sys_fstatfs64		2
	sys	sys_sendfile64		5
	sys	sys_timer_create	3	/* 6220 */
	sys	sys_timer_settime	4
	sys	sys_timer_gettime	2
	sys	sys_timer_getoverrun	1
	sys	sys_timer_delete	1
	sys	sys_clock_settime	2	/* 6225 */
	sys	sys_clock_gettime	2
	sys	sys_clock_getres	2
	sys	sys_clock_nanosleep	4
	sys	sys_tgkill		3
	sys	sys_utimes		2	/* 6230 */
	sys	sys_ni_syscall		0	/* sys_mbind */
	sys	sys_ni_syscall		0	/* sys_get_mempolicy */
	sys	sys_ni_syscall		0	/* sys_set_mempolicy */
	sys	sys_mq_open		4
	sys	sys_mq_unlink		1	/* 6235 */
	sys	sys_mq_timedsend	5
	sys	sys_mq_timedreceive	5
	sys	sys_mq_notify		2
	sys	sys_mq_getsetattr	3
	sys	sys_ni_syscall		0	/* 6240, sys_vserver */
	sys	sys_waitid		5
	sys	sys_ni_syscall		0	/* available, was setaltroot */
	sys	sys_add_key		5
	sys	sys_request_key		4
	sys	sys_keyctl		5	/* 6245 */
	sys	sys_set_thread_area	1
	sys	sys_inotify_init	0
	sys	sys_inotify_add_watch	3
	sys	sys_inotify_rm_watch	2
	sys	sys_migrate_pages	4	/* 6250 */
	sys	sys_openat		4
	sys	sys_mkdirat		3
	sys	sys_mknodat		4
	sys	sys_fchownat		5
	sys	sys_futimesat		3	/* 6255 */
	sys	sys_fstatat64		4
	sys	sys_unlinkat		3
	sys	sys_renameat		4
	sys	sys_linkat		5
	sys	sys_symlinkat		3	/* 6260 */
	sys	sys_readlinkat		4
	sys	sys_fchmodat		3
	sys	sys_faccessat		3
	sys	sys_pselect6		6
	sys	sys_ppoll		5	/* 6265 */
	sys	sys_unshare		1
	sys	sys_splice		6
	sys	sysn32_sync_file_range	0
	sys	sys_tee			4
	sys	sys_vmsplice		4	/* 6270 */
	sys	sys_move_pages		6
	sys	sys_set_robust_list	2
	sys	sys_get_robust_list	3
	sys	sys_kexec_load		4
	sys	sys_getcpu		3	/* 6275 */
	sys	sys_epoll_pwait		6
	sys	sys_ioprio_set		3
	sys	sys_ioprio_get		2
	sys	sys_utimensat		4
	sys	sys_signalfd		3	/* 6280 */
	sys	sys_ni_syscall		0
	sys	sys_eventfd		1
	sys	sysn32_fallocate	0
	sys	sys_timerfd_create	2
	sys	sys_timerfd_gettime	2	/* 6285 */
	sys	sys_timerfd_settime	4
	sys	sys_signalfd4		4
	sys	sys_eventfd2		2
	sys	sys_epoll_create1	1
	sys	sys_dup3		3	/* 6290 */
	sys	sys_pipe2		2
	sys	sys_inotify_init1	1
	sys	sys_preadv		6
	sys	sys_pwritev		6
	sys	sys_rt_tgsigqueueinfo	4	/* 6295 */
	sys	sys_perf_event_open	5
	sys	sys_accept4		4
	sys     sys_recvmmsg		5
	sys     sys_getdents64		3
	.size	sysn32_call_table,.-sysn32_call_table

	.set pop
