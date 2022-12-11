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
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
                                vm_initializer *init, void *aux) {
  ASSERT (VM_TYPE (type) != VM_UNINIT)

  struct supplemental_page_table *spt = &thread_current ()->spt;

  /* Check wheter the upage is already occupied or not. */
  if (spt_find_page (spt, upage) == NULL) {
    /* TODO: Create the page, fetch the initialier according to the VM type,
     * TODO: and then create "uninit" page struct by calling uninit_new. You
     * TODO: should modify the field after calling the uninit_new. */
    struct page *page = (struct page *) malloc (sizeof (struct page));
    typedef bool (*initializer) (struct page *, enum vm_type, void *);

    initializer initializer_vm = NULL;

    switch (VM_TYPE (type)) {
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

    uninit_new (page, upage, init, type, aux, initializer_vm);

    page->writable = writable;

    return spt_insert_page (spt, page);

    /* TODO: Insert the page into the spt. */
  }
err:
  return false;
}

/* Find VA from spt and return page. On error, return NULL. */
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
  // struct page *page = NULL;
  /* TODO: Fill this function. */

  struct page *page = (struct page *) malloc (sizeof (struct page));
  struct hash_elem *e;

  page->va = pg_round_down (va);
  e = hash_find (&spt->pages, &page->elem_hash);
  free (page);

  return e != NULL ? hash_entry (e, struct page, elem_hash) : NULL;
}

/* Insert PAGE into spt with validation. */
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED,
                 struct page *page UNUSED) {
  int succ = false;
  /* TODO: Fill this function. */

  return insert_page (&spt->pages, page);
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

  struct thread *cur = thread_current ();
  struct list_elem *s;

  for (s = list_begin (&frame_table); s != list_end (&frame_table);
       s = list_next (s)) {
    victim = list_entry (s, struct frame, elem_fr);
    if (pml4_is_accessed (cur->pml4, victim->page->va))
      pml4_set_accessed (cur->pml4, victim->page->va, 0);
    else
      return victim;
  }

  return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
  struct frame *victim = vm_get_victim ();
  /* TODO: swap out the victim and return the evicted frame. */
  swap_out (victim->page);

  return victim;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
  struct frame *frame = (struct frame *) malloc (sizeof (struct frame));
  /* TODO: Fill this function. */

  frame->kva = palloc_get_page (PAL_USER);
  if (frame->kva == NULL) {
    frame = vm_evict_frame ();
    frame->page = NULL;
    return frame;
  }
  list_push_back (&frame_table, &frame->elem_fr);
  frame->page = NULL;

  ASSERT (frame != NULL);
  ASSERT (frame->page == NULL);

  return frame;
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
bool
vm_claim_page (void *va UNUSED) {
  struct page *page = NULL;
  /* TODO: Fill this function */
  page = spt_find_page (&thread_current ()->spt, va);

  if (page == NULL) {
    return false;
  }

  return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
  struct frame *frame = vm_get_frame ();

  /* Set links */
  frame->page = page;
  page->frame = frame;

  /* TODO: Insert page table entry to map page's VA to frame's PA. */

  if (install_page (page->va, frame->kva, page->writable))
    return swap_in (page, frame->kva);

  return false;
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
    // if (parent_page->operations->type == VM_UNINIT) {
	// 	ASSERT(child_aux != NULL);
    //   if (!vm_alloc_page_with_initializer (type, upage, writable, init,
    //                                        (void *) child_aux))
    //     return false;
    // }
    // else if (parent_page->operations->type != VM_UNINIT) {
	// 	vm_alloc_page(type, upage, writable);
	// 	struct page *child_page = spt_find_page(&thread_current()->spt, upage);
	// 	vm_claim_page(child_page->va);
	// 	// struct page *child_page = vm_claim_page(dst, upage);
	// 	if (child_page == NULL)
	// 		return false;	
	// 	memcpy (child_page->frame->kva, parent_page->frame->kva, PGSIZE);
    // }


	if (parent_page->uninit.type & VM_MARKER_0) {
      setup_stack (&thread_current ()->tf);
    } else if (parent_page->operations->type == VM_UNINIT) {
      if (!vm_alloc_page_with_initializer (type, upage, writable, init,
                                           (void *) child_aux))
        return false;
    } else {
      if (!vm_alloc_page (type, upage, writable))
        return false;
      if (!vm_claim_page (upage))
        return false;
    }

    if (parent_page->operations->type != VM_UNINIT) {
      struct page *child_page = spt_find_page (dst, upage);
      if (child_page == NULL)
        return false;
      memcpy (child_page->frame->kva, parent_page->frame->kva, PGSIZE);
    }
  }
  return true;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
  /* TODO: Destroy all the supplemental_page_table hold by thread and
   * TODO: writeback all the modified contents to the storage. */


  struct hash_iterator i;
  hash_first(&i, &spt->pages);
  while(hash_next(&i)){
  	struct page *page = hash_entry( hash_cur(&i), struct page, elem_hash);

  	if (page->operations->type == VM_FILE)
  		do_munmap(page->va);
  
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
  if (hash_insert (pages, &p->elem_hash) == NULL) {
    return true;

  } else {
    return false;
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
  free(p);
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