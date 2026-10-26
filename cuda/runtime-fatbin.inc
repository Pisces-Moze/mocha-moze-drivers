    else if (*(int*)cubin_ptr == __cudaFatMAGIC2) {
        // CUDA 6.5 record sizes include both the fixed and variable headers.
        const __cudaFatCudaBinary2 *binary =
            static_cast<const __cudaFatCudaBinary2 *>(cubin_ptr);
        const __cudaFatCudaBinary2Header *header =
            reinterpret_cast<const __cudaFatCudaBinary2Header *>(binary->fatbinData);
        unsigned headerSize = header->version >> 16;
        if (header->magic != __cudaFatMAGIC3 || (header->version & 0xffff) != 1 ||
            headerSize < sizeof(*header) || headerSize > 4096 ||
            header->length > 64u * 1024u * 1024u)
            throw std::runtime_error("Unsupported CUDA fatbinary header");
        const char *base = reinterpret_cast<const char *>(header) + headerSize;
        unsigned long long offset = 0;
        unsigned best = 0;
        while (offset < header->length) {
            __cudaFatCudaBinary2Entry entry;
            unsigned long long remaining = header->length - offset;
            if (remaining < sizeof(entry))
                throw std::runtime_error("Truncated CUDA fatbinary entry");
            std::memcpy(&entry, base + offset, sizeof(entry));
            if (entry.binary < sizeof(entry) || entry.binary > remaining ||
                entry.binarySize > remaining - entry.binary)
                throw std::runtime_error("CUDA fatbinary entry exceeds its container");
            const char *record = base + offset;
            bool validName = entry.name < entry.binary && entry.nameSize &&
                entry.nameSize < entry.binary - entry.name &&
                std::memchr(record + entry.name, 0, entry.nameSize + 1);
            // A GK20A can execute sm_30 or sm_32, preferring native sm_32.
            if ((entry.type & FATBIN_2_ELF) && entry.unknown4 >= 30 &&
                entry.unknown4 <= 32 && entry.unknown4 > best) {
                if (!validName || entry.binarySize < 52 ||
                    std::memcmp(record + entry.binary, "\177ELF", 4))
                    throw std::runtime_error("Invalid CUDA cubin record");
                _cubin = record + entry.binary;
                _name = record + entry.name;
                best = entry.unknown4;
            }
            offset += entry.binary + entry.binarySize;
        }
        // Prefer the native image. Compressed PTX uses a different format in
        // CUDA 6.5; never feed its compressed bytes into the text parser.
        if (!_cubin)
            throw std::runtime_error("A native sm_30/sm_32 cubin is required; PTX JIT is unavailable");
        _ptx = 0;
    }
