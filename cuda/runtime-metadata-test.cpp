// SPDX-License-Identifier: MIT
#include <ocelot/cuda/interface/FatBinaryContext.h>
#include <hydrazine/interface/ELFFile.h>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
namespace elf = hydrazine::elf;

static std::vector<char> read(const char *path)
{
    std::ifstream f(path,std::ios::binary);
    assert(f);
    return std::vector<char>(std::istreambuf_iterator<char>(f),std::istreambuf_iterator<char>());
}
static void fatbin(std::vector<char>& data,bool valid)
{
    __cudaFatCudaBinary2 wrapper={};
    wrapper.magic=__cudaFatMAGIC2;wrapper.version=1;
    wrapper.fatbinData=reinterpret_cast<const unsigned long long *>(&data[0]);
    bool accepted=false;
    try {
        cuda::FatBinaryContext context(&wrapper);
        accepted=true;
        assert(context.cubin()&&!context.ptx()&&context.name());
        assert(!std::memcmp(context.cubin(),"\177ELF",4));
    } catch(const std::runtime_error& e) {if(valid)std::cerr<<e.what()<<"\n";}
    assert(accepted==valid);
}
static void elf32(const std::vector<char>& data)
{
    hydrazine::ELFFile file(&data[0]);
    elf::Elf32_Ehdr head;
    std::memcpy(&head,&data[0],sizeof(head));
    assert(file.header().header().e_shoff==head.e_shoff);
    assert(file.sections()==head.e_shnum&&file.programs()==head.e_phnum);
    unsigned checked=0;
    for(unsigned i=0;i<head.e_shnum;i++) {
        elf::Elf32_Shdr sh;
        std::memcpy(&sh,&data[head.e_shoff+i*head.e_shentsize],sizeof(sh));
        const elf::Elf64_Shdr& actual=file.sectionHeader(i).header();
        assert(actual.sh_offset==sh.sh_offset&&actual.sh_size==sh.sh_size);
        assert(actual.sh_name==sh.sh_name&&actual.sh_type==sh.sh_type);
        if(sh.sh_type==elf::SHT_SYMTAB) {
            assert(file.symbols()==sh.sh_size/sizeof(elf::Elf32_Sym)-1);
            for(unsigned n=0;n<file.symbols();n++) {
                elf::Elf32_Sym symbol;
                std::memcpy(&symbol,&data[sh.sh_offset+(n+1)*sizeof(symbol)],sizeof(symbol));
                const elf::Elf64_Sym& got=file.symbolHeader(n).header();
                assert(got.st_name==symbol.st_name&&got.st_value==symbol.st_value);
                assert(got.st_size==symbol.st_size&&got.st_shndx==symbol.st_shndx);
                ++checked;
            }
        }
    }
    assert(file.endOfFile()<=&data[0]+data.size());
    std::cout<<"ELF32_SECTIONS="<<head.e_shnum<<" SYMBOLS="<<checked<<"\n";
}
int main(int argc,char **argv)
{
    assert(argc==3);
    std::vector<char> original=read(argv[1]);
    fatbin(original,true);
    unsigned rejected=0;
    for(unsigned mode=0;mode<5;mode++) {
        std::vector<char> data=original;
        __cudaFatCudaBinary2Header *h=reinterpret_cast<__cudaFatCudaBinary2Header *>(&data[0]);
        __cudaFatCudaBinary2Entry *e=reinterpret_cast<__cudaFatCudaBinary2Entry *>(&data[16]);
        if(mode==0)h->magic=0;
        if(mode==1)h->length=0xffffffff;
        if(mode==2)e->binary=0;
        if(mode==3)e->binarySize=h->length;
        if(mode==4)e->name=e->binary;
        fatbin(data,false);++rejected;
    }
    elf32(read(argv[2]));
    std::cout<<"FATBIN_MALFORMED_CASES="<<rejected<<" PASS\n";
}
