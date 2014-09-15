#include "vmm.hpp"
#include "locks.hpp"

const size_t VMM_ENTRIES_PER_TABLE=1024;
const size_t VMM_KERNEL_PAGES=VMM_KERNELSPACE_END/VMM_PAGE_SIZE;
const size_t VMM_KERNEL_TABLES=VMM_KERNEL_PAGES/VMM_ENTRIES_PER_TABLE;
const size_t VMM_MAX_PAGES=VMM_ENTRIES_PER_TABLE * VMM_ENTRIES_PER_TABLE;
const size_t VMM_MAX_RAM=VMM_MAX_PAGES*VMM_PAGE_SIZE;
const uint32_t VMM_ADDRESS_MASK=0xFFFFF000;
const uint32_t VMM_FLAGS_MASK=0x00000E00;

#define PAGING_ENABLED_FLAG 0x80000000

namespace PageFlags{
	enum{
		Present 		= 1 << 0,
		Writable 		= 1 << 1,
		Usermode 		= 1 << 2,
		WriteThrough 	= 1 << 3,
		NoCache			= 1 << 4,
		Accessed		= 1 << 5,
		Dirty			= 1 << 6,
		Global			= 1 << 7,
	};
}

namespace TableFlags{
	enum{
		Present 		= 1 << 0,
		Writable 		= 1 << 1,
		Usermode 		= 1 << 2,
		WriteThrough 	= 1 << 3,
		NoCache			= 1 << 4,
		Accessed		= 1 << 5,
		LargePages		= 1 << 6,
    };
}

extern char _start, _end;
lock vmm_lock;

struct vmm_region{
	void *base;
	size_t pages;
};

void vmm_refresh_addr(uint32_t pageaddr);
bool is_paging_enabled();

class vmm_pagestack{
private:
    uint32_t *bottom;
    uint32_t *top;
    uint32_t *limit;

public:
    void init(uint32_t *ptr, size_t size){
        bottom=top=ptr;
        limit=(uint32_t*)((size_t)ptr+size);
        memset(ptr, 0, size);
    }

    void push(uint32_t page){
        if(top+1 < limit){
            *(top)=page;
            top++;
        }
    }

    uint32_t pop(){
        if(top-1 >= bottom){
        	uint32_t ret=*(top-1);
        	*(top-1)=0;
        	top--;
            return ret;
        } else return 0;
    }

    uint32_t pop(size_t i){
        uint32_t ret=bottom[i];
        memmove(&bottom[i], &bottom[i+1], size()-i);
        top--;
        return ret;
    }

    size_t size(){
        return top-bottom;
    }

    uint32_t at(size_t i){
        return bottom[i];
    }
};

vmm_pagestack vmm_pages;
uint32_t vmm_tableframe[VMM_ENTRIES_PER_TABLE] __attribute__((aligned(0x1000)));
lock vmm_framelock;

class vmm_pagedir{
private:
    uint32_t* pagedir;
    uint32_t* curtable;
    uint32_t phys_addr;
    size_t userpagecount;

    void maptable(uint32_t phys_addr){
        uint32_t virtpage=(uint32_t)curtable/VMM_PAGE_SIZE;
        uint32_t physpage=phys_addr/VMM_PAGE_SIZE;
        size_t tableindex=virtpage/VMM_ENTRIES_PER_TABLE;
        size_t tableoffset=virtpage-(tableindex * VMM_ENTRIES_PER_TABLE);
        uint32_t table=pagedir[tableindex] & VMM_ADDRESS_MASK;
        ((uint32_t*)table)[tableoffset]=(physpage*VMM_PAGE_SIZE) | (PageFlags::Present | PageFlags::Writable);
        vmm_refresh_addr(virtpage * VMM_PAGE_SIZE);
    }
public:
    void init(uint32_t *dir);
    void init();

    uint32_t *getvirt(){
        return pagedir;
    }
    uint32_t getphys(){
    	return phys_addr;
    }

    uint32_t virt2phys(void *ptr);
    void add_table(size_t tableno, uint32_t *table);

    bool is_mapped(void *ptr){
        return !!virt2phys(ptr);
    }

    size_t find_free_virtpages(size_t pages, vmm_allocmode::Enum mode);
    void map_page(size_t virtpage, size_t physpage, bool alloc=true, vmm_allocmode::Enum mode=vmm_allocmode::Kernel);

    void identity_map(size_t page, bool alloc=true){
    	map_page(page, page, alloc);
    }

    size_t unmap_page(size_t virtpage);

    void copy_kernelspace(vmm_pagedir *other){
    	memcpy(pagedir, other->pagedir, VMM_KERNEL_TABLES * sizeof(uint32_t));
    }

    size_t getuserpagecount(){
    	return userpagecount;
    }

    void destroy();

    void set_flags(uint32_t pageaddr, amm_flags::Enum flags);
    amm_flags::Enum get_flags(uint32_t pageaddr);
};

void vmm_pagedir::init(uint32_t *dir){
	pagedir=dir;
	phys_addr=(uint32_t)dir;
	curtable=(uint32_t*)&vmm_tableframe;
	vmm_pages.push((uint32_t)curtable);
}

void vmm_pagedir::init(){
	curtable=(uint32_t*)&vmm_tableframe;
	pagedir=(uint32_t*)vmm_alloc(1, vmm_allocmode::Kernel);
	phys_addr=vmm_cur_pagedir->virt2phys(pagedir);
	memset(pagedir, 0, VMM_ENTRIES_PER_TABLE * sizeof(uint32_t));
	if((uint32_t)curtable != ((uint32_t)curtable & VMM_ADDRESS_MASK)) panic("VMM: Misaligned table frame!");
}

void vmm_pagedir::add_table(size_t tableno, uint32_t *table){
	uint32_t tableflags=(TableFlags::Present | TableFlags::Writable);
	if(tableno >= VMM_KERNEL_TABLES) tableflags |= TableFlags::Usermode;
	pagedir[tableno]=(uint32_t)table | tableflags;
}

uint32_t vmm_pagedir::virt2phys(void *ptr){
	hold_lock hl(vmm_framelock);
	uint32_t pageno=(size_t)ptr/VMM_PAGE_SIZE;
	size_t tableindex=pageno/VMM_ENTRIES_PER_TABLE;
	size_t tableoffset=pageno-(tableindex * VMM_ENTRIES_PER_TABLE);
	uint32_t table=pagedir[tableindex];
	if(!(table & TableFlags::Present)) return 0;
	table &= VMM_ADDRESS_MASK;
	maptable(table);
	uint32_t ret=curtable[tableoffset];
	if(!(ret & PageFlags::Present)) return 0;
	ret &= VMM_ADDRESS_MASK;
	return ret;
}

size_t vmm_pagedir::find_free_virtpages(size_t pages, vmm_allocmode::Enum mode){
	size_t freecount=0;
	size_t startpage=0;
	size_t loopstart=1;
	size_t loopend=VMM_KERNEL_PAGES;

	if(mode == vmm_allocmode::Userlow){
		loopstart=VMM_KERNEL_PAGES;
		loopend=VMM_MAX_PAGES;
	}else if(mode == vmm_allocmode::Userhigh){
		loopstart=VMM_MAX_PAGES;
		loopend=VMM_KERNEL_PAGES;
	}
	if(loopstart < loopend){
		for(size_t i=loopstart; i<loopend; ++i){
			void *pageptr=(void*)(i*VMM_PAGE_SIZE);
			if(!is_mapped(pageptr) && !(get_flags((uint32_t)pageptr) & amm_flags::Do_Not_Use)){
				if(!startpage) startpage=i;
				freecount++;
			}else {
                startpage = 0;
                freecount = 0;
            }
			if(freecount==pages) return startpage;
		}
	}else{
		for(size_t i=loopstart; i>=loopend; --i){
			void *pageptr=(void*)(i*VMM_PAGE_SIZE);
			if(!is_mapped(pageptr) && !(get_flags((uint32_t)pageptr) & amm_flags::Do_Not_Use)){
				startpage = i;
				freecount++;
			}else{
				startpage=0;
                freecount=0;
			}
			if(freecount==pages) return startpage;
		}
	}
	return 0;
}

size_t vmm_pagedir::unmap_page(size_t virtpage){
	hold_lock hl(vmm_framelock);
	if(!pagedir) panic("(VMM) Invalid page directory!");
	//dbgpf("VMM: Unammping %x.\n", virtpage*VMM_PAGE_SIZE);
	size_t tableindex=virtpage/VMM_ENTRIES_PER_TABLE;
	size_t tableoffset=virtpage-(tableindex * VMM_ENTRIES_PER_TABLE);
	uint32_t table=pagedir[tableindex] & VMM_ADDRESS_MASK;
	if(!table){
		panic("(VMM) No table for allocation!");
	}
	maptable(table);
	uint32_t ret=curtable[tableoffset] & VMM_ADDRESS_MASK;
	curtable[tableoffset]=0;
	bool freetable=true;
	for(size_t i=0; i<VMM_ENTRIES_PER_TABLE; ++i){
		if(curtable[i]){
			freetable=false;
			break;
		}
	}
	if(freetable){
		dbgpf("VMM: Page table %i no longer needed.\n", tableindex);
		pagedir[tableindex] = 0 | TableFlags::Writable;
		vmm_pages.push(table);
		amm_mark_free(table);
	}
	vmm_refresh_addr(virtpage * VMM_PAGE_SIZE);
	if(virtpage >= VMM_KERNEL_PAGES) userpagecount--;
	return ret/VMM_PAGE_SIZE;
}

void vmm_pagedir::destroy(){
	if(this==vmm_cur_pagedir){
		panic("VMM: Attempt to delete current page directory!");
	}
	for(size_t i=VMM_KERNEL_TABLES; i<VMM_ENTRIES_PER_TABLE; ++i){
		if(pagedir[i] & TableFlags::Present){
			hold_lock hl(vmm_framelock);
			uint32_t table=pagedir[i] & VMM_ADDRESS_MASK;
			maptable(table);
			for(size_t j=0; j<VMM_ENTRIES_PER_TABLE; ++j){
				if(curtable[j] & PageFlags::Present){
					vmm_pages.push(curtable[j] & VMM_ADDRESS_MASK);
					amm_mark_free(curtable[j] & VMM_ADDRESS_MASK);
				}
			}
			vmm_pages.push(table);
			amm_mark_free(table);
		}
	}
	vmm_free((void*)pagedir, 1);
}

void vmm_pagedir::map_page(size_t virtpage, size_t physpage, bool alloc, vmm_allocmode::Enum mode){
	//dbgpf("VMM: Mapping %x (v) to %x (p).\n", virtpage*VMM_PAGE_SIZE, physpage*VMM_PAGE_SIZE);
	uint32_t pageflags = (PageFlags::Present | PageFlags::Writable);
	if(mode != vmm_allocmode::Kernel) pageflags |= PageFlags::Usermode;
	if(!virtpage || !physpage) panic("(VMM) Attempt to map page/address 0!");
	if(!pagedir) panic("(VMM) Invalid page directory!");
	size_t tableindex=virtpage/VMM_ENTRIES_PER_TABLE;
	size_t tableoffset=virtpage-(tableindex * VMM_ENTRIES_PER_TABLE);
	uint32_t table=pagedir[tableindex] & VMM_ADDRESS_MASK;
	if(!table){
		if(alloc){
			table=vmm_pages.pop();
			dbgpf("VMM: Creating new page table %i at %x (p)\n", tableindex, table);
			add_table(tableindex, (uint32_t*)table);
			if(is_paging_enabled()){
				maptable(table);
				memset(curtable, 0, VMM_PAGE_SIZE);
				if(mode == vmm_allocmode::Kernel){
                    amm_mark_alloc(table, (amm_flags::Enum)(amm_flags::Kernel | amm_flags::PageTable), 0);
                }else{
                    amm_mark_alloc(table, (amm_flags::Enum)(amm_flags::User | amm_flags::PageTable));
                }
			}else{
				memset((void*)table, 0, VMM_PAGE_SIZE);
			}
		}else{
			panic("(VMM) Cannot allocate page table for mapping!");
		}
	}
	if(is_paging_enabled()){
		hold_lock hl(vmm_framelock);
		maptable(table);
		curtable[tableoffset]=(physpage*VMM_PAGE_SIZE) | pageflags;
	}else{
		((uint32_t*)table)[tableoffset]=(physpage*VMM_PAGE_SIZE) | pageflags;
	}
	vmm_refresh_addr(virtpage * VMM_PAGE_SIZE);
	if(mode != vmm_allocmode::Kernel) userpagecount++;
}

void vmm_pagedir::set_flags(uint32_t pageaddr, amm_flags::Enum flags){
	uint32_t flagval = (uint32_t)flags & VMM_FLAGS_MASK;
	size_t virtpage=pageaddr/VMM_PAGE_SIZE;
	size_t tableindex=virtpage/VMM_ENTRIES_PER_TABLE;
    size_t tableoffset=virtpage-(tableindex * VMM_ENTRIES_PER_TABLE);
    uint32_t table=pagedir[tableindex] & VMM_ADDRESS_MASK;
    if(!table) return;
    {   hold_lock hl(vmm_framelock);
        maptable(table);
        curtable[tableoffset] |= flagval;
    }
}

amm_flags::Enum vmm_pagedir::get_flags(uint32_t pageaddr){
	size_t virtpage=pageaddr/VMM_PAGE_SIZE;
	size_t tableindex=virtpage/VMM_ENTRIES_PER_TABLE;
    size_t tableoffset=virtpage-(tableindex * VMM_ENTRIES_PER_TABLE);
    uint32_t table=pagedir[tableindex] & VMM_ADDRESS_MASK;
    if(!table) return amm_flags::Normal;
    {   hold_lock hl(vmm_framelock);
        maptable(table);
        return (amm_flags::Enum)(curtable[tableoffset] & VMM_FLAGS_MASK);
    }
}

const size_t VMM_MAX_REGIONS=32;
vmm_region vmm_regions[VMM_MAX_REGIONS]={0, 0};
uint32_t vmm_kpagedir[VMM_ENTRIES_PER_TABLE] __attribute__((aligned(0x1000)));
uint32_t vmm_kinitable[VMM_ENTRIES_PER_TABLE] __attribute__((aligned(0x1000)));
vmm_pagedir *vmm_cur_pagedir, vmm_kernel_pagedir;
size_t vmm_totalmem=0;

void *vmm_ministack_alloc(size_t pages=1);
void vmm_ministack_free(void *ptr, size_t pages=1);
void vmm_identity_map(uint32_t *pagedir, size_t page, bool alloc=true);
void vmm_page_fault_handler(int,isr_regs*);
void vmm_checkstack();

char *freemem_infofs(){
	char *buffer=(char*)malloc(512);
    memset(buffer, 0, 512);
    size_t freemem=vmm_pages.size() * VMM_PAGE_SIZE;
    dbgpf("VMM: Stack size: %i\n", vmm_pages.size());
    sprintf(buffer, "%i\n", freemem);
    return buffer;
}

char *totalmem_infofs(){
	char *buffer=(char*)malloc(512);
    memset(buffer, 0, 512);
    sprintf(buffer, "%i\n", vmm_totalmem);
    return buffer;
}

char *totalused_infofs(){
	char *buffer=(char*)malloc(512);
    memset(buffer, 0, 512);
    size_t used=0;
    for(size_t i=0; i<VMM_MAX_PAGES; ++i){
       	if(vmm_cur_pagedir->is_mapped((void*)(i*VMM_PAGE_SIZE))) used+=VMM_PAGE_SIZE;
    }
    sprintf(buffer, "%i\n", used);
    return buffer;
}

void vmm_init(multiboot_info_t *mbt){
	init_lock(vmm_lock);
	init_lock(vmm_framelock);
	dbgout("VMM: Init\n");
	memory_map_t *mmap = (memory_map_t*)mbt->mmap_addr;
	size_t k_first_page=((size_t)&_start/VMM_PAGE_SIZE);
	size_t k_last_page=((size_t)&_end/VMM_PAGE_SIZE);
	dbgpf("VMM: Kernel start: %x (page %x) end: %x (page %x).\n", &_start,
		k_first_page, &_end, k_last_page);
	size_t cregion=0;
	vmm_totalmem=0;
    while(mmap < (memory_map_t*)mbt->mmap_addr + mbt->mmap_length) {
		if(mmap->type == 1 && mmap->length_low > 0){
			dbgpf("VMM: Usable region base: 0x%x pages: %u\n", mmap->base_addr_low, mmap->length_low/VMM_PAGE_SIZE);
			if(mmap->base_addr_low < 1024*1024 && mmap->length_low < 1024*1024){
				dbgpf("VMM: Ignoring low 1MB RAM\n");
			}else{
				vmm_totalmem+=mmap->length_low;
				vmm_regions[cregion].base=(void*)mmap->base_addr_low;
				vmm_regions[cregion].pages=mmap->length_low/VMM_PAGE_SIZE;
				++cregion;
				if(cregion>=VMM_MAX_REGIONS) break;
			}
		}
		mmap = (memory_map_t*) ( (unsigned int)mmap + mmap->size + sizeof(unsigned int) );
    }
    size_t totalpages=0;
    for(size_t i=0; i<VMM_MAX_REGIONS; ++i){
    	if(vmm_regions[i].base){
    		for(size_t j=0; j<vmm_regions[i].pages; ++j){
    			size_t phys_page_num=((size_t)vmm_regions[i].base/VMM_PAGE_SIZE)+j;
    			void* phys_page_addr=(void*)(phys_page_num*VMM_PAGE_SIZE);
    			if(phys_page_num < 256) panic("(VMM) Page number too low!");
    			if(phys_page_num>=k_first_page && phys_page_num<=k_last_page){
    				dbgpf("VMM: Region %i, page %i (%x) - KERNEL\n", i, j, phys_page_num);
    			}else{
    				memset(phys_page_addr, 0, VMM_PAGE_SIZE);
    				++totalpages;
    			}
    		}
    	}else break;
    }
    dbgpf("VMM: Total pages: %i\n", totalpages);
    printf("VMM: Total RAM: %iKB\n", vmm_totalmem/1024);
   	dbgpf("VMM: Initializing kernel page directory at %x.\n", vmm_kpagedir);
	for(size_t i=0; i<VMM_ENTRIES_PER_TABLE; ++i){
		vmm_kpagedir[i]=0 | TableFlags::Writable;
	}
	dbgpf("VMM: Initializing initial page table at %x.\n", vmm_kinitable);
	memset((void*)vmm_kinitable, 0, VMM_PAGE_SIZE);
	vmm_kernel_pagedir.init(vmm_kpagedir);
	vmm_kernel_pagedir.add_table(0, vmm_kinitable);
    size_t stacksize=totalpages * sizeof(uint32_t);
    size_t stackpages=(stacksize/VMM_PAGE_SIZE)+1;
    dbgpf("VMM: Pages needed for page stack: %i\n", stackpages);
    size_t firstfreepage=k_last_page+stackpages+1;
    dbgpf("VMM: First free page: %x\n", firstfreepage);
    dbgpf("VMM: Setting up identity mappings.\n");
    for(size_t i=1; i<firstfreepage; ++i){
    	vmm_kernel_pagedir.identity_map(i, false);
    }
    int_handle(0x0e, &vmm_page_fault_handler);
    dbgout("VMM: Enabing paging...");
    asm volatile("mov %0, %%cr3":: "b"(vmm_kpagedir));
    unsigned int cr0;
    asm volatile("mov %%cr0, %0": "=b"(cr0));
    cr0 |= PAGING_ENABLED_FLAG;
    asm volatile("mov %0, %%cr0":: "b"(cr0));
    dbgout("Done.\n");
    vmm_cur_pagedir=&vmm_kernel_pagedir;
    vmm_pages.init((uint32_t*)((k_last_page+1)*VMM_PAGE_SIZE), stacksize);
    for(size_t i=0; i<VMM_MAX_REGIONS; ++i){
    	if(vmm_regions[i].base){
    		size_t base=(size_t)vmm_regions[i].base;
    		for(size_t j=0; j<vmm_regions[i].pages; ++j){
    			size_t pageaddr=base+(VMM_PAGE_SIZE*j);
    			if(pageaddr >= (firstfreepage*VMM_PAGE_SIZE)){
    				vmm_pages.push(pageaddr);
    			}
    		}
    	}else{
    		break;
    	}
    }
    dbgout("VMM: Page stack initialized.\n");
    dbgout("VMM: Init complete.\n");
    dbgout("VMM: Init AMM...\n");
    amm_init();
    dbgout("VMM: Accouting for kernel pages...\n");
    vmm_cur_pagedir->set_flags(0, amm_flags::Guard_Page);
    for(size_t i=1; i<VMM_KERNEL_PAGES; ++i){
        void *pageptr=(void*)(i*VMM_PAGE_SIZE);
        if(vmm_cur_pagedir->is_mapped(pageptr)){
            dbgpf("VMM: Marking page %x\n", pageptr);
            amm_mark_alloc(vmm_cur_pagedir->virt2phys(pageptr), amm_flags::Kernel, 0);
            vmm_cur_pagedir->set_flags((i*VMM_PAGE_SIZE), amm_flags::Kernel);
        }
    }
    infofs_register("FREEMEM", &freemem_infofs);
    infofs_register("TOTALMEM", &totalmem_infofs);
    infofs_register("ACTIVEMEM", &totalused_infofs);
}

void vmm_checkstack(){
    size_t mappedpages=0;
    for(size_t i=0; i<VMM_MAX_PAGES; ++i){
    	if(vmm_cur_pagedir->is_mapped((void*)(i*VMM_PAGE_SIZE))){
    		mappedpages++;
    		uint32_t physpage=vmm_cur_pagedir->virt2phys((void*)(i*VMM_PAGE_SIZE));
    		for(size_t j=0; j<vmm_pages.size(); ++j){
    			if(vmm_pages.at(j)==physpage){
    				dbgpf("VMM: Page %x in use and in stack at %i!\n", physpage, j);
    				panic("(VMM) In-use pages in stack!");
    			}
    		}
    	}
    }
    dbgpf("VMM: TOTAL MAPPED PAGES: %i\n", mappedpages);
    for(size_t i=0; i<vmm_pages.size(); ++i){
    	if(vmm_pages.at(i) < 256*VMM_PAGE_SIZE){
    		panic("(VMM) Low pages in stack!");
    	}
    	for(size_t j=0; j<vmm_pages.size(); ++j){
    		if(i!=j && vmm_pages.at(i)==vmm_pages.at(j)){
    			dbgpf("VMM: Duplicate page in stack! %x: %i, %i\n", vmm_pages.at(i), i, j);
    			panic("(VMM) Duplicate pages in stack!");
    		}
    	}
    }
}

bool is_paging_enabled(){
    unsigned int cr0;
    asm volatile("mov %%cr0, %0": "=b"(cr0));
    return cr0 & PAGING_ENABLED_FLAG;
}

void vmm_page_fault_handler(int, isr_regs *regs){
	uint32_t addr;
	asm volatile("mov %%cr2, %%eax\r\n mov %%eax,%0": "=m"(addr): : "eax");
	dbgpf("VMM: Page fault on %x at %x!\n", addr, regs->eip);
	if(addr < VMM_PAGE_SIZE) panic("(VMM) Probable NULL pointer dereference!");
	else panic("(VMM) Page fault!");
}

void vmm_refresh_addr(uint32_t pageaddr){
	asm volatile("invlpg (%0)" ::"r" (pageaddr) : "memory");
}

void *vmm_alloc(size_t pages, vmm_allocmode::Enum mode){
	hold_lock hl(vmm_lock, false);
	size_t virtpage=vmm_cur_pagedir->find_free_virtpages(pages, mode);
	if(!virtpage) return NULL;
	for(size_t i=0; i<pages; ++i){
		uint32_t phys_page=vmm_pages.pop()/VMM_PAGE_SIZE;
		if(!phys_page){
		    dbgpf("VMM: Allocation of %i pages failed.\n", pages);
		    return NULL;
		}
		vmm_cur_pagedir->map_page(virtpage+i, phys_page, true, mode);
	}
    for(size_t i=0; i<pages; ++i){
        uint32_t phys_page=vmm_pages.pop()/VMM_PAGE_SIZE;
        if(mode == vmm_allocmode::Kernel){
            amm_mark_alloc(phys_page*VMM_PAGE_SIZE, amm_flags::Kernel, 0);
        }else{
            amm_mark_alloc(phys_page*VMM_PAGE_SIZE, amm_flags::Normal);
        }
    }
	void *ret=(void*)(virtpage*VMM_PAGE_SIZE);
	memset(ret, 0xaa, pages*VMM_PAGE_SIZE);
	return ret;
}

void *vmm_alloc_at(size_t pages, size_t baseaddr){
	hold_lock hl(vmm_lock);
	size_t virtpage=baseaddr/VMM_PAGE_SIZE;
	for(size_t i=0; i<pages; ++i){
		if(vmm_cur_pagedir->is_mapped((void*)((virtpage+i)*VMM_PAGE_SIZE))) continue;
		uint32_t phys_page=vmm_pages.pop()/VMM_PAGE_SIZE;
		if(!phys_page){
			dbgpf("VMM: Allocation of %i pages failed.\n", pages);
			return NULL;
		}
		vmm_allocmode::Enum mode = ((virtpage+i)*VMM_PAGE_SIZE < VMM_KERNELSPACE_END)?
			vmm_allocmode::Kernel : vmm_allocmode::Userlow;
		vmm_cur_pagedir->map_page(virtpage+i, phys_page, true, mode);
		if(mode == vmm_allocmode::Kernel){
            amm_mark_alloc(phys_page*VMM_PAGE_SIZE, amm_flags::Kernel, 0);
        }else{
            amm_mark_alloc(phys_page*VMM_PAGE_SIZE, amm_flags::Normal);
        }
	}
	return (void*)baseaddr;
}

void vmm_free(void *ptr, size_t pages){
	hold_lock hl(vmm_lock, false);
	memset(ptr, 0xfe, pages * VMM_PAGE_SIZE);
	size_t virtpage=(uint32_t)ptr/VMM_PAGE_SIZE;
	for(size_t i=0; i<pages; ++i){
		size_t physpage=vmm_cur_pagedir->unmap_page(virtpage+i);
		vmm_pages.push(physpage*VMM_PAGE_SIZE);
		amm_mark_free(physpage*VMM_PAGE_SIZE);
	}
}

void vmm_activate_pagedir(vmm_pagedir *pagedir){
	uint32_t dir=pagedir->getphys();
	if(!dir) panic("VMM: Invalid page directory!");
	disable_interrupts();
	asm volatile("mov %0, %%cr3":: "b"(dir));
    unsigned int cr0;
    asm volatile("mov %%cr0, %0": "=b"(cr0));
    cr0 &= ~PAGING_ENABLED_FLAG;
    asm volatile("mov %0, %%cr0":: "b"(cr0));
    asm volatile("mov %%cr0, %0": "=b"(cr0));
    cr0 |= PAGING_ENABLED_FLAG;
    asm volatile("mov %0, %%cr0":: "b"(cr0));
    enable_interrupts();
}

void vmm_switch(vmm_pagedir *dir){
	if(dir!=vmm_cur_pagedir){
		vmm_kernel_pagedir.copy_kernelspace(vmm_cur_pagedir);
		dir->copy_kernelspace(&vmm_kernel_pagedir);
		vmm_cur_pagedir=dir;
		vmm_activate_pagedir(vmm_cur_pagedir);
	}
}

vmm_pagedir *vmm_newpagedir(){
	vmm_pagedir *ret=new vmm_pagedir();
	ret->init();
	ret->copy_kernelspace(vmm_cur_pagedir);
	return ret;
}

void vmm_deletepagedir(vmm_pagedir *dir){
	dir->destroy();
	delete dir;
}

size_t vmm_getusermemory(vmm_pagedir *dir){
	return dir->getuserpagecount() * VMM_PAGE_SIZE;
}

size_t vmm_getkernelmemory(){
	size_t ret=0;
	for(size_t i=256; i<VMM_KERNEL_PAGES; ++i){
		if(vmm_cur_pagedir->is_mapped((void*)(i*VMM_PAGE_SIZE))) ret+=VMM_PAGE_SIZE;
	}
	return ret;
}