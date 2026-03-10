
#include "Brotli.hpp"

#define DEFAULT_LGWIN 24
#define BROTLI_WINDOW_GAP 16
#define BROTLI_MAX_BACKWARD_LIMIT(W) (((size_t)1 << (W)) - BROTLI_WINDOW_GAP)

static const size_t kFileBufferSize = 1 << 19;

bool Brotli::CompressFile(const fs::path& absolutePath, const fs::path& output)
{
    HexEngine::DiskFile file(absolutePath, std::ios::in | std::ios::binary);

    if (!file.Open())
        return false;

    uint32_t size = file.GetSize();
    uint8_t* data = new uint8_t[size];
    file.Read(data, size);
    file.Close();

    std::vector<uint8_t> compressed;
    std::vector<uint8_t> uncompressed;
    uncompressed.insert(uncompressed.end(), data, data + size);

    if (!CompressData(uncompressed, compressed))
        return false;

    HexEngine::DiskFile out(output, std::ios::out | std::ios::binary);

    if (!out.Open())
        return false;

    out.Write(compressed.data(), (uint32_t)compressed.size());
    out.Close();

    return true;
}

bool Brotli::DecompressFile(const fs::path& absolutePath, const fs::path& output)
{
    HexEngine::DiskFile file(absolutePath, std::ios::in | std::ios::binary);

    if (!file.Open())
        return false;

    uint32_t size = file.GetSize();
    uint8_t* data = new uint8_t[size];
    file.Read(data, size);
    file.Close();

    std::vector<uint8_t> compressed;
    std::vector<uint8_t> uncompressed;
    compressed.insert(compressed.end(), data, data + size);

    if (!DecompressData(compressed, uncompressed))
        return false;

    HexEngine::DiskFile out(output, std::ios::out | std::ios::binary);

    if (!out.Open())
        return false;

    out.Write(uncompressed.data(), (uint32_t)uncompressed.size());
    out.Close();

    return true;
}

bool Brotli::CompressFile(const fs::path& absolutePath, std::vector<uint8_t>& output)
{
    HexEngine::DiskFile file(absolutePath, std::ios::in | std::ios::binary);

    if (!file.Open())
        return false;

    uint32_t size = file.GetSize();
    uint8_t* data = new uint8_t[size];
    file.Read(data, size);
    file.Close();

    std::vector<uint8_t> compressed;
    std::vector<uint8_t> uncompressed;
    uncompressed.insert(uncompressed.end(), data, data + size);

    if (!CompressData(uncompressed, compressed))
        return false;

    return true;
}

bool Brotli::DecompressFile(const fs::path& absolutePath, std::vector<uint8_t>& output)
{
    HexEngine::DiskFile file(absolutePath, std::ios::in | std::ios::binary);

    if (!file.Open())
        return false;

    uint32_t size = file.GetSize();
    uint8_t* data = new uint8_t[size];
    file.Read(data, size);
    file.Close();

    std::vector<uint8_t> compressed;
    compressed.insert(compressed.end(), data, data + size);

    if (!DecompressData(compressed, output))
        return false;

    return true;
}

bool Brotli::CompressData(const std::vector<uint8_t>& data, std::vector<uint8_t>& output)
{
    BrotliEncoderState* state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);

    if (!state)
        return false;

    BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY, CompressionQuality);

    std::array<uint8_t, kFileBufferSize> buffer;
    std::stringstream result;

    size_t available_in = data.size(), available_out = buffer.size();
    const uint8_t* next_in = reinterpret_cast<const uint8_t*>(data.data());
    uint8_t* next_out = buffer.data();

    do
    {
        BrotliEncoderCompressStream
        (
            state, BROTLI_OPERATION_FINISH,
            &available_in, &next_in, &available_out, &next_out, nullptr
        );

        output.insert(output.end(), buffer.data(), buffer.data() + (buffer.size() - available_out));
        available_out = buffer.size();
        next_out = buffer.data();
    } while (!(available_in == 0 && BrotliEncoderIsFinished(state)));

    BrotliEncoderDestroyInstance(state);
    return true;
}

bool Brotli::DecompressData(const std::vector<uint8_t>& data, std::vector<uint8_t>&output)
{
    BrotliDecoderState* state = BrotliDecoderCreateInstance(nullptr, nullptr, nullptr);

    if (!state)
        return false;

    std::array<uint8_t, kFileBufferSize> buffer;

    size_t available_in = data.size(), available_out = buffer.size();
    const uint8_t* next_in = reinterpret_cast<const uint8_t*>(data.data());
    uint8_t* next_out = buffer.data();
    BrotliDecoderResult oneshot_result;
    
    do
    {
        oneshot_result = BrotliDecoderDecompressStream(state,
            &available_in, &next_in,
            &available_out, &next_out,
            nullptr);
        
        output.insert(output.end(), buffer.data(), buffer.data() + (buffer.size() - available_out));    

        available_out = buffer.size();
        next_out = buffer.data();
    }
    while (!(available_in == 0 && oneshot_result == BROTLI_DECODER_RESULT_SUCCESS));


    BrotliDecoderDestroyInstance(state);
    return true;
}