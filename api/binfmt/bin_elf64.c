/************************************************************************/
/* rop-tool - A Return Oriented Programming and binary exploitation     */
/*            tool                                                      */
/* 								        */
/* Copyright 2013-2015, -TOSH-					        */
/* File coded by -TOSH-						        */
/* 								        */
/* This file is part of rop-tool.	       			        */
/* 								        */
/* rop-tool is free software: you can redistribute it and/or modif      */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation, either version 3 of the License, or    */
/* (at your option) any later version.				        */
/* 								        */
/* rop-tool is distributed in the hope that it will be useful,	        */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.			        */
/* 								        */
/* You should have received a copy of the GNU General Public License    */
/* along with rop-tool.  If not, see <http://www.gnu.org/licenses/>     */
/************************************************************************/
#include "api/binfmt.h"
#include "api/binfmt/elf.h"


/* =========================================================================
   This file contain the functions for loading ELF64 binaries
   ======================================================================= */


/* Fill bin->mlist structure */
static void r_binfmt_elf64_load_mlist(r_binfmt_s *bin) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)bin->mapped;
  Elf64_Phdr *phdr;
  int i;
  u64 p_vaddr, p_offset, p_filesz, e_phoff;
  u32 flags;
  u32 p_type, p_flags;
  u16 e_phnum;

  R_BINFMT_ASSERT(bin->mapped_size >= sizeof(Elf64_Ehdr));

  bin->mlist = r_binfmt_mlist_new();

  e_phoff = r_binfmt_get_int64((byte_t*)&ehdr->e_phoff, bin->endian);
  R_BINFMT_ASSERT(e_phoff < bin->mapped_size);

  phdr = (Elf64_Phdr*)(bin->mapped + e_phoff);

  e_phnum = r_binfmt_get_int16((byte_t*)&ehdr->e_phnum, bin->endian);

  R_BINFMT_ASSERT(r_utils_add64(NULL, e_phnum*sizeof(Elf64_Phdr), e_phoff) &&
		  e_phnum*sizeof(Elf64_Phdr) + e_phoff <= bin->mapped_size);

  for(i = 0; i < e_phnum; i++) {
    p_type = r_binfmt_get_int32((byte_t*)&phdr[i].p_type, bin->endian);
    p_flags = r_binfmt_get_int32((byte_t*)&phdr[i].p_flags, bin->endian);
    p_vaddr = r_binfmt_get_int64((byte_t*)&phdr[i].p_vaddr, bin->endian);
    p_offset = r_binfmt_get_int64((byte_t*)&phdr[i].p_offset, bin->endian);
    p_filesz = r_binfmt_get_int64((byte_t*)&phdr[i].p_filesz, bin->endian);

    R_BINFMT_ASSERT(r_utils_add64(NULL, p_offset, p_filesz) &&
		    p_offset + p_filesz <= bin->mapped_size);

    if(p_type == PT_LOAD) {

      flags = 0;
      if(p_flags & PF_X)
	flags |= R_BINFMT_MEM_FLAG_PROT_X;
      if(p_flags & PF_R)
	flags |= R_BINFMT_MEM_FLAG_PROT_R;
      if(p_flags & PF_W)
	flags |= R_BINFMT_MEM_FLAG_PROT_W;

      r_binfmt_mlist_add(bin->mlist,
		     p_vaddr,
		     bin->mapped + p_offset,
		     p_filesz,
		     flags);
    }
  }
}

/* Get the section name, with the e_shstrndx and the sh_name */
static const char* r_binfmt_elf64_get_name(r_binfmt_s *bin, u32 section_id, u32 name) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)bin->mapped;
  Elf64_Shdr *shdr;
  u64 offset;

  shdr = (Elf64_Shdr*)(bin->mapped + r_binfmt_get_int32((byte_t*)&ehdr->e_shoff, bin->endian));

  offset = r_binfmt_get_int64((byte_t*)&shdr[section_id].sh_offset, bin->endian);

  return (const char*)(bin->mapped + offset + name);
}

static void r_binfmt_elf64_load_syms(r_binfmt_s *bin) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)bin->mapped;
  Elf64_Shdr *shdr;
  Elf64_Sym *symhdr;
  u64 e_shoff, sh_size, sh_offset, link_off;
  u32 i, j, sh_type, st_name, sh_link;
  u64 num;
  u16 e_shnum;
  r_binfmt_sym_s *sym;

  R_BINFMT_ASSERT(bin->mapped_size >= sizeof(Elf64_Ehdr));

  e_shoff = r_binfmt_get_int64((byte_t*)&ehdr->e_shoff, bin->endian);

  R_BINFMT_ASSERT(e_shoff < bin->mapped_size);

  shdr = (Elf64_Shdr*)(bin->mapped + e_shoff);

  e_shnum = r_binfmt_get_int16((byte_t*)&ehdr->e_shnum, bin->endian);

  R_BINFMT_ASSERT(r_utils_add64(NULL, e_shnum*sizeof(Elf64_Shdr), e_shoff) &&
		  e_shnum*sizeof(Elf64_Shdr) + e_shoff <= bin->mapped_size);

  for(i = 0; i < e_shnum; i++) {
    sh_type = r_binfmt_get_int32((byte_t*)&shdr[i].sh_type, bin->endian);

    if(sh_type == SHT_SYMTAB || sh_type == SHT_DYNSYM) {
      sh_size = r_binfmt_get_int64((byte_t*)&shdr[i].sh_size, bin->endian);
      num = sh_size / sizeof(Elf64_Sym);
      sh_offset = r_binfmt_get_int64((byte_t*)&shdr[i].sh_offset, bin->endian);

      R_BINFMT_ASSERT(r_utils_add64(NULL, sh_offset, sh_size) &&
		      sh_offset + sh_size <= bin->mapped_size);

      symhdr = (Elf64_Sym*)(bin->mapped + sh_offset);
      sh_link = r_binfmt_get_int32((byte_t*)&shdr[i].sh_link, bin->endian);

      R_BINFMT_ASSERT(sh_link < e_shnum);

      for(j = 0; j < num; j++) {
	st_name = r_binfmt_get_int32((byte_t*)&symhdr[j].st_name, bin->endian);
	link_off = r_binfmt_get_int64((byte_t*)&shdr[sh_link].sh_offset, bin->endian);

	R_BINFMT_ASSERT(r_utils_add64(NULL, link_off, st_name) &&
			link_off + st_name <= bin->mapped_size);

	sym = r_binfmt_sym_new();
	sym->name = r_binfmt_elf64_get_name(bin, sh_link, st_name);
	sym->addr = symhdr[j].st_value;
	r_utils_list_push(&bin->syms, sym);
      }
    }
  }
}

/* Fill bin->sections structure */
static void r_binfmt_elf64_load_sections(r_binfmt_s *bin) {
  r_binfmt_section_s *section;
  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)bin->mapped;
  Elf64_Shdr *shdr;
  u32 sh_name;
  u64 sh_addr, sh_size, e_shoff, strndx_off;
  u16 e_shnum, e_shstrndx;
  u32 i;

  R_BINFMT_ASSERT(bin->mapped_size >= sizeof(Elf64_Ehdr));

  e_shoff = r_binfmt_get_int64((byte_t*)&ehdr->e_shoff, bin->endian);

  R_BINFMT_ASSERT(e_shoff <= bin->mapped_size);

  shdr = (Elf64_Shdr*)(bin->mapped + e_shoff);

  e_shnum = r_binfmt_get_int16((byte_t*)&ehdr->e_shnum, bin->endian);
  e_shstrndx = r_binfmt_get_int16((byte_t*)&ehdr->e_shstrndx, bin->endian);

  R_BINFMT_ASSERT(r_utils_add64(NULL, e_shnum*sizeof(Elf64_Shdr), e_shoff) &&
		  e_shnum*sizeof(Elf64_Shdr) + e_shoff <= bin->mapped_size);

  R_BINFMT_ASSERT(e_shstrndx < e_shnum);

  strndx_off = r_binfmt_get_int64((byte_t*)&shdr[e_shstrndx].sh_offset, bin->endian);

  for(i = 0; i < e_shnum; i++) {
    sh_addr = r_binfmt_get_int64((byte_t*)&shdr[i].sh_addr, bin->endian);
    sh_size = r_binfmt_get_int64((byte_t*)&shdr[i].sh_size, bin->endian);
    sh_name = r_binfmt_get_int32((byte_t*)&shdr[i].sh_name, bin->endian);

    R_BINFMT_ASSERT(r_utils_add64(NULL, strndx_off, sh_name) &&
		    strndx_off + sh_name <= bin->mapped_size);

    section = r_binfmt_section_new();
    section->addr = sh_addr;
    section->size = sh_size;
    section->name = r_binfmt_elf64_get_name(bin, e_shstrndx, sh_name);


    r_utils_list_push(&bin->sections, section);
  }
}

/* Check if it's a ELF64 binary */
static int r_binfmt_elf64_is(r_binfmt_s *bin) {

  if(bin->mapped_size < sizeof(Elf64_Ehdr))
     return 0;

  if(memcmp(bin->mapped, ELFMAG, SELFMAG))
    return 0;

  if(bin->mapped[EI_CLASS] != ELFCLASS64)
    return 0;

  return 1;
}

/* Get the architecture */
static r_binfmt_arch_e r_binfmt_elf64_getarch(r_binfmt_s *bin) {
  R_BINFMT_ASSERT_RET(R_BINFMT_ENDIAN_UNDEF, bin->mapped_size >= sizeof(Elf64_Ehdr));

  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)bin->mapped;

  if(ehdr->e_machine == EM_X86_64 ||
     ehdr->e_machine == EM_IA_64)
    return R_BINFMT_ARCH_X86_64;
  if(ehdr->e_machine == EM_AARCH64)
    return R_BINFMT_ARCH_ARM64;

  return R_BINFMT_ARCH_UNDEF;
}

/* Get the endianness */
static r_binfmt_endian_e r_binfmt_elf64_getendian(r_binfmt_s *bin) {
  R_BINFMT_ASSERT_RET(R_BINFMT_ENDIAN_UNDEF, bin->mapped_size >= sizeof(Elf64_Ehdr));

  if(bin->mapped[EI_DATA] == ELFDATA2LSB)
    return R_BINFMT_ENDIAN_LITTLE;
  if(bin->mapped[EI_DATA] == ELFDATA2MSB)
    return R_BINFMT_ENDIAN_BIG;

  return R_BINFMT_ENDIAN_UNDEF;
}

static addr_t r_binfmt_elf64_getentry(r_binfmt_s *bin) {
  R_BINFMT_ASSERT_RET(0, bin->mapped_size >= sizeof(Elf64_Ehdr));

  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)(bin->mapped);

  return r_binfmt_get_int64((byte_t*)&ehdr->e_entry, bin->endian);
}

/* Check if NX bit is enabled */
static r_binfmt_nx_e r_binfmt_elf64_check_nx(r_binfmt_s *bin) {
  Elf64_Ehdr *ehdr = (Elf64_Ehdr*)(bin->mapped);
  Elf64_Phdr *phdr;
  u64 e_phoff;
  u32 i, p_type;
  u16 e_phnum;

  R_BINFMT_ASSERT_RET(R_BINFMT_NX_UNKNOWN, bin->mapped_size >= sizeof(Elf64_Ehdr));

  e_phoff = r_binfmt_get_int64((byte_t*)&ehdr->e_phoff, bin->endian);
  R_BINFMT_ASSERT_RET(R_BINFMT_NX_UNKNOWN, e_phoff < bin->mapped_size);

  phdr = (Elf64_Phdr*)(bin->mapped + e_phoff);

  e_phnum = r_binfmt_get_int16((byte_t*)&ehdr->e_phnum, bin->endian);

  R_BINFMT_ASSERT_RET(R_BINFMT_NX_UNKNOWN, r_utils_add64(NULL, e_phnum*sizeof(Elf64_Phdr), e_phoff) &&
		  e_phnum*sizeof(Elf64_Phdr) + e_phoff <= bin->mapped_size);

  for(i = 0; i < e_phnum; i++) {
    p_type = r_binfmt_get_int32((byte_t*)&phdr[i].p_type, bin->endian);
    if(p_type == PT_GNU_STACK)
      return R_BINFMT_NX_ENABLED;
  }
  return R_BINFMT_NX_DISABLED;
}


/* Fill the BINFMT structure if it's a correct ELF64 */
r_binfmt_err_e r_binfmt_elf64_load(r_binfmt_s *bin) {

  if(!r_binfmt_elf64_is(bin))
    return R_BINFMT_ERR_UNRECOGNIZED;

  r_binfmt_elf64_load_mlist(bin);
  r_binfmt_elf64_load_sections(bin);
  r_binfmt_elf64_load_syms(bin);

  bin->type = R_BINFMT_TYPE_ELF64;
  bin->arch = r_binfmt_elf64_getarch(bin);
  bin->endian = r_binfmt_elf64_getendian(bin);
  bin->entry = r_binfmt_elf64_getentry(bin);
  bin->nx = r_binfmt_elf64_check_nx(bin);

  if(bin->arch == R_BINFMT_ARCH_UNDEF)
    return R_BINFMT_ERR_NOTSUPPORTED;

  if(bin->endian == R_BINFMT_ENDIAN_UNDEF)
    return R_BINFMT_ERR_NOTSUPPORTED;

  return R_BINFMT_ERR_OK;
}
