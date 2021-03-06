#include <stdint.h>
#include "ata.hpp"
#include <btos_module.h>

struct part_entry{
	uint8_t boot;
	uint8_t starthead;
	uint16_t start_sector_cylinder;
	uint8_t system;
	uint8_t endhead;
	uint16_t end_sector_cylinder;
	uint32_t start_lba;
	uint32_t sectors;
} __attribute__((packed));

struct mbr_part_instance{
	uint64_t start, sectors;
	char device[9];
};

struct mbr_instance{
	mbr_part_instance *part;
	bt_filesize_t pos, devpos;
	void *dh;
};

void *mbr_open(void *id){
	mbr_instance *i=new mbr_instance();
	i->part=(mbr_part_instance*)id;
	i->pos=0;
	i->dh=devopen(i->part->device);
	i->devpos=devseek(i->dh, i->part->start*512, false);
	return (void*)i;
}

bool mbr_close(void *instance){
	if(instance){
		delete (mbr_instance*)instance;
		return true;
	}
	return false;
}

size_t mbr_read(void *instance, size_t bytes, char *buf){
	if(instance){
		mbr_instance *inst=(mbr_instance*)instance;
		if(bytes % 512) return 0;
		size_t r=devread(inst->dh, bytes, buf);
		inst->pos+=r; inst->devpos+=r;
		return r;
	}
	return 0;
}

size_t mbr_write(void *instance, size_t bytes, char *buf){
	if(instance){
		mbr_instance *inst=(mbr_instance*)instance;
		if(bytes % 512) return 0;
		size_t r=devwrite(inst->dh, bytes, buf);
		inst->pos+=r; inst->devpos+=r;
		return r;
	}
	return 0;
}

bt_filesize_t mbr_seek(void *instance, bt_filesize_t pos, uint32_t flags){
	if(instance){
		mbr_instance *inst=(mbr_instance*)instance;
		if(flags == FS_Set){
			inst->pos = pos;
		}else if(flags == FS_Relative){
			inst->pos += pos;
		}else if(flags == FS_Backwards){
			inst->pos = (inst->part->sectors * 512) - pos;
		}else if(flags == (FS_Relative | FS_Backwards)){
			inst->pos -= pos;
		}
		bt_filesize_t devpos = inst->pos + (inst->part->start * 512);
		if((uint64_t)devpos > (inst->part->start * inst->part->sectors) * 512) devpos = (inst->part->start * inst->part->sectors) * 512;
		devpos = devseek(inst->dh, devpos, FS_Set);
		inst->pos = devpos - (inst->part->start * 512);
		inst->devpos = devpos;
    	return inst->pos;
    }
    return 0;
}

int mbr_ioctl(void *, int, size_t, char *){
	return 0;
}

int mbr_type(){
	return driver_types::STR_PART;
};

char *mbr_desc(){
	return "ATA HDD Partition (MBR scheme).";
}

drv_driver mbr_driver={mbr_open, mbr_close, mbr_read, mbr_write, mbr_seek, mbr_ioctl, mbr_type, mbr_desc};

void mbr_parse(char *device){
	char blockbuf[512];
	part_entry part_table[4];
	void *dh=devopen(device);
	devread(dh, 512, blockbuf);
	memcpy((void*)part_table, (void*)&blockbuf[0x1be], 64);
	for(size_t i=0; i<4; ++i){
		part_entry &p=part_table[i];
		dbgpf("ATA: MBR parition %i: %x, %i,+%i\n", (int)i, (int)p.system, p.start_lba, (int)p.sectors);
		if(p.system){
			mbr_part_instance *pi=new mbr_part_instance();
			pi->start=p.start_lba; pi->sectors=p.sectors;
			strncpy(pi->device, device, 9);
			char devname[9];
			sprintf(devname, "%sP", device);
			add_device(devname, &mbr_driver, pi);
		}
	}
}