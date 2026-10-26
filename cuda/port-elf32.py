"""Extend Gdev and Ocelot's existing ELF readers to ARMhf CUDA images."""
from pathlib import Path

def write(path, text):
    if not path.exists() or path.read_text() != text:
        path.write_text(text)

def apply(root):
    p = root/'cuda/driver/gdev_cuda.c'
    s = p.read_text()
    if 'MOCHA_CUBIN_CLASS_DISPATCH' not in s:
        begin = s.index('static int load_cubin(struct CUmod_st *mod, char *bin)')
        end = s.index('\n#ifndef __KERNEL__\nstatic int save_ptx', begin)
        loader = s[begin:end]
        loader = loader.replace('mod->arch = GDEV_ARCH_SM_2X;',
            'mod->arch = ((ehead->e_flags & 0xff) >= 30 && (ehead->e_flags & 0xff) <= 37) ? 0xe0 : GDEV_ARCH_SM_2X;')
        (p.parent/'mocha_cubin_loader.inc').write_text(loader)
        dispatch = '/* MOCHA_CUBIN_CLASS_DISPATCH */\n'
        for bits in [32,64]:
            for kind in ['Ehdr','Shdr','Phdr','Sym']:
                dispatch += '#undef Elf_'+kind+'\n#define Elf_'+kind+' Elf'+str(bits)+'_'+kind+'\n'
            dispatch += '#define load_cubin load_cubin'+str(bits)+'\n#include "mocha_cubin_loader.inc"\n#undef load_cubin\n'
        dispatch += '''static int load_cubin(struct CUmod_st *mod, char *bin)
{
    if (memcmp(bin, "\\177ELF", 4)) return -ENOENT;
    if ((unsigned char)bin[4] == ELFCLASS32) return load_cubin32(mod, bin);
    if ((unsigned char)bin[4] == ELFCLASS64) return load_cubin64(mod, bin);
    return -EINVAL;
}
'''
        write(p, s[:begin]+dispatch+s[end:])

    p = root/'cuda/runtime/hydrazine/implementation/ELFFile.cpp'
    s = p.read_text()
    conversions = {
        'Ehdr': ['e_type','e_machine','e_version','e_entry','e_phoff','e_shoff','e_flags','e_ehsize','e_phentsize','e_phnum','e_shentsize','e_shnum','e_shstrndx'],
        'Shdr': ['sh_name','sh_type','sh_flags','sh_addr','sh_offset','sh_size','sh_link','sh_info','sh_addralign','sh_entsize'],
        'Phdr': ['p_type','p_flags','p_offset','p_vaddr','p_paddr','p_filesz','p_memsz','p_align'],
        'Sym': ['st_name','st_info','st_other','st_shndx','st_value','st_size'],
    }
    for kind, fields in conversions.items():
        src = 'd' if kind=='Ehdr' else '(char*)d + b'
        old = '\t\tstd::memcpy(&_header, '+src+', sizeof(elf::Elf64_'+kind+'));'
        marker = '// MOCHA_ELF32_'+kind
        if marker in s: continue
        assert old in s, kind
        new = '\t\t'+marker+'\n\t\tif (static_cast<const unsigned char *>(d)[4] == elf::ELFCLASS32) {\n'
        new += '\t\t\telf::Elf32_'+kind+' h32;\n\t\t\tstd::memcpy(&h32, '+src+', sizeof(h32));\n'
        new += '\t\t\tstd::memset(&_header, 0, sizeof(_header));\n'
        if kind=='Ehdr':new += '\t\t\tstd::memcpy(_header.e_ident, h32.e_ident, 16);\n'
        new += ''.join('\t\t\t_header.'+field+' = h32.'+field+';\n' for field in fields)
        new += '\t\t} else {\n'+old+'\n\t\t}'
        s=s.replace(old,new,1)
    s=s.replace('_header.e_type == elf::ELFCLASS64',
        '(_header.e_ident[4] == elf::ELFCLASS32 || _header.e_ident[4] == elf::ELFCLASS64)')
    s=s.replace('Only 64-bit ELF files are supported for now.', 'Unsupported ELF class.')
    s=s.replace('+ (h + 1) * sizeof(elf::Elf64_Sym);',
        '+ (h + 1) * (header().header().e_ident[4] == elf::ELFCLASS32 ? sizeof(elf::Elf32_Sym) : sizeof(elf::Elf64_Sym));')
    s=s.replace('(header.sh_size / sizeof(elf::Elf64_Sym)) - 1;',
        '(header.sh_size / (this->header().header().e_ident[4] == elf::ELFCLASS32 ? sizeof(elf::Elf32_Sym) : sizeof(elf::Elf64_Sym))) - 1;')
    old = '''\treturn (const char*) _elfdata + header().header().e_phoff
\t\t+ sizeof(elf::Elf64_Phdr) * (programs() + 1);'''
    new = '''    // MOCHA_ELF_FILE_END: program tables are not necessarily last.
    const elf::Elf64_Ehdr& h = header().header();
    unsigned long long end = h.e_ehsize;
    unsigned long long tables[] = {h.e_phoff + h.e_phentsize * h.e_phnum,
        h.e_shoff + h.e_shentsize * h.e_shnum};
    for (unsigned i = 0; i < 2; ++i) if (tables[i] > end) end = tables[i];
    for (unsigned i = 0; i < sections(); ++i) {
        const elf::Elf64_Shdr& sh = sectionHeader(i).header();
        if (sh.sh_type != elf::SHT_NOBITS && sh.sh_offset + sh.sh_size > end)
            end = sh.sh_offset + sh.sh_size;
    }
    return (const char*)_elfdata + end;'''
    assert old in s or 'MOCHA_ELF_FILE_END' in s
    write(p, s.replace(old,new,1))

    p = root/'cuda/runtime/ocelot/cuda/implementation/FatBinaryContext.cpp'
    s=p.read_text()
    begin=s.index('\telse if (*(int*)cubin_ptr == __cudaFatMAGIC2) {') if '\telse if (*(int*)cubin_ptr == __cudaFatMAGIC2) {' in s else s.index('    else if (*(int*)cubin_ptr == __cudaFatMAGIC2) {')
    end=s.index('\n\telse {\n\t\tassertM(false, "unknown fat binary',begin)
    fragment=Path(__file__).with_name('runtime-fatbin.inc').read_text().rstrip()
    s=s[:begin]+fragment+s[end:]
    if '#include <stdexcept>' not in s:s=s.replace('#include <fstream>', '#include <fstream>\n#include <stdexcept>')
    write(p, s)
    print('MOCHA_ELF32_AND_BOUNDED_FATBIN_APPLIED')
