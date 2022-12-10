/* file.c: Implementation of memory backed file object (mmaped object). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "threads/mmu.h"
#include "userprog/process.h"

static bool file_backed_swap_in (struct page *page, void *kva);
static bool file_backed_swap_out (struct page *page);
static void file_backed_destroy (struct page *page);

/* DO NOT MODIFY this struct */
static const struct page_operations file_ops = {
	.swap_in = file_backed_swap_in,
	.swap_out = file_backed_swap_out,
	.destroy = file_backed_destroy,
	.type = VM_FILE,
};

/* The initializer of file vm */
void
vm_file_init (void) {
}

/* Initialize the file backed page */
bool
file_backed_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &file_ops;

	struct file_page *file_page = &page->file;
}

/* Swap in the page by read contents from the file. */
static bool
file_backed_swap_in (struct page *page, void *kva) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	struct file *mfile = file_reopen(file);

	void *ori_addr = addr;
	size_t read_byte = length > file_length(file) ? file_length(file) : length;
	size_t zero_byte = PGSIZE - read_byte % PGSIZE;

	while(read_byte > 0 || zero_byte > 0){
		size_t page_read_byte = read_byte < PGSIZE ? read_byte : PGSIZE;
		size_t page_zero_byte = PGSIZE - page_read_byte;

		struct container *container_p = (struct container *)malloc(sizeof(struct container));
		container_p->file = mfile;
		container_p->offset = offset;
		container_p->read_byte = page_read_byte;
		
		if(!vm_alloc_page_with_initializer(VM_FILE, addr, writable, lazy_load_segment, container_p)){
			return NULL;
		}

		read_byte -=page_read_byte;
		zero_byte -=page_zero_byte;
		addr +=PGSIZE;
		offset +=page_read_byte;
	}
	return ori_addr;
}

/* Do the munmap */
void
do_munmap (void *addr) {
	while (true){
		struct page *page_ = spt_find_page(&thread_current()->spt, addr);

		if(page_ == NULL)
			break;

		struct container * aux = (struct container *) page_->uninit.aux;

		if(pml4_is_dirty(thread_current()->pml4, page_->va)){
			file_write_at(aux->file, addr, aux->read_byte, aux->offset);
			pml4_set_dirty(thread_current()->pml4, page_->va, 0);
		}

		pml4_clear_page(thread_current()->pml4, page_->va);
		addr +=PGSIZE;
	}
}
