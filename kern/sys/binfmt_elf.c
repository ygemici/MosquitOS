#include <types.h>

#include "binfmt_elf.h"
#include "paging.h"
#include "vm.h"
#include "syscall.h"

#define elf_check_magic(x) \
	x[0] != 0x7F || x[1] != 0x45 || \
	x[2] != 0x4C || x[3] != 0x46

elf_file_t* elf_load_binary(void* elfIn) {
	uint8_t *fileBuffer = (uint8_t *) elfIn;
	elf_header_t *header = (elf_header_t*) fileBuffer;

	if(elf_check_magic(header->ident.magic)) {
		kprintf("Invalid ELF file: Invalid magic 0x%X%X%X%X\n", header->ident.magic[0], header->ident.magic[1], header->ident.magic[2], header->ident.magic[3]);
		return NULL;
	}

	// Check architecture
	if(header->machine != 0x03) {
		kprintf("Invalid ELF file: Invalid architecture 0x%X\n", header->machine);
		return NULL;
	}

	// If the file is valid, allocate some memory
	elf_file_t *file_struct = (elf_file_t *) kmalloc(sizeof(elf_file_t));
	ASSERT(file_struct != NULL);

	memclr(file_struct, sizeof(elf_file_t));

	file_struct->elf_file_memory = fileBuffer;
	file_struct->header = header;

	// If this ELF file doesn't match what we expect, we're screwed
	ASSERT(header->ph_entry_size == sizeof(elf_program_entry_t));
	ASSERT(header->sh_entry_size == sizeof(elf_section_entry_t));

	// Variables
	uint32_t offset;

	// Get pointer to the string table
	if(header->sh_str_index != SHN_UNDEF) {
		offset = header->sh_offset + (sizeof(elf_section_entry_t) * header->sh_str_index);
		elf_section_entry_t *entry = (elf_section_entry_t *) ((uint32_t)fileBuffer+(uint32_t)offset);
		file_struct->stringTable = (unsigned char*) ((uint32_t)fileBuffer+(uint32_t)entry->sh_offset);
	}

	// Parse program header table
	for(int i = 0; i < header->ph_entry_count; i++) {
		offset = header->ph_offset + (sizeof(elf_program_entry_t) * i);
		elf_program_entry_t *entry = (elf_program_entry_t *) ((uint32_t)fileBuffer+(uint32_t)offset);

		// We care only for LOAD headers
		if(entry->p_type == PT_LOAD) {
			kprintf("LOAD offset 0x%X to virtual 0x%X (0x%X bytes copy, 0x%X total)\n", entry->p_offset, entry->p_vaddr, entry->p_filesz, entry->p_memsz);
		}
	}

	// Parse section header table
	for(int i = 0; i < header->sh_entry_count; i++) {
		offset = header->sh_offset + (sizeof(elf_section_entry_t) * i);
		elf_section_entry_t *entry = (elf_section_entry_t *) ((uint32_t)fileBuffer+(uint32_t)offset);

		// Ignore NULL sections
		if(entry->sh_type != SHT_NULL) {
			char *sectionName = (char *) file_struct->stringTable+entry->sh_name;

			// Check if it's a section we are interested in
			if(strncasecmp(sectionName, ".strtab", strlen(sectionName)) == 0) {
				file_struct->symbolStringTable = (void *) ((uint32_t)fileBuffer+(uint32_t)entry->sh_offset);
			} else if(strncasecmp(sectionName, ".text", strlen(sectionName)) == 0)  {
				file_struct->section_text = (void *) ((uint32_t)fileBuffer+(uint32_t)entry->sh_offset);
			} else if(strncasecmp(sectionName, ".symtab", strlen(sectionName)) == 0) {
				file_struct->symbolTable = (elf_symbol_entry_t *) ((uint32_t)fileBuffer+(uint32_t)entry->sh_offset);
			}
		}
	}

	return file_struct;
}