#include <ShaderConductor/ShaderConductor.hpp>

int main()
{
    ShaderConductor::Compiler::Options options;
    options.packMatricesInRowMajor = true;
    return options.packMatricesInRowMajor ? 0 : 1;
}
