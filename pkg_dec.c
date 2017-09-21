/**
 * PS Vita PKG Decrypt
 * Decrypts PS Vita PKG files
 * The code is a total mess, use at your own risk.
 * Written by St4rk
 * Special thanks to Proxima <3
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <openssl/aes.h>

const unsigned char pkg_key_psp[] = {
	0x07, 0xF2, 0xC6, 0x82, 0x90, 0xB5, 0x0D, 0x2C, 0x33, 0x81, 0x8D, 0x70, 0x9B, 0x60, 0xE6, 0x2B
};

const unsigned char pkg_vita_2[] = {
	0xE3, 0x1A, 0x70, 0xC9, 0xCE, 0x1D, 0xD7, 0x2B, 0xF3, 0xC0, 0x62, 0x29, 0x63, 0xF2, 0xEC, 0xCB
};

const unsigned char pkg_vita_3[] = {
	0x42, 0x3A, 0xCA, 0x3A, 0x2B, 0xD5, 0x64, 0x9F, 0x96, 0x86, 0xAB, 0xAD, 0x6F, 0xD8, 0x80, 0x1F
};

const unsigned char pkg_vita_4[] = {
	0xAF, 0x07, 0xFD, 0x59, 0x65, 0x25, 0x27, 0xBA, 0xF1, 0x33, 0x89, 0x66, 0x8B, 0x17, 0xD9, 0xEA
};

typedef struct ctr {
	unsigned char iv[AES_BLOCK_SIZE];
	unsigned char counter[AES_BLOCK_SIZE];
	unsigned int num;
} ctr;


/** Credits: http://www.psdevwiki.com/ps3/PKG_files */
typedef struct PKG_FILE_HEADER {
	unsigned int filename_offset;
	unsigned int filename_size;
	uint64_t data_offset;
	uint64_t data_size;
	unsigned int flags;
	unsigned int padding;
} PKG_FILE_HEADER;


int main(int argc, char **argv) {
	FILE *pkg = NULL;

	if (argc == 3) {
		pkg = fopen(argv[1], "rb");

		if (pkg == NULL) {
			printf("PKG %s not found !\n", argv[1]);
			return 0;
		}

		/**
		 * Dump tail.bin, head.bin
		 */
		// <-- tail.bin
		unsigned int pkgSize = 0;
		unsigned char *aux_1 = NULL;
		FILE *aux_2 = NULL;

		// <-- calculate the size of pkg file
		fseek(pkg, 0L, SEEK_END);
		pkgSize = ftell(pkg);
		rewind(pkg);
		fseek(pkg, pkgSize-480, SEEK_SET);
		
		// <-- allocate enough memory to tail.bin
		aux_1 = (unsigned char*) malloc(sizeof(unsigned char) * 480); // 480 bytes

		// <-- read tail into buffer
		fread(aux_1, 1, 480, pkg);
		printf("Saving tail.bin...\n");

		// <-- write tail.bin into the file
		aux_2 = fopen("tail.bin", "wb");
		fwrite(aux_1, sizeof(unsigned char), 480, aux_2);

		// <-- free memory and close the file descriptors
		free(aux_1);
		fclose(aux_2);

		// <-- now allocate enough memory to head.bin (I will allocate around ~50Kbytes, but I'm really sure)
		// <-- that it's not the right size, I will do some research to know the exactly size used ;)
		aux_1 = (unsigned char*) malloc(sizeof(unsigned char) * 0xA960);

		// <-- set the fd to beginning
		fseek(pkg, 0L, SEEK_SET);

		// <-- read 0xA960 bytes into the buffer
		fread(aux_1, 1, 0xA960, pkg);
		printf("Saving head.bin...\n");

		aux_2 = fopen("head.bin", "wb");
		fwrite(aux_1, sizeof(unsigned char), 0xA960, aux_2);

		free(aux_1);
		fclose(aux_2);


		// <-- we will generate a fake RIF file (work.bin)
		// structure:  
		// version: 00 01
		// version flag : 00 01
		// license type: 00 01
		// license flags: 00 02
		// psn account id (not used): EF CD AB 89 67 45 23 01
		// Content ID : Extracted from .PKG file
		// RIF Key (offset 0x50), it's left empty to be filled later

		aux_1 = (unsigned char*) malloc(sizeof(unsigned char) * 512);

		memset(aux_1, 0, 512);

		// <-- version and version flag
		*(unsigned int*)(aux_1) = 0x01000100;
		// <-- license type and flags
		*(unsigned int*)(aux_1+4) = 0x02000100;
		// <-- psn account id (not used probably)
		*(unsigned int*)(aux_1+8) = 0x89ABCDEF;
		// <-- high 4 bytes
		*(unsigned int*)(aux_1+0xC) = 0x01234567;
		// <-- Content ID 
		fseek(pkg, 0x30, SEEK_SET);
		fread(aux_1+0x10, 1, 0x30, pkg);

		// <-- save the work.bin
		printf("Saving work.bin...\n");
		aux_2 = fopen("work.bin", "wb");
		fwrite(aux_1, sizeof(unsigned char), 512, aux_2);
		free(aux_1);
		fclose(aux_2);


		/** get pkg key type */
		unsigned int keyType = 0;
		fseek(pkg, 0xE4, SEEK_SET);
		fread(&keyType, sizeof(unsigned int), 1, pkg);

		keyType = (keyType >> 24) & 7;

		/** pkg key */
		unsigned char pkg_key[0x10] = {0};

		fseek(pkg, 0x70, SEEK_SET);
		fread(pkg_key, 1, 0x10, pkg);

		/** encrypted data information */
		uint64_t dataOffset = 0;
		uint64_t dataSize = 0;
		fseek(pkg, 0x20, SEEK_SET);
		fread(&dataOffset, sizeof(uint64_t), 1, pkg);
		fseek(pkg, 0x28, SEEK_SET);
		fread(&dataSize, sizeof(uint64_t), 1, pkg);
		dataSize =  __builtin_bswap64(dataSize);
		dataOffset = __builtin_bswap64(dataOffset);

		printf("Offset: 0x%lX\n", dataOffset);
		printf("Size: 0x%lX\n", dataSize);

		AES_KEY key;

		FILE *content = fopen("out.bin", "wb+");

		/**
		 * encrypt PKG Key with AES_Key to generate the CTR Key
		 * only with PKG Type 2, 3 and 4
		 */
		unsigned char ctr_key[0x10];

		switch (keyType) {
			case 2:
				AES_set_encrypt_key(pkg_vita_2, 128, &key);
				AES_ecb_encrypt(pkg_key, ctr_key, &key, AES_ENCRYPT);
			break;

			case 3:
				AES_set_encrypt_key(pkg_vita_3, 128, &key);
				AES_ecb_encrypt(pkg_key, ctr_key, &key, AES_ENCRYPT);
			break;

			case 4:
				AES_set_encrypt_key(pkg_vita_4, 128, &key);
				AES_ecb_encrypt(pkg_key, ctr_key, &key, AES_ENCRYPT);
			break;
		}

		/**
		 * Set AES CTR key and use PKG key as IV
		 */

		/* decrypt chunks */
		unsigned char buffer[AES_BLOCK_SIZE];
		unsigned char out[AES_BLOCK_SIZE];
		ctr d_ctr;	

		memcpy(d_ctr.iv, pkg_key, AES_BLOCK_SIZE);
		memset(d_ctr.counter, 0, AES_BLOCK_SIZE);

		d_ctr.num = 0;

		/**
		 * AES CTR Decrypt, using the old key as IV
		 */
		AES_set_encrypt_key(keyType != 1 ? ctr_key : pkg_key_psp, 128, &key);

		printf("Decrypting...");
		fseek(pkg, dataOffset, SEEK_SET);

		while (fread(buffer, 1, AES_BLOCK_SIZE, pkg) == AES_BLOCK_SIZE) {
			AES_ctr128_encrypt(buffer, out, AES_BLOCK_SIZE, &key, d_ctr.iv, d_ctr.counter, &d_ctr.num);
			fwrite(out, 1, AES_BLOCK_SIZE, content);
		}

		printf("Done !\n");

		/* total file entry */
		unsigned int itemCnt = 0;
		fseek(pkg, 0x14, SEEK_SET);
		fread(&itemCnt, sizeof(unsigned int), 1, pkg);
		itemCnt = __builtin_bswap32(itemCnt);
		printf("Item Cnt: %d\n", itemCnt);
		PKG_FILE_HEADER fileEntry[itemCnt];
		rewind(content);
		fread(fileEntry, sizeof(PKG_FILE_HEADER), itemCnt, content);

		/** create out directory */
		struct stat st = {0};
		if (stat(argv[2], &st) == -1) {
#ifdef __MINGW32__
			mkdir(argv[2]);
#else
			mkdir(argv[2], 0777);
#endif
		}
		
		char* extraName;
		extraName = strcat(argv[2],"/%s\0\n");

		for (int i = 0; i < itemCnt; i++) {
			switch ((__builtin_bswap32(fileEntry[i].flags) & 0xFF)) {
				/** dir */
				case 4:
				case 18: {
					char dirName[0xFF];
					char fileName[0xFF];
					
					memset(dirName, 0, 0xFF);
					memset(fileName, 0, 0xFF);
					struct stat st = {0};

					/** read file name */
					fseek(content, __builtin_bswap32(fileEntry[i].filename_offset), SEEK_SET);
					fread(fileName, sizeof(char), __builtin_bswap32(fileEntry[i].filename_size), content);
					sprintf(dirName, extraName , fileName);
					printf("dirName: %s\n", dirName);

					if (stat(dirName, &st) == -1) {
#ifdef __MINGW32__
						mkdir(dirName);
#else
						mkdir(dirName, 0777);
#endif
					}
				}
				break;

				case 0:
				case 1:
				case 3:
				case 14:
				case 15:
				case 16:
				case 17:
				case 19:
				case 21:
				case 22: {
					FILE *temp = NULL;
					char dirName[0xFF];
					char fileName[0xFF];
					unsigned char *data = NULL;

					memset(dirName, 0, 0xFF);
					memset(fileName, 0, 0xFF);

					/** read file name */
					fseek(content, __builtin_bswap32(fileEntry[i].filename_offset), SEEK_SET);
					fread(fileName, sizeof(char), __builtin_bswap32(fileEntry[i].filename_size), content);

					sprintf(dirName, extraName, fileName);
					printf("fileName: %s\n", dirName);
					temp = fopen(dirName, "wb");
					
					/** Read data in 64kb chunks */
					data = (unsigned char*)malloc(sizeof(unsigned char) * 0x10000);
					if (data) {
						/** seek to the file data start */
						fseek(content, __builtin_bswap64(fileEntry[i].data_offset), SEEK_SET);
						uint64_t left = __builtin_bswap64(fileEntry[i].data_size);
						while (left > 0) {
							/** read file data */
							int read = fread(data, sizeof(unsigned char), min(left, 0x10000), content);

							/** write file data */
							int written = 0;
							while (written < read)
								written += fwrite(data + written, sizeof(unsigned char), read - written, temp);

							left -= read;
						}

						free(data);
					} else {
						printf("Failed to allocate output buffer for file unpacking.");
					}

					fclose(temp);
				}
				break;


				default:

				break;
			}
		}



		fclose(content);
		fclose(pkg);
	} else {
		printf("Usage: pkg_dec filename.pkg output_directory \n");
		printf("out.bin is the package decrypted and out folder\nhas the files inside of out.bin\n");
	}


	return 0;
}
