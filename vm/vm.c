/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/process.h"
#include "userprog/syscall.h"
#include "filesys/file.h"
#include "threads/vaddr.h"

/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */

struct list frame_table;
struct list_elem *start;

void
vm_init (void) {
  vm_anon_init ();
  vm_file_init ();
#ifdef EFILESYS /* For project 4 */
  pagecache_init ();
#endif
  register_inspect_intr ();

  list_init (&frame_table);
  /* DO NOT MODIFY UPPER LINES. */
  /* TODO: Your code goes here. */
}

/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
  int ty = VM_TYPE (page->operations->type);
  switch (ty) {
  case VM_UNINIT:
    return VM_TYPE (page->uninit.type);
  default:
    return ty;
  }
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
// Initializer를 사용하여 보류 중인 페이지 개체를 만듦.
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable, vm_initializer *init, void *aux) {
  ASSERT (VM_TYPE (type) != VM_UNINIT)

  struct supplemental_page_table *spt = &thread_current ()->spt;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page (spt, upage) == NULL) { // 만일 page가 spt안에 이미 존재하고 있는지 아닌지 확인
    /* TODO: Create the page, fetch the initialier according to the VM type,
     * TODO: and then create "uninit" page struct by calling uninit_new. You
     * TODO: should modify the field after calling the uninit_new. */
    struct page *page = (struct page *) malloc (sizeof (struct page)); // page가 없기 때문에 새로 하나 열어주고
    typedef bool (*initializer) (struct page *, enum vm_type, void *); // initializer 함수를 만들어서 initializer 정보를 받아줌

    initializer initializer_vm = NULL; // initializer 함수에 initializer_vm 으로 명칭을 선언한 후 NULL로 선언해줌

    switch (VM_TYPE (type)) { // input 받은 type에 따라서 anon, file 에 따라서 초기화함
    case VM_ANON:
      initializer_vm = anon_initializer;
      break;
    case VM_FILE:
      initializer_vm = file_backed_initializer;
      break;
    default:
      PANIC ("vm initial fail");
      break;
    }

    uninit_new (page, upage, init, type, aux, initializer_vm);  // 그 이후 uninit_new 를 사용하여 uninit 페이지 구조를 생성함

    page->writable = writable; // 할당받은 page의 writable에 input 된 writable 정보를 넣어줌

    /* TODO: Insert the page into the spt. */
    return spt_insert_page (spt, page); // 새로만들어진 page를 spt 에 insert를 해줌

  }
err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
  // struct page *page = NULL;
  /* TODO: Fill this function. */

  struct page *page = (struct page *) malloc (sizeof (struct page)); // va 와 일치하는 page 정보를 찾기 위해서 임시적인 공간을 만들어줌
  struct hash_elem *e; // hash_find를 통해서 찾은 hash_elem을 저장하기 위한 공간
  page->va = pg_round_down (va); // pg_round_down() 함수를 통해서 page의 시작주소로 변경을 해준 후 위에서 만든 임시공간의 va와 연결시켜줌
  e = hash_find (&spt->pages, &page->elem_hash); // 입력받은 spt hash에서 일치하는 page를 찾고 --> 해당 page의 hash_elem을 e로 return 한다
  free (page); // page의 역할을 다 끝났으니 free 해주고

  return e != NULL ? hash_entry (e, struct page, elem_hash) : NULL; // e가 NULL이 아니면 e를 page로 확장해서 page를 return하고 NULL이면 NULL 을 return 함
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
                 struct page *page UNUSED) {
  // int succ = false;
  /* TODO: Fill this function. */

  return insert_page (&spt->pages, page); // hash와 page를 이용해서 hash에 page를 insert 한다.
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
  vm_dealloc_page (page);
  return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
  struct frame *victim = NULL;
  /* TODO: The policy for eviction is up to you. */

  struct thread *cur = thread_current (); // 현재 thread를 불러오고
  struct list_elem *s; // list 순회를 위해서 필요한 list_elem 선언

  for (s = list_begin (&frame_table); s != list_end (&frame_table); s = list_next (s)) { // frame은 전역변수로 선언이 되어있음
    victim = list_entry (s, struct frame, elem_fr); // frame으로 확장
    if (pml4_is_accessed (cur->pml4, victim->page->va)) // PML4에 VPAGE용 PTE가 있는지 없는지 확인함 -> 즉 pml4에 해당 page가 있는지 없는지 찾는 부분 --> 있으면 true, 없으면 false
      pml4_set_accessed (cur->pml4, victim->page->va, 0); // 만일 있었다면 해당 access를 0으로 바꿔줌
    else
      return victim; 
  }

  return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
  struct frame *victim = vm_get_victim (); // vm_get_victim() --> 제거될 frame을 가져옴
  /* TODO: swap out the victim and return the evicted frame. */
  swap_out (victim->page); // 제거될 frame을 disk에 복사함

  return victim; // 제거될 frame을 return 함
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
//모든 유저 공간 페이지들은 이 함수를 통해서 할당될 것임
static struct frame *
vm_get_frame (void) { // palloc으로 page를 얻고 frame을 가져옴
  struct frame *frame = (struct frame *) malloc (sizeof (struct frame)); // frame 공간을 할당 받고
  /* TODO: Fill this function. */

  frame->kva = palloc_get_page (PAL_USER); // frame의 물리메모리 주소에 PAL_USER로 page를 할당 받아서 넣는다 -- Gitbook 에 PAL_USER로 할당 받아야한다는게 있음
  if (frame->kva == NULL) { // 만약 할당에 실패했다면
    frame = vm_evict_frame (); // 제거될 frame을 return 받아서 frame에 저장해줌
    frame->page = NULL; // page는 NULL로 하고
    return frame; // 그 frame을 반환함
  }
  list_push_back (&frame_table, &frame->elem_fr); // 위 if문에 걸리지 않았다면 frame table에 현재 frame을 넣어주고
  frame->page = NULL; // page는 NULL로 함 -- frame 내의 page에는 아직 할당된게 없으니깐

  ASSERT (frame != NULL);
  ASSERT (frame->page == NULL);

  return frame; // 해당 frame을 return 함
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
  if (vm_alloc_page (VM_ANON | VM_MARKER_0, addr, 1)) {
    vm_claim_page (addr);
    thread_current ()->stack_bottom -= PGSIZE;
  }
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
                     bool user UNUSED, bool write UNUSED,
                     bool not_present UNUSED) {
  struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
  struct page *page = NULL;
  /* TODO: Validate the fault */
  /* TODO: Your code goes here */
  if (is_kernel_vaddr (addr))
    return false;

  void *rsp_stack =
      is_kernel_vaddr (f->rsp) ? thread_current ()->rsp_stack : f->rsp;
  if (not_present) {
    if (!vm_claim_page (addr)) {
      if (rsp_stack - 8 <= addr && USER_STACK - 0x100000 <= addr &&
          addr <= USER_STACK) {
        vm_stack_growth (thread_current ()->stack_bottom - PGSIZE);
        return true;
      }
      return false;
    } else
      return true;
  }

  return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
  destroy (page);
  free (page);
}

/* Claim the page that allocate on VA. */
// 주어진 va에 page를 할당함
bool
vm_claim_page (void *va UNUSED) {
  struct page *page = NULL;
  /* TODO: Fill this function */
  page = spt_find_page (&thread_current ()->spt, va); // spt에 va와 일치하는 page를 찾아옴
  
  if (page == NULL) { // page가 NULL이면 va와 일치하는 page가 없기 때문에
    return false; // false를 return 하고
  }

  return vm_do_claim_page (page); // 일치하는 page를 찾았다면 frame 도 할당함
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
  struct frame *frame = vm_get_frame ();

  /* Set links */
  frame->page = page; // frame과 page를 이어줌
  page->frame = frame;

  /* TODO: Insert page table entry to map page's VA to frame's PA. */

  if (install_page (page->va, frame->kva, page->writable)) // page의 가상메모리와 frame의 물리메모리를 매핑해주고 page의 writable 정보도 같이 써줌
    return swap_in (page, frame->kva); // 해당 매핑이 성공한다면 해당 페이지를 물리메모리로 swap in 해줌

  return false; // 실패했다면 false를 return 함
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
  hash_init (&spt->pages, page_hash, page_cmp_less, NULL);
}

/* Copy supplemental page table from src to dst */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
                              struct supplemental_page_table *src UNUSED) {

  struct hash_iterator i;

  hash_first (&i, &src->pages);
  while (hash_next (&i)) {
    struct page *parent_page =
        hash_entry (hash_cur (&i), struct page, elem_hash);
    enum vm_type type = page_get_type (parent_page);
    void *upage = parent_page->va;
    bool writable = parent_page->writable;

    vm_initializer *init = parent_page->uninit.init;

    struct container *child_aux =
        (struct container *) malloc (sizeof (struct container));

    struct container *aux = (struct container *) parent_page->uninit.aux;

    if (child_aux == NULL)
      return false;
    if (aux != NULL) {
      child_aux->file = aux->file;
      child_aux->offset = aux->offset;
      child_aux->read_byte = aux->read_byte;

      // void *aux = parent_page->uninit.aux;
    }
    if (parent_page->operations->type == VM_UNINIT) {
      ASSERT (child_aux != NULL);
      if (!vm_alloc_page_with_initializer (type, upage, writable, init, (void *) child_aux))
        return false;
    } else if (parent_page->operations->type != VM_UNINIT) {
        vm_alloc_page (type, upage, writable);
        struct page *child_page = spt_find_page (&thread_current ()->spt, upage);
        vm_claim_page (child_page->va);
        // struct page *child_page = vm_claim_page(dst, upage);
        if (child_page == NULL)
          return false;
      memcpy (child_page->frame->kva, parent_page->frame->kva, PGSIZE);
    }

    // if (parent_page->uninit.type & VM_MARKER_0) {
    //     setup_stack (&thread_current ()->tf);
    //   } else if (parent_page->operations->type == VM_UNINIT) {
    //     if (!vm_alloc_page_with_initializer (type, upage, writable, init,
    //                                          (void *) child_aux))
    //       return false;
    //   } else {
    //     if (!vm_alloc_page (type, upage, writable))
    //       return false;
    //     if (!vm_claim_page (upage))
    //       return false;
    //   }

    //   if (parent_page->operations->type != VM_UNINIT) {
    //     struct page *child_page = spt_find_page (dst, upage);
    //     if (child_page == NULL)
    //       return false;
    //     memcpy (child_page->frame->kva, parent_page->frame->kva, PGSIZE);
    //   }
  }
  return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */

  struct hash_iterator i;
  hash_first (&i, &spt->pages);
  while (hash_next (&i)) {
    struct page *page = hash_entry (hash_cur (&i), struct page, elem_hash);

    if (page->operations->type == VM_FILE)
      do_munmap (page->va);

    // free(page);
  }
  // hash_destroy(&spt->pages, spt_des);
  hash_clear (&spt->pages, spt_des);
}

unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
  const struct page *p = hash_entry (p_, struct page, elem_hash);
  return hash_bytes (&p->va, sizeof (p->va));
}

bool
page_cmp_less (const struct hash_elem *a_, const struct hash_elem *b_,
               void *aux UNUSED) {
  const struct page *a = hash_entry (a_, struct page, elem_hash);
  const struct page *b = hash_entry (b_, struct page, elem_hash);

  return a->va < b->va;
}

bool
insert_page (struct hash *pages, struct page *p) {
  if (hash_insert (pages, &p->elem_hash) == NULL) { // pages(hash)에 page의 hash_elem을 넣어서 연결해줌 연결에 성공하면 NULL이 return 됨
    return true; // 연결에 성공했으면 true를 return 하고
  } else {
    return false; // 실패하면 false를 return 함
  }
}

bool
delete_page (struct hash *pages, struct page *p) {
  if (!hash_delete (pages, &p->elem_hash))
    return true;
  else
    return false;
}

void
spt_des (struct hash_elem *e, void *aux) {
  const struct page *p = hash_entry (e, struct page, elem_hash);
  // vm_dealloc_page (p);
  free (p);
}

void
printf_hash (struct supplemental_page_table *spt) {
  struct hash *h = &spt->pages;
  struct hash_iterator i;
  hash_first (&i, h);
  printf ("===== hash 순회시작 =====\n");
  while (hash_next (&i)) {
    struct page *p = hash_entry (hash_cur (&i), struct page, elem_hash);
    if (p->frame == NULL) {
      printf ("va: %X, writable : %X\n", p->va, p->writable);
    } else {
      printf ("va: %X, kva : %X, writable : %X\n", p->va, p->frame->kva,
              p->writable);
    }
  }
  printf ("===== hash 순회종료 =====\n");
}