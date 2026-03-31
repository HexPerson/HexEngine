#include <brotli/decode.h>

int main()
{
    const uint32_t version = BrotliDecoderVersion();
    return version > 0 ? 0 : 1;
}
