#include "io/FilesystemReader.h"
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace openhi2txt {
namespace {
uint16_t u16(const std::vector<uint8_t>& b, size_t p) {
    return uint16_t(b.at(p)) | (uint16_t(b.at(p+1)) << 8);
}
uint32_t u32(const std::vector<uint8_t>& b, size_t p) {
    return uint32_t(u16(b,p)) | (uint32_t(u16(b,p+2)) << 16);
}
void need(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// Deliberately bounded: this reader extracts score files, not game assets.
constexpr uint64_t maxFileBytes = 16 * 1024 * 1024;
class Ext2Reader final : public FilesystemReader {
    DiskRead read;
    uint64_t start = 0, length = 0;
    uint32_t blocks = 0, inodes = 0, blockSize = 0, blocksPerGroup = 0;
    uint32_t inodesPerGroup = 0, inodeSize = 0, firstBlock = 0;
    bool fileTypes = false;
    bool invertedSuperblock = false;
    std::vector<uint8_t> get(uint64_t offset, size_t count) {
        need(offset <= length && count <= length-offset, "ext2 read exceeds partition bounds");
        std::vector<uint8_t> b; std::string error;
        if (!read(start+offset,count,b,error)) throw std::runtime_error(error);
        need(b.size() == count, "Short filesystem read");
        return b;
    }
    std::vector<uint8_t> block(uint32_t n) {
        need(n >= firstBlock && n < blocks && n != 0, "Invalid ext2 block pointer");
        return get(uint64_t(n)*blockSize,blockSize);
    }
    std::vector<uint8_t> inode(uint32_t n) {
        need(n && n <= inodes, "Invalid ext2 inode number");
        uint32_t group = (n-1)/inodesPerGroup;
        need(group < (uint64_t(blocks)-firstBlock+blocksPerGroup-1)/blocksPerGroup,
             "Invalid ext2 inode group");
        auto descriptor = get(uint64_t(firstBlock+1)*blockSize+uint64_t(group)*32,32);
        uint32_t table = u32(descriptor,8);
        need(table >= firstBlock && table < blocks && table, "Invalid ext2 inode table");
        auto result = get(uint64_t(table)*blockSize+uint64_t((n-1)%inodesPerGroup)*inodeSize,inodeSize);
        need(u16(result,26) != 0 && u32(result,20) == 0, "Deleted ext2 inode");
        // This observed revision-0 variant stores timestamps in i_flags.
        need((invertedSuperblock && u32(result,32) == u32(result,12)) ||
             (u32(result,32) & ~uint32_t(0x38)) == 0, "Unsupported ext2 inode flags");
        return result;
    }
    uint32_t indirect(uint32_t pointer, uint64_t index, unsigned depth) {
        if (!pointer) return 0; // sparse block
        auto pointers = block(pointer);
        uint64_t span = 1;
        for (unsigned d=1;d<depth;++d) span *= blockSize/4;
        need(index/span < blockSize/4, "Invalid ext2 indirect index");
        uint32_t next = u32(pointers,size_t(index/span)*4);
        return depth == 1 ? next : indirect(next,index%span,depth-1);
    }
    std::vector<uint8_t> contents(const std::vector<uint8_t>& node) {
        uint64_t size = u32(node,4);
        if ((u16(node,0)&0xf000) == 0x8000)
            size |= uint64_t(u32(node,108)) << 32;
        need(size <= maxFileBytes, "ext2 file exceeds 16 MiB extraction limit");
        std::vector<uint8_t> result(static_cast<size_t>(size),0);
        for (uint64_t i=0,position=0;position<size;++i,position+=blockSize) {
            uint32_t pointer = 0;
            if (i<12) pointer=u32(node,40+size_t(i)*4);
            else {
                uint64_t index=i-12,capacity=blockSize/4;
                unsigned depth=1;
                while (depth<3 && index>=capacity) {index-=capacity;capacity*=blockSize/4;++depth;}
                need(index<capacity,"ext2 file block index too large");
                pointer=indirect(u32(node,88+(depth-1)*4),index,depth);
            }
            if (pointer) {
                auto data=block(pointer);
                std::copy_n(data.begin(),size_t(std::min<uint64_t>(blockSize,size-position)),result.begin()+size_t(position));
            }
        }
        return result;
    }
public:
    Ext2Reader(unsigned partition,uint64_t diskSize,DiskRead callback) : read(std::move(callback)) {
        need(partition>=1 && partition<=4,"MBR partition must be 1..4");
        length=diskSize;
        auto mbr=get(0,512);
        need(u16(mbr,510)==0xaa55,"Invalid MBR signature");
        size_t entry=446+(partition-1)*16;
        need(mbr[entry+4] != 0 && mbr[entry+4] != 5 && mbr[entry+4] != 15 && mbr[entry+4] != 0x85,
             "Expected a primary data partition");
        start=uint64_t(u32(mbr,entry+8))*512;
        length=uint64_t(u32(mbr,entry+12))*512;
        need(length && start<=diskSize && length<=diskSize-start,"Invalid MBR partition bounds");
        auto sb=get(1024,1024);
        invertedSuperblock = u16(sb,56)==uint16_t(~0xef53);
        if (invertedSuperblock) for (auto& byte:sb) byte^=0xff;
        need(u16(sb,56)==0xef53,"Invalid ext2 superblock magic");
        uint32_t revision=u32(sb,76),logSize=u32(sb,24);
        need(revision<=1 && (!invertedSuperblock || revision==0) && logSize<=2,"Unsupported ext2 revision or block size");
        blockSize=1024U<<logSize;
        blocks=u32(sb,4);inodes=u32(sb,0);firstBlock=u32(sb,20);
        blocksPerGroup=u32(sb,32);inodesPerGroup=u32(sb,40);
        inodeSize=revision ? u16(sb,88) : 128;
        need(firstBlock==(blockSize==1024 ? 1U:0U) && blocks>firstBlock && inodes &&
             blocksPerGroup && blocksPerGroup<=blockSize*8 && inodesPerGroup && inodesPerGroup<=blockSize*8 &&
             inodeSize>=128 && inodeSize<=blockSize && (inodeSize&(inodeSize-1))==0 &&
             uint64_t(blocks)*blockSize<=length,"Invalid ext2 geometry");
        if (revision) {
            need(u32(sb,92)==0 && (u32(sb,96)&~2U)==0 && (u32(sb,100)&~3U)==0,
                 "Unsupported ext2 features (journal/extents/indexed directories are not supported)");
            fileTypes=(u32(sb,96)&2)!=0;
        }
        // Restrict subsequent reads to the filesystem, not trailing partition space.
        length=uint64_t(blocks)*blockSize;
    }
    bool readFile(const std::string& path,std::vector<uint8_t>& bytes,std::string& error) override {
        bytes.clear();error.clear();
        try {
            need(path.size()>1 && path.front()=='/' && path.back()!='/' && path.find('\\')==std::string::npos,
                 "Expected an absolute filesystem file path");
            auto node=inode(2);
            size_t from=1;
            while (from<path.size()) {
                size_t end=path.find('/',from);if(end==std::string::npos) end=path.size();
                std::string name=path.substr(from,end-from);
                need(!name.empty() && name!="." && name!=".." && name.size()<=255,"Invalid filesystem path component");
                need((u16(node,0)&0xf000)==0x4000,"Filesystem path component is not a directory");
                auto directory=contents(node);uint32_t found=0;
                for (size_t p=0;p<directory.size();) {
                    need(directory.size()-p>=8,"Truncated ext2 directory entry");
                    uint32_t number=u32(directory,p);uint16_t record=u16(directory,p+4);
                    unsigned count=fileTypes ? directory[p+6] : u16(directory,p+6);
                    need(record>=8 && record%4==0 && record<=directory.size()-p &&
                         record<=blockSize-p%blockSize && count<=255 && count<=record-8,
                         "Invalid ext2 directory entry");
                    if (number && std::string(directory.begin()+p+8,directory.begin()+p+8+count)==name) {
                        need(!found,"Duplicate ext2 directory name");found=number;
                    }
                    p+=record;
                }
                if (!found) throw std::runtime_error("Filesystem file not found: "+path);
                node=inode(found);from=end+1;
            }
            need((u16(node,0)&0xf000)==0x8000,"Filesystem source is not a regular file (symlinks unsupported)");
            bytes=contents(node);return true;
        } catch (const std::exception& e) {error=e.what();return false;}
    }
};
}
std::unique_ptr<FilesystemReader> FilesystemReader::open(const std::string& type,
    unsigned partition,uint64_t diskSize,DiskRead read,std::string& error) {
    error.clear();
    try {
        // Add future filesystem implementations here; callers only use readFile.
        if (type=="ext2") return std::make_unique<Ext2Reader>(partition,diskSize,std::move(read));
        error="Unsupported filesystem: "+type;
    } catch (const std::exception& e) {error=e.what();}
    return {};
}
}
