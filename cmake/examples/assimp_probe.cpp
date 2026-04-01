#include <assimp/cimport.h>
#include <assimp/version.h>

int main()
{
    const unsigned int major = aiGetVersionMajor();
    return major > 0 ? 0 : 1;
}
