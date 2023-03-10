/*
 * Loader Implementation
 *
 * 2018, Operating Systems
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "exec_parser.h"
#include <stdbool.h>

#define SIGSEGV_ERROR 139

typedef struct page {
	void *pageAddress;
	struct page *nextPage;
} Page;

typedef struct list {
	Page *cachedPages;
	int pageSize;
} Loader;

static so_exec_t *exec;
static int exec_decriptor;
static Loader *loader;

// initialize the loader structure
void init(Loader **loader)
{
	*loader = malloc(sizeof(Loader));
	(*loader)->cachedPages = NULL;
	(*loader)->pageSize = getpagesize();
}

// adds a page to the loader mapped pages for the program in execution
void addPage(void *pageAddress, Loader *loader)
{
	Page *newPage = malloc(sizeof(Page));

	newPage->pageAddress = pageAddress;
	newPage->nextPage = loader->cachedPages;
	loader->cachedPages = newPage;
}

// search & find the pageAddress in the loader already mapped pages
bool find(void *pageAddress, Loader *loader)
{
	Page *cachedPage = loader->cachedPages;

	while (cachedPage) {
		// if (((char *)pageAddress - (char *)cachedPage->pageAddress) < loader->pageSize && (pageAddress - cachedPage->pageAddress) > 2)
		if ((pageAddress >= cachedPage->pageAddress) && ((char *) pageAddress - (char *) cachedPage->pageAddress < loader->pageSize))
			return true;
		cachedPage = cachedPage->nextPage;
	}
	return false;
}

//reads into the buffer buf count bytes
ssize_t xread(int fd, void *buf, size_t count)
{
	size_t bytes_read = 0;

	while (bytes_read < count) {
		ssize_t bytes_read_now =
			read(fd, buf + bytes_read,
				 count - bytes_read);

		if (bytes_read_now == 0) /* EOF */
			return bytes_read;

		if (bytes_read_now < 0) /* I/O error */
			return -1;

		bytes_read += bytes_read_now;
	}

	return bytes_read;
}

// copy the instructions into the pages
void copy_into(so_seg_t *segment, size_t  offset, void *pageAddress)
{
	ssize_t pageSize = getpagesize();
	char *buffer = malloc(sizeof(char)*pageSize);

	lseek(exec_decriptor, segment->offset + offset, SEEK_SET);
	if (offset + pageSize <= segment->file_size) {
		xread(exec_decriptor, buffer, pageSize);
		memcpy(pageAddress, buffer, pageSize);
	} else if (offset <= segment->file_size) {
		xread(exec_decriptor, buffer, segment->file_size - offset);
		memset(buffer + segment->file_size - offset, 0, offset + pageSize - segment->file_size);
		memcpy(pageAddress, buffer, pageSize);
	} else if (offset > segment->file_size)
		memset(pageAddress, 0, pageSize);
	free(buffer);
}

//find the segment that caused the segfault
so_seg_t *find_segment_of(void *addr)
{
	long diff;

	for (int i = 0; i < exec->segments_no; i++) {
		diff = (char *)addr - (char *)exec->segments[i].vaddr;
		if (diff <= exec->segments[i].mem_size && diff >= 0)
			return &(exec->segments[i]);
	}
	return NULL;
}

//the definition of the new handler for the SIGSEGV signal
static void signal_handler(int sig, siginfo_t *si, void *unused)
{
	size_t pagesize = getpagesize();
	so_seg_t *segment = find_segment_of(si->si_addr);
	size_t segment_offset = (char *)si->si_addr - (char *)segment->vaddr;
	size_t page_offset = segment_offset % pagesize;

	segment_offset -= page_offset;
	if (!segment) {
		perror("I don't find a segmnt!");
		exit(SIGSEGV_ERROR);
	}
	if (find(si->si_addr, loader)) {
		perror("I found a page!");
		exit(SIGSEGV_ERROR);
	}
	//if (si->si_code == SEGV_ACCERR) {
		//perror("Permission denied!");
		//exit(SIGSEGV_ERROR);
	//}
	//map the addres that generated the SIGSEGV signal
	void *pageAddress = mmap((void *)segment->vaddr + segment_offset, getpagesize(), PERM_R | PERM_W, MAP_FIXED | MAP_SHARED | MAP_ANONYMOUS, -1, 0);

	copy_into(segment, segment_offset, pageAddress);
	addPage(pageAddress, loader);
	mprotect(pageAddress, getpagesize(), segment->perm);
}

//initialize loader structure and assigns the new handler for SIGSEGV
int so_init_loader(void)
{
	struct sigaction sig;

	init(&loader);
	memset(&sig, 0, sizeof(sig));
	sig.sa_flags = SA_SIGINFO;
	sigemptyset(&sig.sa_mask);
	sig.sa_sigaction = signal_handler;
	sigaction(SIGSEGV, &sig, NULL);

	return -1;
}

int so_execute(char *path, char *argv[])
{

	exec_decriptor = open(path, O_RDONLY); //tests for file existance are made in the so_parse_exec
	//if this fails then the so_parse_exec will exit
	exec = so_parse_exec(path);
	if (!exec)
		return -1;

	so_start_exec(exec, argv);

	return -1;
}
