#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cache.h"
#include "jbod.h"
#include "mdadm.h"

int is_mounted = 0;
int write_permission = 0;

//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

uint32_t op(uint32_t diskId, uint32_t blockId, uint32_t command, uint32_t reserved)
{
	return (command << 12 | diskId << 8 | blockId | reserved << 18);
}

int mdadm_mount(void)
{
	if (is_mounted == 0)
	{
		jbod_operation(op(0, 0, JBOD_MOUNT, 0), NULL);
		//cache_create(10);
		is_mounted = 1;
		return 1;
	}
	return -1;
}

int mdadm_unmount(void) 
{
	if (is_mounted == 1)
	{
		jbod_operation(op(0, 0, JBOD_UNMOUNT, 0), NULL);
		is_mounted = 0;
		return 1;
	}
	return -1;
}

int mdadm_write_permission(void)
{
	if (write_permission == 0)
	{
		jbod_operation(op(0, 0, JBOD_WRITE_PERMISSION, 0), NULL);
		write_permission = 1;
		return 1;
	}
	return -1;
}


int mdadm_revoke_write_permission(void)
{
	if (write_permission == 1)
	{
		jbod_operation(op(0, 0, JBOD_REVOKE_WRITE_PERMISSION, 0), NULL);
		write_permission = 0;
		return 1;
	}
	return -1;
}


int mdadm_read(uint32_t start_addr, uint32_t read_len, uint8_t *read_buf)
{
	// checks if unmounted, read is too large, wants to read past total storage
	if (is_mounted != 1 || read_len > 1024 || start_addr + read_len > JBOD_DISK_SIZE * JBOD_NUM_DISKS || (read_buf == NULL && read_len != 0))
	{
		return -1;
	}
	uint32_t curr_addr = start_addr;
	uint32_t tot_bits_read = 0;

	// gets current disk and current block
	uint32_t curr_disk = curr_addr / JBOD_DISK_SIZE;
	uint32_t curr_block = (curr_addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE;
	jbod_operation(op(curr_disk, 0, JBOD_SEEK_TO_DISK, 0), NULL);
	jbod_operation(op(0, curr_block, JBOD_SEEK_TO_BLOCK, 0), NULL);

	bool cache_enable = cache_enabled();

	while (tot_bits_read < read_len)
	{
		// reads the block onto curr_buf
		uint8_t curr_buf[JBOD_BLOCK_SIZE];
		
		int max_addr_in_block = JBOD_BLOCK_SIZE - ((curr_addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE);

		if (cache_enable)
		{
			if (cache_lookup(curr_disk, curr_block, curr_buf) == -1)
			{
				jbod_operation(op(0, 0, JBOD_READ_BLOCK, 0), curr_buf);
				cache_insert(curr_disk, curr_block, curr_buf);
			}
		}
		//jbod_operation(op(0, 0, JBOD_READ_BLOCK, 0), curr_buf);
		uint8_t *src = curr_buf;
		uint32_t n = read_len;
		
		// checks if the current block will exceed the read length
		if (curr_addr + JBOD_BLOCK_SIZE <= start_addr + read_len)
		{
			src += JBOD_BLOCK_SIZE - max_addr_in_block;
			n = max_addr_in_block;
		}
		else
		{
			n -= tot_bits_read;
		}
		// copies desired bits from curr_buf into read_buf
		memcpy(read_buf + tot_bits_read, src, n);
		tot_bits_read += max_addr_in_block;
		curr_addr = start_addr + tot_bits_read;
		curr_block++;

		// increments disk if needed
		if (curr_addr / JBOD_DISK_SIZE != curr_disk)
		{
			curr_disk = curr_addr / JBOD_DISK_SIZE;
			curr_block = 0;
			jbod_operation(op(curr_disk, 0, JBOD_SEEK_TO_DISK, 0), NULL);
		}
	}
	return read_len;
}

int mdadm_write(uint32_t start_addr, uint32_t write_len, const uint8_t *write_buf)
{
	if (is_mounted != 1 || write_permission == 0 || write_len > 1024 || start_addr + write_len > JBOD_DISK_SIZE * JBOD_NUM_DISKS || (write_buf == NULL && write_len != 0))
	{
		return -1;
	}

	uint32_t curr_addr = start_addr;
	uint32_t tot_bits_read = 0; 

	bool cache_enable = cache_enabled();

	while (tot_bits_read < write_len)
	{
		// gets current block
		jbod_operation(op(curr_addr / JBOD_DISK_SIZE, 0, JBOD_SEEK_TO_DISK, 0), NULL);
		jbod_operation(op(0, ((curr_addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE), JBOD_SEEK_TO_BLOCK, 0), NULL);

		// reading current memory block
		uint8_t curr_buf[JBOD_BLOCK_SIZE];
		jbod_operation(op(0, 0, JBOD_READ_BLOCK, 0), curr_buf);

		uint32_t addr_in_block = (curr_addr % JBOD_DISK_SIZE) % JBOD_BLOCK_SIZE;

		jbod_operation(op(0, ((curr_addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE), JBOD_SEEK_TO_BLOCK, 0), NULL);
		if (tot_bits_read == 0 || curr_addr + JBOD_BLOCK_SIZE <= start_addr + write_len)
		{
			memcpy(curr_buf + addr_in_block, write_buf + tot_bits_read, JBOD_BLOCK_SIZE - addr_in_block);

			tot_bits_read += JBOD_BLOCK_SIZE - addr_in_block;
		}
		else
		{
			memcpy(curr_buf, write_buf + tot_bits_read, write_len - tot_bits_read);
			tot_bits_read += write_len - tot_bits_read;
		}
		jbod_operation(op(0, 0, JBOD_WRITE_BLOCK, 0), curr_buf);
		if (cache_enable)
		{
			cache_update(curr_addr / JBOD_DISK_SIZE, ((curr_addr % JBOD_DISK_SIZE) / JBOD_BLOCK_SIZE), curr_buf);
		}
		curr_addr = start_addr + tot_bits_read;
	}

	return write_len;
}

