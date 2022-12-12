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

	if (page == NULL)
		return false;
	
	struct container *aux = (struct container *)page->uninit.aux;

	struct file *file = aux->file;
	off_t offset = aux->offset;
	size_t read_byte = aux->read_byte;

	file_seek(file, offset);

	if(file_read(file, kva, read_byte) != (int)read_byte)
		return false;
	
	memset(kva + read_byte, 0, PGSIZE - read_byte);

	return true;
}

/* Swap out the page by writeback contents to the file. */
static bool
file_backed_swap_out (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;

	if (page == NULL)
		return false;
	
	struct container *aux = (struct container *)page->uninit.aux;

	if(pml4_is_dirty(thread_current()->pml4, page->va)){
		file_write_at(aux->file, page->va, aux->read_byte, aux->offset);
		pml4_set_dirty(thread_current()->pml4, page->va, 0);
	}
	pml4_clear_page(thread_current()->pml4, page->va);
}

/* Destory the file backed page. PAGE will be freed by the caller. */
static void
file_backed_destroy (struct page *page) {
	struct file_page *file_page UNUSED = &page->file;
}

/* Do the mmap */
// fd로 열린 파일의 오프셋 바이트부터 length 바이트 만큼을 프로세스의 가상주소공간의 주소 addr 에 매핑 합니다
void *
do_mmap (void *addr, size_t length, int writable, struct file *file, off_t offset) {
	struct file *mfile = file_reopen(file); // 해당 파일의 소유권을 가져와서 새 파일을 반환함 - 매핑에 대해 개별적이고 독립적인 참조를 얻음

	// load_segment와 동일함 -- 하지만 load segment는 read_byte 가 input으로 들어오지만 여기선 들어오지 않음
	// 그에 따라서 실제 사용할 data를 구성해줘야함
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
		struct page *page_ = spt_find_page(&thread_current()->spt, addr); // 해당 address에 맞는 page를 찾음

		if(page_ == NULL) // page가 NULL이면 할 필요가 없으니깐
			break;

		struct container * aux = (struct container *) page_->uninit.aux; // page_의 aux를 형변환 한다 -- 내부 데이터를 다 지울거야

		if(pml4_is_dirty(thread_current()->pml4, page_->va)){ // pml4 의 가상페이지에 page_->va 가 dirty 인 경우 (즉, page_->va 가 설치된 후 페이지가 수정된 경우 true를 반환)
			file_write_at(aux->file, addr, aux->read_byte, aux->offset); // addr에 있는 정보를 aux->offset부터 read_byte만큼 aux->file에 씁니다
			pml4_set_dirty(thread_current()->pml4, page_->va, 0); // pml4 의 가상페이지에 있는 page_->va의 dirty 비트를 dirty 로 설정
		}

		pml4_clear_page(thread_current()->pml4, page_->va); // pml4 에 존재하는 page_->va를 존재하지 않음으로 표기함 --> 추후에 접근하려고 하면 error 가 발생함
		addr +=PGSIZE; // page를 지운 후 addr를 다음 page의 시작지점으로 옮김
	}
}
