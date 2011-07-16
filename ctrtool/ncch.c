#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "types.h"
#include "ncch.h"
#include "utils.h"
#include "ctr.h"

void ncch_init(ncch_context* ctx)
{
	memset(ctx, 0, sizeof(ncch_context));
	filepath_init(&ctx->exefspath);
	filepath_init(&ctx->romfspath);
	filepath_init(&ctx->exheaderpath);
	exefs_init(&ctx->exefs);
}

void ncch_set_offset(ncch_context* ctx, u32 ncchoffset)
{
	ctx->offset = ncchoffset;
}

void ncch_set_file(ncch_context* ctx, FILE* file)
{
	ctx->file = file;
}

void ncch_load_keys(ncch_context* ctx, keyset* keys)
{
	memcpy(ctx->key, keys->ncchctrkey.data, 16);
	memcpy(&ctx->ncsdrsakey, &keys->ncsdrsakey, sizeof(rsakey2048));
	memcpy(&ctx->ncchrsakey, &keys->ncchrsakey, sizeof(rsakey2048));
	memcpy(&ctx->nccholdrsakey, &keys->nccholdrsakey, sizeof(rsakey2048));
	memcpy(&ctx->ncchdlprsakey, &keys->ncchdlprsakey, sizeof(rsakey2048));
	memcpy(&ctx->crrrsakey, &keys->crrrsakey, sizeof(rsakey2048));
}

void ncch_set_key(ncch_context* ctx, u8 key[16])
{
	memcpy(ctx->key, key, 16);
}

void ncch_set_exefspath(ncch_context* ctx, const char* path)
{
	filepath_set(&ctx->exefspath, path);
}

void ncch_set_exefsdirpath(ncch_context* ctx, const char* path)
{
	exefs_set_dirpath(&ctx->exefs, path);
}

void ncch_set_romfspath(ncch_context* ctx, const char* path)
{
	filepath_set(&ctx->romfspath, path);
}

void ncch_set_exheaderpath(ncch_context* ctx, const char* path)
{
	filepath_set(&ctx->exheaderpath, path);
}


int ncch_extract_prepare(ncch_context* ctx, u32 type, u32 flags)
{
	u32 offset = 0;
	u32 size = 0;


	switch(type)
	{	
		case NCCHTYPE_EXEFS:
		{
			offset = ncch_get_exefs_offset(ctx);
			size = ncch_get_exefs_size(ctx);
		}
		break;

		case NCCHTYPE_ROMFS:
		{
			offset = ncch_get_romfs_offset(ctx);
			size = ncch_get_romfs_size(ctx);
		}
		break;

		case NCCHTYPE_EXHEADER:
		{
			offset = ncch_get_exheader_offset(ctx);
			size = ncch_get_exheader_size(ctx);
		}
		break;
	
		default:
		{
			fprintf(stderr, "Error invalid NCCH type\n");
			goto clean;
		}
		break;
	}

	ctx->extractsize = size;
	ctx->extractflags = flags;
	fseek(ctx->file, offset, SEEK_SET);
	ctr_init_ncch(&ctx->aes, ctx->key, ctx->header.partitionid, type);

	return 1;

clean:
	return 0;
}

int ncch_extract_buffer(ncch_context* ctx, u8* buffer, u32 buffersize, u32* outsize)
{
	u32 max = buffersize;

	if (max > ctx->extractsize)
		max = ctx->extractsize;

	*outsize = max;

	if (ctx->extractsize)
	{
		if (max != fread(buffer, 1, max, ctx->file))
		{
			fprintf(stdout, "Error reading input file\n");
			goto clean;
		}

		if (0 == (ctx->extractflags & PlainFlag))
			ctr_crypt_counter(&ctx->aes, buffer, buffer, max);

		ctx->extractsize -= max;
	}

	return 1;

clean:
	return 0;
}

void ncch_save(ncch_context* ctx, u32 type, u32 flags)
{
	FILE* fout = 0;
	filepath* path = 0;
	u8 buffer[16*1024];


	if (0 == ncch_extract_prepare(ctx, type, flags))
		goto clean;

	switch(type)
	{	
		case NCCHTYPE_EXEFS: path = &ctx->exefspath; break;
		case NCCHTYPE_ROMFS: path = &ctx->romfspath; break;
		case NCCHTYPE_EXHEADER: path = &ctx->exheaderpath; break;
	}

	if (path == 0 || path->valid == 0)
		goto clean;

	fout = fopen(path->pathname, "wb");
	if (0 == fout)
	{
		fprintf(stdout, "Error opening out file %s\n", path->pathname);
		goto clean;
	}

	switch(type)
	{
		case NCCHTYPE_EXEFS: fprintf(stdout, "Saving ExeFS...\n"); break;
		case NCCHTYPE_ROMFS: fprintf(stdout, "Saving RomFS...\n"); break;
		case NCCHTYPE_EXHEADER: fprintf(stdout, "Saving Extended Header...\n"); break;
	}

	while(1)
	{
		u32 max;

		if (0 == ncch_extract_buffer(ctx, buffer, sizeof(buffer), &max))
			goto clean;

		if (max == 0)
			break;

		if (max != fwrite(buffer, 1, max, fout))
		{
			fprintf(stdout, "Error writing output file\n");
			goto clean;
		}
	}
clean:
	if (fout)
		fclose(fout);
	return;
}

void ncch_verify_hashes(ncch_context* ctx, u32 flags)
{
	u32 exefshashregionsize = getle32(ctx->header.exefshashregionsize) * 0x200;
	u32 romfshashregionsize = getle32(ctx->header.exefshashregionsize) * 0x200;
	u32 exheaderhashregionsize = getle32(ctx->header.extendedheadersize) * 0x200;
	u8* exefshashregion = malloc(exefshashregionsize);
	u8* romfshashregion = malloc(romfshashregionsize);
	u8* exheaderhashregion = malloc(exheaderhashregionsize);

	if (0 == ncch_extract_prepare(ctx, NCCHTYPE_EXEFS, flags))
		goto clean;
	if (0 == ncch_extract_buffer(ctx, exefshashregion, exefshashregionsize, &exefshashregionsize))
		goto clean;
	if (0 == ncch_extract_prepare(ctx, NCCHTYPE_ROMFS, flags))
		goto clean;
	if (0 == ncch_extract_buffer(ctx, romfshashregion, romfshashregionsize, &romfshashregionsize))
		goto clean;
	if (0 == ncch_extract_prepare(ctx, NCCHTYPE_EXHEADER, flags))
		goto clean;
	if (0 == ncch_extract_buffer(ctx, exheaderhashregion, exheaderhashregionsize, &exheaderhashregionsize))
		goto clean;

	ctx->exefshashcheck = ctr_sha_256_verify(exefshashregion, exefshashregionsize, ctx->header.exefssuperblockhash);
	ctx->romfshashcheck = ctr_sha_256_verify(romfshashregion, romfshashregionsize, ctx->header.romfssuperblockhash);
	ctx->exheaderhashcheck = ctr_sha_256_verify(exheaderhashregion, exheaderhashregionsize, ctx->header.extendedheaderhash);

	free(exefshashregion);
	free(romfshashregion);
	free(exheaderhashregion);
clean:
	return;
}

void ncch_process(ncch_context* ctx, u32 actions)
{
	fseek(ctx->file, ctx->offset, SEEK_SET);
	fread(&ctx->header, 1, 0x200, ctx->file);

	exheader_set_file(&ctx->exheader, ctx->file);
	exheader_set_offset(&ctx->exheader, ncch_get_exheader_offset(ctx) );
	exheader_set_size(&ctx->exheader, ncch_get_exheader_size(ctx) );
	exheader_set_key(&ctx->exheader, ctx->key);
	exheader_set_partitionid(&ctx->exheader, ctx->header.partitionid);
	exheader_set_programid(&ctx->exheader, ctx->header.programid);

	exefs_set_file(&ctx->exefs, ctx->file);
	exefs_set_offset(&ctx->exefs, ncch_get_exefs_offset(ctx) );
	exefs_set_size(&ctx->exefs, ncch_get_exefs_size(ctx) );
	exefs_set_key(&ctx->exefs, ctx->key);
	exefs_set_partitionid(&ctx->exefs, ctx->header.partitionid);

	if (actions & VerifyFlag)
		ncch_verify_hashes(ctx, actions);

	if (actions & InfoFlag)
		ncch_print(ctx);		

	if (actions & ExtractFlag)
	{
		ncch_save(ctx, NCCHTYPE_EXEFS, actions);
		ncch_save(ctx, NCCHTYPE_ROMFS, actions);
		ncch_save(ctx, NCCHTYPE_EXHEADER, actions);
	}


	if (exheader_process(&ctx->exheader, actions))
	{
		exefs_process(&ctx->exefs, actions);
	}
}

int ncch_signature_verify(const ctr_ncchheader* header, rsakey2048* key)
{
	u8 hash[0x20];
	u8 output[0x100];

	ctr_rsa_public(header->signature, output, key);

	memdump(stdout, "RSA decrypted:      ", output, 0x100);

	ctr_sha_256(header->magic, 0x100, hash);
	return ctr_rsa_verify_hash(header->signature, hash, key);
}

u32 ncch_get_exefs_offset(ncch_context* ctx)
{
	return ctx->offset + getle32(ctx->header.exefsoffset) * 0x200;
}

u32 ncch_get_exefs_size(ncch_context* ctx)
{
	return getle32(ctx->header.exefssize) * 0x200;
}

u32 ncch_get_romfs_offset(ncch_context* ctx)
{
	return ctx->offset + getle32(ctx->header.romfsoffset) * 0x200;
}

u32 ncch_get_romfs_size(ncch_context* ctx)
{
	return getle32(ctx->header.romfssize) * 0x200;
}

u32 ncch_get_exheader_offset(ncch_context* ctx)
{
	return ctx->offset + 0x200;
}

u32 ncch_get_exheader_size(ncch_context* ctx)
{
	return getle32(ctx->header.extendedheadersize);
}

void ncch_print(ncch_context* ctx)
{
	char magic[5];
	char productcode[0x11];
	ctr_ncchheader *header = &ctx->header;
	int sigcheck = 0;
	u32 offset = ctx->offset;

/*
	sigcheck = ncch_signature_verify(header, &ctx->ncchrsakey);
	if (!sigcheck)
		sigcheck = ncch_signature_verify(header, &ctx->ncchdlprsakey);
	if (!sigcheck)
		sigcheck = ncch_signature_verify(header, &ctx->crrrsakey);
	if (!sigcheck)
		sigcheck = ncch_signature_verify(header, &ctx->ncsdrsakey);
	if (!sigcheck)
		sigcheck = ncch_signature_verify(header, &ctx->nccholdrsakey);
*/

	fprintf(stdout, "\nNCCH:\n");
	memcpy(magic, header->magic, 4);
	magic[4] = 0;
	memcpy(productcode, header->productcode, 0x10);
	productcode[0x10] = 0;

	fprintf(stdout, "Header:                 %s\n", magic);
	if (sigcheck)
		memdump(stdout, "Signature (GOOD):       ", header->signature, 0x100);
	else
		memdump(stdout, "Signature (FAIL):       ", header->signature, 0x100);
	fprintf(stdout, "Content size:           0x%08x\n", getle32(header->contentsize)*0x200);
	fprintf(stdout, "Partition id:           %016llx\n", getle64(header->partitionid));
	fprintf(stdout, "Maker code:             %04x\n", getle16(header->makercode));
	fprintf(stdout, "Version:                %04x\n", getle16(header->version));
	fprintf(stdout, "Program id:             %016llx\n", getle64(header->programid));
	fprintf(stdout, "Temp flag:              %02x\n", header->tempflag);
	fprintf(stdout, "Product code:           %s\n", productcode);
	fprintf(stdout, "Exheader size:          %08x\n", getle32(header->extendedheadersize));
	if (ctx->exheaderhashcheck == HashUnchecked)
		memdump(stdout, "Exheader hash:          ", header->extendedheaderhash, 0x20);
	else if (ctx->exheaderhashcheck == HashGood)
		memdump(stdout, "Exheader hash (GOOD):   ", header->extendedheaderhash, 0x20);
	else
		memdump(stdout, "Exheader hash (FAIL):   ", header->extendedheaderhash, 0x20);
	fprintf(stdout, "Flags:                  %016llx\n", getle64(header->flags));
	fprintf(stdout, "Plain region offset:    0x%08x\n", getle32(header->plainregionsize)? offset+getle32(header->plainregionoffset)*0x200 : 0);
	fprintf(stdout, "Plain region size:      0x%08x\n", getle32(header->plainregionsize)*0x200);
	fprintf(stdout, "ExeFS offset:           0x%08x\n", getle32(header->exefssize)? offset+getle32(header->exefsoffset)*0x200 : 0);
	fprintf(stdout, "ExeFS size:             0x%08x\n", getle32(header->exefssize)*0x200);
	fprintf(stdout, "ExeFS hash region size: 0x%08x\n", getle32(header->exefshashregionsize)*0x200);
	fprintf(stdout, "RomFS offset:           0x%08x\n", getle32(header->romfssize)? offset+getle32(header->romfsoffset)*0x200 : 0);
	fprintf(stdout, "RomFS size:             0x%08x\n", getle32(header->romfssize)*0x200);
	fprintf(stdout, "RomFS hash region size: 0x%08x\n", getle32(header->romfshashregionsize)*0x200);
	if (ctx->exefshashcheck == HashUnchecked)
		memdump(stdout, "ExeFS Hash:             ", header->exefssuperblockhash, 0x20);
	else if (ctx->exefshashcheck == HashGood)
		memdump(stdout, "ExeFS Hash (GOOD):      ", header->exefssuperblockhash, 0x20);
	else
		memdump(stdout, "ExeFS Hash (FAIL):      ", header->exefssuperblockhash, 0x20);
	if (ctx->romfshashcheck == HashUnchecked)
		memdump(stdout, "RomFS Hash:             ", header->romfssuperblockhash, 0x20);
	else if (ctx->romfshashcheck == HashGood)
		memdump(stdout, "RomFS Hash (GOOD):      ", header->romfssuperblockhash, 0x20);
	else
		memdump(stdout, "RomFS Hash (FAIL):      ", header->romfssuperblockhash, 0x20);
}
