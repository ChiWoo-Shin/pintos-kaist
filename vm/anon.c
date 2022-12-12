/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "threads/vaddr.h"
#include "devices/disk.h"
#include "kernel/bitmap.h"
#include "threads/mmu.h"

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);

/* project for 3 - start */
struct bitmap *swap_table;
const size_t SECTORS_PER_PAGE = PGSIZE / DISK_SECTOR_SIZE; // 256KB/512B -> 512
/* project for 3 - end */

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
// anonymous page를 위한 data를 초기화함
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1,1); // swap disk를 받아줌

	size_t swap_size = disk_size(swap_disk) / SECTORS_PER_PAGE; // 할당된 swap_disk의 사이즈를 512로 나눔
	swap_table = bitmap_create(swap_size); // swap_size에 맞는 bitmap을 만들어서 swap_table로 반환함
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops; // page->operation 에 anonymous type을 넣어줌

	struct anon_page *anon_page = &page->anon; // page->anon 의 주소를 anon_page로 연결
}

/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon; // anon_page에 page->anon 주소를 연결하고

	int page_no = anon_page->swap_index; // swap_index - 몇번째 swap data랑 바꿀것인지
	if(bitmap_test(swap_table, page_no) == false) // swap_talbe(전역변수) 에서 page_no를 bit로 찾을건데 없으면 false 있으면 해당 index를 return함
		return false;
	
	for ( int i = 0; i < SECTORS_PER_PAGE; i++){
		disk_read(swap_disk, page_no*SECTORS_PER_PAGE+ i, kva + DISK_SECTOR_SIZE* i);
		//swap disk에서 page_no*SECOTRS_PER_PAGE+ i sec를 kva+DISK_SECTOR_SIZE* i 만큼 읽음
	}

	bitmap_set(swap_table, page_no, false);
	// page_no을 index로 swap_table 안에 설정함


	return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;

	/* for project 3 - start */
	int page_no = bitmap_scan(swap_table, 0, 1, false); // swap_table에 value로 설정된 애들중 첫번째 index를 반환
	if(page_no == BITMAP_ERROR)
		return false;

	for (int i = 0; i < SECTORS_PER_PAGE; i++){
		disk_write(swap_disk, page_no*SECTORS_PER_PAGE+ i, page->va + DISK_SECTOR_SIZE* i);
		//swap disk로 page_no*SECOTRS_PER_PAGE+ i sec를 kva+DISK_SECTOR_SIZE* i 만큼 씀
	}

	bitmap_set (swap_table, page_no, true);
	pml4_clear_page(thread_current() -> pml4, page->va);
	anon_page->swap_index = page_no;

	return true;
	/* for project 3 - end */
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
}
