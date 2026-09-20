std::vector<std::string> split(const std::string& s, char delimiter);

class ROM
{
    private:
        std::vector<uint8_t> rom;
        uint32_t addr;
        uint32_t FATAddr;
        uint32_t FNTAddr;

    public:
        std::vector<uint8_t> Read8Range(uint32_t start, uint32_t end);
        uint16_t Read16LE(uint32_t read_addr);
        uint32_t Read32LE(uint32_t read_addr);
        ROM(const std::filesystem::path &path);
        int SeekEntry(uint32_t folder_addr, const std::string &name);
        std::array<uint32_t, 2> GetFileAddr(const std::string &path);
        std::array<uint32_t, 2> GetNarcEntryAddr(uint32_t narc_addr, uint32_t entry);
        std::vector<uint8_t> GetNarcEntry(uint32_t narc_addr, uint32_t entry);
        std::vector<std::string> GetMsgEntry(uint32_t entry);
        std::vector<std::string> GetSEQList(uint32_t sdat_addr);
};