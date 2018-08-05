#include "mem.h"

const uint64_t PMASK = (~0xfull << 8) & 0xfffffffffull;
static void FillRWInfo(ProcessData* data, uint64_t dirBase, RWInfo* info, int count, uint64_t local, uint64_t remote, size_t len);

int VMemRead(ProcessData* data, uint64_t dirBase, uint64_t local, uint64_t remote, size_t size)
{
	if ((remote >> 12ull) == ((remote + size) >> 12ull))
		return MemRead(data, local, VTranslate(data, dirBase, remote), size);

	int dataCount = ((size - 1) / 0x1000) + 2;
	RWInfo rdata[dataCount];
	FillRWInfo(data, dirBase, rdata, dataCount, local, remote, size);
	return MemReadMul(data, rdata, dataCount);
}

int VMemWrite(ProcessData* data, uint64_t dirBase, uint64_t local, uint64_t remote, size_t size)
{
	if ((remote >> 12ull) == ((remote + size) >> 12ull))
		return MemWrite(data, VTranslate(data, dirBase, remote), local, size);

	int dataCount = ((size - 1) / 0x1000) + 2;
	RWInfo wdata[dataCount];
	FillRWInfo(data, dirBase, wdata, dataCount, local, remote, size);
	return MemWriteMul(data, wdata, dataCount);
}

uint64_t MemReadU64(ProcessData* data, uint64_t remote)
{
	uint64_t dest;
	MemRead(data, (uint64_t)&dest, remote, sizeof(uint64_t));
	return dest;
}

uint64_t MemWriteU64(ProcessData* data, uint64_t remote)
{
	uint64_t dest;
	MemRead(data, (uint64_t)&dest, remote, sizeof(uint64_t));
	return dest;
}

/*
  Translates a virtual address to a physical one. This (most likely) is windows specific and might need extra work to work on Linux.
*/

uint64_t VTranslate(ProcessData* data, uint64_t dirBase, uint64_t address)
{
	dirBase &= ~0xf;

	uint64_t pageOffset = address & ~(~0ul << PAGE_OFFSET_SIZE);
    uint64_t pte = ((address >> 12) & (0x1ffll));
    uint64_t pt = ((address >> 21) & (0x1ffll));
    uint64_t pd = ((address >> 30) & (0x1ffll));
    uint64_t pdp = ((address >> 39) & (0x1ffll));

	uint64_t pdpe = MemReadU64(data, dirBase + 8 * pdp);
	if (~pdpe & 1)
		return 0;

	uint64_t pde = MemReadU64(data, (pdpe & PMASK) + 8 * pd);
	if (~pde & 1)
		return 0;

	/* Large page, use pde's 12-34 bits */
	if (pde & 0x80)
		return (pde & (~0ull << 42 >> 12)) + (address & ~(~0ull << 30));

	uint64_t pteAddr = MemReadU64(data, (pde & PMASK) + 8 * pt);
	if (~pteAddr & 1)
		return 0;

	/* Large page */
	if (pteAddr & 0x80)
		return (pteAddr & PMASK) + ((pte & 0xfff) << 12) + pageOffset;

	address = MemReadU64(data, (pteAddr & PMASK) + 8 * pte) & PMASK;

	if (!address)
		return 0;

	return address + pageOffset;
}

/* Static functions */

static void FillRWInfo(ProcessData* data, uint64_t dirBase, RWInfo* info, int count, uint64_t local, uint64_t remote, size_t len)
{
	info[0].local = local;
    info[0].remote = VTranslate(data, dirBase, remote);
    info[0].size = 0x1000 - (remote & 0xfff);

	uint64_t curAddress = (remote & ~0xfff) + 0x1000;
	int i = 1;
	for (; curAddress < (remote + len); curAddress += 0x1000) {
		info[i].local = local + 0x1000 * (i-1) + info[0].size;
	    info[i].remote = VTranslate(data, dirBase, remote + 0x1000 * (i-1) + info[0].size);
	    info[i].size = len - curAddress + remote;
		if (len > 0x1000)
			len = 0x1000;
		i++;
	}
}
