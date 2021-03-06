// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>
#include <inc/error.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display the stack trace", mon_backtrace },
	{ "alloc_page", "Allocate a page in the memory", mon_alloc_page },
	{ "page_status", "Show status of a page with given physical address", mon_page_status },
	{ "free_page", "Free a page with given physical address", mon_free_page },
	
};
#define NCOMMANDS (sizeof(commands)/sizeof(commands[0]))

unsigned read_eip();

/***** Implementations of basic kernel monitor commands *****/

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < NCOMMANDS; i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start %08x (virt)  %08x (phys)\n", _start, _start - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		(end-_start+1023)/1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	unsigned int i; // dummy
	unsigned int ebp = read_ebp();
	unsigned int eip = read_eip();
	struct Eipdebuginfo eip_info;
	cprintf("Stack backtrace:\n");
	while (ebp != 0) {
		eip = *(unsigned int *) (ebp + 4);
		cprintf("  ebp %x eip %x  args", ebp, eip);
		for (i = 0; i < 5; ++i) // arguments, 1st through 5th
			cprintf(" %08x", *(unsigned int *) (ebp + 8 + 4 * i));
		cprintf("\n");

		// source file, line number, function name, etc.
		debuginfo_eip(eip, &eip_info);
		cprintf("    %s:%u: %.*s+%u\n",
			eip_info.eip_file,
			eip_info.eip_line,
			eip_info.eip_fn_namelen,
			eip_info.eip_fn_name,
			eip - eip_info.eip_fn_addr);

		// get caller ebp
		ebp = *(unsigned int *) ebp;
	}

	return 0;
}

int
mon_alloc_page(int argc, char **argv, struct Trapframe *tf)
{
	struct Page *pp;
	if (page_alloc(&pp) == -E_NO_MEM) {
		cprintf("mon_alloc_page: No memory available.\n");
		return 1;
	}
	cprintf("\t%p\n", page2pa(pp));

	return 0;
}

int
mon_page_status(int argc, char **argv, struct Trapframe *tf)
{
	void *addr;
	if (argc < 2) {
		// need to provide physical address
		cprintf("Please provide the physical address in hex format.\n");
		return 1;
	}
	addr = (void *) strtol(argv[1], NULL, 16);
	if (page_lookup(boot_pgdir, KADDR((uint32_t) addr), NULL) != NULL)
		cprintf("\tassigned\n");
	else
		cprintf("\tfree\n");

	return 0;
}

int
mon_free_page(int argc, char **argv, struct Trapframe *tf)
{
	void *addr;
	if (argc < 2) {
		// need to provide physical address
		cprintf("Please provide the physical address in hex format.\n");
		return 1;
	}
	addr = (void *) strtol(argv[1], NULL, 16);
	page_remove(boot_pgdir, KADDR((uint32_t) addr));

	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < NCOMMANDS; i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}

// return EIP of caller.
// does not work if inlined.
// putting at the end of the file seems to prevent inlining.
unsigned
read_eip()
{
	uint32_t callerpc;
	__asm __volatile("movl 4(%%ebp), %0" : "=r" (callerpc));
	return callerpc;
}
