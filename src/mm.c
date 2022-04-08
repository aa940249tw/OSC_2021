#include "mm.h"
#include "uart.h"
#include "devicetree.h"
#include "mbox.h"
#include "utils.h"

struct BUDDY_SYSTEM buddy[MAX_BUDDY_ORDER + 1];
struct PAGE_FRAME *page_frame;
struct MEM_POOL mem_pool[MEMPOOL_TYPE];

void memset_pageframe(struct PAGE_FRAME *buf, int size, int order, USAGE used) {
    for(int i = 0; i < size; i++) {
        buf[i].order = order;
        buf[i].used = used;
    }
}

void buddy_info() {
    printf("Printing Info of Buddy System ~\n");
    for(int i = 0; i <= MAX_BUDDY_ORDER; i++) {
        printf("%2d(%d): ", i, buddy[i].pg_free);
        struct PAGE_LIST *tmp = &(buddy[i].head);
        while(tmp->next != &(buddy[i].head)) {
            printf("%5d{%d} -> ", ((struct PAGE_FRAME *)tmp->next - page_frame), ((struct PAGE_FRAME *)tmp->next)->used);
            tmp = tmp->next;
        }
        printf("    null\n");
    }
}

void insert_pageframe(struct BUDDY_SYSTEM *bd, struct PAGE_LIST *pgl) {
    /* // Debug only
    if(((struct PAGE_FRAME *)pgl)->used == USED || bd->head.next == &(bd->head)) {
        struct PAGE_LIST *last = bd->head.prev;
        pgl->next = &(bd->head);
        bd->head.prev = pgl;
        pgl->prev = last;
        last->next = pgl;
    }
    else if(((struct PAGE_FRAME *)pgl)->used == AVAL) {
        struct PAGE_LIST *start = bd->head.next;
        while((long)start < (long)pgl && ((struct PAGE_FRAME *)start)->used == AVAL) start = start->next;
        pgl->next = start;
        pgl->prev = start->prev;
        start->prev->next = pgl;
        start->prev = pgl;
    }
    if(((struct PAGE_FRAME *)pgl)->used == AVAL) bd->pg_free++;
    */
    struct PAGE_LIST *last = bd->head.prev;
    pgl->next = &(bd->head);
    bd->head.prev = pgl;
    pgl->prev = last;
    last->next = pgl;
    bd->pg_free++;
}

void remove_pageframe(struct BUDDY_SYSTEM *bd, struct PAGE_LIST *pgl) {
    bd->pg_free--;
    pgl->prev->next = pgl->next;
    pgl->next->prev = pgl->prev;
    pgl->prev = NULL;
    pgl->next = NULL;
}

void insert_dpage(struct DYNAMIC_PAGE *d, void *addr) {
    d->free_obj++;
    struct PAGE_LIST *l = d->freelist.next;
    d->freelist.next = (struct PAGE_LIST *)addr;
    d->freelist.next->next = l;
}

void *remove_dpage(struct DYNAMIC_PAGE *d) {
    d->free_obj--;
    struct PAGE_LIST *l = d->freelist.next;
    d->freelist.next = l->next;
    l->next = NULL;
    //printf("dpage: 0x%x\n", d->freelist.next);
    return (void *) l;
}

struct PAGE_FRAME *release_redundant_block(struct PAGE_FRAME *pf, int target_order) {
    if(pf->order > target_order) {
        int prev_order = pf->order - 1;
        struct PAGE_FRAME *second = pf + (1 << prev_order);
        second->used = AVAL;
        second->order = prev_order;
        memset_pageframe(second+1, (1 << prev_order) - 1, prev_order, F);
        insert_pageframe(&(buddy[prev_order]), &(second->list));
        pf->order--;
        return release_redundant_block(pf, target_order);
    }
    else if(pf->order == target_order){
        pf->used = USED;
        memset_pageframe(pf+1, (1 << pf->order) - 1, pf->order, USED);
        //insert_pageframe(&(buddy[pf->order]), &(pf->list));   // Debug Only
        return pf;
    }
    return NULL;
}

struct PAGE_FRAME *get_pageframe(int order, int target_order) {
    struct PAGE_LIST *tmp = buddy[order].head.next;
    while(((struct PAGE_FRAME *)tmp)->used != AVAL) {
        tmp = tmp->next;
    }
    buddy[order].pg_free--;
    tmp->next->prev = tmp->prev;
    tmp->prev->next = tmp->next;
    tmp->next = NULL;
    tmp->prev = NULL;
    return release_redundant_block((struct PAGE_FRAME *)tmp, target_order);
}

void merge_page(struct PAGE_FRAME *p, int page_id) {
    int buddy_page_id = page_id ^ (1 << p->order);
    struct PAGE_FRAME *buddy_page = &page_frame[page_id ^ (1 << p->order)];
    if(buddy_page->used == AVAL && buddy_page->order == p->order && p->order < MAX_BUDDY_ORDER) {
        remove_pageframe(&buddy[buddy_page->order], &(buddy_page->list));
        if(buddy_page - p > 0) {
            p->order++;
            merge_page(p, page_id);
        }
        else {
            buddy_page->order++;
            merge_page(buddy_page, buddy_page_id);
        }
    }
    else {
        p->used = AVAL;
        memset_pageframe(p+1, (1 << p->order) - 1, p->order, F);
        insert_pageframe(&(buddy[p->order]), &(p->list));
    }
}

// No reserve buddy init
void buddy_init() {
    for(int i = 0; i <= MAX_BUDDY_ORDER; i++) {
        buddy[i].head.next = &buddy[i].head;
        buddy[i].head.prev = &buddy[i].head;
        buddy[i].pg_free = 0;
    }
    page_frame = (struct PAGE_FRAME *)simple_malloc((unsigned long)(sizeof(struct PAGE_FRAME) * PAGE_NUM));
    /*  No reserve startup
    page_frame[0].used = AVAL;
    page_frame[0].order = MAX_BUDDY_ORDER;
    for(int i = 1; i < PAGE_NUM; i++) {
        page_frame[i].order = MAX_BUDDY_ORDER;
        page_frame[i].used = F;
        page_frame[i].list.next = NULL;
        page_frame[i].list.prev = NULL;
    }
    insert_pageframe(&(buddy[MAX_BUDDY_ORDER]), &(page_frame[0].list));
    */
    memset_pageframe(page_frame, PAGE_NUM, 0, AVAL);
    for(int i = 0; i < PAGE_NUM; i++) insert_pageframe(&(buddy[0]), &(page_frame[i].list));
    //  debug use
    //  mem_reserve(0x10004000, 11000);
    for(int i = 0; i < PAGE_NUM; i++) {
        if(page_frame[i].order != 0 || page_frame[i].used != AVAL) continue;
        remove_pageframe(&(buddy[0]), &(page_frame[i].list));
        merge_page(&page_frame[i], i);
    }
    //buddy_info();
}

// Reserved buddy init
void dtb_mem_reserve() {
    struct fdt_header **header = (struct fdt_header **)&__devicetree;
    unsigned int totalsize, off_dt_strings, off_dt_struct, size_dt_struct, off_mem_rsvmap;
    totalsize        = convert_big_to_small_endian((*header)->totalsize);
    off_dt_strings   = convert_big_to_small_endian((*header)->off_dt_strings);
    off_dt_struct    = convert_big_to_small_endian((*header)->off_dt_struct);
    size_dt_struct   = convert_big_to_small_endian((*header)->size_dt_struct);
    off_mem_rsvmap   = __builtin_bswap32((*header)->off_mem_rsvmap);

    // Get available memory block, insert to buddy system 
    unsigned long struct_address = (unsigned long)*header + (unsigned long) off_dt_struct;
    unsigned long struct_end = struct_address + (unsigned long) size_dt_struct;
    unsigned char *cur_address = (unsigned char *)struct_address;
    int found_mem_device = 0;
    unsigned long start_addr, addr_size;
    while((unsigned long)cur_address < struct_end) {
        if(found_mem_device == 2) break;
        unsigned int token = convert_big_to_small_endian(*((unsigned int *)cur_address));
        cur_address += 4;
        switch(token) {
            case FDT_BEGIN_NODE:
                if(!strncmp((char *)cur_address, "memory", 6)) found_mem_device = 1;
                cur_address += strlen((char *)cur_address);
                if((unsigned long)cur_address % 4) cur_address +=  (4 - (unsigned long)cur_address % 4); 
                break;
            case FDT_END_NODE:
                break;
            case FDT_PROP: 
                ;
                unsigned int len, nameoff;
                len = convert_big_to_small_endian(*((unsigned int *)cur_address));
                cur_address += 4;
                nameoff = convert_big_to_small_endian(*((unsigned int *)cur_address));
                cur_address += 4;
                if(len > 0) {
                    char *tmp = (char *)((unsigned long)*header + (unsigned long) off_dt_strings + nameoff);
                    if(found_mem_device && !strncmp(tmp, "reg", 3)) {
                        for(int i = 0; i < len; i += 8) {
                            start_addr =  __builtin_bswap32(*((unsigned long *)cur_address));
                            addr_size =  __builtin_bswap32(*((unsigned long *)(cur_address + 4)));
                            printf("0x%x 0x%x\n", start_addr, addr_size);
                            // Too Lazy to implement, just insert every available framepage into buddy system
                        }
                        found_mem_device = 2;
                    }
                    cur_address += len;
                    if((unsigned long)cur_address % 4) cur_address +=  (4 - (unsigned long)cur_address % 4);
                }
                break;
            case FDT_NOP:
                break;
            case FDT_END:
                break;
            default: break;
        }
    }

    // Check and reserve mem-reserve block
    struct_address = (unsigned long)*header + (unsigned long) off_mem_rsvmap;
    cur_address = (unsigned char *)struct_address;
    while(1) {
        unsigned long long address = __builtin_bswap64(*((unsigned long long *)cur_address));
        cur_address += 8;
        unsigned long long size = __builtin_bswap64(*((unsigned long long *)cur_address));
        cur_address += 8;
        printf("Address: %d\nSize: %d\n", address, size);
        mem_reserve((unsigned long)address, (int)size);
        if(address == 0 && size == 0) break;
    }
    // Reserve dtb
    mem_reserve((unsigned long)(*header), totalsize);
}

void __buddy_init() {
    for(int i = 0; i <= MAX_BUDDY_ORDER; i++) {
        buddy[i].head.next = &buddy[i].head;
        buddy[i].head.prev = &buddy[i].head;
        buddy[i].pg_free = 0;
    }
    // Get total memory from mailbox
    unsigned int __PAGE_NUM = get_address() / PAGE_SIZE;
    printf("%d\n", __PAGE_NUM);
    page_frame = (struct PAGE_FRAME *)simple_malloc((unsigned long)(sizeof(struct PAGE_FRAME) * __PAGE_NUM));
    // Lazy implement
    memset_pageframe(page_frame, __PAGE_NUM, 0, AVAL);
    for(int i = 0; i < __PAGE_NUM; i++) insert_pageframe(&(buddy[0]), &(page_frame[i].list));
    // Check available memory block, reserve dtb, reserve mem-reserve section
    dtb_mem_reserve();
    // reserve CPIO
    unsigned long cpio_start = get_initramfs("linux,initrd-start");
    unsigned long cpio_end = get_initramfs("linux,initrd-end");
    mem_reserve(cpio_start, cpio_end - cpio_start);
    // reserve kernel img
    extern unsigned long __start, __end;
    mem_reserve((unsigned long)&__start, (&__end - &__start));
    // reserve simple allocator
    mem_reserve(allocator_init, PAGE_SIZE);
    // check if any framepage can be merged, not O(n) method
    for(int i = 0; i < __PAGE_NUM; i++) {
        if(page_frame[i].order != 0 || page_frame[i].used != AVAL) continue;
        remove_pageframe(&(buddy[0]), &(page_frame[i].list));
        merge_page(&page_frame[i], i);
    }
    //buddy_info();
}

void dynamic_init() {
    for(int i = 0; i < MEMPOOL_TYPE; i++) {
        mem_pool[i].size = (1 << i);
        for(int j = 0; j < MAX_DYNAMIC_PAGE; j++) {
            mem_pool[i].d_page[j].used = 0;
            mem_pool[i].d_page[j].free_obj = 0;
            mem_pool[i].d_page[j].page_addr = 0x0;
            mem_pool[i].d_page[j].freelist.next = &(mem_pool[i].d_page[j].freelist);
            mem_pool[i].d_page[j].freelist.prev = &(mem_pool[i].d_page[j].freelist);
        }
    }
}

void *dynamic_allocator(int order) {
    struct MEM_POOL *m = &mem_pool[order];
    int i = 0;
    while(m->d_page[i].used == 1 && m->d_page[i].free_obj == 0 && i < MAX_DYNAMIC_PAGE) i++;
    if(i == MAX_DYNAMIC_PAGE) {
        printf("Unable to request more memory !!!\n");
        return NULL;
    }
    struct DYNAMIC_PAGE *d = &(m->d_page[i]);
    if(d->used == 0) {
        d->page_addr = (unsigned long)alloc_pages(0);
        if(d->page_addr == NULL) return NULL;
        d->used = 1;
        d->free_obj = PAGE_SIZE / m->size;
        struct PAGE_LIST *l = &(d->freelist);
        for(int j = 0; j < d->free_obj; j++) {
            l->next = (struct PAGE_LIST *)(d->page_addr + j * m->size);
            l = l->next;
        }
        l->next = &(d->freelist);
    }
    return remove_dpage(d);
}

void dynamic_free(struct DYNAMIC_PAGE *d, void *addr) {
    insert_dpage(d, addr);
}

void mm_init() {
    buddy_init();
    dynamic_init();
}

void *alloc_pages(int order) {
    if(order > MAX_BUDDY_ORDER) {
        printf("Requesting memory larger than max size !!!\n");
        return NULL;
    }
    for(int i = order; i <= MAX_BUDDY_ORDER; i++) {
        if(buddy[i].pg_free > 0) {
            struct PAGE_FRAME *tmp = get_pageframe(i, order);
            //buddy_info();
            return (void *)(PAGE_INIT + (tmp - page_frame)*PAGE_SIZE);
        }
    }
    printf("Can't find memory to allocate.\n");
    return NULL;
}

void free_pages(void *addr) {
    int page_id = ((unsigned long)addr - (unsigned long)PAGE_INIT) / PAGE_SIZE;
    struct PAGE_FRAME *p = &page_frame[page_id];
    p->used = AVAL;
    /*  For Debug Only
    ((struct PAGE_LIST *)p)->prev->next = ((struct PAGE_LIST *)p)->next;
    ((struct PAGE_LIST *)p)->next->prev = ((struct PAGE_LIST *)p)->prev;
    ((struct PAGE_LIST *)p)->prev = NULL;
    ((struct PAGE_LIST *)p)->next = NULL;
    */
    memset_pageframe(p+1, (1 << p->order) - 1, p->order, F);
    merge_page(p, page_id);
    //buddy_info();
}

void *kmalloc(int size) {
    int order = 0;
    if(size > (1 << (MEMPOOL_TYPE - 1))) {
        //printf("Using Buddy System.\n");
        for (int i = 0; i <= MAX_BUDDY_ORDER; i++) {
            if (size <= ((1 << i) * PAGE_SIZE)) {
                order = i;
                break;
            }
        }
        return alloc_pages(order);
    }
    else {
        //printf("Using Dynamic System.\n");
        for (int i = 3; i < MEMPOOL_TYPE; i++) {
            if (size <= (1 << i)) {
                order = i;
                break;
            }
        }
        return dynamic_allocator(order);
    }
}

void kfree(void *addr) {
    for(int i = 3; i < MEMPOOL_TYPE; i++) {
        for(int j = 0; j < MAX_DYNAMIC_PAGE; j++) {
            struct DYNAMIC_PAGE *d = &(mem_pool[i].d_page[j]);
            if(d->used && (unsigned long)(addr - d->page_addr) < PAGE_SIZE) {
                //printf("Free Dynamic.\n");
                dynamic_free(d, addr);
                return;
            }
        }
    }
    //printf("Free Buddy.\n");
    free_pages(addr);
}

void mem_reserve(unsigned long addr, int size) {
    int page_id = ((unsigned long)addr - (unsigned long)PAGE_INIT) / PAGE_SIZE;
    if(page_id < 0) return;
    for(int i = page_id; (PAGE_INIT + i * PAGE_SIZE) < addr + size; i++) {
        page_frame[i].used = USED;
        remove_pageframe(&(buddy[page_frame[i].order]), (struct PAGE_LIST *)&page_frame[i]);
    }
}

void buddy_test() {
    void *p = kmalloc(8);
    void *p1 = kmalloc(8);
    void *q = kmalloc(520);
    void *a = kmalloc(4096);
    void *b = kmalloc(16684);
    void *c = kmalloc(3200);

    kfree(b);
    kfree(p);
    kfree(a);
    kfree(p1);
    kfree(q);
    kfree(c);
}
