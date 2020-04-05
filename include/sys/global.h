
/*++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                            global.h
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
                                                    Forrest Yu, 2005
++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++*/

/* EXTERN is defined as extern except in global.c */
#ifdef	GLOBAL_VARIABLES_HERE
#undef	EXTERN
#define	EXTERN
#endif

EXTERN	int	ticks;

EXTERN	int	disp_pos;

EXTERN	u8			gdt_ptr[6];	/* 0~15:Limit  16~47:Base */
EXTERN	struct descriptor	gdt[GDT_SIZE];
EXTERN	u8			idt_ptr[6];	/* 0~15:Limit  16~47:Base */
EXTERN	struct gate		idt[IDT_SIZE];

EXTERN	u32	k_reenter;
EXTERN	int	current_console;

EXTERN	int	key_pressed; /**
			      * used for clock_handler
			      * to wake up TASK_TTY when
			      * a key is pressed
			      */

EXTERN	struct tss	tss;
EXTERN	struct proc*	p_proc_ready;

extern	char		task_stack[];
extern	struct proc	proc_table[];
extern  struct task	task_table[];
extern  struct task	user_proc_table[];
extern	irq_handler	irq_table[];
extern	TTY		tty_table[];
extern  CONSOLE		console_table[];

/* FS */
extern	u8 *			fsbuf;
extern	const int		FSBUF_SIZE;
extern	struct dev_drv_map	dd_map[];
EXTERN  struct fat16_t fat16;
EXTERN	struct file_desc	f_desc_table[NR_FILE_DESC];
extern	const int		FSBUF_SIZE;
EXTERN	MESSAGE			fs_msg;
EXTERN	struct proc *		pcaller;

