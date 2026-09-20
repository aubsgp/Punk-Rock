#include <stdint.h>
#include <stdexcept>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <cstring>
#include <string>
#include <memory>
#include <array>
#include <vector>

#include <ROM.h>

std::vector<std::string> split(const std::string &s, char delimiter)
{
    std::vector<std::string> s_vect;
    std::string fragment;
    std::stringstream s_stream(s);
    
    while (std::getline(s_stream, fragment, delimiter)) {
        s_vect.push_back(fragment);
    }

    return s_vect;
}

std::vector<uint8_t> ROM::Read8Range(uint32_t start, uint32_t end)
{
    addr = end;
    return std::vector<uint8_t>(rom.begin() + start, rom.begin() + end);
}

uint16_t ROM::Read16LE(uint32_t read_addr)
{
    uint16_t ret;
    std::memcpy(&ret, rom.data() + read_addr, sizeof(ret));
    addr = read_addr + sizeof(ret);
    return ret;
}

uint32_t ROM::Read32LE(uint32_t read_addr)
{
    uint32_t ret;
    std::memcpy(&ret, rom.data() + read_addr, sizeof(ret));
    addr = read_addr + sizeof(ret);
    return ret;
}

ROM::ROM(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);

    if (!file.is_open())
    {
        throw std::runtime_error("Error: Could not open file.");
    }

    std::streamsize size = file.tellg();
    rom.resize(size);
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char *>(rom.data()), size))
    {
        throw std::runtime_error("Error: Failed to read the complete file.");
    }
    else
    {
        FNTAddr = Read32LE(0x40);
        FATAddr = Read32LE(0x48);
    }
}

int ROM::SeekEntry(uint32_t folder_addr, const std::string &name)
{
    addr = folder_addr;
    int metadata;
    int count = 0;
    do
    {
        metadata = rom[addr++];
        int length = metadata & 0b01111111;
        bool is_folder = (metadata & 0b10000000) != 0;

        std::vector<uint8_t> entry = Read8Range(addr, addr + length);
        if (std::string(entry.begin(), entry.end()) == name)
        {
            return count;
        }

        if (is_folder)
        {
            addr += 2;
        }
        else
        {
            count ++;
        }
    } while (metadata != 0);

    throw std::runtime_error(name + " not found in NDS filesystem.");
}

std::array<uint32_t, 2> ROM::GetFileAddr(const std::string &path)
{
    std::vector<std::string> segments = split(path, '/');
    uint32_t dirAddr = 0;
    int count = 0;
    int ID = 0;
    for (int i = 0; i < segments.size(); i++) // All but the last segment are presumed to be folders.
    {
        dirAddr = FNTAddr + (ID * 0x08);
        uint32_t foldersOffset = Read32LE(dirAddr);
        count = SeekEntry(FNTAddr + foldersOffset, segments[i]);
        ID = Read16LE(addr) & 0x0fff; // Reads garbage against a file, but only the last segment is a file, so this is fine.
    }
    int FAT_ID = Read16LE(dirAddr + 0x04u) + count;
    uint32_t start = Read32LE(FATAddr + (FAT_ID * 0x08));
    uint32_t end   = Read32LE(FATAddr + (FAT_ID * 0x08) + 0x04);
    return {start, end};
}

std::array<uint32_t, 2> ROM::GetNarcEntryAddr(uint32_t narc_addr, uint32_t entry) // Returns the start and end addresses of the given entry
{
    uint32_t entryStartOffset = Read32LE(narc_addr + 0x1c + (entry * 0x08));
    uint32_t entryEndOffset = Read32LE(narc_addr + 0x1c + (entry * 0x08) + 4);
    uint32_t BTAFLength = Read32LE(narc_addr + 0x14);
    uint32_t BTNFLength = Read32LE(narc_addr + BTAFLength + 0x14);
    uint32_t filesAddr = narc_addr + 0x10 + BTAFLength + BTNFLength + 0x08;
    return {filesAddr + entryStartOffset, filesAddr + entryEndOffset};
}

std::vector<uint8_t> ROM::GetNarcEntry(uint32_t narc_addr, uint32_t entry)
{
    auto [entryStartAddr, entryEndAddr] = GetNarcEntryAddr(narc_addr, entry);
    return Read8Range(entryStartAddr, entryEndAddr);
}

// https://projectpokemon.org/rawdb/platinum/formats/msg.php
std::vector<std::string> ROM::GetMsgEntry(uint32_t entry)
{
    auto [entryStartAddr, entryEndAddr] = GetNarcEntryAddr(std::get<0>(GetFileAddr("msgdata/pl_msg.narc")), entry);
    addr = entryStartAddr;
    uint16_t numStrings = Read16LE(addr);
    uint16_t seed = Read16LE(addr);

    std::vector<uint32_t> offsets(numStrings);
    std::vector<uint32_t> sizes(numStrings);
    std::vector<std::string> strings(numStrings);

    for (uint32_t i = 0; i < numStrings; i++)
    {
        uint32_t key = (seed * (i + 1) * 0x2FD) & 0xFFFF;
        key = (key << 16) | key;
        offsets[i] = Read32LE(addr) ^ key;
        sizes[i] = Read32LE(addr) ^ key;
    }

    for (uint32_t i = 0; i < numStrings; i++)
    {
        addr = entryStartAddr + offsets[i];
        uint16_t key = 0x1BD3 * (i + 1);
        std::vector<uint16_t> stringData(sizes[i]);

        for (uint32_t j = 0; j < sizes[i]; j++)
        {
            stringData[j] = Read16LE(addr) ^ key;
            key = key + 0x493D;
        }
        // strings[i] = ParseString(stringData); TODO: I'll implement this later, too lazy right now and its not important for my current project.
    }

    return strings;
}

std::vector<std::string> ROM::GetSEQList(uint32_t sdat_addr)
{   
    std::vector<std::string> names;
    uint32_t symb_addr = sdat_addr + Read32LE(sdat_addr + 0x10);
    uint32_t seq_addr  = symb_addr + Read32LE(symb_addr + 0x08);
    uint32_t seq_size  = Read32LE(seq_addr);

    for (uint32_t i = 0; i < seq_size; i++)
    {
        uint32_t offset = Read32LE(seq_addr + ((i+1) * 0x04));
        if (offset != 0)
        {
            uint32_t length = 0;
            uint32_t name_addr = symb_addr + offset;
            while (rom[name_addr + length] != 0)
            {
                length++;
            }
            names.push_back(std::string(rom.data() + name_addr, rom.data() + name_addr + length));
        }
        else
        {
            names.push_back("");
        }
    }
    
    return names;
}